#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

#define CHUNK_SIZE (4 * 1024 * 1024) // 4 MB
#define SIZE_MASK 0XFFFFFFFFFFFFFFF8

static void *memory_chunk = NULL;
static unsigned long * head =NULL;

void *memalloc(unsigned long size) {
	int need_more_space = 0;
	unsigned long new_size = 0;
	unsigned long * allocated_mem = NULL;
	unsigned long * ptr = NULL;
	unsigned long * prev = NULL;
	unsigned long * next = NULL;
	unsigned long total_size = 0;
	
	if(size == 0){
                return NULL;
        }
	size = (size + 7) & SIZE_MASK;
	if(size == 8){
                total_size = size + 16;
        }
        else{
        	total_size = size + 8;
        }
	if(head){
		allocated_mem = head;
		while(*allocated_mem < total_size){
			allocated_mem = (unsigned long *)*(allocated_mem+1);
			if(allocated_mem == NULL){
				need_more_space = 1;
				break;
			}	
		}
		if(need_more_space == 0){
			new_size = *allocated_mem - total_size;
			prev = (unsigned long *)*(allocated_mem + 2);
                        next = (unsigned long *)*(allocated_mem + 1);
			if(new_size >= 24){
				//If the space which we looking for is met at the last chunk, 
				//it has to be handled sperately
				if(next == NULL && prev != (unsigned long *)&head){
					ptr = allocated_mem + total_size/8;
					*allocated_mem = total_size;
					*(prev + 1) = (unsigned long)NULL;
					*ptr = new_size;
					*(ptr + 1) = (unsigned long)head;
					*(ptr + 2) = *(head+2);
					return allocated_mem + 1;
				}
				else if(next == NULL && prev == (unsigned long *)&head){
					ptr = allocated_mem + total_size/8;
					head = ptr;
                                        *allocated_mem = total_size;
					*ptr = new_size;
					*(ptr + 1) = (unsigned long)NULL;
                                        *(ptr + 2) = (unsigned long)&head;
					return allocated_mem + 1;
				}
				else if(next !=NULL && prev == (unsigned long *)&head){
					ptr = allocated_mem + total_size/8;
					head = ptr;
					*allocated_mem = total_size;
					*ptr = new_size;
					*(next + 2) = (unsigned long)ptr;
					*(ptr + 2) = (unsigned long)&head;
                                        *(ptr + 1) = *(allocated_mem + 1);
				}

				else{
					ptr = allocated_mem + total_size/8;
					*(prev + 1) = *(allocated_mem + 1);
					*(next + 2) = *(allocated_mem + 2);
					*allocated_mem = total_size;
					*ptr = new_size;
					*(ptr + 2) = *(head + 2);
					*(ptr + 1) = (unsigned long)head;
					return (allocated_mem+1);
				}
			}
			//after getting the memory which is asked, if remaining memory is less than 32,
			//then we cannot make another chunk so add the remaining space and give the total to user
			else{
				if(next == NULL && prev != (unsigned long *)&head){
                                        *(prev + 1) = (unsigned long)NULL;
                                        return (allocated_mem + 1);
                                }
				else if(next == NULL && prev == (unsigned long *)&head){
					head = NULL;
					return (allocated_mem + 1);
				}
				else if(next !=NULL && prev == (unsigned long *)&head){
					head = next;
					*(next + 2) = (unsigned long)allocated_mem;
					return (allocated_mem+1);
				}
				 else{
                                        *(prev + 1) = *(allocated_mem + 1);
                                        *(next + 2) = *(allocated_mem + 2);
                                        return (allocated_mem+1);
                                }


			}	
		}
		//if we have not found the memory asked in the free list then ask OS to give a chunk
		else{
			size_t chunk_size = CHUNK_SIZE;
		        while (chunk_size < total_size) {
                		chunk_size += CHUNK_SIZE;
        		}
			memory_chunk = sbrk(chunk_size);
	                if (memory_chunk == (void *)-1) {
        	                memory_chunk = mmap(NULL, chunk_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                	        if(memory_chunk <= 0)
					return NULL;
                	}
			allocated_mem = (unsigned long *)memory_chunk;
			*allocated_mem = chunk_size;
			new_size = *allocated_mem - total_size;
			if(new_size >= 24){
				ptr = allocated_mem + total_size/8;
				*allocated_mem = total_size;
				*ptr = new_size;
				*(ptr + 2) = *(head + 2);
				*(ptr + 1) = (unsigned long)head;
				return (allocated_mem+1);
			}
			else{
				return (allocated_mem+1);
			}
		}
	}
	else{
		unsigned long chunk_size = CHUNK_SIZE;
		while (chunk_size < total_size) {
			chunk_size += CHUNK_SIZE;
		}
		memory_chunk = sbrk(chunk_size);
		if (memory_chunk == (void *)-1) {
			memory_chunk = mmap(NULL, chunk_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if(memory_chunk <= 0)
				return NULL;
		}
		allocated_mem = (unsigned long *)memory_chunk;
		*allocated_mem = chunk_size;
		new_size = *allocated_mem - total_size;
		if(new_size >= 32){

			ptr = allocated_mem + total_size/8;
			*allocated_mem = total_size;
			*ptr = new_size;
			head = ptr;
			*(ptr + 2) =(unsigned long) &head;
			*(ptr + 1) = (unsigned long)NULL;
			return (allocated_mem+1);
		}
		else{
			//printf("1.(Inside memalloc()) allocated_mem + 1: %p\n", allocated_mem + 1);
			return (allocated_mem+1);
		}
	
	}

}

int memfree(unsigned long *ptr) {
    if (ptr == NULL) {
        return 0;
    }
    ptr = ptr - 1;
    int no_of_parts = *ptr/8;
    unsigned long new_size = 0;
    unsigned long * left = NULL;
    unsigned long * right = NULL;
    unsigned long * temp = NULL;
    int l = 1, r = 1;
// If there is no free list so then head would be NULL and so we have to create one
    
    if(head == NULL){
	head = ptr;
	*(ptr+2) = (unsigned long)&head;
	*(ptr+1) = (unsigned long)NULL;
	return 0;
    }

    else{
//////////////////////////////////////////////////////////
	temp = head;
	while((temp + *temp/8) != ptr){
		temp = (unsigned long *)*(temp+1);
		if(temp == NULL){
			l = 0;
			break;
		}
	}
	if(l == 1){
		left = temp;
	}
	temp = head;
        while(temp != (ptr + *ptr/8)){
                temp = (unsigned long *)*(temp+1);
                if(temp == NULL){
                        r = 0;
                        break;
                }
        }
        if(r == 1){
                right = temp;
        }
///////////////////////////////////////////////////////
	if(left != NULL && right != NULL){
	/*
			new_size = *ptr + *left + *right;
			if(*((unsigned long *)*(left + 2) + 1) == (unsigned long)left){
                                *((unsigned long *)*(left + 2) + 1) = *(left + 1);
                        }
			else if(*((unsigned long *)*(right + 2) + 1) == (unsigned long)right){
                                *((unsigned long *)*(right + 2) + 1) = *(right + 1);
                        }
			*(left) = new_size;
			if(*(head + 1) == (unsigned long) left){
				*(head + 1) = *(left + 1);
			}
			if(*(head + 1) == (unsigned long) right){
                                *(head + 1) = *(right + 1);
                        }
                                
                        *(head + 2) = (unsigned long)left;
			if((head == left && *(head + 1) == (unsigned long)right) || (head == right && *(right + 1) != (unsigned long)left)){
				*(left + 1) = *(right + 1);
			}
			else if(head != left && head != right){
				*(left + 1) = (unsigned long)head;
			}

                        *(left + 2) = (unsigned long)(&head);
                        head = left;
          */
          		if((*(left+2) == &head) && (*(left+1) == NULL))
			{
				*left = *left + *ptr;
				//*(left+1) = NULL;
				//*(left+2) = &head;
			}
			else if(*(left+1) == NULL)
			{
				*left = *left + *ptr;
				*((unsigned long *)*(left + 2) + 1) = NULL;
				*(left + 2) = &head;
				*(left+1) = head;
				*(head+2) = left;
				head = left;
			}
			else if(*(left+2) == &head)
			{
				*left = *left + *ptr;
			}
			else
			{
				*left = *left + *ptr;
				*((unsigned long *)*(left+1)+2) = *(left+2);
				*((unsigned long *)*(left+2)+1) = *(left+1);
				*(left+2) = &head;
				*(left+1) = head;
				*(head+2) = left;
				head = left;
			}
			
			ptr = left;
			
			
			if(*(right+1) == NULL)
			{
				*ptr = *ptr + *right;
				*((unsigned long *)*(right+2)+1) = NULL;
				/*
				*(ptr+2) = &head;
				*(ptr+1) = head;
				*(head+2) = ptr;
				head = ptr;
				*/
			}
			else
			{
				*ptr = *ptr + *right;
				*((unsigned long *)*(right+2)+1) = *(right+1);
				*((unsigned long *)*(right+1)+2) = *(right+2);
				/*
				*(ptr+2) = &(head);
				*(ptr+1) = head;
				*(head+2) = ptr;
				head = ptr;
				*/
				
			}
			
			
        		                
	}
	else if(left != NULL){
			/*
			new_size = *ptr + *left;
			if(*((unsigned long *)*(left + 2) + 1) == (unsigned long)left){
                                *((unsigned long *)*(left + 2) + 1) = *(left + 1);
                        }
			*left = new_size;
			if(*(head + 1) == (unsigned long) left){
				*(head + 1) = *(left + 1);
			}	
			*(head + 2) = (unsigned long)left;
			if(head != left){
				*(left + 1) = (unsigned long)head;
			}
			*(left + 2) = (unsigned long)(&head);	
			head = left;
			*/
			if((*(left+2) == &head) && (*(left+1) == NULL))
			{
				*left = *left + *ptr;
				//*(left+1) = NULL;
				//*(left+2) = &head;
			}
			else if(*(left+1) == NULL)
			{
				*left = *left + *ptr;
				*((unsigned long *)*(left + 2) + 1) = NULL;
				*(left + 2) = &head;
				*(left+1) = head;
				*(head+2) = left;
				head = left;
			}
			else if(*(left+2) == &head)
			{
				*left = *left + *ptr;
			}
			else
			{
				*left = *left + *ptr;
				*((unsigned long *)*(left+1)+2) = *(left+2);
				*((unsigned long *)*(left+2)+1) = *(left+1);
				*(left+2) = &head;
				*(left+1) = head;
				*(head+2) = left;
				head = left;
			}
	}
	else if(right != NULL){	
			/*new_size = *ptr + *right;
			if(*((unsigned long *)*(right + 2) + 1) == (unsigned long)right){
				*((unsigned long *)*(right + 2) + 1) = *(right + 1);
			}
			*(ptr+2) = (unsigned long)(&head);
			if(head != right){
				*(ptr + 1) = (unsigned long)head;
			}
			else{
				*(ptr + 1) = *(right + 1);
			}
			*ptr = new_size;
			*(head + 2) = (unsigned long)ptr;
			if(*(head + 1) == (unsigned long) right){
				*(head + 1) = *(right + 1);
			}
			head = ptr;
			*/
			
			if((*(right+2) == &head) && (*(right+1) == NULL))
			{
				*ptr = *ptr + *right;
				*(ptr+1) = NULL;
				*(ptr+2) = &head;
				head = ptr;
			}
			else if(*(right+1) == NULL)
			{
				*ptr = *ptr + *right;
				*((unsigned long *)*(right+2)+1) = NULL;
				*(ptr+2) = &head;
				*(ptr+1) = head;
				*(head+2) = ptr;
				head = ptr;
			}
			else if(*(right+2) == &head)
			{
				*ptr = *ptr + *right;
				*(ptr+2) = &head;
				*(ptr+1) = *(head+1);
				*((unsigned long *)*(right+1)+2) = ptr;
				head = ptr;
			}
			else
			{
				*ptr = *ptr + *right;
				*((unsigned long *)*(right+2)+1) = *(right+1);
				*((unsigned long *)*(right+1)+2) = *(right+2);
				*(ptr+2) = &(head);
				*(ptr+1) = head;
				*(head+2) = ptr;
				head = ptr;
				
			}
	}
	else{
		*(ptr + 2) = (unsigned long)&head;
		*(ptr + 1) = (unsigned long)head;
		*(head+2) = (unsigned long)ptr;
		head = ptr;
	}	
    	
    }
    return 0;	
}    
