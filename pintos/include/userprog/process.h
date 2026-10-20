#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd(const char *file_name);
tid_t process_fork(const char *name, struct intr_frame *if_);
int process_exec(void *f_name);
int process_wait(tid_t);
void process_exit(void);
void process_activate(struct thread *next);

struct fork_context
{
    struct intr_frame user_frame; /* 사용자 intr_frame 스냅샷 */
    struct semaphore done;        /* 복제 완료를 알릴 세마포어*/
    struct thread *parent;        /* 부모 스레드를 찾을 정보 */
    bool success;                 /* 복제 성공/실패 결과 */
};

#endif /* userprog/process.h */
