#include<types.h>
#include<context.h>
#include<page.h>
#include<mmap.h>
#include<memory.h>
#include<lib.h>
#include<apic.h>
#include<fork.h>
#include<v2p.h>

/*PFN management APIs*/

static struct pfn_info_list list_pfn_info;
//Returns the pfn_info corresponding to the PFN passed as index argument.
struct pfn_info * get_pfn_info(u32 index){
    struct pfn_info * p = ((struct pfn_info *)list_pfn_info.start)+index;
    return p;
}

//Sets refcount of pfn_info corresponding to the PFN passed as index argument.
void set_pfn_info(u32 index){
    struct pfn_info * p = ((struct pfn_info *)list_pfn_info.start)+index;
    if(p->refcount != 0){
         printk("%s: Error in PFN refcounting count %d\n", __func__, p->refcount);
	 OS_BUG("PFN refcount");
    }
    p->refcount = 1;
}

void reset_pfn_info(u32 index){
    struct pfn_info * p = ((struct pfn_info *)list_pfn_info.start)+index;
    if(p->refcount != 0){
         printk("%s: Error in PFN refcounting count %d\n", __func__, p->refcount);
	 OS_BUG("PFN refcount");
    }
    p->refcount = 0;
}

#if 0
//Increments refcount of pfn_info object passed as argument.
void increment_pfn_info_refcount(struct pfn_info * p){
    p->refcount+=1;
    return;
}

//Decrements refcount of pfn_info object passed as argument.
void decrement_pfn_info_refcount(struct pfn_info * p){
    p->refcount-=1;
    return;
}

#endif

void init_pfn_info(u64 startpfn)
{
    list_pfn_info.start = (void *)(startpfn << PAGE_SHIFT);
    list_pfn_info.end = list_pfn_info.start + (NUM_PAGES << PAGE_SHIFT);
}

s8 get_pfn_refcount(u32 pfn){
    struct pfn_info *info = get_pfn_info(pfn);
    return info->refcount;
}
s8 get_pfn(u32 pfn)
{
    struct pfn_info *info = get_pfn_info(pfn);
    if(info->refcount < 1){
         printk("%s: Error in PFN refcounting count %d\n", __func__, info->refcount);
	 OS_BUG("PFN refcount");
    }
    info->refcount++;
    return info->refcount;   
}

s8 put_pfn(u32 pfn)
{
    struct pfn_info *info = get_pfn_info(pfn);
    if(info->refcount < 1){
         printk("%s: Error in PFN refcounting count %d\n", __func__, info->refcount);
	 OS_BUG("PFN refcount");
    }
    info->refcount--;  
    return info->refcount; 
}

/*Page table mapping APIs*/
/*Page table handling code for all types of page table manipulation*/
/*Definitions*/

#define PGD_MASK 0xff8000000000UL 
#define PUD_MASK 0x7fc0000000UL
#define PMD_MASK 0x3fe00000UL
#define PTE_MASK 0x1ff000UL

#define PGD_SHIFT 39
#define PUD_SHIFT 30
#define PMD_SHIFT 21
#define PTE_SHIFT 12


//Bit positions in X86 PTE
#define FLAG_MASK 0x3ffffffff000UL 

#if 0
typedef struct{
	     unsigned long p:1,
			   w:1,
			   u:1,
			   pwt:1,
			   pct:1,
			   a:1,
			   d:1,
			   pat:1,
			   g:1,
			   unused1:3,
			   pfn:40,
			   unused2:11,
			   x:1;
}__pte;

typedef struct{
	     unsigned long p:1,
			   w:1,
			   u:1,
			   pwt:1,
			   pct:1,
			   a:1,
			   d:1,
			   ps:1,
			   g:1,
			   unused1:3,
			   unused2:9,
			   pfn:31,
			   unused2:11,
			   x:1;
}__pmd;
#endif

#define PF_ERR_P 0
#define PF_ERR_W 1
#define PF_ERR_USER 2
#define PF_ERR_RSVD 3
#define PF_ERR_FETCH 4

