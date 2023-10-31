#include <types.h>
#include <mmap.h>
#include <fork.h>
#include <v2p.h>
#include <page.h>

/* 
 * You may define macros and other helper functions here
 * You must not declare and use any static/global variables 
 * */

/*XXX Remove from the template*/

#define PF_ERR_P 0
#define PF_ERR_W 1
#define PF_ERR_USER 2
#define PF_ERR_RSVD 3
#define PF_ERR_FETCH 4
#define FLAG_MASK 0x3ffffffff000UL 
#define B_PTE_P 0
#define B_PTE_W 3
#define B_PTE_U 4
#define B_PTE_A 1
#define B_PTE_D 2
#define B_PMD_PS 7
#define B_PTE_G 8
#define B_PTE_XD 63
#define USER_RO_FLAGS ((1 << B_PTE_P) | (1 << B_PTE_U))
#define USER_RW_FLAGS ((1 << B_PTE_P) | (1 << B_PTE_W) | (1 << B_PTE_U))

static inline void invlpg(unsigned long addr) {
    asm volatile("invlpg (%0)" ::"r" (addr) : "memory");
}

extern u32 map_physical_page(unsigned long base, u64 address, u32 access_flags, u32 upfn);
extern struct vm_area * get_vm_area(struct exec_context *ctx, u64 address);
extern u32 invalidate_pte(struct exec_context *ctx, unsigned long addr);
extern u64* get_user_pte(struct exec_context *ctx, u64 addr, int dump);
extern void copy_vma(struct exec_context *child, struct exec_context *parent, u8 cow);
extern void cfork_copy_mm(struct exec_context *child, struct exec_context *parent);
extern int purge_mapping_range(struct exec_context *ctx, u64 start, u64 end);

/************************************** Start helper functions ********************************/

//change protection of the mapping, be careful about CoW mappings

static long change_mapping_prot(struct exec_context *ctx, u64 start, u64 end, int access_flags)
{
    long count = 0;	
    u64 addr = start;
    while(addr < end){
         u64 *pte = get_user_pte(ctx, addr, 0);
	 if(!pte){
              addr += PAGE_SIZE;  
	      continue;
	 }
	 u32 pfn = (*pte & FLAG_MASK) >> PAGE_SHIFT;
	 if(get_pfn_refcount(pfn) == 1){ //Non CoW page
                *pte = ((u64)pfn << PAGE_SHIFT) | USER_RO_FLAGS;
                if(access_flags & MM_WR)
                        *pte |= USER_RW_FLAGS;
		invlpg(addr);
		count++;
	 }else{
		 gassert(((*pte & (1UL << B_PTE_W)) == 0), "Invalid Cow PFN"); 
	 }
         addr += PAGE_SIZE;  
    }    
    return count;
}

static void dealloc_vm_area(struct vm_area *vm)
{
    stats->num_vm_area--;
    os_free(vm, sizeof(struct vm_area));
}
static inline int get_num_pages(int length)
{
    int num_pages = (length / PAGE_SIZE);

    if(length % PAGE_SIZE != 0)
    {
        ++num_pages;
    }
    return num_pages;
}

// Function to get the end address of vm_area given the start address and the length
static inline u64 get_end_addr(u64 start_addr, int length)
{
    return (((start_addr >> PAGE_SHIFT)+ get_num_pages(length)) << PAGE_SHIFT);
}

// Function to create and merge vm areas.
static u64 map_vm_area(struct vm_area* vm, u64 start_addr, int length, int prot)
{
    u64 addr = -1;

    // Merging the requested region with the existing vm_area (END)
    if(vm && vm -> access_flags == prot)
    {
        addr = vm->vm_end;
        vm->vm_end = get_end_addr(vm->vm_end, length);

        struct vm_area *next = vm -> vm_next;

        // If End address is same as next vm_area. Then expand the current vm_area and delete the other one.
        if(next && vm->vm_end == next ->vm_start && vm->access_flags == next->access_flags)
        {
            vm->vm_end = next->vm_end;
            vm->vm_next = next->vm_next;
            dealloc_vm_area(next);
        }
    } else if(vm->vm_next && vm->vm_next->access_flags == prot) {
        // Merging the requested region with existing vm_area (Front)
        struct vm_area *next = vm -> vm_next;
        next->vm_start = start_addr;
        addr = start_addr;
    } else {
        // Creating a new vm_area with requested access permission

        struct vm_area *new_vm_area = create_vm_area(start_addr, get_end_addr(start_addr, length), prot);

        if(vm->vm_next)
        {
            new_vm_area ->vm_next = vm->vm_next;
        }
        vm->vm_next = new_vm_area;

        addr = new_vm_area->vm_start;
    }
   return addr;
}


