#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
#include "threads/thread.h"

void syscall_init (void);

void exit (int status);
tid_t fork (const char *thread_name, struct intr_frame *if_);
void exec (const char *cmd_line);
int wait (tid_t tid);
bool create (const char *file_name, unsigned initial_size);
bool remove (const char *file_name);
int open (const char *file_name);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

void check_address (void *addr);
struct file *get_file_with_fd (int fd);

static int64_t get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);

#endif /* userprog/syscall.h */
