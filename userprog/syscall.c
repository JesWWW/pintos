#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include <devices/shutdown.h>
#include <stdint.h>
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

// lab01 Hint - Here are the system calls you need to implement.

static void syscall_handler(struct intr_frame *f);

/* --- Function  --- */
void *check_pointer(const void *vaddr);
void sys_exit_status(int status);
void check_pointer_range(const void *vaddr, size_t size);
void check_user_string(const char *str);
bool is_valid_pointer(void *esp, uint8_t argc);
struct thread_file *get_thread_file(int fd);
static int get_user(const uint8_t *vaddr);
/* System call for process. */

void sys_halt(void);
void sys_exit(struct intr_frame* f);
void sys_exec(struct intr_frame* f);
void sys_wait(struct intr_frame* f);

/* System call for file. */
void sys_create(struct intr_frame* f);
void sys_remove(struct intr_frame* f);
void sys_open(struct intr_frame* f);
void sys_filesize(struct intr_frame* f);
void sys_read(struct intr_frame* f);
void sys_write(struct intr_frame* f);
void sys_seek(struct intr_frame* f);
void sys_tell(struct intr_frame* f);
void sys_close(struct intr_frame* f);


/* --- check syscall arguments --- */
#define CHECK(n) (*(int *)check_pointer(((int *)(f->esp)) + (n)))



