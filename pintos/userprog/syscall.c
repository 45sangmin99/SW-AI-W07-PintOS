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
#include "userprog/process.h"
#include "threads/palloc.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/init.h"
#include "devices/input.h"

#include "include/lib/kernel/stdio.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

static void exit_helper(const char *name, int status) NO_RETURN;

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
	case SYS_HALT:
	{
		power_off();
		NOT_REACHED();
	}
	case SYS_EXIT:
	{
		exit_helper(thread_name(), (int)f->R.rdi);
	}
	case SYS_FORK:
	{
		tid_t success;
		const char *t_name;

		t_name = f->R.rdi;
		success = process_fork(t_name, f);

		/* 사용자 프로그램에 결과 전달 */
		f->R.rax = success;

		break;
	}
	case SYS_EXEC:
	{
		const char *cmd_line;
		const char *t_name;

		char *f_name;
		void *current_page;

		int size;

		t_name = thread_name();
		cmd_line = (char *)f->R.rdi;
		/* 유효성 검증 */
		/* 명령줄이 NULL 인지 */
		if (cmd_line == NULL)
			exit_helper(t_name, -1);

		/* 문자열 검증 */
		size = 0;
		while (size < PGSIZE)
		{
			/* 사용자 공간에 있는지 */
			if (!is_user_vaddr(cmd_line + size))
				exit_helper(t_name, -1);

			/* 시작 주소가 페이지에 매핑되어 있는지 */
			if (pml4_get_page(thread_current()->pml4, cmd_line + size) == NULL)
				exit_helper(t_name, -1);

			/* 종료 조건 */
			if (cmd_line[size] == '\0')
				break;
			size++;
		}

		/* 커널 한 페이지 (4096바이트) 안에 들어가는 지 */
		if (size == PGSIZE)
			exit_helper(t_name, -1);

		/* 커널 공간 매핑 */
		f_name = palloc_get_page(0);
		if (f_name == NULL)
			exit_helper(t_name, -1);

		/* 인수 전달 */
		if (process_exec(memcpy(f_name, cmd_line, size + 1)) == -1)
			exit_helper(t_name, -1);
		break;
	}
	case SYS_WAIT:
	{
		int pid;
		int exit_status;

		/* wait 호출 후 반환값 기록*/
		pid = f->R.rdi;

		exit_status = process_wait(pid);
		f->R.rax = exit_status;
		break;
	}
	case SYS_CREATE:
	{
		break;
	}
	case SYS_REMOVE:
	{
		break;
	}
	case SYS_OPEN:
	{
		struct file **curr_fd_table;
		struct file *opened_file;
		const char *filename;
		int size;

		filename = f->R.rdi;
		curr_fd_table = thread_current()->fd_table;

		/* 파일명 검증 루틴 */
		if (filename == NULL)
		{
			f->R.rax = -1;
			break;
		}

		size = 0;
		while (size < PGSIZE)
		{
			/* 사용자 공간에 있는지 */
			if (!is_user_vaddr(filename + size))
				exit_helper(thread_name(), -1);

			/* 시작 주소가 페이지에 매핑되어 있는지 */
			if (pml4_get_page(thread_current()->pml4, filename + size) == NULL)
				exit_helper(thread_name(), -1);

			/* 종료 조건 */
			if (filename[size] == '\0')
				break;
			size++;
		}

		/* 커널 한 페이지 (4096바이트) 안에 들어가는 지 */
		if (size == PGSIZE)
			exit_helper(thread_name(), -1);

		/* open 루틴 */
		for (size_t i = 2; i < PGSIZE / sizeof(*curr_fd_table); i++)
			if (curr_fd_table[i] == NULL)
			{
				opened_file = filesys_open(filename);
				if (opened_file == NULL)
				{
					f->R.rax = -1;
					return;
				}
				/* 반환 */
				curr_fd_table[i] = opened_file;
				f->R.rax = i;
				return;
			}
		f->R.rax = -1;
		break;
	}
	case SYS_FILESIZE:
	{
		struct file **curr_fd_table;
		int fd;

		fd = f->R.rdi;

		if (fd < 2 || fd >= PGSIZE / sizeof(*curr_fd_table))
		{
			f->R.rax = -1;
			return;
		}

		curr_fd_table = thread_current()->fd_table;
		if (curr_fd_table == NULL || curr_fd_table[fd] == NULL)
		{
			f->R.rax = -1;
			return;
		}

		f->R.rax = file_length(curr_fd_table[fd]);
		break;
	}
	case SYS_READ:
	{
		struct thread *curr;
		struct file **curr_fd_table;
		int fd;
		char *buf;
		unsigned len;

		uint64_t pg_start;
		uint64_t pg_end;

		fd = f->R.rdi;
		buf = (char *)f->R.rsi;
		len = (off_t)f->R.rdx;

		if (len == 0)
		{
			f->R.rax = 0;
			return;
		}

		/* 주소 범위 검사 */
		if (buf == NULL || !is_user_vaddr(buf))
			exit_helper(thread_name(), -1);

		if (len > (uintptr_t)KERN_BASE - (uintptr_t)buf)
		{
			f->R.rax = -1;
			return;
		}

		curr = thread_current();

		/* 페이지 매핑과 NULL 검사 */

		pg_start = (uintptr_t)pg_round_down((uintptr_t)buf);
		pg_end = (uintptr_t)pg_round_down((uintptr_t)buf + len - 1);

		for (uint64_t pg = pg_start; pg <= pg_end; pg += PGSIZE)
		{
			if (pml4_get_page(curr->pml4, (const void *)pg) == NULL)
				exit_helper(thread_name(), -1);

			uint64_t *pte = pml4e_walk(curr->pml4, pg, 0);
			if (!is_user_pte(pte) || !is_writable(pte))
				exit_helper(thread_name(), -1);
		}

		curr_fd_table = curr->fd_table;

		/* 표준 출력일 경우 오류 */
		if (fd == 1)
		{
			f->R.rax = -1;
			return;
		}

		/* 표준 입력 분기 */
		if (fd == 0)
		{
			for (size_t i = 0; i < len; i++)
				buf[i] = input_getc();
			f->R.rax = len;
		}
		/* 일반 파일 분기 */
		else
		{
			if (curr_fd_table == NULL || fd < 0 || fd >= PGSIZE / sizeof(*curr_fd_table))
			{
				f->R.rax = -1;
				return;
			}

			if (curr_fd_table[fd] == NULL)
			{
				f->R.rax = -1;
				return;
			}

			f->R.rax = (uint64_t)(curr_fd_table[fd], buf, len);
		}

		break;
	}
	case SYS_WRITE:
	{
		int fd;

		fd = f->R.rdi;
		if (fd == 1)
			putbuf(f->R.rsi, f->R.rdx);
		break;
	}
	case SYS_SEEK:
	{
		break;
	}
	case SYS_TELL:
	{
		break;
	}
	case SYS_CLOSE:
	{
		struct file **curr_fd_table;
		int size;
		int fd;

		fd = (int)f->R.rdi;

		size = PGSIZE / sizeof(*curr_fd_table);
		if (fd < 2 || fd >= size)
			break;

		curr_fd_table = thread_current()->fd_table;

		if (curr_fd_table == NULL || curr_fd_table[fd] == NULL)
			break;

		file_close(curr_fd_table[fd]);
		curr_fd_table[fd] = NULL;
		break;
	}
	default:
	{
		break;
	}
	}
}

/* 임시 종료 헬퍼 함수입니다.
 * 추후 커널에서의 종료 계약이 완료되면, 대체할 수 있습니다. */
static void exit_helper(const char *name, int status)
{
	struct thread *curr = thread_current();

	if (curr->own_status != NULL)
		curr->own_status->exit_status = status;

	printf("%s: exit(%d)\n", name, status);
	thread_exit();
}