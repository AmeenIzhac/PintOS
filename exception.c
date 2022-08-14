  /* if we have an spte entry we need to lazy-load, load from swap or (re)mmap*/
  if (spte) 
  {
     /* Lazy load: 
         - get a kpage an install it using load_info set in load_segment */
    if(!spte->loaded) {
       uint8_t *kpage;

       /* Our ideal implementation for sharing:
         Before we create a new kernel page to map to, we would first check
         if the page is read-only and if so, lookup in the file sharing table 
         to see if there is already a frame allocated for that read-only page.
         It would look something like the following,
         if(!spte->load_info.writable) {
            struct share_f_elem* sf = share_f_lookup(spte->load_info.file);
            if (sf) {
               if(install_page(spte->virtual address, sf->kpage, false))
                  return;
            }
         }
         If there is something, then we use install_page to create a mapping
         between the shared kernel page and the spts user page. We would then
         increment the ft_elem's number of users member by 1.
         In process exit, when we remove entries from the frame table, we would 
         monitor the number of processes using a frame by checking the
         num_of_users member of the ft_elem struct and only free the 
         frames which have a num_of_users.
      */ 
       
       kpage = ft_alloc_page (rnd_fault_addr, PAL_USER);
       if (kpage != NULL) {
          if (install_page (spte->virtual_address, kpage, 
                            spte->load_info.writable)) {
             /* If the page is read-only, add to file sharing table */
             if (!spte->load_info.writable) {
               share_f_insert(spte->load_info.file, kpage);
             }
             frame_insert(kpage, spte->virtual_address);
             spte->holds_frame = true;

             bool already_owned_filesys = true;
             if (!lock_held_by_current_thread(&filesys_lock)) {
                lock_acquire(&filesys_lock);
                already_owned_filesys = false;
             }

             file_seek (spte->load_info.file, spte->load_info.file_offset);
             file_read (spte->load_info.file, kpage, 
                           spte->load_info.read_bytes);
             
             if (!already_owned_filesys)
               lock_release(&filesys_lock);
             
             /* Zero the rest of the page */
             memset(kpage + spte->load_info.read_bytes, 0, 
                   spte->load_info.zero_bytes);
             spte->loaded = true;
             
            if (!already_owned_movement) {
               lock_release(&movement_lock);
            }
             return;
          }
       }
    } 
    
    /* Get from swap:
        - get a kpage an install the previously evicted page */
    else if (spte->is_in_swap) {
      uint8_t *kpage = st_get_frame_back(spte);
      install_page (spte->virtual_address, kpage, 
                            spte->load_info.writable);
      spte->swap_index = INVALID_INDEX;
      spte->is_in_swap = false;

      if (!already_owned_movement) {
         lock_release(&movement_lock);
      }
      return;
    }

    /* re-map unmapped file*/
    else if (spte->is_mmap) {
       uint8_t *kpage = ft_alloc_page (rnd_fault_addr, PAL_USER);
       spte->mapid = mmap(spte->fd, kpage);
    }
  }


   /* If not in spte, check to see if we need to grow stack. Stack heuristic 
      allows push(a) instructions to grow the stack */
  else if (f->esp - PUSHA_DIST <= fault_addr && fault_addr < PHYS_BASE 
      && alloced_stack_pages < MAX_STACK_PAGES) {

     uint8_t *kpage = ft_alloc_page (rnd_fault_addr, PAL_USER);
     install_page (rnd_fault_addr, kpage, true);
     alloced_stack_pages++;

     struct spt_elem *insertee = spt_insert(t->spt, rnd_fault_addr);
     insertee->accessed_bit = true;
     insertee->dirty_bit    = true;
     insertee->is_in_swap   = false;
     insertee->holds_frame  = true;
     insertee->loaded       = true;

     if (!already_owned_movement) {
      lock_release(&movement_lock);
     }    
     return;
  }
  
  if (!already_owned_movement) {
   lock_release(&movement_lock);
  }
