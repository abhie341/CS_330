#include<types.h>
#include<mmap.h>

extern u32 map_physical_page(unsigned long base, u64 address, u32 access_flags, u32 upfn);
extern struct vm_area * get_vm_area(struct exec_context *ctx, u64 address);
extern u32 invalidate_pte(struct exec_context *ctx, unsigned long addr);
extern u64* get_user_pte(struct exec_context *ctx, u64 addr, int dump);

static struct vm_area * alloc_vm_area()
{
    struct vm_area *vm = os_alloc(sizeof(struct vm_area));
    bzero(vm, sizeof(struct vm_area));
    stats->num_vm_area++;
    return vm;
}

static void dealloc_vm_area(struct vm_area *vm)
{
    stats->num_vm_area--;
    os_free(vm, sizeof(struct vm_area));
}

// Function used to dump the vm_area
int vm_area_dump(struct vm_area *vm, int details)
{
    /** TODO Have to remove -1 to get exact count */
    if(!details)
    {
        printk("VM_Area:[%d]\tMMAP_Page_Faults[%d]\n", (stats->num_vm_area - 1), stats->mmap_page_faults);
        return 0;
    }
    struct vm_area *temp = vm->vm_next; // Ommiting the dummy head nodes
    printk("\n\n\t########### \tVM Area Details \t################\n");
    printk("\tVM_Area:[%d]\t\tMMAP_Page_Faults[%d]\n\n", (stats->num_vm_area - 1), stats->mmap_page_faults);
    while(temp)
    {
        printk("\t[%x\t%x] #PAGES[%d]\t", temp->vm_start, temp->vm_end, (( temp->vm_end - temp->vm_start)/ PAGE_SIZE));
        if(temp->access_flags & PROT_READ)
            printk("R ");
        else
            printk("_ ");

        if(temp->access_flags & PROT_WRITE)
            printk("W ");
        else
            printk("_ ");

        if(temp->access_flags & PROT_EXEC)
            printk("X\n");
        else
            printk("_\n");
        
        temp = temp->vm_next;
    }
    printk("\n\t###############################################\n\n");
    return 0;
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

// Function to create a new vm_area
struct vm_area* create_vm_area(u64 start_addr, u64 end_addr, u32 flags)
{
    struct vm_area *new_vm_area = alloc_vm_area();
    new_vm_area-> vm_start = start_addr;
    new_vm_area-> vm_end = end_addr;
    new_vm_area-> access_flags = flags;
    return new_vm_area;
}

#if 0
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


/**
 * mprotect System call Implementation.
 */
int vm_area_mprotect(struct exec_context *current, u64 addr, int length, int prot)
{
    u64 end_addr = get_end_addr(addr, length);
    int isValid = 0;
    struct vm_area *vm = current -> vm_area;

    while(!isValid && vm)
    {
        if(vm ->vm_start <= addr && end_addr <= vm->vm_end)
            isValid = 1;
        else
            vm = vm->vm_next;
    }

    if(isValid)
    {
        int flag = -1;
        u64 *pte_entry = get_user_pte(current, addr, 0);
        flag = (!pte_entry) ? 0 : MAP_POPULATE;

        vm_area_unmap(current, addr, length);
        vm_area_map(current, addr, length, prot, flag);
        isValid = 0;
    } else
    {
        isValid = -1;
    }
    
    return isValid;
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

int vm_area_unmap(struct exec_context *current, u64 addr, int length)
{
    int isValid = 0;
    // Address should be page aligned
    if(addr % PAGE_SIZE != 0)
    {
        isValid = -1;
        return isValid;
    }

    int num_pages_deallocate = get_num_pages(length);
    int deallocated = 0;
    struct vm_area *vm = current->vm_area;
    struct vm_area *vm_prev = NULL;

    // Traversing the vm_area linked list
    while(!deallocated && vm)
    {
        if(addr >= vm->vm_start && addr < vm->vm_end)
        {
            u64 end_addr = get_end_addr(addr, length);
            
            // Region to be deallcated is at start
            if(addr == vm->vm_start)
            {
                vm->vm_start = end_addr;
                // Entire Region is deallocated
                if(vm->vm_start == vm->vm_end)
                {
                    if(vm_prev)
                        vm_prev->vm_next = vm->vm_next;
                    else
                         current->vm_area = NULL;
                    dealloc_vm_area(vm);
                }
            } else if(end_addr == vm->vm_end) // Region to be deallocated is toward the end
            {
                vm->vm_end = addr;
            } else
            {
                // If the region is inside the existing region. 
                // Then spiliting the region and creating a new vm_area

                struct vm_area *new_vm_area = create_vm_area(end_addr, vm->vm_end, vm->access_flags);
                new_vm_area->vm_next = vm->vm_next;
                vm->vm_next = new_vm_area;
                vm->vm_end = addr;
            }
            
            deallocated = 1;
             
        } else
        {
            vm_prev = vm;
            vm = vm->vm_next;
        }

    }

    if(deallocated)
    {
        deallocated = 0;
        while(deallocated < num_pages_deallocate)
        {
            unsigned long addr_dealloc = addr + (PAGE_SIZE * deallocated);
            invalidate_pte(current, addr_dealloc);
            deallocated++;
        }
    }
    return isValid;
}
#endif

void do_vma_exit(struct exec_context *ctx)
{
    struct vm_area *vm = ctx->vm_area;
    while(vm){
	   struct vm_area *next = vm;
	   vm = vm->vm_next;
	   dealloc_vm_area(next); 
    }	    
    ctx->vm_area = NULL;
}
long do_expand(struct exec_context *ctx, u64 size, int segment_t)
{
    u64 old_next_free;
    struct mm_segment *segment;

    // Sanity checks
    if(size > MAX_EXPAND_PAGES)
              goto bad_segment;

    if(segment_t == MM_RD)
         segment = &ctx->mms[MM_SEG_RODATA];
    else if(segment_t == MM_WR)
         segment = &ctx->mms[MM_SEG_DATA];
    else
          goto bad_segment;

    if(segment->next_free + (size << PAGE_SHIFT) > segment->end)
          goto bad_segment;
     
     // Good expand call, do it!
     old_next_free = segment->next_free;
     segment->next_free += (size << PAGE_SHIFT);
     return old_next_free;
   
bad_segment:
      return 0;
}

long do_shrink(struct exec_context *ctx, u64 size, int segment_t)
{
    struct mm_segment *segment;
    u64 address;
    // Sanity checks
    if(size > MAX_EXPAND_PAGES)
              goto bad_segment;

    if(segment_t == MM_RD)
         segment = &ctx->mms[MM_SEG_RODATA];
    else if(segment_t == MM_WR)
         segment = &ctx->mms[MM_SEG_DATA];
    else
          goto bad_segment;
    if(segment->next_free - (size << PAGE_SHIFT) < segment->start)
          goto bad_segment;
    
    // Good shrink call, do it!
    segment->next_free -= (size << PAGE_SHIFT);
    for(address = segment->next_free; address < segment->next_free + (size << PAGE_SHIFT); address += PAGE_SIZE) 
          invalidate_pte(ctx, address);
    return segment->next_free;

bad_segment:
      return 0;
   
}
