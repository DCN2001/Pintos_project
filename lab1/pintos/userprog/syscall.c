#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include <devices/shutdown.h>
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

#include <string.h>
#include <filesys/file.h>
#include <devices/input.h>
#include <threads/malloc.h>
#include <threads/palloc.h>
#include "process.h"
#include "pagedir.h"
#include <threads/vaddr.h>
#include <filesys/filesys.h>

#define MAX_SYSCALL 20
#define BUF_MAX 200

// lab01 Hint - Here are the system calls you need to implement.

/* System call for process. */
struct lock filesys_lock;
void  sys_halt(void);
void  sys_exit(int status);
pid_t sys_exec(const char *cmdline);
int   sys_wait(pid_t pid);

/* System call for file. */
bool     sys_create(const char *file, unsigned initial_size);
bool     sys_remove(const char *file);
int      sys_open(const char *file);
int      sys_filesize(int fd);
int      sys_read(int fd, void *buffer, unsigned size);
int      sys_write(int fd, const void *buffer, unsigned size);
void     sys_seek(int fd, unsigned position);
unsigned sys_tell(int fd);
void     sys_close(int fd);

static bool valid_mem_access (const void *);
static struct openfile * getFile (int);

static void (*syscalls[MAX_SYSCALL])(struct intr_frame *) = {
  [SYS_HALT] = sys_halt,
  [SYS_EXIT] = sys_exit,
  [SYS_EXEC] = sys_exec,
  [SYS_WAIT] = sys_wait,
  [SYS_CREATE] = sys_create,
  [SYS_REMOVE] = sys_remove,
  [SYS_OPEN] = sys_open,
  [SYS_FILESIZE] = sys_filesize,
  [SYS_READ] = sys_read,
  [SYS_WRITE] = sys_write,
  [SYS_SEEK] = sys_seek,
  [SYS_TELL] = sys_tell,
  [SYS_CLOSE] = sys_close
};

static void syscall_handler (struct intr_frame *);

static int get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);


void syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
}

/* Verify that the user pointer is valid */
static bool
valid_mem_access (const void *up)
{
	struct thread *t = thread_current ();

	if (up == NULL)
		return false;
  if (is_kernel_vaddr (up))
    return false;
  if (pagedir_get_page (t->pagedir, up) == NULL)
   	return false;
  uint8_t *check_byteptr = (uint8_t *) up;
  for (uint8_t i = 0; i < 4; i++) 
  {
    // printf("check_byteptr + i: %p\n", check_byteptr + i);
    if (get_user(check_byteptr + i) == -1)
    {
      t -> exit_status = -1;
      thread_exit();
    }
    // printf("%d\n\n", i);
  }
  // printf("valid_mem_access\n");
	return true;
}

