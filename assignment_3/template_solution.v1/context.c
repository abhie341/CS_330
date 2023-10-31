#include<types.h>
#include<lib.h>
#include<context.h>
#include<memory.h>
#include<init.h>
#include<apic.h>
#include<idt.h>
#include<page.h>
#include<file.h>
#include<entry.h>

static int num_contexts;
static struct exec_context *current; 
static struct exec_context *ctx_list;

extern int install_os_pts(u32 pl4);
extern u32 map_physical_page(unsigned long base, u64 address, u32 access_flags, u32 upfn);
extern u32 invalidate_pte(struct exec_context *ctx, unsigned long addr);
extern u64* get_user_pte(struct exec_context *ctx, u64 addr, int dump);
extern int do_unmap_user(struct exec_context *ctx, u64 addr);
extern void install_page_table(struct exec_context *ctx, u64 addr, u64 error_code);

void release_context(struct exec_context *ctx)
{
  ctx->state = UNUSED;
  dprintk("The process %s:%d has exited [ptr = %x]\n", ctx->name, ctx->pid, ctx); 
  stats->num_processes--; 
  num_contexts--;
}
struct exec_context *get_new_ctx()
{
  int ctr;
  struct exec_context *ctx = ctx_list;
  for(ctr=0; ctr < MAX_PROCESSES; ++ctr){
      if(ctx->state == UNUSED){ 
          ctx->pid = ctr;
          ctx->state = NEW;
	  num_contexts++;
          return ctx; 
      }
     ctx++;
  }
  OS_BUG("System limit on processes reached\n");
  return NULL;
}

struct exec_context *create_context(char *name, u8 type)
{
  int i;
  struct mm_segment *seg;
  struct exec_context *ctx = get_new_ctx();
  ctx->state = NEW;
  ctx->type = type;
  ctx->used_mem = 0;
  ctx->pgd = 0;
  ctx->os_stack_pfn = os_pfn_alloc(OS_PT_REG);
  ctx->os_rsp = (u64)osmap(ctx->os_stack_pfn + 1);

   // For the standart inputs FD

  ctx->files[0] = create_standard_IO(STDIN);
  ctx->files[1] = create_standard_IO(STDOUT);
  ctx->files[2] = create_standard_IO(STDERR);


  memcpy(ctx->name, name, strlen(name));
  seg = &ctx->mms[MM_SEG_CODE];
  
  seg->start = CODE_START;
  seg->end   = RODATA_START - 1;
  seg->access_flags = MM_RD | MM_EX;
  seg++; 
  
  seg->start = RODATA_START;
  seg->next_free = seg->start;
  seg->end   = DATA_START - 1;
  seg->access_flags = MM_RD;
  seg++; 
  
  seg->start = DATA_START;
  seg->end   = MMAP_START - 1;
  seg->access_flags = MM_RD | MM_WR;
  seg++; 
  
  seg->start = STACK_START - MAX_STACK_SIZE;
  seg->end   = STACK_START;
  seg->access_flags = MM_RD | MM_WR;
  return ctx; 
}


static void install_init_code(u64 base, struct mm_segment *mm, char *code, int num_pages)
{
	u64 vaddr = mm->start;
	for(int ctr=0; ctr<num_pages; ++ctr){
	     u32 upfn = os_pfn_alloc(USER_REG);
             char *ptr = osmap(upfn);
             memcpy((char *)ptr, code, PAGE_SIZE);
	     map_physical_page(base, vaddr, mm->access_flags, upfn);
	     code += PAGE_SIZE;
	     vaddr += PAGE_SIZE;
	}
}

