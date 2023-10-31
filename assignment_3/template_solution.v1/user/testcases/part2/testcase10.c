#include<ulib.h>

int main(u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5)
{
  int pages = 4096;
  // vm_area will be created without physical pages.
  char * lazy_alloc = mmap(NULL, pages*50, PROT_READ|PROT_WRITE, 0);
  if((long)lazy_alloc < 0)
  {
    // Testcase failed.
    printf("Test case failed \n");
    return 1;
  }
  dump_page_table(lazy_alloc);
  pmap(0);
  for(int i = 0; i<25; i++)
  {
    lazy_alloc[(pages * i)] = 'X';
  }
  dump_page_table(lazy_alloc);
  pmap(0);
  dump_page_table(lazy_alloc+(25*pages));


  int val1 = munmap((void*)lazy_alloc, pages*50);
  if(val1 < 0)
  {
    printf("Test case failed \n");
    return 1;
  }
  
  dump_page_table(lazy_alloc);

  return 0;  

}
