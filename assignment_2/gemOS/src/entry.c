#include<entry.h>
#include<lib.h>
#include<context.h>
#include<memory.h>
#include<schedule.h>
#include<file.h>
#include<kbd.h>
#include<fs.h>
#include<cfork.h>
#include<page.h>
#include<mmap.h>
#include<tracer.h>


int do_create_trace_buffer(struct exec_context *ctx, int mode)
{
  int val = sys_create_trace_buffer(ctx, mode);
  return val;
}

int do_start_strace(struct exec_context *ctx, int fd, int tracing_mode)
{
  int val = sys_start_strace(ctx, fd, tracing_mode);
  return val;
}

int do_end_strace(struct exec_context *ctx)
{
  int val = sys_end_strace(ctx);
  return val;
}

int do_strace(struct exec_context *ctx, int syscall_num, int action)
{
  int val = sys_strace(ctx, syscall_num, action);
  return val;
}

int do_read_strace(struct exec_context *ctx, u64 fd, u64 buff, u64 count)
{
        struct file *filep = ctx->files[fd];
        if(!filep){
            return -EINVAL; //file is not opened
        }
        int val = sys_read_strace(filep, (char*)buff, count);
        return val;
}



int do_read_ftrace(struct exec_context *ctx, u64 fd, u64 buff, u64 count)
{
        struct file *filep = ctx->files[fd];
        if(!filep){
            return -EINVAL; //file is not opened
        }
        int val = sys_read_ftrace(filep, (char*)buff, count);
        return val;
}

long do_fork() {

        struct exec_context *new_ctx = get_new_ctx();
        struct exec_context *ctx = get_current_ctx();
        u32 pid = new_ctx->pid;

        *new_ctx = *ctx;  //Copy the process
        new_ctx->pid = pid;
        new_ctx->ppid = ctx->pid;
        copy_mm(new_ctx, ctx);
        setup_child_context(new_ctx);

        return pid;
}

/*system call handler to open file */
/*todo: implement O_TRUNC */

int do_file_open(struct exec_context *ctx,u64 filename, u64 flag, u64 mode)
{

	struct file *filep;
	struct inode * inode;
	u32 fd, i;

	dprintk("%s %u %u\n",filename,mode,flag);
	/*This code is to handle stdin,stdout,stderr cases */

	if(!strcmp((char*)filename,"stdin"))
		return open_standard_IO(ctx, STDIN);

	if(!strcmp((char*)filename,"stdout"))
		return open_standard_IO(ctx, STDOUT);

	if(!strcmp((char*)filename,"stderr"))
		return open_standard_IO(ctx, STDERR);

	/* END of stdin,stdout,stderr cases */
	return do_regular_file_open(ctx, (char *)filename, flag, mode);
}

/*system call handler to read file */

int do_file_read(struct exec_context *ctx, u64 fd, u64 buff, u64 count){
	int read_size = 0;
	struct file *filep = ctx->files[fd];
	dprintk("fd in read:%d\n",fd);


	if(!filep){
		return -EINVAL; //file is not opened
	}
	if((filep->mode & O_READ) != O_READ){
		return -EACCES; //file is write only
	}


	if(filep->fops->read){
		read_size = filep->fops->read(filep, (char*)buff, count);
		dprintk("buff inside read:%s\n",buff);
		dprintk("read size:%d\n",read_size);
		return read_size;
	}
	return -EINVAL;
}

/*system call handler to write file */

int do_file_write(struct exec_context *ctx,u64 fd,u64 buff,u64 count){
	int write_size;
	struct file *filep = ctx->files[fd];
	if(!filep){
		return -EINVAL; //file is not opened
	}
	if(!(filep->mode & O_WRITE)){
		return -EACCES; // file is not opened in write mode
	}
	if(filep->fops->write){
		write_size = filep->fops->write(filep, (char*)buff, count);
		dprintk("write size:%d\n",write_size);
		return write_size;
	}
	return -EINVAL;
}

int do_dup(struct exec_context *ctx, int oldfd)
{
	int val = fd_dup(ctx, oldfd);
	return val;
}


int do_dup2(struct exec_context *ctx, int oldfd, int newfd)
{
	int val = fd_dup2(ctx, oldfd, newfd);
	return val;
}

int do_close(struct exec_context *ctx, int fd)
{
	int ret;
	struct file *filep = ctx->files[fd];

	if(!filep || !filep->fops || !filep->fops->close){
		return -EINVAL; //file is not opened
	}
	ctx->files[fd] = NULL;
	ret = filep->fops->close(filep);
}

long do_lseek(struct exec_context *ctx, int fd, long offset, int whence)
{
	struct file *filep = ctx->files[fd];
	if(filep && filep->fops->lseek)
	{
		return filep->fops->lseek(filep, offset, whence);
	}
	return -EINVAL; 
}

