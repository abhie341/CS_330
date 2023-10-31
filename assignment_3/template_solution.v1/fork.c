#include <fork.h>
#include <page.h>
#include <mmap.h>

extern u32 install_ptable(unsigned long base, struct  mm_segment *mm, u64 address, u32 upfn);
extern u32 map_physical_page(unsigned long base, u64 address, u32 access_flags, u32 upfn);
extern struct vm_area * get_vm_area(struct exec_context *ctx, u64 address);
extern u64* get_user_pte(struct exec_context *ctx, u64 addr, int dump);
extern void replicate_page_table(struct exec_context *child, struct exec_context *parent, struct vm_area *vma, u8 cow);
extern void remap_cow_page(struct exec_context *child, u64* p_pte, u64 addr, int access_flags);



#define FLAG_MASK 0x3ffffffff000UL 

void copy_vma(struct exec_context *child, struct exec_context *parent, u8 cow)
{
    unsigned long vm_start, vm_end;
    u32 access_flags;
    struct vm_area* parent_vm_area = parent->vm_area;
    struct vm_area* child_vm_area = NULL;
    struct vm_area* prev = NULL;
    while(parent_vm_area){
           vm_start = parent_vm_area->vm_start;
           vm_end = parent_vm_area->vm_end;
           access_flags = parent_vm_area->access_flags;
           child_vm_area = create_vm_area(vm_start, vm_end, access_flags);
	   if(!child->vm_area)
		   child->vm_area = child_vm_area;
           if(prev)
		   prev->vm_next = child_vm_area;
	   prev = child_vm_area;
           replicate_page_table(child, parent, child_vm_area, cow); 
           parent_vm_area = parent_vm_area->vm_next;
        } 
}

void cfork_copy_mm(struct exec_context *child, struct exec_context *parent){

    u64 vaddr;
    struct mm_segment *seg;

    child->pgd = os_pfn_alloc(OS_PT_REG);

    seg = &parent->mms[MM_SEG_CODE];
    for(vaddr = seg->start; vaddr < seg->next_free; vaddr += PAGE_SIZE){
        u64 *p_pte = get_user_pte(parent, vaddr, 0);
	if(p_pte)
	      remap_cow_page(child, p_pte, vaddr, seg->access_flags);  	
    }
    
    seg = &parent->mms[MM_SEG_RODATA];
    for(vaddr = seg->start; vaddr < seg->next_free; vaddr += PAGE_SIZE){
        u64 *p_pte = get_user_pte(parent, vaddr, 0);
	if(p_pte)
	      remap_cow_page(child, p_pte, vaddr, seg->access_flags);  	
    }
    
    seg = &parent->mms[MM_SEG_DATA];
    for(vaddr = seg->start; vaddr < seg->next_free; vaddr += PAGE_SIZE){
        u64 *p_pte = get_user_pte(parent, vaddr, 0);
	if(p_pte)
	      remap_cow_page(child, p_pte, vaddr, seg->access_flags);  	
    }

    seg = &parent->mms[MM_SEG_STACK];
    for(vaddr = seg->end - PAGE_SIZE; vaddr >= seg->next_free; vaddr -= PAGE_SIZE){
        u64 *p_pte = get_user_pte(parent, vaddr, 0);
	if(p_pte)
	      remap_cow_page(child, p_pte, vaddr, seg->access_flags);  	
    }

    copy_vma(child, parent, 1);
    //copy_os_pts(parent->pgd, child->pgd);
    return;
}


static void vfork_copy_mm(struct exec_context *child, struct exec_context *parent ){
    child->pgd = os_pfn_alloc(OS_PT_REG);
    u64 vaddr;
    void * os_addr;
    struct mm_segment *seg;
    struct pfn_info * p;
    u64 pfn;
    u32 ppfn;

    os_addr = osmap(child->pgd);
    bzero((char *)os_addr, PAGE_SIZE);

    void * os_p_addr;

    os_p_addr = osmap(parent->pgd);

    memcpy((char*)os_addr,(char*)os_p_addr, PAGE_SIZE);
    return;
    
}


void vfork_exit_handle(struct exec_context *ctx){
  u32 ppid = ctx->ppid;
  dprintk("pid:%u ppid:%u\n",ctx->pid,ppid);
  struct exec_context *parent_ctx = get_ctx_by_pid(ppid);
  dprintk("child_state:%d parent_state:%d\n",ctx->state,parent_ctx->state);
  if(parent_ctx->state == WAITING){
     parent_ctx->state = READY;
  }
  return;
}

void setup_child_context(struct exec_context *child)
{
   //Allocate pgd and OS stack
   child->os_stack_pfn = os_pfn_alloc(OS_PT_REG);
   child->os_rsp = (((u64) child->os_stack_pfn) << PAGE_SHIFT) + PAGE_SIZE;
   child->state = READY;  // Will be eligible in next tick
   stats->num_processes++;
}