// Function to handle the hint address and MAP_FIXED flags
static long look_up_hint_addr(struct vm_area* vm, u64 addr, int length, int prot, int flags)
{
    long ret_addr = 0;
    while(vm)
    {
        // Requested Region is already mapped
        if(addr >= vm ->vm_start && addr < vm->vm_end)
        {
            break;
        } else
        {
            // Creating a new area Region
            u64 start_page = (vm->vm_end);
            u64 end_page = vm -> vm_next ? vm->vm_next->vm_start : MMAP_AREA_END;

            if(addr >= start_page && addr < end_page)
            {
                int available_pages = (end_page >> PAGE_SHIFT) - (addr >> PAGE_SHIFT);
                int required_pages = get_num_pages(length);
                // printk("vm[%x -> %x]addr [%x] available[%d] requested[%d] \n",vm->vm_start, vm->vm_end, addr, available_pages, required_pages);
                if(available_pages >= required_pages)
                {
                    u64 end_address = get_end_addr(addr, length);
                    struct vm_area * vm_next = vm->vm_next;

                    if(vm->vm_end == addr && vm->access_flags == prot)
                    {
                        vm->vm_end = end_address;
                        if(vm_next && vm->vm_end == vm_next->vm_start && vm_next->access_flags == prot)
                        {
                            vm->vm_end = vm_next->vm_end;
                            vm->vm_next = vm_next->vm_next;
                             dealloc_vm_area(vm_next);
                        }
			     } else if(vm_next && vm_next->vm_start == end_address && vm_next->access_flags == prot)
                    {
                        vm_next->vm_start = addr;
                    } else
                    {
                        struct vm_area *new_vm_area = create_vm_area(addr, get_end_addr(addr, length), prot);
                        if(vm)
                        {
                            if(vm->vm_next)
                            {
                                new_vm_area ->vm_next = vm->vm_next;
                            }
                            vm->vm_next = new_vm_area;
                        }
                    }
                    ret_addr = addr;
                }

                break;
            }
        }
        vm = vm -> vm_next;
    }

    // If ret_addr is zero and MAP_FIXED is not set. Then we have to look for a new region 
    // Which statisfies the request. Address wont be considered as hint.
    if(ret_addr <= 0 && (flags & MAP_FIXED))
    {
        ret_addr = -1;
    }
    return ret_addr;
}

static struct vm_area* lookup_vma(struct vm_area *vm, u64 start, struct vm_area **prev)
{
   while(vm){
	  if(vm->vm_start <= start && start < vm->vm_end)  //overlap check
		 return vm;
	   *prev = vm;
	   vm = vm->vm_next; 
   }	   
   return NULL;
}

//Always maintain original VM. prev and next may be combined
static struct vm_area* try_merge_vms(struct vm_area *vm, struct vm_area *prev, u8 check_next)
{
   struct vm_area *next = vm->vm_next;	
   if(prev && vm->access_flags == prev->access_flags && vm->vm_start == prev->vm_end){
	     prev->vm_end = vm->vm_end;  //Keep prev, free vm
             prev->vm_next = vm->vm_next;
	     dealloc_vm_area(vm);
	     vm = prev;
   }
   if(check_next && next && next->access_flags == vm->access_flags && next->vm_start == vm->vm_end){ //keep vm, free next
             vm->vm_next = next->vm_next;
	     vm->vm_end = next->vm_end;
	     dealloc_vm_area(next);
   }
   return vm;
      	   
}