static void (*syscalls[MAX_SYSCALL])(struct intr_frame *) = {
    [SYS_HALT] = (void (*)(struct intr_frame *))sys_halt,
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


static void syscall_handler(struct intr_frame *f UNUSED) {
    
  if (!is_user_vaddr(f->esp)) {
      thread_exit(); 
  }
    int type = CHECK(0);
    if (type <= 0 || type >= MAX_SYSCALL || syscalls[type] == NULL) {
        thread_current()->status_exit = -1;
        thread_exit();
    }
    syscalls[type](f);
}

void syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* --- Pointer Validation --- */
void sys_exit_status(int status) {
  thread_current()->status_exit = status;
  thread_exit();
}
void *check_pointer(const void *vaddr) {
  if (!is_user_vaddr(vaddr) || pagedir_get_page(thread_current()->pagedir, vaddr) == NULL) {
      sys_exit_status(-1);  
  }

  // Try to read 4 bytes from vaddr, abort if any fail
  for (int i = 0; i < 4; i++) {
      if (get_user((uint8_t *)vaddr + i) == -1) {
          thread_exit();
      }
  }

  return (void *)vaddr;
}

void check_pointer_range(const void *vaddr, size_t size) {
  for (size_t i = 0; i < size; i++) {
      check_pointer((uint8_t *)vaddr + i);
  }
}

void check_user_string(const char *str) {
  while (true) {
      check_pointer(str);
      if (*str == '\0') break;
      str++;
  }
}

bool is_valid_pointer(void *esp, uint8_t argc) {
  for (int i = 0; i <= argc; i++) {
      if (!is_user_vaddr((int *)esp + i)) return false;
  }
  return true;
}

/* --- File descriptor handling  --- */

struct thread_file *get_thread_file(int file_id) {
  struct list_elem *e;
  struct thread_file * temp = NULL;
  struct list *files = &thread_current ()->files;
  for (e = list_begin (files); e != list_end (files); e = list_next (e)){
    temp = list_entry (e, struct thread_file, file_element);
    if (file_id == temp->fd)
      return temp;
  }
  return NULL;
}

/* --- Helper: safely read user memory --- */
static int get_user(const uint8_t *vaddr) {
  int result;
  if (!is_user_vaddr(vaddr) || pagedir_get_page(thread_current()->pagedir, vaddr) == NULL) {
    thread_exit(); 
  }
  asm("movl $1f, %0\n"
      "movzbl %1, %0\n"
      "1:"
      : "=&a"(result)
      : "m"(*vaddr));
  return result;
}

/* --- System Calls --- */

void sys_halt(void) {
    shutdown_power_off();
}

void sys_exit(struct intr_frame *f) {
    int exit_code = CHECK(1);
    thread_current()->status_exit = exit_code;
    thread_exit();
}

void sys_exec(struct intr_frame *f) {
    check_pointer_range(f->esp, 4);
    const char *cmd_line = (const char *)CHECK(1);
    check_user_string(cmd_line);
    // Execute the process
    f->eax = process_execute(cmd_line);
    
}

void sys_wait(struct intr_frame *f) {
    int pid = CHECK(1);
    f->eax = process_wait(pid);
}

void sys_write(struct intr_frame *f) {
    int fd = CHECK(1);
    char *buffer = (char *)CHECK(2);
    off_t size = CHECK(3);
    check_pointer_range(buffer, size);
    if (fd == 1) {
        putbuf(buffer, size);
        f->eax = size;
    } else {
        struct thread_file *file = get_thread_file(fd);
        if (file) {
            create_lock();
            f->eax = file_write(file->file, buffer, size);
            end_lock();
        } else {
            f->eax = 0;
        }
    }
}

void sys_create(struct intr_frame *f) {
    check_pointer_range(f->esp, 5);
    const char *file = (const char *)CHECK(1);
    check_user_string(file); 
    unsigned size = CHECK(2);
    create_lock();
    f->eax = filesys_create(file, size);
    end_lock();
}

void sys_remove(struct intr_frame *f) {
    check_pointer_range(f->esp, 2);
    const char *file = (const char *)CHECK(1);
    create_lock();
    f->eax = filesys_remove(file);
    end_lock();
}

void sys_open(struct intr_frame *f) {
    check_pointer_range(f->esp, 2);
    const char *file = (const char *)CHECK(1);
    check_user_string(file);
    // Check if the file name is valid
    if (file == NULL || !is_user_vaddr(file)) {
      f->eax = -1;
      return;
  }
    create_lock();
    struct file *opened_file = filesys_open(file);
    struct thread *t = thread_current();
    if (opened_file) {
      if (strstr(file, ".exe") != NULL) {
        file_deny_write(opened_file);  // 禁止對執行檔寫入
      }
        struct thread_file *file_entry = malloc(sizeof(struct thread_file));
        file_entry->fd = t->file_fd++;
        file_entry->file = opened_file;
        list_push_back(&t->files, &file_entry->file_element);
        f->eax = file_entry->fd;
    } else {
        f->eax = -1;
    }
    end_lock();
}

void sys_filesize(struct intr_frame *f) {
    int fd = CHECK(1);
    struct thread_file *file_entry = get_thread_file(fd);
    if (file_entry) {
        create_lock();
        f->eax = file_length(file_entry->file);
        end_lock();
    } else {
        f->eax = -1;
    }
}

void sys_seek(struct intr_frame *f) {
    int fd = CHECK(1);
    struct thread_file *file_entry = get_thread_file(fd);
    if (file_entry) {
        create_lock();
        file_seek(file_entry->file, CHECK(2));
        end_lock();
    }
}

void sys_tell(struct intr_frame *f) {
    int fd = CHECK(1);
    struct thread_file *file_entry = get_thread_file(fd);
    if (file_entry) {
        create_lock();
        f->eax = file_tell(file_entry->file);
        end_lock();
    } else {
        f->eax = -1;
    }
}

void sys_close(struct intr_frame *f) {
    int fd = CHECK(1);
    if (fd == 0 || fd == 1) {
        return;
    }
    struct thread_file *file_entry = get_thread_file(fd);
    if (file_entry) {
        create_lock();
        file_close(file_entry->file);
        end_lock();
        list_remove(&file_entry->file_element);
        free(file_entry);
    }
}
void sys_read(struct intr_frame *f) {
  int fd = CHECK(1);
  void *buffer = (void *)CHECK(2);
  off_t size = CHECK(3);

  // 1. Validate the buffer pointer and size
  check_pointer_range(buffer, size);

  int bytes_read = -1; // Initialize to an error value

  if (fd == 0) {
      // Read from standard input
      uint8_t *local_buffer = palloc_get_page(PAL_USER | PAL_ZERO);
      if (local_buffer == NULL) {
          f->eax = -1;
          return;
      }
      for (off_t i = 0; i < size; i++) {
          local_buffer[i] = input_getc();
      }
      memcpy(buffer, local_buffer, size);
      palloc_free_page(local_buffer);
      bytes_read = size;
  } else {
      // Read from a file
      struct thread_file *file_entry = get_thread_file(fd);
      if (file_entry) {
          create_lock();
          bytes_read = file_read(file_entry->file, buffer, size);
          end_lock();
      }
  }
  f->eax = bytes_read;
}