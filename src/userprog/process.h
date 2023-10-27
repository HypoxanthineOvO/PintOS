#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include "syscall.h"


tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

typedef int pid_t;

struct process{
    pid_t pid;
    int exit_code;
    struct semaphore sema_load;
    struct semaphore sema_wait;
    struct list_elem elem;
    enum status{RUNNING, EXITED, INIT, ERROR} status;
};

#endif /* userprog/process.h */