void do_exit()
{
        /*You may need to invoke the scheduler from here if there are
        other processes except swapper in the system. Make sure you make 
        the status of the current process to UNUSED before scheduling 
        the next process. If the only process alive in system is swapper, 
        invoke do_cleanup() to shutdown gem5 (by crashing it, huh!)
        */
        // Scheduling new process (swapper if no other available)
        int ctr;
        struct exec_context *ctx = get_current_ctx();
        struct exec_context *new_ctx;

        //Need to wake up parent when child process created with vfork exits

#ifdef vfork_var
        vfork_exit_handle(ctx);
#endif
        do_file_exit(ctx);   // Cleanup the files

        // cleanup of this process
        os_pfn_free(OS_PT_REG, ctx->os_stack_pfn);
        ctx->state = UNUSED;
        // check if we need to do cleanup
        int proc_exist = -1;

        for(ctr = 1; ctr < MAX_PROCESSES; ctr++) {
                struct exec_context *new_ctx = get_ctx_by_pid(ctr);
                if(new_ctx->state != UNUSED) {
                        proc_exist = 1;
                        break;
                }
        }

        stats->num_processes--;
        if(proc_exist == -1)
                do_cleanup();  /*Call this conditionally, see comments above*/

        new_ctx = pick_next_context(ctx);
        schedule(new_ctx);  //Calling from exit
}





/*System Call handler*/
long  do_syscall(u64 syscall, u64 param1, u64 param2, u64 param3, u64 param4)
{
	struct exec_context *current = get_current_ctx();
	unsigned long saved_sp;

	asm volatile(
		"mov %%rbp, %0;"
		: "=r" (saved_sp) 
		:
		: "memory"
	);  

	saved_sp += 0x10;    //rbp points to entry stack and the call-ret address is pushed onto the stack
	memcpy((char *)(&current->regs), (char *)saved_sp, sizeof(struct user_regs));  //user register state saved onto the regs 
	stats->syscalls++;
	dprintk("[GemOS] System call invoked. syscall no = %d\n", syscall);

	int ret = perform_tracing(syscall, param1, param2, param3, param4);
	if(ret != 0)
	{
		return -EINVAL;
	} 


	switch(syscall)
	{
	case SYSCALL_EXIT:
		dprintk("[GemOS] exit code = %d\n", (int) param1);
		do_exit();
		break;
	case SYSCALL_GETPID:
		dprintk("[GemOS] getpid called for process %s, with pid = %d\n", current->name, current->pid);
		return current->pid;      

	case SYSCALL_EXPAND:
		return do_expand(current, param1, param2);

	case SYSCALL_SHRINK:
		return do_shrink(current, param1, param2);

	case SYSCALL_ALARM:
		return do_alarm(param1);

	case SYSCALL_SLEEP:
		return do_sleep(param1);

	case SYSCALL_SIGNAL: 
		return do_signal(param1, param2);

	case SYSCALL_CLONE:
		return do_clone((void *)param1, (void *)param2);

	case SYSCALL_FORK:
		return do_fork();

	case SYSCALL_CFORK:
		return do_cfork();

	case SYSCALL_VFORK:
		return do_vfork();

	case SYSCALL_STATS:
		printk("ticks = %d swapper_invocations = %d context_switches = %d lw_context_switches = %d\n", 
		stats->ticks, stats->swapper_invocations, stats->context_switches, stats->lw_context_switches);
		printk("syscalls = %d page_faults = %d used_memory = %d num_processes = %d\n",
		stats->syscalls, stats->page_faults, stats->used_memory, stats->num_processes);
		printk("copy-on-write faults = %d allocated user_region_pages = %d\n",stats->cow_page_faults,
		stats->user_reg_pages);
		break;
	case SYSCALL_GET_USER_P:
		return stats->user_reg_pages;
	case SYSCALL_GET_COW_F:
		return stats->cow_page_faults;

	case SYSCALL_CONFIGURE:
		memcpy((char *)config, (char *)param1, sizeof(struct os_configs));      
		break;
	case SYSCALL_PHYS_INFO:
		printk("OS Data strutures:     0x800000 - 0x2000000\n");
		printk("Page table structures: 0x2000000 - 0x6400000\n");
		printk("User pages:            0x6400000 - 0x20000000\n");
		break;

	case SYSCALL_DUMP_PTT:
		return (u64) get_user_pte(current, param1, 1);

	case SYSCALL_MMAP:
		return (long) vm_area_map(current, param1, param2, param3, param4);

	case SYSCALL_MUNMAP:
		return (u64) vm_area_unmap(current, param1, param2);

	case SYSCALL_MPROTECT:
		return (long) vm_area_mprotect(current, param1, param2, param3);

	case SYSCALL_PMAP:
		return (long) vm_area_dump(current->vm_area, (int)param1);

	case SYSCALL_OPEN:
		return do_file_open(current,param1,param2,param3);

	case SYSCALL_READ:
		return do_file_read(current,param1,param2,param3);

	case SYSCALL_WRITE:
		return do_file_write(current,param1,param2,param3);

	case SYSCALL_DUP:
		return do_dup(current, param1);

	case SYSCALL_DUP2:
		return do_dup2(current, param1, param2);  

	case SYSCALL_CLOSE:
		return do_close(current, param1);

	case SYSCALL_LSEEK:
		return do_lseek(current, param1, param2, param3);

	case SYSCALL_FTRACE:
      		return  do_ftrace(current, param1, param2, param3, param4);

	case SYSCALL_TRACE_BUFFER:
		return do_create_trace_buffer(current, param1);

        case SYSCALL_START_STRACE:
                return do_start_strace(current, param1, param2);

        case SYSCALL_END_STRACE:
                return do_end_strace(current);

        case SYSCALL_READ_STRACE:
                return do_read_strace(current, param1, param2, param3);

        case SYSCALL_READ_FTRACE:
                return do_read_ftrace(current, param1, param2, param3);

	case SYSCALL_STRACE:
                return do_strace(current, param1, param2);
	default:
		return -1;

	}
	return 0;   /*GCC shut up!*/
}

