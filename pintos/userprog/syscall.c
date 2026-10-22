#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "include/lib/kernel/stdio.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f)
{
		
	switch (f->R.rax)
	{
	case SYS_CREATE:
		if(!f->R.rdi || !is_user_vaddr(f->R.rdi) || !pml4_get_page(thread_current()->pml4, f->R.rdi)){
			break;
		}
		f->R.rax = filesys_create(f->R.rdi, f->R.rsi);
		return;
	case SYS_REMOVE:
		if(!f->R.rdi || !is_user_vaddr(f->R.rdi) || !pml4_get_page(thread_current()->pml4, f->R.rdi)){
			break;
		}
		filesys_remove(f->R.rdi);
		return;
	case SYS_OPEN:{
		if(!f->R.rdi || !is_user_vaddr(f->R.rdi) || !pml4_get_page(thread_current()->pml4, f->R.rdi)){
			break;
		}
		struct file_descriptor *fd = malloc(sizeof(struct file_descriptor));
		fd->file = filesys_open(f->R.rdi);
		fd->fd = fd->file?thread_current()->next_fd++:-1;
		list_push_back(&thread_current()->file_descriptor_list, &fd->file_descriptor_list_elem);
		f->R.rax = fd->fd;
		return;
	}
	case SYS_CLOSE:{
		if(!f->R.rdi)break;
		struct file_descriptor *fd = find_file_descriptor_by_fd(f->R.rdi);
		if(!fd)break;
		f->R.rax = fd->fd;
		free(fd->file);
		list_remove(&fd->file_descriptor_list_elem);
		return;
	}
	case SYS_FILESIZE:
		if(!find_file_descriptor_by_fd(f->R.rdi) || !find_file_descriptor_by_fd(f->R.rdi)->file)break;
		f->R.rax = file_length(find_file_descriptor_by_fd(f->R.rdi)->file);
		return;
	case SYS_READ:{
		if(!f->R.rdi)break;
		if(!f->R.rsi || !is_user_vaddr(f->R.rsi) || !pml4_get_page(thread_current()->pml4, f->R.rsi))break;
		struct file_descriptor *fd = find_file_descriptor_by_fd(f->R.rdi);
		if(!fd)break;
		f->R.rax = file_read(fd->file, f->R.rsi, f->R.rdx);
		return;
	}
	case SYS_WRITE:{
		if(!f->R.rdi)break;	
		if(!f->R.rsi || !is_user_vaddr(f->R.rsi) || !pml4_get_page(thread_current()->pml4, f->R.rsi))break;
		if(f->R.rdi == 1){
			putbuf(f->R.rsi, f->R.rdx);
		}else{
			struct file_descriptor *fd = find_file_descriptor_by_fd(f->R.rdi);
			if(!fd)break;
			f->R.rax = file_write(fd->file, f->R.rsi, f->R.rdx);
		}
		return;
	}
	case SYS_SEEK:{
		struct file_descriptor *fd = find_file_descriptor_by_fd(f->R.rdi);
		file_seek(fd->file, f->R.rsi);
		return;
	}
	case SYS_WAIT:
		f->R.rax = process_wait(f->R.rdi);
		return;
	case SYS_EXIT:
		printf("%s: exit(%d)\n",thread_name() ,f->R.rdi);
		if(thread_current()->parent){
			find_child_by_tid(&thread_current()->parent->children, thread_current()->tid)->exit_status = f->R.rdi;
			find_child_by_tid(&thread_current()->parent->children, thread_current()->tid)->is_exit = true;
		}
		thread_exit();
		return;
	case SYS_FORK:
		f->R.rax = process_fork(thread_name(),f);
		return;
	case SYS_EXEC:
		if(!f->R.rdi || !is_user_vaddr(f->R.rdi) || !pml4_get_page(thread_current()->pml4, f->R.rdi))break;
		void *temp = palloc_get_page(0b011);
		strlcpy(temp, f->R.rdi, PGSIZE);
		f->R.rax = process_exec(temp);
		return;
	default:
		printf("%d is unknown systemcall \n", f->R.rax);
		break;
	}
	f->R.rdi = -1;
	printf("%s: exit(%d)\n",thread_name() ,f->R.rdi);
	if(thread_current()->parent)find_child_by_tid(&thread_current()->parent->children,thread_current()->tid)->exit_status = -1;
	thread_exit();
	// TODO: Your implementation goes here.
	// printf("system call!\n");
	// thread_exit();
}
