#include<entry.h>
#include<lib.h>
#include<context.h>
#include<memory.h>
#include<schedule.h>
#include<file.h>
#include<pipe.h>
#include<kbd.h>
#include<fs.h>
#include<fork.h>
#include<page.h>
#include<mmap.h>

extern u64* get_user_pte(struct exec_context *ctx, u64 addr, int dump);
extern int destroy_user_mappings(struct exec_context *ctx); 


void do_exit(u8 normal) 
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
  destroy_user_mappings(ctx); 
  do_vma_exit(ctx);
  if(!put_pfn(ctx->pgd)) 
      os_pfn_free(OS_PT_REG, ctx->pgd);   //XXX Now its fine as it is a single core system
  if(!put_pfn(ctx->os_stack_pfn))
     os_pfn_free(OS_PT_REG, ctx->os_stack_pfn);
  release_context(ctx); 
  new_ctx = pick_next_context(ctx);
  dprintk("Scheduling %s:%d [ptr = %x]\n", new_ctx->name, new_ctx->pid, new_ctx); 
  schedule(new_ctx);  //Calling from exit
}

/*system call handler for sleep*/
static long do_sleep(u32 ticks) 
{
  struct exec_context *new_ctx;
  struct exec_context *ctx = get_current_ctx();
  ctx->ticks_to_sleep = ticks;
  ctx->state = WAITING;
  new_ctx = pick_next_context(ctx);
   
  dprintk("Outgoing %x scheduling %x\n", ctx, new_ctx); 
  schedule(new_ctx);  //Calling from sleep
  return 0;
}

long invoke_sync_signal(int signo, u64 *ustackp, u64 *urip) 
{
  /*
     If signal handler is registered, manipulate user stack and RIP to execute signal handler
     ustackp and urip are pointers to user RSP and user RIP in the exception/interrupt stack
     Default behavior is exit() if sighandler is not registered for SIGFPE or SIGSEGV.
     Ignored for SIGALRM
  */

  struct exec_context *ctx = get_current_ctx();
  dprintk("Called signal with ustackp=%x urip=%x\n", *ustackp, *urip);
  
  if(ctx->sighandlers[signo] == NULL && signo != SIGALRM) {
      do_exit(0);
  }else if(ctx->sighandlers[signo]){  
      /*
        Add a frame to user stack
        XXX Better to check the sanctity before manipulating user stack
      */
      u64 rsp = (u64)*ustackp;
      *((u64 *)(rsp - 8)) = *urip;
      *ustackp = (u64)(rsp - 8);
      *urip = (u64)(ctx->sighandlers[signo]);
  }
  return 0;

}

/*system call handler for signal, to register a handler*/
static long do_signal(int signo, unsigned long handler) 
{
  struct exec_context *ctx = get_current_ctx();
  if(signo < MAX_SIGNALS && signo > -1) {
         ctx -> sighandlers[signo] = (void *)handler; // save in context
         return 0;
  }
 
  return -1;
}

/*system call handler for alarm*/
static long do_alarm(u32 ticks) 
{
  struct exec_context *ctx = get_current_ctx();
  if(ticks > 0) {
    ctx -> alarm_config_time = ticks;
    ctx -> ticks_to_alarm = ticks;
    return 0;
  }
  return -1;
}


/*System Call handler*/
long  do_syscall(long syscall, u64 param1, u64 param2, u64 param3, u64 param4, struct user_regs *regs)
{
    struct exec_context *current = get_current_ctx();

#if 0
    unsigned long saved_sp;
    
    asm volatile(
                   "mov %%rbp, %0;"
                    : "=r" (saved_sp) 
                    :
                    : "memory"
                );  

    saved_sp += 0x10;    //rbp points to entry stack and the call-ret address is pushed onto the stack
    memcpy((char *)(&current->regs), (char *)saved_sp, sizeof(struct user_regs));  //user register state saved onto the regs 
#endif
    current->regs = *regs;
    stats->syscalls++;
    dprintk("[GemOS] System call invoked. syscall no = %d ctx=%x\n", syscall, current);
    switch(syscall)
    {
          case SYSCALL_EXIT:
                              dprintk("[GemOS] exit code = %d\n", (int) param1);
                              do_exit(1);  //normal exit
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
                              return do_clone((void *)param1, (void *)param2, (void*)param3);
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
          case SYSCALL_PIPE:
                                  return do_create_pipe(current, (void*) param1);

          case SYSCALL_DUP:
                                  return do_dup(current, param1);

          case SYSCALL_DUP2:
                                  return do_dup2(current, param1, param2);  
          case SYSCALL_CLOSE:
                                  return do_close(current, param1);

          case SYSCALL_LSEEK:
                                  return do_lseek(current, param1, param2, param3);

          default:
                              return -1;
    }
    return 0;   /*GCC shut up!*/
}

extern int do_div_by_zero(struct user_regs *regs) {
    u64 rip = regs->entry_rip;
    printk("Div-by-zero @ [%x]\n", rip);
    invoke_sync_signal(SIGFPE, &regs->entry_rsp, &regs->entry_rip);
    return 0;
}

