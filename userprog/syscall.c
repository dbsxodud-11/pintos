#include "devices/input.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include <stdio.h>
#include <stdbool.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/palloc.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/mmu.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// printf ("system call: %d\n", f->R.rax);
	switch (f->R.rax) {
		case SYS_HALT:
			power_off ();
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = fork(f->R.rdi, f);
			break;
		case SYS_EXEC:
			exec(f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = (int) create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		default:
			exit(-1);
			break;
	}
}

/* Exit: Terminates the current user program, returning status to the kernel */
void 
exit (int status) {
	struct thread *curr = thread_current ();
	curr->exit_status = status;
	printf("%s: exit(%d)\n", curr->name, curr->exit_status);
	thread_exit ();
}

tid_t
fork (const char *thread_name, struct intr_frame *if_) {
	struct thread *curr = thread_current ();
	memcpy (&curr->parent_if, if_, sizeof (struct intr_frame));

	tid_t child_tid = process_fork (thread_name, if_);
	if (child_tid == TID_ERROR)
		return TID_ERROR;
	return child_tid;
}

void
exec (const char *cmd_line) {
	check_address (cmd_line);
	char *cmd_line_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	cmd_line_copy = palloc_get_page (0);
	if (cmd_line_copy == NULL)
		return TID_ERROR;
	strlcpy (cmd_line_copy, cmd_line, PGSIZE);

	if (process_exec (cmd_line_copy) == -1)
		exit(-1);
}

int
wait (tid_t tid) {
	// struct thread *child_thread = get_child_thread_with_tid (tid);
	// if (child_thread == NULL)
	// 	return -1;

	// sema_down (&child_thread->sema[2]);
	// int status = child_thread->exit_status;

	// sema_up (&child_thread->sema[1]);
	// list_remove (&child_thread->child_elem);
	// return status;
	return process_wait (tid);
}

bool
create (const char *file_name, unsigned initial_size) {
	check_address (file_name);
	bool result = filesys_create (file_name, initial_size);
	return result;
}

bool
remove (const char *file_name) {
	check_address (file_name);
	return filesys_remove (file_name);
}

/* open: Opens the file called file. Returns a nonnegative integer handle called a "file descriptor" (fd),
 or -1 if the file could not be opened. File descriptors numbered 0 and 1 are reserved for the console: 
 fd 0 (STDIN_FILENO) is standard input, fd 1 (STDOUT_FILENO) is standard output. The open system call will 
 never return either of these file descriptors, which are valid as system call arguments only as explicitly 
 described below. Each process has an independent set of file descriptors. File descriptors are inherited 
 by child processes. When a single file is opened more than once, whether by a single process or different 
 processes, each open returns a new file descriptor. Different file descriptors for a single file are closed
 independently in separate calls to close and they do not share a file position. You should follow the linux
 scheme, which returns integer starting from zero, to do the extra. */

int
open (const char *file_name) {
	check_address (file_name);
	struct file *file = filesys_open (file_name);
	if (file == NULL)
		return -1;	

	struct thread *curr = thread_current ();
	curr->files[curr->fd] = file;

	/* Add code to deny writes to files in use as executables */
	if (!strcmp (curr->name, file_name))
		file_deny_write (file);
	return curr->fd++;
}

/* filesize: Returns the size, in bytes, of the file open as fd. */
int
filesize (int fd) {
	if (fd < 3)
		return NULL;
	struct file *file = get_file_with_fd (fd);
	return file_length (file);
}

/* read: Reads size bytes from the file open as fd into buffer. Returns 
the number of bytes actually read (0 at end of file), or -1 if the file 
could not be read (due to a condition other than end of file). fd 0 reads 
from the keyboard using input_getc() */
int
read (int fd, void *buffer, unsigned size) {
	check_address (buffer);
	struct file *file = get_file_with_fd (fd);
	if (file == NULL)
		return -1;
	
	// STDIN
	int bytes_read = 0;
	if (fd == 0) {
		char *char_buffer = (char *)buffer;
		while (bytes_read < size) {
			char_buffer = input_getc ();
			*char_buffer++;
			bytes_read++;
			if (char_buffer == "\0") {
				break;
			}
		}
	}
	//STDOUT
	else if (fd == 1) {
		bytes_read = -1;
	}
	//STDERR
	else if (fd == 2) {
		bytes_read = -1;
	}
	else {
		bytes_read = file_read (file, buffer, size);
	}
	return bytes_read;
}

/* write: Writes size bytes from buffer to the open file fd. Returns the number 
of bytes actually written, which may be less than size if some bytes could not 
be written. Writing past end-of-file would normally extend the file, but file 
growth is not implemented by the basic file system. The expected behavior is to 
write as many bytes as possible up to end-of-file and return the actual number
written, or 0 if no bytes could be written at all. fd 1 writes to the console. 
Your code to write to the console should write all of buffer in one call to 
putbuf(), at least as long as size is not bigger than a few hundred bytes (It 
is reasonable to break up larger buffers). Otherwise, lines of text output by 
different processes may end up interleaved on the console, confusing both human 
readers and our grading scripts. */

int 
write (int fd, void *buffer, unsigned size) {
	check_address (buffer);
	struct file *file = get_file_with_fd (fd);
	if (file == NULL)
		return -1;
	
	// STDIN
	int bytes_written = 0;
	if (fd == 0) {
		bytes_written = 0;
	}
	//STDOUT
	else if (fd == 1) {
		putbuf (buffer, size);
		bytes_written = size;
	}
	//STDERR
	else if (fd == 2) {
		bytes_written = -1;
	}
	else {
		bytes_written = file_write (file, buffer, size);
	}
	return bytes_written;
}

void
seek (int fd, unsigned position) {
	struct file *file = get_file_with_fd (fd);
	if ((file == NULL) || (fd < 3))
		return NULL;
	file_seek (file, position);
}

unsigned
tell (int fd) {
	struct file *file = get_file_with_fd (fd);
	if ((file == NULL) || (fd < 3))
		return NULL;
	return file_tell (file);
}

void
close (int fd) {
	struct file *file = get_file_with_fd (fd);
	if (file == NULL)
		return NULL;
	if (fd < 3)
		return NULL;
	else {
		file_close (file);
		thread_current ()->files[fd] = NULL;
	}
}

/* Check Address is valid */
void
check_address (void *addr) {
	// printf("%d\n", is_kernel_vaddr (addr));
	if ((addr == NULL) || (is_kernel_vaddr (addr))) //null pointer or pointer to kernel address space
		exit(-1);
	if (pml4_get_page (thread_current ()->pml4, addr) == NULL) // page fault case
		exit(-1);
}

/* Get file with file descriptor */
struct file *
get_file_with_fd (int fd) {
	if (fd < 0 || fd > 128)
		return NULL;
	return thread_current ()->files[fd];
}

/* Reads a byte at user virtual address UADDR.
 * UADDR must be below KERN_BASE.
 * Returns the byte value if successful, -1 if a segfault
 * occurred. */
static int64_t
get_user (const uint8_t *uaddr) {
    int64_t result;
    __asm __volatile (
    "movabsq $done_get, %0\n"
    "movzbq %1, %0\n"
    "done_get:\n"
    : "=&a" (result) : "m" (*uaddr));
    return result;
}

/* Writes BYTE to user address UDST.
 * UDST must be below KERN_BASE.
 * Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte) {
    int64_t error_code;
    __asm __volatile (
    "movabsq $done_put, %0\n"
    "movb %b2, %1\n"
    "done_put:\n"
    : "=&a" (error_code), "=m" (*udst) : "q" (byte));
    return error_code != -1;
}