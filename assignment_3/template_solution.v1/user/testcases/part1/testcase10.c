#include<ulib.h>

int main(u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5)
{
  int pages = 4096;

  char * mm1 = mmap(NULL, pages, PROT_READ|PROT_WRITE, 0);
  if((long)mm1 < 0)
  {
    printf("Test case failed \n");
    return 1;
  }
  
  int val1 = munmap((void*)mm1, pages);
  if(val1 < 0)
  {
    printf("Test case failed \n");
    return 1;
  }
  //should allocate at mm1 as given hint address is not aligned to 4KB
  char* mm2 = mmap(mm1-1024, pages, PROT_READ|PROT_WRITE, 0);
  if(mm1 != mm2)
  {
    printf("Test case failed \n");
    return 1;
  }
    printf("Test case passed\n");
  
  return 0;

}
  
