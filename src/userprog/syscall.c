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
#define SYSCALL_NUM_MAX 20

int syscall_argc[SYSCALL_NUM_MAX];
void* syscall_func[SYSCALL_NUM_MAX];

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


void check_pt(int const* vaddr){
	uint8_t* check_byteptr = (uint8_t*)vaddr;
	for(uint8_t i = 0; i < 4; i++){
		if(!is_user_vaddr(check_byteptr + i) || get_user(check_byteptr + i) == -1){
			exit_special();
		}
	}
}

void check_buffer(uint8_t const* buffer, unsigned int size){
	if(!is_user_vaddr(buffer) || !is_user_vaddr(buffer + size - 1)
		|| get_user(buffer) == -1 || get_user(buffer + size - 1) == -1){
		exit_special();
	}
}

void check_string(char const* str){
	while(1){
		if(!is_user_vaddr(str)) exit_special();
		int user = get_user((uint8_t const *)str);
		if (user == 0) return;
		if (user == -1) exit_special();
		str++;
	}
}

struct thread_node* get_file(struct thread* thread, int fd){
	struct file_list* file_list = &thread->file_list;
	struct thread_node* thread_node = NULL;
	struct list_elem* e;
	for(
		e = list_begin(file_list);
		e != list_end(file_list);
		e = list_next(e)
	){
		thread_node = list_entry(e, struct thread_node, elem);
		if (thread_node->file_descriptor == fd){
			break;
		}
	}
	return thread_node;
}

/* Systemcall Function Implementation */
// Halt
void syscall_halt(void){
	shutdown_power_off();
}
// Exit
void syscall_exit(int status){
	struct thread* current_thread = thread_current();
	current_thread->exit_code = status;
	thread_exit();
}
// Execute
int syscall_exec(const char* cmd_line){
	check_string(cmd_line);
	int id = process_execute(cmd_line);
	return id;
}
// Wait
int syscall_wait(int pid){
	return process_wait(pid);
}

// Create
bool syscall_create(const char* file, unsigned initial_size){
	check_string(file);
	acquire_file_lock();
	bool ret_val = filesys_create(file, initial_size);
	release_file_lock();
	return ret_val;
}

// Remove
bool syscall_remove(const char* file){
	check_string(file);
	acquire_file_lock();
	bool ret_val = filesys_remove(file);
	release_file_lock();
	return ret_val;
}

// Open
int syscall_open(const char* file){
	check_string(file);
	// Use filesys_open
	struct file* file_ptr = NULL;
	struct dir* dir_ptr = NULL;
	if(filesys_open_f_or_d(file, &file_ptr, &dir_ptr)){
		ASSERT (file_ptr != NULL || dir_ptr != NULL);
		ASSERT (file_ptr == NULL || dir_ptr == NULL);
		struct thread* current_thread = thread_current();
			struct thread_node* thread_node = malloc(sizeof(struct thread_node));
		if(file_ptr){
			// Add file to file_list
			thread_node->file = file_ptr;
			thread_node->is_dir = false;
		}
		else{
			// Add dir to file_list
			thread_node->dir = dir_ptr;
			thread_node->is_dir = true;
		}
		thread_node->file_descriptor = current_thread->self_fd++;
		list_push_back(&current_thread->file_list, &thread_node->elem);
		return thread_node->file_descriptor;
	}
	else{
		return -1;
	}
}

// Filesize
int syscall_filesize(int fd){
	int ret_val = -1;
	// Find file by id
	struct list_elem* e;
	struct thread_node* thread_node = get_file(thread_current(), fd);
	
	if(thread_node){
		acquire_file_lock();
		int file_len = file_length(thread_node->file);
		release_file_lock();
		return file_len;
	}
	else{
		return -1;
	}
	NOT_REACHED();
}

// Read
int syscall_read(int fd, uint8_t* buffer, unsigned length){
	check_buffer(buffer, length);
	if (fd == 0){
		// Read from Console
		for (int i = 0; i < length; i++){
			buffer[i] = input_getc();
		}
		return length;
	}
	else{
		int ret_val = -1;
		// Find file by id
		struct list_elem* e;
		struct thread_node* thread_node = get_file(thread_current(), fd);
		
		if(thread_node){
			if(thread_node->is_dir) return -1;
			acquire_file_lock();
			ret_val = file_read(thread_node->file, buffer, length);
			release_file_lock();
			return ret_val;
		}
		else return -1;
	}
	NOT_REACHED();
}
// Write
int syscall_write(int fd, const void* buffer, unsigned length){
	check_buffer(buffer, length);
	if (fd == 1){
		// Write to Console
		putbuf((const char*)buffer, length);
		return length;
	}
	else{
		int ret_val = -1;
		// Find file by id
		struct list_elem* e;
		struct thread_node* thread_node = get_file(thread_current(), fd);
		if(thread_node){
			if(thread_node->is_dir) return -1;
			acquire_file_lock();
			ret_val = file_write(thread_node->file, buffer, length);
			release_file_lock();
		}
		return ret_val;
	}
	NOT_REACHED();
}

// Seek
void syscall_seek(int fd, unsigned int position){
	// Find file by id
	struct list_elem* e;
	struct thread_node* thread_node = get_file(thread_current(), fd);
	if(thread_node){
		acquire_file_lock();
		file_seek(thread_node->file, position);
		release_file_lock();
	}
	else{
		exit_special();
	}
}

