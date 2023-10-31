#include <lib.h>
#include <idt.h>
#include <memory.h>
#include <apic.h>
#include <entry.h>
#include <file.h>
#include<fs.h>

struct os_stats *stats = NULL;
struct os_configs *config = NULL;

int kmain(unsigned long magic)
{
           struct IDTR gdt;
           unsigned mb_info;
           u32 cs,ds,ss,esp,ebp;
     
           unsigned long cr0, cr3, cr4;
  
        /*Specialized handling when booted by Gem5. We are in long mode (64 bit) 
          cmdline is @ 0x90000   realmode data is @ 0x90200. Refer to the following
          files: arch/x86/linux/system.cc, arch/x86/system.cc. Assume our tty to be
          a serial ttyS0*/ 
          console_init();
          printk("            Starting IITK gemOS ......\n");
          //cs = 64 * mem_calculate();

         __asm__("movl %%cs, %0;"
             "movl %%ss, %1;"
             "movl %%ds, %2;"
             : "=a"(cs), "=b"(ss), "=c"(ds)
             );
         printk("Initial Segment Map: cs = %x ds = %x ss = %x\n",cs,ds,ss);
         __asm__("movl %%esp, %0; movl %%ebp, %1":"=a"(esp),"=b"(ebp));
         printk("ESP=%x EBP=%x\n",esp,ebp);
         __asm__("mov %%cr0, %0; mov %%cr3, %1;": "=r" (cr0), "=r" (cr3)
                 ::"memory");
         __asm__("mov %%cr4, %0;": "=r" (cr4)
                 ::"memory");

         printk("cr0=%x cr3=%x cr4=%x\n",cr0, cr3, cr4);
         printk("Initializing memory\n");
         init_mems(); 
         gdt.base = 0x76000;
         gdt.limit = 31;
         setup_gdt_tss(&gdt); 
         setup_idt(&gdt); 
         
         init_apic();
         stats = os_alloc(sizeof(struct os_stats)); 
         bzero((char *) stats, sizeof(struct os_stats));

         config = os_alloc(sizeof(struct os_configs)); 
         bzero((char *) config, sizeof(struct os_configs));
         config->apic_tick_interval = 32;

         init_file_system(); /*Initiating the file system */
         init_swapper();  //XXX This should be the last act of boot. After this swapper takes over!
         return 0;
}
   