// Funtion to handle the MAP_POPULATE Flags. Mapping the physical pages with vm_area
void vm_map_populate(u64 pgd, u64 addr, u32 prot, u32 page_count)
{
    u64 base_addr = (u64) osmap(pgd);
    u32 access_flags = (prot & (PROT_WRITE)) ? MM_WR : 0;
    u64 virtual_addr = addr;
    while(page_count > 0)
    {
        map_physical_page(pgd, virtual_addr, access_flags, 0);
        virtual_addr += PAGE_SIZE;
        --page_count;
    }
}
/*XXX End remove from the template*/
/************************************** End helper functions ********************************/

/**
 * mprotect System call Implementation.
 */

long vm_area_mprotect(struct exec_context *current, u64 addr, int length, int prot)
{
    u64 end_addr = get_end_addr(addr, length);
    u64 caddr = (addr >> PAGE_SHIFT) << PAGE_SHIFT;
    struct vm_area *vm = current->vm_area;
    struct vm_area *prev_vma = NULL;
   
    //Start vma is beyond
    if(vm && vm->vm_next && caddr < vm->vm_next->vm_start)
          caddr = vm->vm_next->vm_start;
   

    while(vm && caddr < end_addr){
               vm = lookup_vma(vm, caddr, &prev_vma);
	       if(!vm)
		       break;
	       if(vm->access_flags == prot){
		       vm = try_merge_vms(vm, prev_vma, 0);
		       caddr = vm->vm_end;
		       prev_vma = vm;
		       vm = vm->vm_next;
	       }else{
                    if(vm->vm_start == caddr && end_addr >= vm->vm_end){  //The whole VMA is getting updated
                              change_mapping_prot(current, vm->vm_start, vm->vm_end, prot);
		              vm->access_flags = prot;
			      vm = try_merge_vms(vm, prev_vma, 1);
		              caddr = vm->vm_end;
		              prev_vma = vm;
		              vm = vm->vm_next;

                     }else if(caddr > vm->vm_start && end_addr >= vm->vm_end){  //higher side of the VMA getting updated
                              struct vm_area *new_vm_area = create_vm_area(caddr, vm->vm_end, prot);
		              
			      vm->vm_end = caddr;
			      
			      new_vm_area->vm_next = vm->vm_next;
			      vm->vm_next = new_vm_area;
                              change_mapping_prot(current, new_vm_area->vm_start, new_vm_area->vm_end, prot);
			      vm = try_merge_vms(new_vm_area, NULL, 1);
			      caddr = vm->vm_end;
                              prev_vma = vm;
			      vm = vm->vm_next;
                      }else if(caddr == vm->vm_start && end_addr < vm->vm_end){  //lower side of the VMA getting updated
                              struct vm_area *new_vm_area = create_vm_area(end_addr, vm->vm_end, vm->access_flags);
			      new_vm_area->vm_next = vm->vm_next;
			      vm->vm_next = new_vm_area;
			      
			      caddr = vm->vm_end;
			      vm->vm_end = end_addr;
			      vm->access_flags = prot;
                              change_mapping_prot(current, vm->vm_start, vm->vm_end, prot);
			      vm = try_merge_vms(vm, prev_vma, 0);
		              prev_vma = vm;
			      vm = vm->vm_next;
	              }else if(caddr > vm->vm_start && end_addr < vm->vm_end){ //in the middle
                              struct vm_area *new_vm_area = create_vm_area(caddr, end_addr, prot);
                              struct vm_area *old_vm_area = create_vm_area(end_addr, vm->vm_end, vm->access_flags);
                              
			      old_vm_area->vm_next = vm->vm_next;
			      new_vm_area->vm_next = old_vm_area;
			      
			      vm->vm_next = new_vm_area;
                              vm->vm_end = caddr;
                              change_mapping_prot(current, caddr, end_addr, prot);
			      caddr = old_vm_area->vm_end;
                              prev_vma = old_vm_area;
			      vm = old_vm_area->vm_next;
	              }else{
			      gassert(0, "mprotect logic error");
		      }

	       }
	 if(vm && caddr < vm->vm_start)
		 caddr = vm->vm_start;
    }
    return 0;
  	
}
  
/**
 * mmap system call implementation.
 */
