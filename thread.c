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
