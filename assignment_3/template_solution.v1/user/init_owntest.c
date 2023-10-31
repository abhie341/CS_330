#include<ulib.h>
#define SZ (1 << 21)
int main(u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5)
{
  char *ptr, *ptr1 = NULL;
  int sum = 0;
  get_stats();

  ptr = mmap(NULL, SZ, PROT_READ|PROT_WRITE, MAP_POPULATE); 	
  for(int ctr=0; ctr<4; ++ctr){ 
     cfork();
  }
  printf("I am process %d going into my first access loop\n", getpid());
  for(int i=0; i< SZ; i += 4096){
	  ptr[i] = 'x';
  }
  printf("I am process %d finshed my first access loop\n", getpid());
  if(getpid() != 1)
	  exit(0);
  get_stats();
  munmap(ptr + 16384, 16384);

  for(int ctr=0; ctr<4; ++ctr){ 
     cfork();
  }
 
  ptr1 = mmap(NULL, 8192, PROT_READ, 0); 	
  printf("I am process %d going into my second access loop\n", getpid());
  for(int i=0; i< 16384; i += 4096){
	  ptr[i] = 'x';
  }
  for(int i=32768; i< SZ; i += 4096){
	  ptr[i] = 'x';
  }
  for(int i=0; i< 8192; i += 4096){
	  sum += ptr1[i];
  }
  mprotect(ptr1, 8192, PROT_READ|PROT_WRITE);
  for(int i=0; i< 8192; i += 4096){
	  ptr1[i] = 'x';
  }
  printf("I am process %d finshed my second access loop sum %d\n", getpid(), sum);
  if(getpid() != 1){
          munmap(ptr, SZ);
          munmap(ptr1, 8192);
	  exit(0);
  }else{
	  sleep(32);
  }
  printf("Init is exiting now\n");
  get_stats();
  munmap(ptr, SZ);
  munmap(ptr1, SZ);
  get_stats();

  return 0;
}
