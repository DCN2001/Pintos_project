#ifndef VM_FRAMETABLE_H
#define VM_FRAMETABLE_H

#include <stdio.h>
#include <stdbool.h>
#include "filesys/inode.h"
#include <hash.h>
#include <list.h>
#include "threads/synch.h"

/* Represents information about a file-backed page, including the file pointer and offset. */
struct file_info
{
  struct file *file;        // File backing the page
  off_t end_offset;         // End offset for the mapped region
};

/* Information associated with each frame. */
struct frame
{
  void *kpage;                  // Kernel virtual address for this frame
  struct list page_info_list;   // List of all page_infos sharing this frame
  unsigned short lock;          // Lock count to prevent eviction
  bool io;                      // Whether I/O is in progress for this frame
  struct condition io_done;     // Condition variable for I/O completion
  struct hash_elem hash_elem;   // For insertion into read-only cache
  struct list_elem list_elem;   // Element in global frame list
};

void frametable_init(void);
bool frametable_load_frame(uint32_t *pd, const void *upage, bool write);
void frametable_unload_frame (uint32_t *pd, const void *upage);
bool frametable_lock_frame(uint32_t *pd, const void *upage, bool write);
void frametable_unlock_frame(uint32_t *pd, const void *upage);

#endif