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
   3. We first push the arguments string on stack, and save the pointer of them in `argv`.
   4. We use `int argc` and `int argv[]` to record the number of arguments and the address of each argument.
    - argv have type `int` because we have alignment requirement.
   5. Then we push the arguments onto the stack at inverse order.

## Rational
### A3: Why does Pintos implement strtok_r() but not strtok()?
PintOS implement `strtok_r()` instead of `strtok()` have several reasons:
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
struct thread{
    ...
	#ifdef USERPROG
		/* Owned by userprog/process.c. */
		uint32_t* pagedir;                  /* Page directory. */
		/* Structure for Project 2 */
		// Tree structure of thread
		struct list children_list; // List of children
		struct thread* parent; // pointer to parent
		struct thread_link* child; // pointer to child

		// Status Flags
		int exit_code; // Exit Status of Thread
		bool success; // Whether execute successfully
		// Locks
		struct semaphore sema; // Lock for thread

		// Files
		int self_fd; // file descriptor
		struct list file_list; // List of files
		struct file* file_opened; // File opened by thread
	#endif
    ...
}

struct thread_link {
	/* A Tracer of parent and child thread. */
	int tid; // tid of child
	struct list_elem elem; // child list elem
	struct semaphore sema; // semaphore to syn exit state
	int exit_code; // Exit Status of Thread
};

struct thread_file {
	int file_descriptor; // num of file descriptor
	struct file* file; // file in the thread
	struct list_elem file_elem; // file list elem
};

```

### B2: Describe how file descriptors are associated with open files. Are file descriptors unique within the entire OS or just within a single process?
- A file descriptor is a non-negative number.
- For each thread, we have a list of files and a pointer to the file opened by the thread.
- We store the file descriptor of the file opened by `self_fd`. Notice that `self_fd` is **unique within a single process**.
- In `syscall.c`, we implement a function `get_file`, which can get the file pointer by the file descriptor and `thread_current()->file_list`.
- In pintos, file descriptor 0 and 1 is occupied by stdin and stdout, thus file descriptors defined by ourselves start from 2.

## Algorithms

### B3: Describe your code for reading and writing user data from the kernel.
- Because interrupts does not change the page directory, we can access the user memory directly in the interrupt handler.
- After checking the user address is valid, we can read or write the memeory directly.
- The method we adopted to check the validity of the user memory address is described in **B6**.

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
In `process_wait`, we find the child thread by `pid` and call `sema_down(&child->sema)` to wait for the child thread to exit. Only when the child thread exit, it will call `sema_up`, we can get the exit code of the child thread. Then we return the exit code. If we can't find the child thread in the children_list of the thread, we return -1.


### B6: Any access to user program memory at a user-specified address can fail due to a bad pointer value.  Such accesses must cause the process to be terminated.  System calls are fraught with such accesses, e.g. a "write" system call requires reading the system call number from the user stack, then each of the call's three arguments, then an arbitrary amount of user memory, and any of these can fail at any point. This poses a design and error-handling problem: how do you best avoid obscuring the primary function of code in a morass of error-handling?  Furthermore, when an error is detected, how do you ensure that all temporarily allocated resources (locks, buffers, etc.) are freed?  In a few paragraphs, describe the strategy or strategies you adopted for managing these issues.  Give an example.
- We implement a series function to check the validity of the user memory address according to the various input types of the functions.
	```c++
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
	```
	- `check_pt` check the validity of the user memory address. It can check the address of the system call number and the arguments.
	- `check_buffer` check the validity of the buffer. It can check the address of the buffer and the address of the end of the buffer.
	- `check_string` check the validity of the string. It can check the address of all the characters of the string.
- Following the document, we modified `page_fault()`  function's exception handler. If the page fault is occurred in kernel mode, instead of panicking, we set eax to -1 and set eip to eax because eax stores the address of the next instruction. Then we return to the next instruction and continue to execute.
- In `syscall_handler()`, we use `check_pt()` to check the address of the system call number and the arguments. If any of them is invalid, we exit the thread.
  - For each system call, we check the address of the system call number and the arguments. We modified the check object according to the system call type.
- For Example: In `syscall_handler`, we do:
  - Check `f->esp` by `check_pt`
  - If `f->esp` is valid, we get the system call number by `get_user(f->esp)`. Assume the system call is `syscall_write`, it have 3 arguments.
  - We will check `f->esp + 1`, `f->esp + 2`, `f->esp + 3` by `check_pt`.
  - Because `syscall_write` need to write a buffer to the file, we need to check the buffer by `check_buffer`.
  - Then we do the system call.


## Synchronization
### B7: The "exec" system call returns -1 if loading the new executable fails, so it cannot return before the new executable has completed loading.  How does your code ensure this?  How is the load success/failure status passed back to the thread that calls "exec"?
- In our `syscall_exec()`, we call `process_execute()`.
- In `process_execute()`, after we create a new thread, we call `sema_down(&thread_current()->sema)` to wait for the child thread to load the new executable. Noticed that the child process will run `start_process()` as the first function.
- In `start_process()`, we call `sema_up(&thread_current()->parent->sema)` to wake up the parent thread.
- And also in `start_process()`, we pass the load success/failure status with `thread_current()->parent->success = true/false` according to the load status of `thread_current()`.

### B8: Consider parent process P with child process C.  How do you ensure proper synchronization and avoid race conditions when P calls wait(C) before C exits?  After C exits?  How do you ensure that all resources are freed in each case?  How about when P terminates without waiting, before C exits?  After C exits?  Are there any special cases?

To ensure proper synchronization and avoid race conditions when the parent process `P` calls `wait(C)` before `C` exits, we can use the following approach:

1. Introduce a `success` flag in the `struct thread` to record whether the thread executed successfully. Additionally, use the `parent` field in the `struct thread` to access and update the parent's status based on the loading result. This design allows the parent to record the child's execution result instead of the child itself.
2. Use a semaphore to implement the "parent waits for child" mechanism. When a child process is created, it will perform a down operation on the semaphore to block the parent. Once the child process completes its execution, it will perform an up operation on the semaphore to wake up its parent.

For the case of `P` calling `wait(C)` after `C` exits, since w can't find `C` in the child list of `P`, we return -1.

To ensure that all resources are freed in each case, we can follow these steps:

1. When P calls wait(C) before C exits, the parent process P will block using the down operation on the semaphore. Once C finishes its execution, it will perform the up operation on the semaphore, allowing the parent process P to continue. At this point, the parent process can free any resources associated with the child process C.
2. If P terminates without waiting for C before C exits, the child process need do nothing. The parent process will be terminated by the kernel, and the kernel will free all resources associated with the parent process.

In both cases, proper synchronization and resource cleanup are ensured by using semaphores and appropriate checks on the parent-child relationship. These measures help avoid race conditions and ensure that all resources are freed correctly.

It's important to note that there may be special cases or additional considerations depending on the specific implementation and requirements of the system.

## Rational

### B9: Why did you choose to implement access to user memory from the kernel in the way that you did?
- We follow the document's method 2 to implement the access to user memory from the kernel.
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