long vm_area_map(struct exec_context *current, u64 addr, int length, int prot, int flags)
{
 long ret_addr = -1;
    // Checking the hint address ranges
    if((addr && !( addr >= MMAP_AREA_START && addr < MMAP_AREA_END))|| length <=0 )
    {
        return ret_addr;
    }

    struct vm_area *vm = current->vm_area;
    int required_pages = get_num_pages(length);
    // Allocating one page dummy vm_area
    if(!vm)
    {        
        vm = create_vm_area(MMAP_AREA_START, get_end_addr(MMAP_AREA_START, PAGE_SIZE), PROT_NONE);
        current->vm_area = vm;
    }

    /*Hook out this*/    
    if((flags & MAP_TH_PRIVATE) && (flags & MAP_FIXED)){ 
                return ret_addr;
    }

    // Hint address handling 
    if(addr || (flags & MAP_FIXED))
    {
       ret_addr = look_up_hint_addr(vm, addr, length, prot, flags);
       if(ret_addr > 0 || ret_addr == -1)
       {
           if(ret_addr > 0 && (flags & MAP_POPULATE))
           {
                u64 base_addr = (u64) osmap(current->pgd);
                vm_map_populate(base_addr, ret_addr, prot, required_pages);
           }
           return ret_addr;
       }
    }

    int do_created = 0;
   

    // Traversing the linked list of vm_areas
    while(!do_created && vm)
    {
        u64 start_page = (vm->vm_end) >> PAGE_SHIFT;
        u64 end_page;

        if(vm -> vm_next)
            end_page  = (vm->vm_next->vm_start) >> PAGE_SHIFT;
        else
            end_page = MMAP_AREA_END >> PAGE_SHIFT;

        // Available Free pages serves the requested. 
        // then either create a new vm_area or merge with existing one
        if((end_page - start_page) >= required_pages)
        {
            
            ret_addr = map_vm_area(vm, vm->vm_end, length, prot);
            do_created = 1;
        } else
        {
            vm = vm -> vm_next;
        }
    }
    // MAP_POPULATE flag handlers
    // printk("Before MAP POPULATE [%x] [%d] [%d]\n", ret_addr, flags, MAP_POPULATE);
   if(ret_addr > 0 && (flags & MAP_POPULATE))
   {
        u64 base_addr = (u64) osmap(current->pgd);
        vm_map_populate(base_addr, ret_addr, prot, required_pages);
   }
    return ret_addr;
}

/**
 * munmap system call implemenations
 */

long vm_area_unmap(struct exec_context *current, u64 addr, int length)
{
   struct vm_area *vm = current->vm_area;
   struct vm_area *prev_vma = NULL;
   u64 caddr = (addr >> PAGE_SHIFT) << PAGE_SHIFT;
   u64 end_addr = get_end_addr(addr, length);
   
   if(caddr != addr || !vm)
	  return -1; 

   //Start vma is beyond
   if(vm->vm_next && caddr < vm->vm_next->vm_start)
          caddr = vm->vm_next->vm_start;

   vm = lookup_vma(vm, caddr, &prev_vma);
   if(!vm)
          return 0;

   while(vm && caddr < end_addr){
        if(vm->vm_start == caddr && end_addr >= vm->vm_end){  //The whole VMA is getting unmapped
	         purge_mapping_range(current, vm->vm_start, vm->vm_end);
                 caddr = vm->vm_end;
		 prev_vma->vm_next = vm->vm_next;
	         dealloc_vm_area(vm);
                 vm = prev_vma->vm_next;
        }else if(caddr > vm->vm_start && end_addr >= vm->vm_end){  //higher (end) side of the VMA getting unmapped
	         u64 end = vm->vm_end;
		 purge_mapping_range(current, caddr, vm->vm_end);
                 vm->vm_end = caddr;
		 caddr = end;
		 prev_vma = vm;
                 vm = vm->vm_next;
         }else if(caddr == vm->vm_start && end_addr < vm->vm_end){  //lower side of the VMA getting updated
		 purge_mapping_range(current, caddr, end_addr);
		 vm->vm_start = end_addr;
		 caddr = vm->vm_end;
                 vm = vm->vm_next;
		 //No need for setting the prev, the loop terminates here
         }else if(caddr > vm->vm_start && end_addr < vm->vm_end){ //in the middle
                 struct vm_area *new_vm_area = create_vm_area(end_addr, vm->vm_end, vm->access_flags);
                 
                 new_vm_area->vm_next = vm->vm_next;
                 vm->vm_next = new_vm_area;
		 purge_mapping_range(current, caddr, end_addr);

                 vm->vm_end = caddr;
		 caddr = new_vm_area->vm_end;
		 vm = new_vm_area->vm_next;
		 //No need for setting the prev, the loop terminates here
         }else{
                 gassert(0, "mmap logic error");
         }
	 if(vm && caddr < vm->vm_start)
		 caddr = vm->vm_start;
    }
    return 0;
  	
}