#ifdef X86_MANUAL
   #define B_PTE_P 0
   #define B_PTE_W 1
   #define B_PTE_U 2
   #define B_PTE_A 5
   #define B_PTE_D 6
   #define B_PMD_PS 7
   #define B_PTE_G 8
   #define B_PTE_XD 63
#else
   #define B_PTE_P 0
   #define B_PTE_W 3
   #define B_PTE_U 4
   #define B_PTE_A 1
   #define B_PTE_D 2
   #define B_PMD_PS 7
   #define B_PTE_G 8
   #define B_PTE_XD 63

#endif

#define OS_RW_FLAGS ((1 << B_PTE_P) | (1 << B_PTE_W))
#define OS_RWG_FLAGS ((1 << B_PTE_P) | (1 << B_PTE_W) | (1 << B_PTE_G))
#define OS_RW_HP_FLAGS ((1 << B_PTE_P) | (1 << B_PTE_W) | (1 << B_PMD_PS))
#define OS_RWG_HP_FLAGS ((1 << B_PTE_P) | (1 << B_PTE_W) | (1 << B_PMD_PS) | (1 << B_PTE_G))

#define USER_RW_FLAGS ((1 << B_PTE_P) | (1 << B_PTE_W) | (1 << B_PTE_U))
#define USER_RO_FLAGS ((1 << B_PTE_P) | (1 << B_PTE_U))

#define IS_PTE_HUGE(p) (((p) >> B_PMD_PS) & 0x1)

static inline void invlpg(unsigned long addr) {
    asm volatile("invlpg (%0)" ::"r" (addr) : "memory");
}

static int purge_pte(u64 pte, u64 start, u64 end)
{
  int retval = 1, i;
  unsigned long *ptep = (unsigned long *)(pte << PAGE_SHIFT);
  unsigned start_entry = ((start & PTE_MASK) >> PTE_SHIFT);
  for(i=0; i < start_entry; ++i){
      if(*ptep)
           retval = 0;
      ptep++;
  }
  do{
      u64 pfn = (*ptep) >> PTE_SHIFT;
      if(pfn){
	       if(!put_pfn(pfn))
                    os_pfn_free(USER_REG, pfn);     //TODO: handle which region is freed. At this point, it is always the user region
               *ptep = 0;
               invlpg(start);
      }
      start += (0x1UL << PTE_SHIFT);
      start = (start >> PTE_SHIFT) << PTE_SHIFT;
      ptep++;
      ++i;
  }while(start <= end && i != 512);

  for(; i < 512; ++i){
      if(*ptep)
           retval = 0;
      ptep++;
  }
  return retval;
}

static int purge_pmd(u64 pmd, u64 start, u64 end)
{
  int retval = 1, i;
  unsigned long *ptep = (unsigned long *)(pmd << PAGE_SHIFT);
  unsigned start_entry = ((start & PMD_MASK) >> PMD_SHIFT);
  for(i=0; i < start_entry; ++i){
      if(*ptep)
           retval = 0;
      ptep++;
  }
  do{
      u64 pfn = (*ptep) >> PTE_SHIFT;
      if(IS_PTE_HUGE(*ptep))
          *ptep = 0;
      else if(pfn){
           int purge = purge_pte(pfn, start, end);
           retval &= purge;
           if(purge){
               *ptep = 0;
	       if(!put_pfn(pfn))
                    os_pfn_free(OS_PT_REG, pfn);
           }
      }
      start += (0x1UL << PMD_SHIFT);
      start = (start >> PMD_SHIFT) << PMD_SHIFT;
      ptep++;
      ++i;
  }while(start <= end && i != 512);

  for(; i < 512; ++i){
      if(*ptep)
           retval = 0;
      ptep++;
  }
  return retval;
}