// Tell
unsigned syscall_tell(int fd){
	// Find file by id
	struct list_elem* e;
	struct thread_node* thread_node = get_file(thread_current(), fd);
	if(thread_node){
		acquire_file_lock();
		unsigned retval = file_tell(thread_node->file);
		release_file_lock();
		return retval;
	}	
	else{
		exit_special();
	}
	NOT_REACHED();
}

// Close
void syscall_close(int fd){
	// Find file by id
	struct list_elem* e;
	struct thread_node* thread_node = get_file(thread_current(), fd);
	if(thread_node){	
		acquire_file_lock();
		if(thread_node->is_dir) dir_close(thread_node->dir);
		else file_close(thread_node->file);
		release_file_lock();

		list_remove(&thread_node->elem);
		free(thread_node);
	}	
	else{
		exit_special();
	}
}

/* Project 4 */
// Chdir
bool syscall_chdir(const char* dir) {
	check_string(dir);
	return filesys_chdir(dir);
}

// Mkdir
bool syscall_mkdir(const char* dir) {
	check_string(dir);
	return filesys_mkdir(dir);
}

// Readdir
bool syscall_readdir(int fd, char* name) {
	check_string(name);
	struct thread_node* thread_node = get_file(thread_current(), fd);
	if(thread_node) {
		if(!thread_node->is_dir) exit_special();
		bool suc = dir_readdir(thread_node->dir, name);
		return suc;
	}
	else{
		exit_special();
	}
}

// Isdir
bool syscall_isdir(int fd) {
	struct thread_node* thread_node = get_file(thread_current(), fd);
	if(thread_node) return thread_node->is_dir;
	else exit_special();
}

// Inumber
int syscall_inumber(int fd) {
	struct thread_node* thread_node = get_file(thread_current(), fd);
	if(thread_node) {
		int inumber;
		if(thread_node->is_dir) inumber = inode_get_inumber(dir_get_inode(thread_node->dir));
		else inumber = inode_get_inumber(file_get_inode(thread_node->file));
		return inumber;
	}
	else {
		return -1;
	}
}

void syscall_init(void){
	intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");

	syscall_argc[SYS_HALT] = 0;
	syscall_func[SYS_HALT] = (void*)syscall_halt;

	syscall_argc[SYS_EXIT] = 1;
	syscall_func[SYS_EXIT] = (void*)syscall_exit;

	syscall_argc[SYS_EXEC] = 1;
	syscall_func[SYS_EXEC] = (void*)syscall_exec;

	syscall_argc[SYS_WAIT] = 1;
	syscall_func[SYS_WAIT] = (void*)syscall_wait;

	syscall_argc[SYS_CREATE] = 2;
	syscall_func[SYS_CREATE] = (void*)syscall_create;

	syscall_argc[SYS_REMOVE] = 1;
	syscall_func[SYS_REMOVE] = (void*)syscall_remove;

	syscall_argc[SYS_OPEN] = 1;
	syscall_func[SYS_OPEN] = (void*)syscall_open;

	syscall_argc[SYS_FILESIZE] = 1;
	syscall_func[SYS_FILESIZE] = (void*)syscall_filesize;

	syscall_argc[SYS_READ] = 3;
	syscall_func[SYS_READ] = (void*)syscall_read;

	syscall_argc[SYS_WRITE] = 3;
	syscall_func[SYS_WRITE] = (void*)syscall_write;

	syscall_argc[SYS_SEEK] = 2;
	syscall_func[SYS_SEEK] = (void*)syscall_seek;

	syscall_argc[SYS_TELL] = 1;
	syscall_func[SYS_TELL] = (void*)syscall_tell;

	syscall_argc[SYS_CLOSE] = 1;
	syscall_func[SYS_CLOSE] = (void*)syscall_close;



	/* Project 4 */
	syscall_argc[SYS_CHDIR] = 1;
	syscall_func[SYS_CHDIR] = (void*)syscall_chdir;

	syscall_argc[SYS_MKDIR] = 1;
	syscall_func[SYS_MKDIR] = (void*)syscall_mkdir;

	syscall_argc[SYS_READDIR] = 2;
	syscall_func[SYS_READDIR] = (void*)syscall_readdir;

	syscall_argc[SYS_ISDIR] = 1;
	syscall_func[SYS_ISDIR] = (void*)syscall_isdir;

	syscall_argc[SYS_INUMBER] = 1;
	syscall_func[SYS_INUMBER] = (void*)syscall_inumber;
}

static void syscall_handler(struct intr_frame* f){
	int* user_pointer = f->esp;
	check_pt(user_pointer + 1);

	int sys_code = *(int*)user_pointer;
	if (sys_code < SYSCALL_NUM_MIN || sys_code >= SYSCALL_NUM_MAX){
		exit_special();
	}

	void* func = syscall_func[sys_code];
	if (func == NULL){
		exit_special();
	}

	int argc = syscall_argc[sys_code];
	ASSERT((argc >= 0) && (argc <= 3));
	int argv[3];
	for(int i = 0; i < argc; i++){
		int* arg = user_pointer + i + 1;
		check_pt(arg);
		argv[i] = *arg;
	}

	int ret_val = 0;
	switch (argc) {
		case 0:
			ret_val = ((int (*)())func)();
			break;
		case 1:
			ret_val = ((int (*)(int))func)(argv[0]);
			break;
		case 2:
			ret_val = ((int (*)(int, int))func)(argv[0], argv[1]);
			break;
		case 3:
			ret_val = ((int (*)(int, int, int))func)(argv[0], argv[1], argv[2]);
			break;
		default:
			exit_special();
			break;
	}

	f->eax = ret_val;
}