/**
 * Function will invoked whenever there is page fault for an address in the vm area region
 * created using mmap
 */

long vm_area_pagefault(struct exec_context *current, u64 addr, int error_code)
{
    long fault_fixed = -1;
    u64 cr3 = current->pgd << PAGE_SHIFT; 
    struct vm_area *vm = current->vm_area;
    while(vm)
    {
        if (addr >= vm->vm_start && addr < vm->vm_end)
        {
            // Checking the vm_area access flags and error_code of the page fault.
            if((error_code & (1 << PF_ERR_W)) && !(vm->access_flags & MM_WR)){
                break;
	    }else if((error_code & (1 << PF_ERR_P)) && (error_code & (1 << PF_ERR_W)) && (vm->access_flags & MM_WR)){
                stats->cow_page_faults++;
		fault_fixed = handle_cow_fault(current, addr, vm->access_flags);
		break;
	    }
	    fault_fixed = map_physical_page(cr3, addr, vm->access_flags, 0);
            break;
        }
        vm = vm->vm_next;
    }
    return fault_fixed;
}

/**
 * cfork system call implemenations
 */

long do_cfork(){
    u32 pid;
    struct exec_context *new_ctx = get_new_ctx();
    struct exec_context *ctx = get_current_ctx();
     /* Do not modify above lines
     * 
     * */   
     /*--------------------- Your code [start]---------------*/
     
    pid = new_ctx->pid;

    *new_ctx = *ctx;
    new_ctx->pid = pid;
    new_ctx->ppid = ctx->pid;
    new_ctx->vm_area = NULL; 
    cfork_copy_mm(new_ctx, ctx);
    do_file_fork(new_ctx);
    setup_child_context(new_ctx);



     /*--------------------- Your code [end] ----------------*/
    
     /*
     * The remaining part must not be changed
     */
    copy_os_pts(ctx->pgd, new_ctx->pgd);
    do_file_fork(new_ctx);
    setup_child_context(new_ctx);
    return pid;
}



/* Cow fault handling, for the entire user address space
 * For address belonging to memory segments (i.e., stack, data) 
 * it is called when there is a CoW violation in these areas. 
 *
 * For vm areas, your fault handler 'vm_area_pagefault'
 * should invoke this function
 * */

long handle_cow_fault(struct exec_context *current, u64 vaddr, int access_flags)
{
  u64 cr3 = current->pgd << PAGE_SHIFT; 
  u64 *pte = get_user_pte(current, vaddr, 0);
  u32 ppfn = (u32)(((*pte) & FLAG_MASK) >> PAGE_SHIFT);
  long retval = -1;
  if(get_pfn_refcount(ppfn) > 1){
	retval = map_physical_page(cr3, vaddr, access_flags, 0);
        u64 pfn = (u64)osmap(retval);
        memcpy((char *)pfn, (char *)osmap(ppfn), PAGE_SIZE);
        if(!put_pfn(ppfn)){
             printk("%s: Error in handle cowfault\n", __func__);
	     OS_BUG("PFN refcount");
	}
  }else{
	 gassert((*pte & (1 << B_PTE_P)), "Invalid cow fault");
	 *pte = *pte | (1 << B_PTE_W); 
	 retval = 0; 
  }
  invlpg(vaddr);
  return retval;
}
