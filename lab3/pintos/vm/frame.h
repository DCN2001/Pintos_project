#ifndef VM_FRAMETABLE_H
#define VM_FRAMETABLE_H

#include <stdbool.h>
#include "filesys/inode.h"


/* Represents information about a file-backed page, including the file pointer and offset. */
struct file_info
{
  struct file *file;  // File backing the page
  off_t end_offset;   // End offset for the mapped region
};

void frametable_init(void);
bool frametable_load_frame(uint32_t *pd, const void *upage, bool write);
void frametable_unload_frame (uint32_t *pd, const void *upage);
bool frametable_lock_frame(uint32_t *pd, const void *upage, bool write);
void frametable_unlock_frame(uint32_t *pd, const void *upage);

#endif /* vm/frame.h */