static int purge_pud(u64 pud, u64 start, u64 end)
{
  int retval = 1, i;
  unsigned long *ptep = (unsigned long *)(pud << PAGE_SHIFT);
  unsigned start_entry = ((start & PUD_MASK) >> PUD_SHIFT);
  for(i=0; i < start_entry; ++i){
      if(*ptep)
           retval = 0;
      ptep++;
  }
  do{
      u64 pfn = (*ptep) >> PTE_SHIFT;
      if(pfn){
           int purge = purge_pmd(pfn, start, end);
           retval &= purge;
           if(purge){
               *ptep = 0;
	       if(!put_pfn(pfn))
                  os_pfn_free(OS_PT_REG, pfn);
           }
      }
      start += (0x1UL << PUD_SHIFT);
      start = (start >> PUD_SHIFT) << PUD_SHIFT;
      ptep++;
      ++i;
  }while(start <= end && i != 512);

  for(; i < 512; ++i){
      if(*ptep)
           retval = 0;
      ptep++;
  }
  return retval;
}

int purge_mapping_range(struct exec_context *ctx, u64 start, u64 end)
{
  unsigned long *pgd = (unsigned long *)(((u64)ctx->pgd) << PAGE_SHIFT);
  unsigned long *ptep;
  if(end <= start){
      printk("Invalid range\n");
      return -1;
  }

  ptep = (unsigned long *)pgd + ((start & PGD_MASK) >> PGD_SHIFT);
  do{
      u64 pfn = (*ptep) >> PTE_SHIFT;
      if(pfn && purge_pud(pfn, start, end)){
           *ptep = 0;
	   if(!put_pfn(pfn))
              os_pfn_free(OS_PT_REG, pfn);
      }
      start += (0x1UL << PGD_SHIFT);
      start = (start >> PGD_SHIFT) << PGD_SHIFT;
      ptep++;
  }while(start <= end);

  if(start == 0 && end >= STACK_START)
     return 1;   // Purge the pgd if you want
  return 0;
}

int destroy_user_mappings(struct exec_context *ctx)
{
   return purge_mapping_range(ctx, CODE_START, STACK_START);
}


static void install_apic_mapping(u64 pl4)
{
   u64 address = (get_apic_base() >> 21)<<21, pfn;
   u64 *pgd, *pmd, *pud;
   
   pl4 = pl4 << PAGE_SHIFT; 

   pgd = (u64 *)pl4 + ((address & PGD_MASK) >> PGD_SHIFT);
   if(((*pgd) & 0x1) == 0){
           pfn = os_pfn_alloc(OS_PT_REG);
           pfn = pfn << PAGE_SHIFT;
           *pgd = pfn  | 0x3;
   }else{
          pfn = ((*pgd) >> PAGE_SHIFT) << PAGE_SHIFT;  
   }
   pud = (u64 *)pfn + ((address & PUD_MASK) >> PUD_SHIFT);
   if(((*pud) & 0x1) == 0){
           pfn = os_pfn_alloc(OS_PT_REG);
           pfn = pfn << PAGE_SHIFT;
           *pud = pfn | 0x3;
   }else{
          pfn = ((*pud) >> PAGE_SHIFT) << PAGE_SHIFT;  
   }
   pmd = (u64 *)pfn + ((address & PMD_MASK) >> PMD_SHIFT);
   if((*pmd) & 0x1)
        printk("BUG!\n");
   if(config->global_mapping && config->adv_global)
       *pmd = address | 0x183UL | (1UL << 52);
   else if(config->global_mapping)
       *pmd = address | 0x183;
   else
       *pmd = address | 0x83;
   //printk("%s pmd=%x *pmd=%x\n", __func__, pmd, *pmd);
   return;
}
int install_os_pts(u32 pl4)
{
    int num_2MB = OS_PT_MAPS, count=0, pmdpos=0;
    u64 pfn, pud_base, seq_pfn = 0;
    u64 pt_base = (u64)pl4 << PAGE_SHIFT;
    unsigned long *entry = (unsigned long *)(pt_base);
    if(!*entry){
          /*Create PUD entry in PGD*/
          pfn = os_pfn_alloc(OS_PT_REG);
          *entry = (pfn << PAGE_SHIFT) | OS_RW_FLAGS;
          pt_base = pfn << PAGE_SHIFT; 
    }else{
          pt_base = *entry & FLAG_MASK;  
    }
    pud_base = pt_base;
again:
    entry = (unsigned long *)(pud_base) + pmdpos;
    if(*entry){
         pt_base = *entry & FLAG_MASK;  
    }else{
        /*Create PMD entry in PUD*/
        pfn = os_pfn_alloc(OS_PT_REG);
        *entry = (pfn << PAGE_SHIFT) | OS_RW_FLAGS;
        pt_base = pfn << PAGE_SHIFT; 
       
    }
    entry = (unsigned long *)(pt_base);
    count = num_2MB > 512 ? 512 : num_2MB;
    while(count){
           if(config->global_mapping)
                *entry = (seq_pfn << 21) | OS_RWG_HP_FLAGS;  //Global
           else
                 *entry = (seq_pfn << 21) | OS_RW_HP_FLAGS;  
          entry++;
          seq_pfn++;
          count--;
    }
    num_2MB -= 512;

    if(num_2MB > 0){
        ++pmdpos;
        goto again;
    }

/*APIC mapping needed*/
    install_apic_mapping((u64) pl4); 
    return 0;
}