static void syscall_handler (struct intr_frame *f UNUSED) 
{
  // printf ("system call!\n");
  void *esp = f->esp;
  uint32_t *eax = &f->eax;
  int syscall_num;

  // printf(((int *) esp) );
  // printf(is_user_vaddr(esp) ? "user\n" : "kernel\n");
  
  if(!valid_mem_access ( ((int *) esp) ))
    sys_exit (-1);
  if(!valid_mem_access ( ((int *) esp)+1 ))
    sys_exit (-1);

  // if (!valid_mem_access (*(((char **) esp) + 1)))
  //   sys_exit (-1);

  // check_ptr2(esp+1);
  syscall_num = *((int *) esp);

  // printf("syscall_num: %d\n", syscall_num);

  if (syscall_num >= 0 && syscall_num < MAX_SYSCALL) {
      switch (syscall_num){
        case SYS_HALT:{
          sys_halt();
          break;
        }
        case SYS_EXIT:{
          int status = *(((int *) esp) + 1);
          sys_exit(status);
          break;
        }
        case SYS_EXEC:{
          // printf("in SYS_EXEC\n");
          // printf("\n");
          // printf("esp: %p\n", esp);
          
          const char *cmd_line = *(((char **) esp) + 1);

  	      *eax = (uint32_t) sys_exec (cmd_line);
          break;
        }
        case SYS_WAIT: {
          pid_t pid = *(((pid_t *) esp) + 1);
  	      *eax = (uint32_t) sys_wait (pid);
          break;
        }
        case SYS_CREATE: {
          const char *file = *(((char **) esp) + 1);
  	      unsigned initial_size = *(((unsigned *) esp) + 2);
  	      *eax = (uint32_t) sys_create (file, initial_size);
          break;
        }
        case SYS_REMOVE: {
          const char *file = *(((char **) esp) + 1);
  	      *eax = (uint32_t) sys_remove (file);
          break;
        }
        case SYS_OPEN: {
          const char *file = *(((char **) esp) + 1);
  	      *eax = (uint32_t) sys_open (file);
          break;
        }
        case SYS_FILESIZE: {
          int fd = *(((int *) esp) + 1);
  	      *eax = (uint32_t) sys_filesize (fd);
          break;
        }
        case SYS_READ: {
          int fd = *(((int *) esp) + 1);
  	      void *buffer = (void *) *(((int **) esp) + 2);
  	      unsigned size = *(((unsigned *) esp) + 3);
  	      *eax = (uint32_t) sys_read (fd, buffer, size);
          break;
        }
        case SYS_WRITE: {
          int fd = *(((int *) esp) + 1);
          const void *buffer = (void *) *(((int **) esp) + 2);
          unsigned size = *(((unsigned *) esp) + 3);
          // printf("buffer: %s\n", buffer);

          *eax = (uint32_t) sys_write (fd, buffer, size);
          break;
        }
        case SYS_SEEK: {
          int fd = *(((int *) esp) + 1);
          unsigned position = *(((unsigned *) esp) + 2);
          sys_seek (fd, position);
          break;
        }
        case SYS_TELL: {
          int fd = *(((int *) esp) + 1);
  	      *eax = (uint32_t) sys_tell (fd);
          break;
        }
        case SYS_CLOSE: {
          int fd = *(((int *) esp) + 1);
  	      sys_close (fd);
          break;
        }
      }

      // printf("syscall_num: %d\n", syscall_num);
      // syscalls[syscall_num](f);
  } else {
      sys_exit(-1);
  }
  // thread_exit();
}

/* System Call: void halt (void)
    Terminates Pintos by calling shutdown_power_off() (declared in devices/shutdown.h). 
*/
void sys_halt(void)
{
  shutdown_power_off();
}

void sys_exit(int status){
  struct thread *cur = thread_current();
  cur->exit_status = status;
  thread_exit();
}

pid_t sys_exec(const char *cmdline){
  // printf("in sys_exec\n");
  // printf("cmdline: %s\n", cmdline);
  tid_t child_tid = TID_ERROR;

  if(!valid_mem_access(cmdline))
  {
    // printf("invalid mem access\n");
    sys_exit (-1);
  } else {
    // printf("valid mem access\n");
  }

  // printf("cmdline: %s\n", cmdline);
  child_tid = process_execute (cmdline);
  // printf("child_tid: %d\n", child_tid);

	return child_tid;
}

int sys_wait(pid_t pid){
  return process_wait (pid);
}

bool sys_create(const char *file, unsigned initial_size){
   bool retval;
  if(valid_mem_access(file)) {
    lock_acquire (&filesys_lock);
    retval = filesys_create (file, initial_size);
    lock_release (&filesys_lock);
    return retval;
  }
	else
    sys_exit (-1);

  return false;
}
bool sys_remove(const char *file){
  bool retval;
	if(valid_mem_access(file)) {
    lock_acquire (&filesys_lock);
    retval = filesys_remove (file);
    lock_release (&filesys_lock);
    return retval;
  }
  else
    sys_exit (-1);

  return false;
}

