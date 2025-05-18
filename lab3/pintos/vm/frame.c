#include <stdio.h>
#include <stdbool.h>
#include <list.h>
#include <hash.h>
#include <debug.h>
#include <string.h>
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"


/* Compute the page-aligned start offset from an end offset. */
static inline off_t offset (off_t end_offset)
{
  return end_offset > 0  ? (end_offset - 1) & ~PGMASK : 0;
}
/* Compute the size of the region between the start offset and end offset. */
static inline off_t size (off_t end_offset)
{
  return end_offset - offset (end_offset);
}


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

/* Global state for managing physical frames. */
static struct lock frame_lock;          // Lock to protect frame table operations
static struct hash read_only_frames;    // Cache for shared, read-only file-backed pages
static struct list frame_list;          // List of frames in use
static struct list_elem *clock_hand;    // Pointer for clock replacement algorithm

// Function declarations
static void frame_init (struct frame *frame);
static struct frame *allocate_frame (void);
static bool load_frame (uint32_t *pd, const void *upage, bool write, bool keep_locked);
static void map_page (struct page_info *page_info, struct frame *frame, const void *upage);
static void wait_for_io_done (struct frame **frame);
static struct frame *lookup_read_only_frame (struct page_info *page_info);
static void *evict_frame (void);
static void *get_frame_to_evict (void);
static unsigned frame_hash (const struct hash_elem *e, void *aux UNUSED);
static bool frame_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);

void
frametable_init (void)
{
  lock_init (&frame_lock);
  list_init (&frame_list);
  clock_hand = list_end (&frame_list);
  hash_init (&read_only_frames, frame_hash, frame_less, NULL);
}

/* Reads data into a frame & maps the user virtual page UPAGE to it.*/
bool
frametable_load_frame(uint32_t *pd, const void *upage, bool write)
{
  return load_frame (pd, upage, write, false);
}

/* Unmap a frame and release all associated resources. Write back to file if modified. */
void
frametable_unload_frame(uint32_t *pd, const void *upage) {
  ASSERT(is_user_vaddr(upage));
  struct page_info *pi = pagedir_get_info(pd, upage);
  if (!pi) return;

  lock_acquire(&frame_lock);
  wait_for_io_done(&pi->frame);

  if (pi->frame) {
    struct frame *f = pi->frame;
    pi->frame = NULL;

    if (list_size(&f->page_info_list) > 1) {
      for (struct list_elem *e = list_begin(&f->page_info_list); e != list_end(&f->page_info_list); e = list_next(e)) {
        if (list_entry(e, struct page_info, elem) == pi) {
          list_remove(e);
          break;
        }
      }
    } else {
      ASSERT(list_entry(list_begin(&f->page_info_list), struct page_info, elem) == pi);
      if ((pi->type & PAGE_TYPE_FILE) && pi->writable == 0)
        hash_delete(&read_only_frames, &f->hash_elem);
      if (clock_hand == &f->list_elem)
        clock_hand = list_next(clock_hand);
      list_remove(&pi->elem);
      list_remove(&f->list_elem);
    }

    pagedir_clear_page(pi->pd, upage);
    lock_release(&frame_lock);

    if (list_empty(&f->page_info_list)) {
      if ((pi->writable & WRITABLE_TO_FILE) && pagedir_is_dirty(pi->pd, upage)) {
        struct file_info *fi = &pi->data.file_info;
        off_t written = process_file_write_at(fi->file, f->kpage, size(fi->end_offset), offset(fi->end_offset));
        ASSERT(written == size(fi->end_offset));
      }
      palloc_free_page(f->kpage);
      ASSERT(f->lock == 0);
      free(f);
    }
  } else {
    lock_release(&frame_lock);
  }

  if (pi->swapped) {
    swap_release(pi->data.swap_sector);
    pi->swapped = false;
  } else if (pi->type & PAGE_TYPE_KERNEL) {
    void *kpage = (void *)pi->data.kpage;
    ASSERT(kpage);
    palloc_free_page(kpage);
    pi->data.kpage = NULL;
  }

  pagedir_set_info(pi->pd, upage, NULL);
  free(pi);
}


/* Identical to frametale_load_frame with the exception that,
   upon return, the frame is locked to prevent it from being evicted. */
bool
frametable_lock_frame(uint32_t *pd, const void *upage, bool write)
{
  return load_frame (pd, upage, write, true);
}

/* Unlocks a frame that was locked with frametable_lock_frame.  NOTE:
   the frame is not unloaded, only unlocked. */
void
frametable_unlock_frame(uint32_t *pd, const void *upage)
{
  struct page_info *page_info;
  
  ASSERT (is_user_vaddr (upage));
  page_info = pagedir_get_info (pd, upage);
  if (page_info == NULL)
    return;
  ASSERT (page_info->frame != NULL);
  lock_acquire (&frame_lock);
  page_info->frame->lock--;
  lock_release (&frame_lock);
}