void copy_os_pts(u64 src, u64 dst)
{
   int i;
   unsigned long *src_entry, *dst_entry;
   src = src << PAGE_SHIFT;
   dst = dst << PAGE_SHIFT;

   src_entry = (unsigned long *)(src);
   dst_entry = (unsigned long *)(dst);

   gassert(*src_entry, "First entry in parent page table is NULL");
   src = *src_entry & FLAG_MASK;
   dst = *dst_entry & FLAG_MASK;
   
   src_entry = (unsigned long *)(src);
   dst_entry = (unsigned long *)(dst);
   
   for(i=0; i<4; ++i)
   {
       dprintk("i=%d source entry = %x dst entry = %x\n", i, *src_entry, *dst_entry);
       *dst_entry = *src_entry;
       dst_entry++;
       src_entry++;
   }
   
   return;
     
}

static u32 pt_walk(struct exec_context *ctx, u32 segment, u32 stack)
{ 
   unsigned long pt_base = (u64) ctx->pgd << PAGE_SHIFT;
   unsigned long entry;
   struct mm_segment *mm = &ctx->mms[segment];
   unsigned long start = mm->start;
   if(stack)
       start = mm->end - PAGE_SIZE;

   
   entry = *((unsigned long *)(pt_base + ((start & PGD_MASK) >> PGD_SHIFT)));
   if((entry & 0x1) == 0)
        return -1;
 
   pt_base = entry & FLAG_MASK;
   
   entry = *((unsigned long *)pt_base + ((start & PUD_MASK) >> PUD_SHIFT));
   if((entry & 0x1) == 0)
        return -1;
   
   pt_base = entry & FLAG_MASK;
   
   entry = *((unsigned long *)pt_base + ((start & PMD_MASK) >> PMD_SHIFT));
   if((entry & 0x1) == 0)
        return -1;
   
   pt_base = entry & FLAG_MASK;
   
   entry = *((unsigned long *)pt_base + ((start & PTE_MASK) >> PTE_SHIFT));
   if((entry & 0x1) == 0)
        return -1;
    
   return (entry >> PAGE_SHIFT);
        
}

/* Returns the pte coresponding to a user address. 
Return NULL if mapping is not present or mapped
in ring-0 */


