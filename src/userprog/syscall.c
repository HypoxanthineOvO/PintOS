#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

static void syscall_handler(struct intr_frame*);

#define SYSCALL_NUM_MIN 0
#define SYSCALL_NUM_MAX 13

int syscall_types[SYSCALL_NUM_MAX];
static void (*syscall_functions[SYSCALL_NUM_MAX])(struct intr_frame*);

/* Util Functions */
void exit_special() {
	thread_current()->exit_code = -1;
	thread_exit();
}

static int get_user(const uint8_t* uaddr) {
	int result;
	asm("movl $1f, %0; movzbl %1, %0; 1:" : "=&a" (result) : "m" (*uaddr));
	return result;
}

void* check_addr(int const* vaddr) {
	/* Judge address */
	if (!is_user_vaddr(vaddr)) {
		exit_special();
	}
	/* Judge the page */
	void* ptr = pagedir_get_page(thread_current()->pagedir, vaddr);
	if (!ptr) {
		exit_special();
	}
	/* Judge the content of page */
	uint8_t* check_byteptr = (uint8_t*)vaddr;
	for (uint8_t i = 0; i < 4; i++) {
		if (get_user(check_byteptr + i) == -1) {
			exit_special();
		}
	}
	return ptr;
}

bool check_pt(void* pt, int size){
	for(int i = 0; i < size; i++){
		if(!is_user_vaddr(pt) || (pagedir_get_page(thread_current()->pagedir, pt) == NULL)){
			return false;
		}
	}
	return true;
}

struct file_of_thread* get_file_by_fd(struct list* file_list, int fd){
	struct file_of_thread* file_of_thread = NULL;
	struct list_elem* e;
	for(
		e = list_begin(file_list);
		e != list_end(file_list);
		e = list_next(e)
	){
		file_of_thread = list_entry(e, struct file_of_thread, file_elem);
		if (file_of_thread->file_descriptor == fd){
			break;
		}
	}
	return file_of_thread;
}

/* Systemcall Function Implementation */
// Halt
void halt(void){
	shutdown_power_off();
}
// Exit
void exit(int status){
	struct thread* current_thread = thread_current();
	current_thread->exit_code = status;
	thread_exit();
}
// Execute
int exec(const char* cmd_line){
	int id = process_execute(cmd_line);
	return id;
}
// Wait
int wait(int pid){
	return process_wait(pid);
}

// Create
bool create(const char* file, unsigned initial_size){
	acquire_file_lock();
	bool ret_val = filesys_create(file, initial_size);
	release_file_lock();
	return ret_val;
}

// Remove
bool remove(const char* file){
	acquire_file_lock();
	bool ret_val = filesys_remove(file);
	release_file_lock();
	return ret_val;
}

// Open
int open(const char* file){
	// Use filesys_open
	acquire_file_lock();
	struct file* file_ptr = filesys_open(file);
	release_file_lock();
	if (file_ptr == NULL){
		return -1;
	}
	// Add file to file_list
	struct thread* current_thread = thread_current();
	struct file_of_thread* file_of_thread = malloc(sizeof(struct file_of_thread));
	file_of_thread->file = file_ptr;
	file_of_thread->file_descriptor = current_thread->now_fd++;
	list_push_back(&current_thread->file_list, &file_of_thread->file_elem);
	return file_of_thread->file_descriptor;
}

// Filesize
int filesize(int fd){
	int ret_val = -1;
	// Find file by id
	struct list_elem* e;
	struct thread* current_thread = thread_current();

	struct file_of_thread* file_of_thread = get_file_by_fd(&current_thread->file_list, fd);
	
	if(file_of_thread){
		acquire_file_lock();
		ret_val = file_length(file_of_thread->file);
		release_file_lock();
	}
	else{
		exit_special();
	}
	//return ret_val;
}

// Read
int read(int fd, void* buffer, unsigned length){
	if (!check_pt (buffer, 1) || !check_pt (buffer + length, 1)){
    	exit_special ();
	}
	if (fd == 0){
		// Read from Console
		for (int i = 0; i < length; i++){
			((char*)buffer)[i] = input_getc();
		}
		return length;
	}
	else{
		int ret_val = -1;
		// Find file by id
		struct list_elem* e;
		struct thread* current_thread = thread_current();
		struct file_of_thread* file_of_thread = get_file_by_fd(&current_thread->file_list, fd);
		if(file_of_thread){
			acquire_file_lock();
			ret_val = file_read(file_of_thread->file, buffer, length);
			release_file_lock();
		}
		return ret_val;
	}
	return -1;
}

