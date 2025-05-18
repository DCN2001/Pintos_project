#include <stdio.h>
#include <stdbool.h>
#include <list.h>
#include <hash.h>
#include <debug.h>
#include <string.h>
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"

struct page_info *pageinfo_create (void)
{
  struct page_info *page_info;
  
  page_info = calloc (1, sizeof *page_info);
  return page_info;
}

void pageinfo_release (struct page_info *page_info)
{
  free (page_info);
}

void pageinfo_set_upage (struct page_info *page_info, const void *upage)
{
  page_info->upage = upage;
}

void pageinfo_set_type (struct page_info *page_info, int type)
{
  page_info->type = type;
}

void pageinfo_set_writable (struct page_info *page_info, int writable)
{
  page_info->writable = writable;
}

void pageinfo_set_pagedir (struct page_info *page_info, uint32_t *pd)
{
  page_info->pd = pd;
}

void pageinfo_set_fileinfo (struct page_info *page_info, struct file *file, off_t end_offset)
{
  page_info->data.file_info.file = file;
  page_info->data.file_info.end_offset = end_offset;
}

void pageinfo_set_kpage (struct page_info *page_info, const void *kpage)
{
  page_info->data.kpage = kpage;
}