u64* get_user_pte(struct exec_context *ctx, u64 addr, int dump) 
{
    u64 *vaddr_base = (u64 *)osmap(ctx->pgd);
    u64 *entry;
    u32 phy_addr;
    
    entry = vaddr_base + ((addr & PGD_MASK) >> PGD_SHIFT);
    phy_addr = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
    vaddr_base = (u64 *)osmap(phy_addr);
  
    /* Address should be mapped as un-priviledged in PGD*/
    if( (*entry & (1 << B_PTE_P)) == 0 || (*entry & (1 << B_PTE_U)) == 0)
        goto out;
    if(dump)
            printk("L4: Entry = %x NextLevel = %x FLAGS = %x\n", (*entry), phy_addr, (*entry) & (~FLAG_MASK)); 

     entry = vaddr_base + ((addr & PUD_MASK) >> PUD_SHIFT);
     phy_addr = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
     vaddr_base = (u64 *)osmap(phy_addr);

     
         
     /* Address should be mapped as un-priviledged in PUD*/
     if( (*entry & (1 << B_PTE_P)) == 0 || (*entry & (1 << B_PTE_U)) == 0)
          goto out;

     if(dump)
            printk("L3: Entry = %x NextLevel = %x FLAGS = %x\n", (*entry), phy_addr, (*entry) & (~FLAG_MASK)); 

      entry = vaddr_base + ((addr & PMD_MASK) >> PMD_SHIFT);
      phy_addr = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
      vaddr_base = (u64 *)osmap(phy_addr);
      
      /* 
        Address should be mapped as un-priviledged in PMD 
         Huge page mapping not allowed
      */
      if( (*entry & (1 << B_PTE_P)) == 0 || (*entry & (1 << B_PTE_U)) == 0 || (*entry & (1 << B_PMD_PS)) != 0)
          goto out;

      if(dump)
            printk("L2: Entry = %x NextLevel = %x FLAGS = %x\n", (*entry), phy_addr, (*entry) & (~FLAG_MASK)); 
     
      entry = vaddr_base + ((addr & PTE_MASK) >> PTE_SHIFT);
      
      /* Address should be mapped as un-priviledged in PTE*/
      if( (*entry & (1 << B_PTE_P)) == 0 || (*entry & (1 << B_PTE_U)) == 0)
          goto out;
      
      if(dump){
            phy_addr = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
            printk("L1: Entry = %x PFN = %x FLAGS = %x\n", (*entry), phy_addr, (*entry) & (~FLAG_MASK)); 
      }
     return entry;

out:
      return NULL;
}

u32 invalidate_pte(struct exec_context *ctx, unsigned long addr)
{
   u32 pfn;	
   u64 *pte_entry = get_user_pte(ctx, addr, 0);
   if(!pte_entry)
            return -1;
   pfn = (*pte_entry >> PTE_SHIFT) & 0xFFFFFFFF;
   if(!put_pfn(pfn))
        os_pfn_free(USER_REG, pfn);
   *pte_entry = 0; 
   invlpg(addr);

   return 0;
}

u32 map_physical_page(unsigned long base, u64 address, u32 access_flags, u32 upfn)
{
   void *os_addr;
   u64 pfn;
   unsigned long *ptep  = (unsigned long *)base + ((address & PGD_MASK) >> PGD_SHIFT);    
   if(!*ptep)
   {
      pfn = os_pfn_alloc(OS_PT_REG);
      *ptep = (pfn << PAGE_SHIFT) | USER_RW_FLAGS; 
      os_addr = osmap(pfn);
      bzero((char *)os_addr, PAGE_SIZE);
   }else 
   {
      os_addr = (void *) ((*ptep) & FLAG_MASK);
   }
   ptep = (unsigned long *)os_addr + ((address & PUD_MASK) >> PUD_SHIFT); 
   if(!*ptep)
   {
      pfn = os_pfn_alloc(OS_PT_REG);
      *ptep = (pfn << PAGE_SHIFT) | USER_RW_FLAGS; 
      os_addr = osmap(pfn);
      bzero((char *)os_addr, PAGE_SIZE);
   } else
   {
      os_addr = (void *) ((*ptep) & FLAG_MASK);
   }
   ptep = (unsigned long *)os_addr + ((address & PMD_MASK) >> PMD_SHIFT); 
   if(!*ptep){
      pfn = os_pfn_alloc(OS_PT_REG);
      *ptep = (pfn << PAGE_SHIFT) | USER_RW_FLAGS; 
      os_addr = osmap(pfn);
      bzero((char *)os_addr, PAGE_SIZE);
   } else
   {
      os_addr = (void *) ((*ptep) & FLAG_MASK);
   }
   ptep = (unsigned long *)os_addr + ((address & PTE_MASK) >> PTE_SHIFT); 
   if(!upfn)
      upfn = os_pfn_alloc(USER_REG);
   *ptep = ((u64)upfn << PAGE_SHIFT) | USER_RO_FLAGS;
   if(access_flags & MM_WR)
      *ptep |= USER_RW_FLAGS;
   return upfn;    
}

