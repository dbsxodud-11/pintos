#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>

void syscall_init (void);

void exit (int status);
void exec (const char *cmd_line);
bool create (const char *file_name, unsigned initial_size);
bool remove (const char *file_name);
int open (const char *file_name);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, void *buffer, unsigned length);

void check_address (void *addr);
struct file *get_file_with_fd (int fd);

#endif /* userprog/syscall.h */
