#include<types.h>
#include<apic.h>
#include<lib.h>
#include<idt.h>
#include<memory.h>
#include<context.h>
#include<entry.h>

#define PGD_MASK 0xff8000000000UL 
#define PUD_MASK 0x7fc0000000UL
#define PMD_MASK 0x3fe00000UL
#define PTE_MASK 0x1ff000UL

#define PGD_SHIFT 39
#define PUD_SHIFT 30
#define PMD_SHIFT 21
#define PTE_SHIFT 12

#define APIC_INTERVAL 256

#define FLAG_MASK 0x3ffffffff000UL 
static unsigned long apic_base;

static void clflush(void *addr)
{
    asm volatile(
                   "clflush %0;"
                    :
                    :"m" (addr)
                    :"memory"
    );
   
}
void ack_irq()
{
    u32 *vector;
#ifndef PERIODIC_TIMER
    //Initial count
     vector = (u32 *)(apic_base + APIC_TIMER_INIT_COUNT_OFFSET); 
     *vector = APIC_INTERVAL; // initial timer value
     clflush(vector);
    //Current count
    vector = (u32 *)(apic_base + APIC_TIMER_CURRENT_COUNT_OFFSET); 
    *vector = APIC_INTERVAL; // current count
    clflush(vector);
#endif
    vector = (u32 *)(apic_base + APIC_EOI_OFFSET);  
    *vector = 0;
    return;
}
int do_irq(struct user_regs *regs)
{
    return(handle_timer_tick(regs)); 
    //regs == stack pointer after pushing the registers  
}

u64 get_apic_base(void)
{
   return apic_base;	
}

void init_apic()
{
    u32 msr = IA32_APIC_BASE_MSR;
    u64 base_low, base_high;
    u32 *vector;
    printk("Initializing APIC\n");
    asm volatile(
                    "movl %2, %%ecx;"
                    "rdmsr;"
                    :"=a" (base_low), "=d" (base_high)
                    :"r" (msr)
                    : "memory"
    );
    base_high = (base_high << 32) | base_low;
    dprintk("APIC MSR = %x\n", base_high);
    dprintk("APIC initial state = %d\n", (base_high & (0x1 << 11)) >> 11);
    base_high = (base_high >> 12) << 12;
    printk("APIC Base address = %x\n", base_high);
    dprintk("APIC ID = %u\n", *((int *)(base_high + APIC_ID_OFFSET)));
    dprintk("APIC VERSION = %x\n", *((int *)(base_high + APIC_VERSION_OFFSET)));
    apic_base = base_high;
    /*Setup LDR and DFR*/
    vector = (u32 *)(base_high + APIC_DFR_OFFSET);  
    dprintk("DFR before = %x\n", *vector);
    *vector = 0xffffffff;
    clflush(vector);
    vector = (u32 *)(base_high + APIC_LDR_OFFSET);  
    dprintk("LDR before = %x\n", *vector);
    *vector = (*vector) & 0xffffff;
    *vector |= 1;
    
    /*Initialize the spurious interrupt LVT*/
    vector = (u32 *)(base_high + APIC_SPURIOUS_VECTOR_OFFSET);  
    *vector = 1 << 8 | IRQ_SPURIOUS;   
    clflush(vector);
    dprintk("SPR = %x\n", *((int *)(base_high + APIC_SPURIOUS_VECTOR_OFFSET)));
    
    /*APIC timer*/
    // Divide configuration register
    vector = (u32 *)(base_high + APIC_TIMER_DIVIDE_CONFIG_OFFSET); 
    *vector = 10; // 1010 --> Divide by 128
    clflush(vector);
    //Initial count
    vector = (u32 *)(base_high + APIC_TIMER_INIT_COUNT_OFFSET); 
    *vector = APIC_INTERVAL; // initial timer value
    clflush(vector);
    //Current count
    vector = (u32 *)(base_high + APIC_TIMER_CURRENT_COUNT_OFFSET); 
    *vector = APIC_INTERVAL; // current count
    clflush(vector);
    //The timer LVT in periodic mode
    vector = (u32 *)(base_high + APIC_LVT_TIMER_OFFSET); 
    #ifdef  PERIODIC_TIMER
    *vector = APIC_TIMER | (1 << 17);
    #else
    *vector = APIC_TIMER;
     
    #endif
}