static bool
load_frame (uint32_t *pd, const void *upage, bool write, bool keep_locked)
{
  struct page_info *pi;
  struct file_info *fi;
  struct frame *f = NULL;
  void *src_kpage;
  off_t read_bytes;
  bool ok = false;

  ASSERT (is_user_vaddr (upage));
  pi = pagedir_get_info (pd, upage);
  if (pi == NULL || (write && !pi->writable))
    return false;

  lock_acquire (&frame_lock);
  wait_for_io_done (&pi->frame);

  ASSERT (pi->frame == NULL || keep_locked);
  if (pi->frame != NULL) {
    if (keep_locked)
      pi->frame->lock++;
    lock_release (&frame_lock);
    return true;
  }

  if ((pi->type & PAGE_TYPE_FILE) && !pi->writable) {
    f = lookup_read_only_frame (pi);
    if (f != NULL) {
      map_page (pi, f, upage);
      f->lock++;
      wait_for_io_done (&f);
      f->lock--;
      ok = true;
    }
  }

  if (f == NULL) {
    f = allocate_frame ();
    if (f != NULL) {
      map_page (pi, f, upage);
      if (pi->swapped || (pi->type & PAGE_TYPE_FILE)) {
        f->io = true;
        f->lock++;
        if (pi->swapped) {
          lock_release (&frame_lock);
          swap_read (pi->data.swap_sector, f->kpage);
          pi->swapped = false;
        } else {
          if (!pi->writable)
            hash_insert (&read_only_frames, &f->hash_elem);
          fi = &pi->data.file_info;
          lock_release (&frame_lock);
          read_bytes = process_file_read_at (fi->file, f->kpage,
                                             size (fi->end_offset),
                                             offset (fi->end_offset));
          ASSERT (read_bytes == size (fi->end_offset));
        }
        lock_acquire (&frame_lock);
        f->lock--;
        f->io = false;
        cond_broadcast (&f->io_done, &frame_lock);
      } else if (pi->type & PAGE_TYPE_KERNEL) {
        src_kpage = (void *) pi->data.kpage;
        ASSERT (src_kpage != NULL);
        memcpy (f->kpage, src_kpage, PGSIZE);
        palloc_free_page (src_kpage);
        pi->data.kpage = NULL;
        pi->type = PAGE_TYPE_ZERO;
      }
      ok = true;
    }
  }

  if (ok && keep_locked)
    f->lock++;

  lock_release (&frame_lock);
  return ok;
}


static void
frame_init (struct frame *frame)
{
  list_init (&frame->page_info_list);
  cond_init (&frame->io_done);
}

static struct frame *
allocate_frame (void)
{
  struct frame *frame;
  void *kpage;
  
  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL)
    {
      frame = calloc (1, sizeof *frame);
      if (frame != NULL)
        {
          frame_init (frame);
          frame->kpage = kpage;
          /* Add the frame to the end of the list so it becomes eligible 
             for eviction. */
          if (!list_empty (&frame_list))
            list_insert (clock_hand, &frame->list_elem);
          else
            {
              list_push_front (&frame_list, &frame->list_elem);
              clock_hand = list_begin (&frame_list);
            }
        }
      else
        palloc_free_page (kpage);
    }
  else
    frame = evict_frame ();
  return frame;
}

static void
map_page (struct page_info *page_info, struct frame *frame, const void *upage)
{
  page_info->frame = frame;
  list_push_back (&frame->page_info_list, &page_info->elem);
  pagedir_set_page (page_info->pd, upage, frame->kpage,
                    page_info->writable != 0);
  pagedir_set_dirty (page_info->pd, upage, false);
  pagedir_set_accessed (page_info->pd, upage, true);
}

static void
wait_for_io_done (struct frame **frame)
{
  while (*frame != NULL && (*frame)->io)
    cond_wait (&(*frame)->io_done, &frame_lock);
}