u32 install_ptable(unsigned long base, struct  mm_segment *mm, u64 address, u32 upfn)
{
   if(!address)
      address = mm->start;
   upfn = map_physical_page(base, address, mm->access_flags, upfn);
   return upfn;    
}

int validate_page_table(struct exec_context *ctx, u64 addr, int dump) {
    u64 *vaddr_base = (u64 *)osmap(ctx->pgd);
    u64 *entry;
    u64 pfn;
    
    entry = vaddr_base + ((addr & PGD_MASK) >> PGD_SHIFT);
    if(*entry & 0x1) {
      // PGD->PUD Present, access it
       pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
       vaddr_base = (u64 *)osmap(pfn);
       if(dump)
           printk("PGD-entry:%x, *(PGD-entry):%x, PUD-VA-Base:%x\n",entry,*entry,vaddr_base);
    }else{
      return 0; //PGD->PUD not present 
    }
  
    entry = vaddr_base + ((addr & PUD_MASK) >> PUD_SHIFT);
    if(*entry & 0x1) {
       // PUD->PMD Present, access it
       pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
       vaddr_base = (u64 *)osmap(pfn);
       if(dump)
           printk("PUD-entry:%x, *(PUD-entry):%x, PMD-VA-Base:%x\n",entry,*entry,vaddr_base);
    }else{
       return 0;
    }
  
   entry = vaddr_base + ((addr & PMD_MASK) >> PMD_SHIFT);
    if(*entry & 0x1) {
       // PMD->PTE Present, access it
       pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
       vaddr_base = (u64 *)osmap(pfn);
        if(dump)
           printk("PMD-entry:%x, *(PMD-entry):%x, PTE-VA-Base:%x\n",entry,*entry,vaddr_base);
    }else{
       return 0;
    }
   
   entry = vaddr_base + ((addr & PTE_MASK) >> PTE_SHIFT);
   if(*entry & 0x1) {
       // PTE->PFN Present,
       pfn = (*entry >> PTE_SHIFT) & 0xFFFFFFFF;
       vaddr_base = (u64 *)osmap(pfn);
        if(dump)
           printk("PTE-entry:%x, *(PTE-entry):%x, PFN:%x\n",entry,*entry,pfn);
       return 1;
    }else{
       return 0;
    }
}

static void replicate_page_table_cow(struct exec_context *child, struct exec_context *parent, struct vm_area *vma)
{
   u64 cr3 = child->pgd << PAGE_SHIFT;	
   u64 addr = vma->vm_start;
   int access_flags = vma->access_flags & (~(PROT_WRITE));
   while(addr < vma->vm_end){
      u64 *p_pte = get_user_pte(parent, addr, 0);
      if(*p_pte){
	   u32 pfn = (*p_pte & FLAG_MASK) >> PAGE_SHIFT;
	   get_pfn(pfn);  //Increment the reference count
	   map_physical_page(cr3, addr, access_flags, pfn); //Map it to the child process
           *p_pte = *p_pte & ~(1UL << B_PTE_W);  //Remove write from parent
	   invlpg(addr);
      }
      addr += PAGE_SIZE;	   
   }	   
    
}
void replicate_page_table(struct exec_context *child, struct exec_context *parent, struct vm_area *vma, u8 cow)
{
   u64 cr3 = child->pgd << PAGE_SHIFT;	
   u64 addr = vma->vm_start;
   
   if(cow)
	   return replicate_page_table_cow(child, parent, vma);

   while(addr < vma->vm_end){
      u64 *p_pte = get_user_pte(parent, addr, 0);
      if(*p_pte){
	   char *src = (char *)(*p_pte & FLAG_MASK);   
	   u64 retval = map_physical_page(cr3, addr, vma->access_flags, 0);
           retval  = (u64)osmap(retval);
           memcpy((char *)retval, src, PAGE_SIZE);
      }
      addr += PAGE_SIZE;	   
   }	   
}