int exec_init(struct exec_context *ctx, struct init_args *args)
{
   unsigned long pl4;
   u64 stack_start, sptr, textpfn, fmem;
   void *os_addr;
   struct mm_segment *mm;

   printk("Setting up init process ...\n");
   ctx->pgd = os_pfn_alloc(OS_PT_REG);
   os_addr = osmap(ctx->pgd);
   bzero((char *)os_addr, PAGE_SIZE);

   mm = &ctx->mms[MM_SEG_DATA];
   map_physical_page((u64) os_addr, mm->start, mm->access_flags, 0);   
   mm->next_free = mm->start + PAGE_SIZE;

   mm = &ctx->mms[MM_SEG_STACK];
   map_physical_page((u64) os_addr, mm->end - PAGE_SIZE, mm->access_flags, 0);   
   mm->next_free = mm->end - PAGE_SIZE;
   
   pl4 = ctx->pgd; 
   if(install_os_pts(pl4))
        return -1;
   pl4 = pl4 << PAGE_SHIFT;
   set_tss_stack_ptr(ctx); 
   stats->num_processes++;   
   sptr = STACK_START;


   //Before executing init, let us change out CR3 to init CR3
   asm volatile("mov %0, %%cr3;"
		 "mfence;"  
		 :
		 : "r" (pl4)
		 : "memory"
		 );

   mm = &ctx->mms[MM_SEG_CODE];
   install_init_code((u64)os_addr, mm, (char*) (0x200000), INIT_CODE_PAGES);
   mm->next_free = mm->start + (INIT_CODE_PAGES << PAGE_SHIFT);

   //fmem = textpfn << PAGE_SHIFT;
   //memcpy((char *)fmem, (char *)(0x200000), CODE_PAGES << PAGE_SHIFT);   /*This is where INIT is in the os binary*/ 
  
   //We are comming from swapper process here
   //We should switch out swapper here itself 
   current->state = READY;

   fmem = mm->start;
   current = ctx;
   current->state = RUNNING;
   printk("Page table setup done, launching init ...\n");

   asm volatile( 
            "cli;"
            "pushq $0x2b;"
            "pushq %0;"
            "pushfq;"
            "popq %%rax;"
            "or $0x200, %%rax;"
            "pushq %%rax;" 
            "pushq $0x23;"
            "pushq %1;"
            "mov $0x2b, %%ax;"
            "mov %%ax, %%ds;"
            "mov %%ax, %%es;"
            "mov %%ax, %%gs;"
            "mov %%ax, %%fs;"
           : 
           : "r" (sptr), "r" (fmem) 
           : "memory"
          );
 asm volatile( "mov %0, %%rdi;"
            "mov %1, %%rsi;"
            "mov %2, %%rcx;"
            "mov %3, %%rdx;"
            "mov %4, %%r8;"
	    "xor %%r9, %%r9;"
	    "xor %%r10, %%r10;"
	    "xor %%r11, %%r11;"
	    "xor %%r12, %%r12;"
	    "xor %%rax, %%rax;"
	    "xor %%rbx, %%rbx;"
            "iretq"
            :
            : "m" (args->rdi), "m" (args->rsi), "m" (args->rcx), "m" (args->rdx), "m" (args->r8)
            : "memory"
           );
   
   return 0; 
}
struct exec_context* get_current_ctx(void)
{
   return current;
}
void set_current_ctx(struct exec_context *ctx)
{
   current = ctx;
}

struct exec_context *get_ctx_list()
{
   return ctx_list; 
}


struct exec_context *get_ctx_by_pid(u32 pid)
{
   if(pid >= MAX_PROCESSES){
        printk("%s: invalid pid %d\n", __func__, pid);
        return NULL;
   }
  return (ctx_list + pid);
}

int set_process_state(struct exec_context *ctx, u32 state)
{
   if(state >= MAX_STATE){
        printk("%s: invalid state %d\n", __func__, state);
        return -1;
   }
   ctx->state = state;
   return 0;
}
#if 0
static void swapper_task()
{
   stats->swapper_invocations++;
   while(1){
             asm volatile("sti;"
                          "hlt;"
                           :::"memory"
                         );
   }
}