/* Evicts and returns a free frame. */
static void *
evict_frame (void)
{
  struct frame *f;
  struct page_info *pi;
  struct file_info *fi;
  off_t written;
  block_sector_t sector;
  struct list_elem *e;
  bool is_dirty = false;

  f = get_frame_to_evict();

  // Unmap and check dirty bits
  for (e = list_begin (&f->page_info_list);
       e != list_end (&f->page_info_list); e = list_next (e))
    {
      pi = list_entry (e, struct page_info, elem);
      is_dirty |= pagedir_is_dirty (pi->pd, pi->upage);
      pagedir_clear_page (pi->pd, pi->upage);  // Force page fault on next access
    }

  // Handle dirty page or swap-only
  if (is_dirty || (pi->writable & WRITABLE_TO_SWAP))
    {
      ASSERT (pi->writable != 0);
      f->io = true;
      f->lock++;

      if (pi->writable & WRITABLE_TO_FILE)
        {
          fi = &pi->data.file_info;
          lock_release (&frame_lock);
          written = process_file_write_at (fi->file, f->kpage,
                                           size (fi->end_offset),
                                           offset (fi->end_offset));
          ASSERT (written == size (fi->end_offset));
        }
      else
        {
          lock_release (&frame_lock);
          sector = swap_write (f->kpage);
        }

      lock_acquire (&frame_lock);
      f->lock--;
      f->io = false;
      cond_broadcast (&f->io_done, &frame_lock);
    }
  else if ((pi->type & PAGE_TYPE_FILE) && pi->writable == 0)
    {
      ASSERT (hash_find (&read_only_frames, &f->hash_elem) != NULL);
      hash_delete (&read_only_frames, &f->hash_elem);
    }

  // Finalize eviction
  for (e = list_begin (&f->page_info_list); e != list_end (&f->page_info_list); )
    {
      pi = list_entry (list_front (&f->page_info_list), struct page_info, elem);
      pi->frame = NULL;
      if (pi->writable & WRITABLE_TO_SWAP)
        {
          pi->swapped = true;
          pi->data.swap_sector = sector;
        }
      e = list_remove (e);
    }

  memset (f->kpage, 0, PGSIZE);
  return f;
}


/* Implementation of the clock page replacement algorithm. */ 
static void *
get_frame_to_evict (void)
{
  struct frame *cur, *start, *victim = NULL;
  struct page_info *pi;
  struct list_elem *e;
  bool accessed;

  ASSERT (!list_empty (&frame_list));
  start = list_entry (clock_hand, struct frame, list_elem);
  cur = start;

  do {
    accessed = false;
    ASSERT (!list_empty (&cur->page_info_list));

    for (e = list_begin (&cur->page_info_list);
         e != list_end (&cur->page_info_list); e = list_next (e))
      {
        pi = list_entry (e, struct page_info, elem);
        accessed |= pagedir_is_accessed (pi->pd, pi->upage);
        pagedir_set_accessed (pi->pd, pi->upage, false);
      }

    if (!accessed && cur->lock == 0)
      victim = cur;

    clock_hand = list_next (clock_hand);
    if (clock_hand == list_end (&frame_list))
      clock_hand = list_begin (&frame_list);
    cur = list_entry (clock_hand, struct frame, list_elem);

  } while (!victim && cur != start);

  if (victim == NULL)
    {
      ASSERT (cur == start);
      if (cur->lock > 0)
        PANIC ("no frame available for eviction");

      victim = cur;
      clock_hand = list_next (clock_hand);
      if (clock_hand == list_end (&frame_list))
        clock_hand = list_begin (&frame_list);
    }

  return victim;
}


static struct frame *
lookup_read_only_frame (struct page_info *page_info)
{
  struct frame frame;
  struct hash_elem *e;

  list_init (&frame.page_info_list);
  list_push_back (&frame.page_info_list, &page_info->elem);
  e = hash_find (&read_only_frames, &frame.hash_elem);
  return e != NULL ? hash_entry (e, struct frame, hash_elem) : NULL;
}

static unsigned
frame_hash (const struct hash_elem *e, void *aux UNUSED)
{
  struct frame *frame = hash_entry (e, struct frame, hash_elem);
  struct page_info *page_info;
  block_sector_t sector;

  ASSERT (!list_empty (&frame->page_info_list));
  page_info = list_entry (list_front (&frame->page_info_list),
                          struct page_info, elem);
  ASSERT (page_info->type & PAGE_TYPE_FILE && page_info->writable == 0);
  sector = inode_get_inumber (file_get_inode (page_info->data.file_info.file));
  return hash_bytes (&sector, sizeof sector)
    ^ hash_bytes (&page_info->data.file_info.end_offset,
                  sizeof page_info->data.file_info.end_offset);
}

static bool
frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
            void *aux UNUSED)
{
  struct frame *frame_a = hash_entry (a_, struct frame, hash_elem);
  struct frame *frame_b = hash_entry (b_, struct frame, hash_elem);
  struct page_info *page_info_a, *page_info_b;
  block_sector_t sector_a, sector_b;
  struct inode *inode_a, *inode_b;

  ASSERT (!list_empty (&frame_a->page_info_list));
  ASSERT (!list_empty (&frame_b->page_info_list));
  page_info_a = list_entry (list_front (&frame_a->page_info_list),
                            struct page_info, elem);
  page_info_b = list_entry (list_front (&frame_b->page_info_list),
                            struct page_info, elem);
  inode_a = file_get_inode (page_info_a->data.file_info.file);
  inode_b = file_get_inode (page_info_b->data.file_info.file);
  sector_a = inode_get_inumber (inode_a);
  sector_b = inode_get_inumber (inode_b);
  if (sector_a < sector_b)
    return true;
  else if (sector_a > sector_b)
    return false;
  else
    if (page_info_a->data.file_info.end_offset
        < page_info_b->data.file_info.end_offset)
      return true;
    else
      return false;
}