int sys_open(const char *file){
  if(valid_mem_access ((void *) file)) {
    struct openfile *new = palloc_get_page (0);
    new->fd = thread_current ()->next_fd;
    thread_current ()->next_fd++;
    lock_acquire (&filesys_lock);
    new->file = filesys_open(file);
    lock_release (&filesys_lock);
    if (new->file == NULL)
      return -1;
    list_push_back(&thread_current ()->openfiles, &new->elem);
    return new->fd;
  }
	else
    sys_exit (-1);

	return -1;
}

int sys_filesize(int fd){
  int retval;
  struct openfile *of = NULL;
	of = getFile (fd);
  if (of == NULL)
    return 0;
  lock_acquire (&filesys_lock);
  retval = file_length (of->file);
  lock_release (&filesys_lock);
  return retval;
}

int sys_read(int fd, void *buffer, unsigned size){
  int bytes_read = 0;
  char *bufChar = NULL;
  struct openfile *of = NULL;
	if (!valid_mem_access(buffer))
    sys_exit (-1);
  bufChar = (char *)buffer;
	if(fd == 0) {
    while(size > 0) {
      input_getc();
      size--;
      bytes_read++;
    }
    return bytes_read;
  }
  else {
    of = getFile (fd);
    if (of == NULL)
      return -1;
    lock_acquire (&filesys_lock);
    bytes_read = file_read (of->file, buffer, size);
    lock_release (&filesys_lock);
    return bytes_read;
  }
}

int sys_write(int fd, const void *buffer, unsigned size){
  // printf("in sys_write\n");
  int bytes_written = 0;
  char *bufChar = NULL;
  struct openfile *of = NULL;
	if (!valid_mem_access(buffer)){
    // printf("invalid mem access\n");
		sys_exit (-1);
  }
  bufChar = (char *)buffer;
  if(fd == 1) {
    /* break up large buffers */
    while(size > BUF_MAX) {
      putbuf(bufChar, BUF_MAX);
      bufChar += BUF_MAX;
      size -= BUF_MAX;
      bytes_written += BUF_MAX;
    }
    putbuf(bufChar, size);
    bytes_written += size;
    return bytes_written;
  }
  else {
    of = getFile (fd);
    if (of == NULL)
      return 0;
    lock_acquire (&filesys_lock);
    bytes_written = file_write (of->file, buffer, size);
    lock_release (&filesys_lock);
    return bytes_written;
  }
}

void sys_seek(int fd, unsigned position){
  struct openfile *of = NULL;
  of = getFile (fd);
  if (of == NULL)
    return;
  lock_acquire (&filesys_lock);
  file_seek (of->file, position);
  lock_release (&filesys_lock);
}

unsigned sys_tell(int fd) {
  unsigned retval;
	struct openfile *of = NULL;
  of = getFile (fd);
  if (of == NULL)
    return 0;
  lock_acquire (&filesys_lock);
  retval = file_tell (of->file);
  lock_release (&filesys_lock);
  return retval;
}

void sys_close(int fd) {
  struct openfile *of = NULL;
  of = getFile (fd);
  if (of == NULL)
    return;
  lock_acquire (&filesys_lock);
  file_close (of->file);
  lock_release (&filesys_lock);
  list_remove (&of->elem);
  palloc_free_page (of);
}

static struct openfile *
getFile (int fd)
{
  struct thread *t = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&t->openfiles); e != list_end (&t->openfiles);
       e = list_next (e))
    {
      struct openfile *of = list_entry (e, struct openfile, elem);
      if(of->fd == fd)
        return of;
    }
  return NULL;
}


/* Reads a byte at user virtual address UADDR.
UADDR must be below PHYS_BASE.
Returns the byte value if successful, -1 if a segfault
occurred. */
static int
get_user (const uint8_t *uaddr)
{
  // printf("get_user\n");
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:" : "=&a" (result) : "m" (*uaddr));
  // printf("result: %d\n", result);
  return result;
}
/* Writes BYTE to user address UDST.
UDST must be below PHYS_BASE.
Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:" : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}