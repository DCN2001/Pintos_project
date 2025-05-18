#include <stdio.h>
#include "threads/thread.h"
#include "threads/pte.h"
#include "userprog/pagedir.h"
#include "vm/stack.h"
#include "vm/page.h"

/* The stack size cannot grow beyond 256K.*/
#define MAX_STACK_SIZE (PHYS_BASE - PGSIZE * 64)
  
/* If a user program page faults on an address that might be a stack access, grow the stack by mapping in a frame. */
void
grow_stack (uint32_t *pd, const void *vaddr)
{
  struct page_info *page_info;
  void *upage = pg_round_down (vaddr);

  if (pagedir_get_info (pd, upage) == NULL && is_stack_access (vaddr))
    {
      page_info = pageinfo_create ();
      if (page_info != NULL)
        {
          pageinfo_set_upage (page_info, pg_round_down (vaddr));
          pageinfo_set_pagedir (page_info, pd);
          pageinfo_set_type (page_info, PAGE_TYPE_ZERO);
          pageinfo_set_writable (page_info, WRITABLE_TO_SWAP);
          pagedir_set_info (pd, upage, page_info);
         }
    }
 }

/* Stack accesses */ 
bool
is_stack_access (const void *vaddr)
{
  void *esp = thread_current ()->user_esp;

  return (vaddr >= MAX_STACK_SIZE
    && ((esp - vaddr) == 8 || (esp - vaddr) == 32
        || vaddr >= esp));
}