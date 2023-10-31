#include<ulib.h>

/*mmap and call cfork testcase.*/

int main(u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5)
{
  long pid;
  int pages = 4096;
  unsigned long  addrs = 0x7FDFFF000;
  char * mm1 = mmap((void*)addrs, pages*1, PROT_READ, 0);

  if(mm1 < 0)
  {
    printf("Testcase failed \n");
    return 1;
  }
  pmap(0);
  char * mm2 = mmap((void*)mm1, pages*1, PROT_READ|PROT_WRITE, 0);
  if(mm2 < 0)
  {
      printf("Testcase failed \n");
      return 1;
  }
  // Vm_area count should be 2
  pmap(0);
  printf("Testcase Passed \n");
  
  return 0;
}
