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
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_EXEC:
			exec(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
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

void
exec (const char *cmd_line) {
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

bool
create (const char *file_name, unsigned initial_size) {
	if (file_name == NULL) {
		exit(-1);
		NOT_REACHED();
	}
	return filesys_create (file_name, initial_size);
}

bool
remove (const char *file_name) {
	if (file_name == NULL) {
		exit(-1);
		NOT_REACHED();
	}
	return filesys_remove (file_name);
}

/* Write */
int 
write (int fd, const void *buffer, unsigned length) {
	putbuf (buffer, length);
	return length;
}