void remap_cow_page(struct exec_context *child, u64* p_pte, u64 addr, int access_flags)
{
    u64 cr3 = child->pgd << PAGE_SHIFT;	
    u32 pfn = (*p_pte & FLAG_MASK) >> PAGE_SHIFT;
    access_flags = access_flags & (~(PROT_WRITE));
    get_pfn(pfn);  //Increment the reference count
    //printk("pid %d addr %x pfn %x refc %d\n", child->pid, addr, pfn, (int)get_pfn(pfn));
    map_physical_page(cr3, addr, access_flags, pfn); //Map it to the child process
    *p_pte = *p_pte & ~(1UL << B_PTE_W);  //Remove write from parent
    invlpg(addr);
}

#if 0
//Pagefault handling logic

/* Cow fault handling, 
 * No need to check anything, just perform CoW
 * */

static long handle_cow_fault(struct exec_context *current, u64 vaddr, int access_flags){
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
#endif
/*
 * Page fault for dafault segments
 */
static long handle_pagefault_memseg(struct exec_context *ctx, struct mm_segment *mm, u64 vaddr, u64 error_code)
{
    long retval = -1;	
    u64 cr3 = ctx->pgd << PAGE_SHIFT; 
    //We do not check for execute permissions
    if((error_code & (1 << PF_ERR_P))){  //Page is present
	 if((error_code & (1 << PF_ERR_W)) && (mm->access_flags & MM_WR)){    
             retval = handle_cow_fault(ctx, vaddr, mm->access_flags); 
             stats->cow_page_faults++;
	 }
    }else{
	 retval = map_physical_page(cr3, vaddr, mm->access_flags, 0);
    }
    return retval;
       	    
}	

#if 0
/**
 * Function will invoked whenever there is page fault. (Lazy allocation)
 * 
 * For valid acess. Map the physical page 
 * Return 0
 * 
 * For invalid access, i.e Access which is not matching the vm_area access rights (Writing on ReadOnly pages)
 * Return -1. 
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
#if 0 
   if(fault_fixed < 0){
	  printk("%s: %d fault at addr %x error code %d\n", __func__, current->pid, addr, error_code); 
	  OS_BUG("Fault handling failed");
   }
#endif
    return fault_fixed;
}
#endif

extern int do_page_fault(struct user_regs *regs, u64 error_code)
{
     u64 rip, cr2;
     struct exec_context *current = get_current_ctx();
     rip = regs->entry_rip;
     stats->page_faults++;
     /*Get the Faulting VA from cr2 register*/
     asm volatile ("mov %%cr2, %0;"
                  :"=r"(cr2)
                  ::"memory");

     dprintk("PageFault:@ pid %d [RIP: %x] [accessed VA: %x] [error code: %x]\n", current->pid, rip, cr2, error_code);
     
     /*Check error code. We only handle user pages that are not present*/
     if(!(error_code & (1 << PF_ERR_USER)))
         OS_BUG("page fault in kernel mode");
    
     for(int ctr=0; ctr<MAX_MM_SEGS; ++ctr){
	    long retval; 
	    struct mm_segment *mm = &current->mms[ctr];
            if((cr2 >= mm->start && cr2 < mm->next_free) ||
	        (ctr == MM_SEG_STACK && cr2 >= mm->start && cr2 < mm->end)){		    
		    retval = handle_pagefault_memseg(current, mm, cr2, error_code);
	            if(retval < 0)
		        goto sig_exit;
	            goto done;
	    }
     } 
    
    if(cr2 >= MMAP_AREA_START && cr2 < MMAP_AREA_END)
    {
        stats->mmap_page_faults++;
        long result = vm_area_pagefault(current, cr2, error_code);
        if(result < 0) 
          goto sig_exit;
	if((error_code & (1 << PF_ERR_P)) && (error_code & (1 << PF_ERR_W)))
                stats->cow_page_faults++;
        goto done;
    }

sig_exit:
  printk("%s: (Sig_Exit) PF Error @ [RIP: %x] [accessed VA: %x] [error code: %x]\n", __func__, rip, cr2, error_code);
  invoke_sync_signal(SIGSEGV, &regs->entry_rsp, &regs->entry_rip);
  return 0;

done:
  return 0;
}

