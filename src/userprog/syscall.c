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
#include "vm/page.h"
#include "vm/frame.h"

static void syscall_handler(struct intr_frame*);

#define SYSCALL_NUM_MIN 0
#define SYSCALL_NUM_MAX 15

typedef int mapid_t;

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

struct thread_file* get_file(struct thread* thread, int fd){
	struct file_list* file_list = &thread->file_list;
	struct thread_file* thread_file = NULL;
	struct list_elem* e;
	for(
		e = list_begin(file_list);
		e != list_end(file_list);
		e = list_next(e)
	){
		thread_file = list_entry(e, struct thread_file, file_elem);
		if (thread_file->file_descriptor == fd){
			break;
		}
	}
	return thread_file;
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
	bool ret_val = filesys_create(file, initial_size);
	return ret_val;
}

// Remove
bool syscall_remove(const char* file){
	check_string(file);
	bool ret_val = filesys_remove(file);
	return ret_val;
}

// Open
int syscall_open(const char* file){
	check_string(file);
	// Use filesys_open
	struct file* file_ptr = filesys_open(file);
	if (file_ptr == NULL){
		return -1;
	}
	// Add file to file_list
	struct thread* current_thread = thread_current();
	struct thread_file* thread_file = malloc(sizeof(struct thread_file));
	thread_file->file = file_ptr;
	thread_file->file_descriptor = current_thread->self_fd++;
	list_push_back(&current_thread->file_list, &thread_file->file_elem);
	return thread_file->file_descriptor;
}

// Filesize
int syscall_filesize(int fd){
	int ret_val = -1;
	// Find file by id
	struct list_elem* e;
	struct thread_file* thread_file = get_file(thread_current(), fd);
	
	if(thread_file){
		return file_length(thread_file->file);
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
		struct thread_file* thread_file = get_file(thread_current(), fd);
		if(thread_file){
			ret_val = file_read(thread_file->file, buffer, length);
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
		struct thread_file* thread_file = get_file(thread_current(), fd);
		if(thread_file){
			ret_val = file_write(thread_file->file, buffer, length);
		}
		return ret_val;
	}
	NOT_REACHED();
}

// Seek
void syscall_seek(int fd, unsigned int position){
	// Find file by id
	struct list_elem* e;
	struct thread_file* thread_file = get_file(thread_current(), fd);
	if(thread_file){
		acquire_file_lock();
		file_seek(thread_file->file, position);
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
	struct thread_file* thread_file = get_file(thread_current(), fd);
	if(thread_file){
		return file_tell(thread_file->file);
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
	struct thread_file* thread_file = get_file(thread_current(), fd);
	if(thread_file){
		file_close(thread_file->file);
		list_remove(&thread_file->file_elem);
		free(thread_file);
	}	
	else{
		exit_special();
	}
}

// MMAP
mapid_t syscall_mmap(int fd, void* addr){
	if (fd < 2 || addr == NULL || pg_ofs(addr) != 0){
		return -1;
	}
	struct thread* cur = thread_current();
	struct thread_file* thread_file = get_file(&cur, fd);
	if(thread_file == NULL){
		return -1;
	}

	size_t file_size = file_length(thread_file->file);
	if (file_size == 0){
		return -1;
	}

	struct hash* page_table = &cur->page_table;
	for(size_t offset = 0; offset < file_size; offset += PGSIZE){
		if(page_find(page_table, addr + offset)){
			return -1;
		}
	}

	struct thread_mmap* thread_mmap = malloc(sizeof(struct thread_mmap));
	thread_mmap->file = file_reopen(thread_file->file);
	thread_mmap->mapped_addr = addr;

	for(size_t offset = 0; offset < file_size; offset += PGSIZE){
		size_t page_read_bytes = file_size - (offset < PGSIZE ? file_size - offset : PGSIZE);
		Page* page = page_create_out_stack(page_table, addr+offset, 1,
			thread_mmap->file, offset, page_read_bytes);
		if(page == NULL){
			for(size_t i = 0; i < offset; i += PGSIZE){
				Page* del_page = page_find(page_table, addr + i);
				page_free(page_table, page);
			}
			free(thread_mmap);
			return -1;
		}
	}
	thread_mmap->mapid = cur->self_mapid++;
	list_push_back(&cur->mmap_list, &thread_mmap->elem);
	return thread_mmap->mapid;
}

void syscall_munmap(mapid_t mapid){
	struct thread *t = thread_current ();
	struct list *mmap_list = &t->mmap_list;
	struct list_elem *e;
	struct thread_mmap *thread_mmap = NULL;
	for (e = list_begin (mmap_list); e != list_end (mmap_list);
		e = list_next (e))
		if (list_entry (e, struct thread_mmap, elem)->mapid == mapid)
		{
			thread_mmap = list_entry (e, struct thread_mmap, elem);
			break;
		}
	if (thread_mmap == NULL){
		exit_special();
	}
	size_t file_size = file_length (thread_mmap->file);
	for (size_t offset = 0; offset < file_size; offset += PGSIZE)
		{
		struct page *page = page_find (&thread_current ()->page_table,
										thread_mmap->mapped_addr + offset);
		page_free(&t->page_table, page);
		}
	file_close (thread_mmap->file);
	list_remove (&thread_mmap->elem);
	free (thread_mmap);
}

void syscall_init(void){
	intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");

	/* Project 2*/
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

	/* Project 3 */
	syscall_argc[SYS_MMAP] = 2;
	syscall_func[SYS_MMAP] = (void*)syscall_mmap;

	syscall_argc[SYS_MUNMAP] = 1;
	syscall_func[SYS_MUNMAP] = (void*)syscall_munmap;
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