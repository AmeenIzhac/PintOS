static void
init_thread (struct thread *t, const char *name, int priority, int nice,
             int recent_cpu, struct thread *parent, struct child *cd, struct hash *spt)
{
  enum intr_level old_level;
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  list_init(&t->priority_list);
  t->lock_waiting_on = NULL;
  t->stopping_from_running = NULL;
  t->magic = THREAD_MAGIC;
  #ifdef USERPROG
    cd->waited = false;
    sema_init(&cd->terminated, 0);
    cd->tid = t->tid;
    cd->child = t;
    t->child_data = cd;
    t->parent = parent;
    t->spt = spt;
  #endif
  
  /* BSD Scheduler */
  if (thread_mlfqs) {
    t->nice = nice;
    t->recent_cpu = recent_cpu;
    recalculate_priority(t, NULL);
  }

  /* User Programs */
  #ifdef USERPROG
    t->next_fd = FIRST_AVAILABLE_FD;
    t->next_mapid = 1;
    list_init(&t->files);
    list_init(&t->children);
    sema_init(&t->load_sema, 0);
  #endif

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

// ...

/* Comparator used to order the semaphore waiters list and the ready list
    in descending order of priority */
bool 
compare_priorities(const struct list_elem *a, const struct list_elem *b,
                       void *aux UNUSED) 
{
  return get_effective_priority(list_entry(a, struct thread, elem))
    > get_effective_priority(list_entry(b, struct thread, elem));
}

/* Comparator used to order the priority list of a thread in 
    descending priority*/
bool 
priority_elem_compare(const struct list_elem *first, 
                      const struct list_elem *second, void *aux UNUSED) 
{
  return (list_entry(first, struct priority_elem, elem)->priority > 
          list_entry(second, struct priority_elem, elem)->priority);
}

/* Returns the effective priority of a thread */
int 
get_effective_priority(struct thread *t) 
{
  if (thread_mlfqs) {
    return t->priority;
  }
  
  int base_priority = t->priority;
  if (list_empty(&t->priority_list)) {
    return base_priority;
  }
  int highest_donated = list_entry(list_begin(&t->priority_list),
                     struct priority_elem, elem)->priority;
  return (highest_donated > base_priority) ? highest_donated : base_priority;
}

/* Donates t's effective priority to the holder of the lock */
void 
donate(struct lock *lock, struct thread *t) 
{  
  struct thread *reciever = lock->holder;
  
  struct priority_elem priority_to_donate;
  priority_to_donate.priority = get_effective_priority(t);
  priority_to_donate.associated_lock = lock;
  
  if (reciever->lock_waiting_on == NULL) {
    list_insert_ordered(&reciever->priority_list, &priority_to_donate.elem,
                        &priority_elem_compare, NULL);
  } else {  
    while (reciever->lock_waiting_on) {
      if (get_effective_priority(reciever) > priority_to_donate.priority)
        break;
      reciever = reciever->lock_waiting_on->holder;
    }

    list_insert_ordered(&reciever->priority_list, &priority_to_donate.elem,
                        &priority_elem_compare, NULL);
  }
}

/* Removes all donations associated to the lock, held in the owner threads
   priority list */
void 
remove_donations(struct lock *lock) 
{
  bool nested = false;
  bool first_waiter;
  struct thread *t = lock->holder;
  
  if (list_empty(&t->priority_list)) {
    return;
  }
  
  struct list_elem *e;
  for (e = list_begin(&t->priority_list); e != list_end(&t->priority_list); 
      e = list_next(e)) {
        struct priority_elem *p_elem = list_entry(e, struct priority_elem, 
                                                  elem);
        ASSERT(p_elem != NULL);

        if (lock == p_elem->associated_lock) {
          if (p_elem->priority < PRI_MIN) {
            return;
          }
          list_remove(e);
        } else {
          nested = true;
          first_waiter = true;
          list_sort(&lock->semaphore.waiters, &compare_priorities, NULL);
          
          struct list_elem *f;
          for (f = list_begin(&lock->semaphore.waiters);
              f != list_end(&lock->semaphore.waiters);
              f = list_next(f)) {
            struct thread *waiting_thread = list_entry(f, struct thread, elem);
            ASSERT (waiting_thread != NULL);
            if (waiting_thread->stopping_from_running) {
              list_remove(e);
              list_insert_ordered(&waiting_thread->priority_list, e, 
                  &priority_elem_compare, NULL);
              if (first_waiter) {
                waiting_thread->lock_waiting_on = NULL;
              }
              first_waiter = false;
            }
          }

        }
      if (nested)
        break;
    }
}

/* Recalculates thread priority after setting nice */
void
recalculate_priority(struct thread* t, void* aux UNUSED) 
{
  int cpu_time = t-> recent_cpu;
  int fp_new_priority = (conv_int_to_fp(PRI_MAX)
                        - divide_fp_int(cpu_time, 4)
                        - conv_int_to_fp(t->nice * 2));
  thread_set_priority_other(t, conv_fp_to_int_rtz(fp_new_priority));
}