/*
   Copies the mm structures along with the page table entries
*/
static void copy_mm(struct exec_context *child, struct exec_context *parent)
{
   void *os_addr;
   u64 vaddr; 
   struct mm_segment *seg;

   child->pgd = os_pfn_alloc(OS_PT_REG);

   os_addr = osmap(child->pgd);
   bzero((char *)os_addr, PAGE_SIZE);
   
   //CODE segment
   seg = &parent->mms[MM_SEG_CODE];
   for(vaddr = seg->start; vaddr < seg->next_free; vaddr += PAGE_SIZE){
      u64 *parent_pte =  get_user_pte(parent, vaddr, 0);
      if(parent_pte){
           u64 pfn = install_ptable((u64)os_addr, seg, vaddr, 0);  //Returns the blank page  
           pfn = (u64)osmap(pfn);
           memcpy((char *)pfn, (char *)(*parent_pte & FLAG_MASK), PAGE_SIZE); 
      }   
   } 
   //RODATA segment
   
   seg = &parent->mms[MM_SEG_RODATA];
   for(vaddr = seg->start; vaddr < seg->next_free; vaddr += PAGE_SIZE){
      u64 *parent_pte =  get_user_pte(parent, vaddr, 0);
      if(parent_pte){
           u64 pfn = install_ptable((u64)os_addr, seg, vaddr, 0);  //Returns the blank page  
           pfn = (u64)osmap(pfn);
           memcpy((char *)pfn, (char *)(*parent_pte & FLAG_MASK), PAGE_SIZE); 
      }	   
   } 
   
   //DATA segment
  seg = &parent->mms[MM_SEG_DATA];
  for(vaddr = seg->start; vaddr < seg->next_free; vaddr += PAGE_SIZE){
      u64 *parent_pte =  get_user_pte(parent, vaddr, 0);
      if(parent_pte){
           u64 pfn = install_ptable((u64)os_addr, seg, vaddr, 0);  //Returns the blank page  
           pfn = (u64)osmap(pfn);
           memcpy((char *)pfn, (char *)(*parent_pte & FLAG_MASK), PAGE_SIZE); 
      }
  } 
  
  //STACK segment
  seg = &parent->mms[MM_SEG_STACK];
  for(vaddr = seg->end - PAGE_SIZE; vaddr >= seg->next_free; vaddr -= PAGE_SIZE){
      u64 *parent_pte =  get_user_pte(parent, vaddr, 0);
      
     if(parent_pte){
           u64 pfn = install_ptable((u64)os_addr, seg, vaddr, 0);  //Returns the blank page  
           pfn = (u64)osmap(pfn);
           memcpy((char *)pfn, (char *)(*parent_pte & FLAG_MASK), PAGE_SIZE); 
      }
  } 
 
  copy_vma(child, parent, 0);
  copy_os_pts(parent->pgd, child->pgd); 
  return; 
}
long do_fork(void)
{
  
  struct exec_context *new_ctx = get_new_ctx();
  struct exec_context *ctx = get_current_ctx();
  u32 pid = new_ctx->pid;
  
  *new_ctx = *ctx;  //Copy the process
  new_ctx->pid = pid;
  new_ctx->ppid = ctx->pid; 
  new_ctx->vm_area = NULL; 
  copy_mm(new_ctx, ctx);
  do_file_fork(new_ctx);
  setup_child_context(new_ctx);

  return pid;
}

#if 0
/*do_cfork creates the child context and makes the child READY for schedule. 
  returns pid of the child process*/

long do_cfork(){
    u32 pid;
    struct exec_context *new_ctx = get_new_ctx();
    struct exec_context *ctx = get_current_ctx();
    pid = new_ctx->pid;

    *new_ctx = *ctx;
    new_ctx->pid = pid;
    new_ctx->ppid = ctx->pid;
    new_ctx->vm_area = NULL; 
    cfork_copy_mm(new_ctx, ctx);
    do_file_fork(new_ctx);
    setup_child_context(new_ctx);

    return pid;

}
#endif

/*do_vfork creates the child context and schedules it after keeping parent to WAITING state.
  In do_vfork the child copies user stack and points entry_rsp and rbp to new locations */

long do_vfork(){
    u32 pid;
    struct exec_context *new_ctx = get_new_ctx();
    struct exec_context *ctx = get_current_ctx();
    pid = new_ctx->pid;
    u64 length = (ctx->mms[MM_SEG_STACK].end)-(ctx->regs.entry_rsp);
    u64 new_entry_rsp = ctx->regs.entry_rsp - length;
    u64 new_rbp = ctx->regs.rbp - length;

    *new_ctx = *ctx;
    new_ctx->pid = pid;
    new_ctx->ppid = ctx->pid;
   
    vfork_copy_mm(new_ctx, ctx);
    setup_child_context(new_ctx);

     memcpy((char*)new_entry_rsp,(char*)ctx->regs.entry_rsp,length);
     ctx->state = WAITING;
     ctx->regs.rax = pid;
     new_ctx->regs.entry_rsp = new_entry_rsp;
     new_ctx->regs.rbp = new_rbp;
     schedule(new_ctx);
     return pid;
}

/*
  system call handler for clone, create thread like 
  execution contexts
*/
long do_clone(void *th_func, void *user_stack, void *user_arg) 
{
  int ctr;
  struct exec_context *new_ctx = get_new_ctx();
  struct exec_context *ctx = get_current_ctx();
  u32 pid = new_ctx->pid;
  struct thread *n_thread;
  
  if(ctx->type != EXEC_CTX_USER){
        new_ctx->state = UNUSED;
        return -EINVAL;
  }

  *new_ctx = *ctx;

  new_ctx->ppid = ctx->pid;
  new_ctx->pid = pid;
  new_ctx->type = EXEC_CTX_USER_TH;
  // allocate page for os stack in kernel part of process's VAS
  setup_child_context(new_ctx);

  new_ctx->regs.entry_rip = (u64) th_func;
  new_ctx->regs.entry_rsp = (u64) user_stack;
  new_ctx->regs.rbp = new_ctx->regs.entry_rsp;

  //The first argument to the thread is the third argument here
  //First arg (user arg) is put to RDI

  new_ctx->regs.rdi = (u64)user_arg;

  sprintk(new_ctx->name, "thr-%d", new_ctx->pid); 

  dprintk("User stack is at %x fp = %x argument = %x\n", user_stack, th_func, user_arg);
  //Return the pid of newly created thread
  dprintk("ctx %x created %x\n", ctx, new_ctx);
 
  new_ctx->state = WAITING;
	
  return pid;

}
