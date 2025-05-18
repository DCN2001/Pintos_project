#ifndef VM_PAGEINFO_H
#define VM_PAGEINFO_H

#include <stdint.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "vm/frame.h"
#include <list.h>

/* Page types. */
/* Page content is zero. */
#define PAGE_TYPE_ZERO    0x01
/* Page content is loaded from a page size chunk of kernel memory. */
#define PAGE_TYPE_KERNEL  0x02
/* Page is backed by a file. */
#define PAGE_TYPE_FILE    0x04

/* If a page is writable, it will be written back to a file or to swap. */
#define WRITABLE_TO_FILE  0x01
#define WRITABLE_TO_SWAP  0x02

struct file;

/* Metadata for a virtual page in user memory. */
struct page_info
{
  uint8_t type;
  uint8_t writable;
  /* The page directory that is mapping the page. */
  uint32_t *pd;
  /* The user virtual page address corresponding to the page. */
  const void *upage;
  /* If true the page is swapped and its contents can be read back from swap_sector. */
  bool swapped;
  /* Information about the frame backing the page. */
  struct frame *frame;
  union
  {
    struct file_info file_info;     // File-related info if file-backed
    block_sector_t swap_sector;     // Swap block index if swapped
    const void *kpage;              // Kernel virtual address if page is preloaded
  } data;
  /* List element for the associated frame's page_info_list. */
  struct list_elem elem;
};

struct page_info *pageinfo_create (void);
void pageinfo_release (struct page_info *page_info);
void pageinfo_set_upage (struct page_info *page_info, const void *upage);
void pageinfo_set_type (struct page_info *page_info, int type);
void pageinfo_set_writable (struct page_info *page_info, int writable);
void pageinfo_set_pagedir (struct page_info *page_info, uint32_t *pd);
void pageinfo_set_fileinfo (struct page_info *page_info, struct file *file, off_t offset_cnt);
void pageinfo_set_kpage (struct page_info *page_info, const void *kpage);

#endif