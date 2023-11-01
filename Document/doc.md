# CS140 Project 2: User Programs Design Document
## Group
- **Yunxiang He** <heyx1@shanghaitech.edu.cn>
- **Yicheng Fan** <fanych1@shanghaitech.edu.cn>

# Argument Passing
## Data Structures
### A1: Copy here the declaration of each new or changed "struct" or "struct" member, global or static variable, typedef, or enumeration.  Identify the purpose of each in 25 words or less.
- We add no new data structures.

## Algorithms

### A2: Briefly describe how you implemented argument parsing.  How do you arrange for the elements of argv[] to be in the right order? How do you avoid overflowing the stack page?
- We implement Argument Passing in two steps:
1. At `process_execute`, we divide the command line into two parts: the executable name and the arguments.
    - The executable name is the first word of the command line.
    - The executable name are used to create the new thread.
2. Then we deal with command line in `setup_stack`.
   1. In `setup_stack`, we have an complete command line string.
   2. We use `strtok_r` to parse the command line string.
   3. We use `int argc` and `int argv[]` to record the number of arguments and the address of each argument.
    - argv have type `int` because we have alignment requirement.
   4. Then we push the arguments onto the stack at inverse order.

## Rational
### A3: Why does Pintos implement strtok_r() but not strtok()?
PintOS implement `strtok_r()` instear of `strtok()` have several reasons:
- `strtok_r()` is thread-safe, but `strtok()` is not.
- `strtok_r()` is reentrant, but `strtok()` is not.

That because `strtok()` use a static variable to record the current position of the string, so it is not thread-safe and reentrant.

But `strtok_r()` get the current position of the string from the third argument, a pointer, so it is thread-safe and reentrant.

### A4 : In Pintos, the kernel separates commands into a executable name and arguments.  In Unix-like systems, the shell does this separation.  Identify at least two advantages of the Unix approach.
- The shell can do more complex parsing, such as quoting, redirection, and piping. That make the shell more powerful.
- This design make the kernel more simple and clear.


# System Calls
## Data Structures
### B1: Copy here the declaration of each new or changed "struct" or "struct" member, global or static variable, typedef, or enumeration.  Identify the purpose of each in 25 words or less.

```c++
struct threar{
    ...
	/* Owned by userprog/process.c. */
	uint32_t* pagedir;                  /* Page directory. */
	/* Structure for Project 2 */
	// Tree structure of thread
	struct list children_list; // List of children
	struct thread* parent; // pointer to parent
	struct user_thread* child; // pointer to child

	// Status Flags
	int exit_code; // Exit Status of Thread
	bool start_success;
	// Locks
	struct semaphore sema; // Lock for thread

	// Files
	int now_fd;
	struct list file_list; // List of files
	struct file* file_opened; // File opened by thread
    ...
}

/* User Process thread */
struct user_thread {
	int id; // Thread ID
	bool success; // Whether the thread is created successfully
	struct list_elem elem; // List Element
	struct semaphore sema; // Lock for thread
	int exit_code; // Exit Status of Thread
};

struct file_of_thread {
	int file_descriptor; // File Descriptor
	struct file* file; // File Pointer
	struct list_elem file_elem; // List Element
};

```

### B2: Describe how file descriptors are associated with open files. Are file descriptors unique within the entire OS or just within a single process?
- A file descriptor is a number.
- For each thread, we have a list of files and a pointer to the file opened by the thread.
- We store the file descriptor of the file opened by `now_fd`. Notice that `now_fd` is **unique within a single process**.
- In `syscall.c`, we implement a function `get_file_by_fd`, which can get the file pointer by the file descriptor and file_list.

## Algorithms

### B3: Describe your code for reading and writing user data from the kernel.
- We implement a function `check_addr()` to get the user data from the kernel.
	```c++
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
	```
	In this function, we check the address, the page and the content of the page. If any of them is invalid, we exit the thread.
- Everywhere we need to read or write user data, we use `check_addr()` to get the user data from the kernel.

### B4: Suppose a system call causes a full page (4,096 bytes) of data to be copied from user space into the kernel.  What is the least and the greatest possible number of inspections of the page table (e.g. calls to pagedir_get_page()) that might result?  What about for a system call that only copies 2 bytes of data?  Is there room for improvement in these numbers, and how much?
- In a naive implementation, we need to check every byte before reading them. So, the greatest possible number is $4096$.
- In a best implementation, the continuous data is on at most $2$ pages. We can use functions such as `lookup_page` to check them. So, the greatest possible number is $2$.
- If system calls can only copies 2 byte of data, the greatest possible number is $2048$ and the minimum possible number is $2$.

### B5: Briefly describe your implementation of the "wait" system call and how it interacts with process termination.
According to the document:
> We suggest that you implement process_wait() according to the comment at the top of the function and then implement the wait system call in terms of process_wait().
We implement `syscall_wait` with:
```c++
int syscall_wait(pid_t pid) {
	return process_wait(pid);
}
```
In `process_wait`, we find the child thread by `pid` and call `sema_down(&child->sema)` to wait for the child thread to exit.

The document:
> The process that calls wait has already called wait on pid. That is, a process may wait for any given child at most once.
So if the wait have been called on a child thread, `active` would be set `true` and we will check it in `process_wait`. 

### B6: Any access to user program memory at a user-specified address can fail due to a bad pointer value.  Such accesses must cause the process to be terminated.  System calls are fraught with such accesses, e.g. a "write" system call requires reading the system call number from the user stack, then each of the call's three arguments, then an arbitrary amount of user memory, and any of these can fail at any point. This poses a design and error-handling problem: how do you best avoid obscuring the primary function of code in a morass of error-handling?  Furthermore, when an error is detected, how do you ensure that all temporarily allocated resources (locks, buffers, etc.) are freed?  In a few paragraphs, describe the strategy or strategies you adopted for managing these issues.  Give an example.



## Synchronization
### B7: The "exec" system call returns -1 if loading the new executable fails, so it cannot return before the new executable has completed loading.  How does your code ensure this?  How is the load success/failure status passed back to the thread that calls "exec"?
- In our `syscall_exec()`, we call `process_execute()`.
- In `process_execute()`, after we create a new thread, we call `sema_down(&thread_current()->sema)` to wait for the child thread to load the new executable. Noticed that the child process will run `start_process()` as the first function.
- In `start_process()`, we call `sema_up(&thread_current()->parent->sema)` to wake up the parent thread.


### B8: Consider parent process P with child process C.  How do you ensure proper synchronization and avoid race conditions when P calls wait(C) before C exits?  After C exits?  How do you ensure that all resources are freed in each case?  How about when P terminates without waiting, before C exits?  After C exits?  Are there any special cases?

## Rational

### B9: Why did you choose to implement access to user memory from the kernel in the way that you did?
- We follow the document to implement the access to user memory from the kernel.
- We find the document give a good way to implement the access to user memory from the kernel, and it even give the code of `get_user()`. So, we follow the document to implement the access to user memory from the kernel.

### B10: What advantages or disadvantages can you see to your design for file descriptors?
- We use linked-list to store the file descriptors.
- Advantages
  - When we insert a new file descriptor, we do not need to move the other file descriptors. So, the time complexity is $O(1)$.
  - The file descriptors are unique within a single process.
- Disadvantages
  - We need iterate the list to find the file descriptor. So, the time complexity is $O(n)$. It is not efficient.

### B11: The default `tid_t` to `pid_t` mapping is the identity mapping.
>> If you changed it, what advantages are there to your approach?
- We do not change the default `tid_t` to `pid_t` mapping.