// Write
int write(int fd, const void* buffer, unsigned length){
	if (fd == 1){
		// Write to Console
		putbuf((const char*)buffer, length);
		return length;
	}
	else{
		int ret_val = -1;
		// Find file by id
		struct list_elem* e;
		struct thread* current_thread = thread_current();
		struct file_of_thread* file_of_thread = get_file_by_fd(&current_thread->file_list, fd);
		if(file_of_thread){
			acquire_file_lock();
			ret_val = file_write(file_of_thread->file, buffer, length);
			release_file_lock();
		}
		return ret_val;
	}
	return 0;
}

// Seek
void seek(int fd, unsigned int position){
	// Find file by id
	struct list_elem* e;
	struct thread* current_thread = thread_current();
	struct file_of_thread* file_of_thread = get_file_by_fd(&current_thread->file_list, fd);
	if(file_of_thread){
		acquire_file_lock();
		file_seek(file_of_thread->file, position);
		release_file_lock();
	}
	else{
		exit_special();
	}
}

// Tell
unsigned tell(int fd){
	unsigned ret_val = -1;
	// Find file by id
	struct list_elem* e;
	struct thread* current_thread = thread_current();
	acquire_file_lock();
	struct file_of_thread* file_of_thread = get_file_by_fd(&current_thread->file_list, fd);
	if(file_of_thread){
		ret_val = file_tell(file_of_thread->file);
		release_file_lock();
	}	
	else{
		exit_special();
	}
	//return ret_val;
}

// Close
void close(int fd){
	// Find file by id
	struct list_elem* e;
	struct thread* current_thread = thread_current();
	struct file_of_thread* file_of_thread = get_file_by_fd(&current_thread->file_list, fd);
	if(file_of_thread){
		acquire_file_lock();
		file_close(file_of_thread->file);
		release_file_lock();
		list_remove(&file_of_thread->file_elem);
		free(file_of_thread);
	}	
	else{
		exit_special();
	}
}


void syscall_init(void) {
	intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}


static void syscall_handler(struct intr_frame* f) {
	int *user_pointer = f->esp;
	check_addr(user_pointer + 1);
	int sys_code = *(int*)user_pointer;
	if (sys_code < SYSCALL_NUM_MIN || sys_code >= SYSCALL_NUM_MAX) {
		exit_special();
	}
	switch (sys_code) {
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		check_addr(user_pointer + 1);
		*user_pointer++;
		exit(*user_pointer);
		break;
	case SYS_EXEC:
		check_addr(user_pointer + 1);
		check_addr(*(user_pointer + 1));
		*user_pointer++;
		f->eax = exec((char*)*(user_pointer));
		break;
	case SYS_WAIT:
		check_addr(user_pointer + 1);
		*user_pointer++;
		f->eax = wait(*user_pointer);
		break;
	case SYS_CREATE:
		check_addr(user_pointer + 5);
		check_addr(*(user_pointer + 4));
		*user_pointer++;
		f->eax = create((const char *)*(user_pointer), *(user_pointer + 1));
		break;
	case SYS_REMOVE:
		check_addr(user_pointer + 1);
		check_addr(*(user_pointer + 1));
		*user_pointer++;
		f->eax = remove((const char *)*user_pointer);
		break;
	case SYS_OPEN:
		check_addr(user_pointer + 1);
		check_addr(*(user_pointer + 1));
		*user_pointer++;
		f->eax = open(*user_pointer);
		break;
	case SYS_FILESIZE:
		check_addr(user_pointer + 1);
		*user_pointer++;
		f->eax = filesize(*user_pointer);
		break;
	case SYS_READ:
		check_addr(user_pointer + 1);
		check_addr(*(user_pointer + 1));
		check_addr(user_pointer + 2);
		check_addr(*(user_pointer + 2));
		check_addr(user_pointer);
		check_addr(*(user_pointer));
		*user_pointer++;
		f->eax = read(*(user_pointer), *(user_pointer + 1), *(user_pointer + 2));
		break;
	case SYS_WRITE:
		check_addr(user_pointer + 7);
		check_addr(*(user_pointer + 6));
		*user_pointer++;
		f->eax = write(*(user_pointer), *(user_pointer + 1), *(user_pointer + 2));
		break;
	case SYS_SEEK:
		check_addr(user_pointer + 5);
		*user_pointer++;
		seek(*(user_pointer), *(user_pointer + 1));
		break;
	case SYS_TELL:
		check_addr(user_pointer + 1);
		*user_pointer++;
		f->eax = tell(*user_pointer);
		break;
	case SYS_CLOSE:
		check_addr(user_pointer + 1);
		*user_pointer++;
		close(*user_pointer);
		break;
	default:
		exit_special();
		break;
	}
}
