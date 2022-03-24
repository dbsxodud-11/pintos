#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>

void syscall_init (void);

void exit (int status);
void exec (const char *cmd_line);
bool create (const char *file_name, unsigned initial_size);
bool remove (const char *file_name);
int write (int fd, const void *buffer, unsigned length);

#endif /* userprog/syscall.h */
