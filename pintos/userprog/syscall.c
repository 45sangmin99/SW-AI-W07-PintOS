#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/file.h"

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
	uint64_t fd = f->R.rdi;
	const char * buffer = f->R.rsi;
	size_t size = f->R.rdx;
	struct thread * t = thread_current();

	// TODO: Your implementation goes here.
	if(f->R.rax == SYS_WRITE){
		if(fd == 1){
			putbuf(buffer , size);
			f->R.rax = size;
			return;
		}
		if(fd<64){
			if(t->fd_table[fd] != NULL){
    			f->R.rax = file_write(t->fd_table[fd],buffer, size);
				return;
			}
		}
	}
	if(f->R.rax == SYS_OPEN){
		struct file * open_file = filesys_open((char *)f->R.rdi);
		int new_fd = 2;
		if(open_file == NULL){
			f->R.rax = -1;
			return;
		}
	
		for(; new_fd <= 63; new_fd++){
			if(t->fd_table[new_fd] == NULL){
				t->fd_table[new_fd] = open_file;
				f->R.rax = new_fd;
				return;
			}
		}
		file_close(open_file);
		f->R.rax = -1;
		return;

	}

	// printf ("system call!\n");
	// thread_exit ();
}
