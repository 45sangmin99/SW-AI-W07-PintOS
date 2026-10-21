#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "include/lib/kernel/stdio.h"

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
	struct thread *tc = thread_current();
	switch (f->R.rax)
	{
	case SYS_WRITE:
		if(f->R.rdi == 1){
			putbuf(f->R.rsi, f->R.rdx);
		}
		break;
	case SYS_WAIT:
		f->R.rax = process_wait(f->R.rdi);
		return;
	case SYS_EXIT:
		if (tc->child_status != NULL) {
    		tc->child_status->exit_status = f->R.rdi;
		}
		printf("%s: exit(%d)\n",thread_name() ,f->R.rdi);
		thread_exit();
		return;
	case SYS_FORK:
		process_fork(thread_name(),f);
		return;

	case SYS_EXEC:
	{
		char *fn_copy;
		const char *s = (const char *) f->R.rdi; /* 시작주소변수 */
		bool result = false; /* 결과변수 */
		size_t i;
		const char *p;
		
		if(s!=NULL){
			for(i=0; i<PGSIZE; i++){
				p = s+i;
				if(!is_user_vaddr(p) || pml4_get_page(thread_current()->pml4, p) == NULL){
					break;
				}
				if(*p == '\0'){
					result = true;
					break;
				}
			}
		}
			
		/* 문자열 끝까지 매핑여부 검사 */
		if (!result) {
			f->R.rax = -1;
			return;
		}
		fn_copy = palloc_get_page(0);

		/* 페이지 할당 검사 */
		if(fn_copy == NULL){
			f->R.rax = -1;
			return;
		}
		strlcpy(fn_copy,(char *)f->R.rdi,PGSIZE);
		process_exec(fn_copy);
		if (tc->child_status != NULL) {
    		tc->child_status->exit_status = -1;
		}
		printf("%s: exit(%d)\n", thread_name(), -1);
		thread_exit();
		return;
	}
	default:
		break;
	}
	// TODO: Your implementation goes here.
	// printf("system call!\n");
	// thread_exit();
}