void load_swapper(struct user_regs *regs)
{
   extern void *return_from_os;
   unsigned long retptr = (unsigned long)(&return_from_os);
   struct exec_context *swapper = ctx_list;
   u64 cr3 = swapper->pgd;
   current = swapper;
   current->state = RUNNING;
   set_tss_stack_ptr(swapper);
   memcpy((char *)regs, (char *)(&swapper->regs), sizeof(struct user_regs));
   ack_irq();
   asm volatile("mov %0, %%cr3;"
                "mov %1, %%rsp;"
                "callq *%2;"
                :
                :"r" (cr3), "r" (regs), "r"  (retptr)
                :"memory");
   
}
void init_swapper()
{
   u64 ss, cs, rflags; 
   struct exec_context *swapper;
   int num_pfns = ((MAX_PROCESSES * sizeof(struct exec_context)) >> PAGE_SHIFT) + 1;
   u64 ctx_l = os_contig_pfn_alloc(OS_PT_REG, num_pfns);
   ctx_list = (struct exec_context *) (ctx_l << PAGE_SHIFT);
   bzero((char*)ctx_list, num_pfns << PAGE_SHIFT);
   swapper = ctx_list;
   swapper->type = EXEC_CTX_OS;
   swapper->state = READY;
   swapper->pgd = 0x70;
   memcpy(swapper->name, "swapper", 8);
   swapper->os_stack_pfn = os_pfn_alloc(OS_PT_REG);
   swapper->os_rsp = (((u64) swapper->os_stack_pfn) << PAGE_SHIFT) + PAGE_SIZE;
   bzero((char *)(&swapper->regs), sizeof(struct user_regs));
   swapper->regs.entry_rip = (unsigned long)(&swapper_task);
   swapper->regs.entry_rsp = swapper->os_rsp;
   asm volatile( "mov %%ss, %0;"
                 "mov %%cs, %1;"
                 "pushf;"
                 "pop %%rax;"
                 "mov %%rax, %2;"
                 : "=r" (ss), "=r" (cs), "=r" (rflags)
                 :
                 : "rax", "memory"
   );
   swapper->regs.entry_ss = ss; 
   swapper->regs.entry_rflags = rflags;
   swapper->regs.entry_cs = cs;
   return;
}
#endif

static void swapper_task()
{
   stats->swapper_invocations++;
   while(1){
	     if(num_contexts == 1)
		     invoke_dsh();

             asm volatile("sti;"
                          "hlt;"
                           :::"memory"
                         );
   }
}

void init_swapper()
{
   u64 ss, cs, rflags, pl4; 
   struct exec_context *swapper;
   int num_pfns = ((MAX_PROCESSES * sizeof(struct exec_context)) >> PAGE_SHIFT) + 1;
   u64 ctx_l = os_contig_pfn_alloc(OS_PT_REG, num_pfns);
   ctx_list = (struct exec_context *) (ctx_l << PAGE_SHIFT);
   bzero((char*)ctx_list, num_pfns << PAGE_SHIFT);
   swapper = ctx_list;
   swapper->type = EXEC_CTX_OS;
   swapper->state = READY;
   swapper->pgd = os_pfn_alloc(OS_PT_REG);
   pl4 = swapper->pgd; 
   
   if(install_os_pts(pl4))
        OS_BUG("Initializing swapper failed");
   pl4 = pl4 << PAGE_SHIFT;

   memcpy(swapper->name, "swapper", 8);
   swapper->os_stack_pfn = os_pfn_alloc(OS_PT_REG);
   swapper->os_rsp = (((u64) swapper->os_stack_pfn) << PAGE_SHIFT) + PAGE_SIZE;
   bzero((char *)(&swapper->regs), sizeof(struct user_regs));
   swapper->regs.entry_rip = (unsigned long)(&swapper_task);
   swapper->regs.entry_rsp = swapper->os_rsp;

   asm volatile( "mov %%ss, %0;"
                 "mov %%cs, %1;"
                 "pushf;"
                 "pop %%rax;"
                 "mov %%rax, %2;"
                 : "=r" (ss), "=r" (cs), "=r" (rflags)
                 :
                 : "rax", "memory"
   );

   swapper->regs.entry_ss = ss; 
   swapper->regs.entry_rflags = rflags;
   swapper->regs.entry_cs = cs;
   swapper->state = RUNNING;
   
   asm volatile("mov %0, %%cr3;"
		 "mfence;"  
		 :
		 : "r" (pl4)
		 : "memory"
		 );
   //Now call the real swapper
   set_current_ctx(swapper);
   num_contexts++;
   swapper_task();
   return;
}
