#include<context.h>
#include<memory.h>
#include<lib.h>
#include<entry.h>
#include<file.h>
#include<tracer.h>


///////////////////////////////////////////////////////////////////////////
//// 		Start of Trace buffer functionality 		      /////
///////////////////////////////////////////////////////////////////////////


// Function to find nth bit from LSB.
int find_bit_from_lsb (int number, int pos) {
	return (number & (1 << (pos - 1)));
}

/**
*  First check whether it belongs to mm segment.
*  Return value 0 -> does not belongs.
*  Return value 1 -> Belongs to mm and have proper access.
*  Return value -1 -> Belongs to mm but don't have proper access.
*  bit_to_check is 1 for read and 2 for write.
*/
int check_in_mm_segment (unsigned long start, unsigned long end, int bit_to_check) {

	int i;
	int belongs_to_mm = 0;
	int has_access = 0;
	int ret_value = 0;


	printk("Inside check_in_mm_segment. buffer start: %x, buffer end: %x\n", start, end);
	// Get current context, i.e. PCB of the process.
	struct exec_context *current = get_current_ctx ();
	
	// If range is in Code segment Area of MM segment then only read is allowed (use of next_free).
	printk ("code segment start: %x, code segment end: %x, code segment next free: %x\n", current -> mms[MM_SEG_CODE].start, current -> mms[MM_SEG_CODE].end, current -> mms[MM_SEG_CODE].next_free);
   	if ((current -> mms[MM_SEG_CODE].start <= start) && (current -> mms[MM_SEG_CODE].next_free >= end)) {

		printk ("Inside Code Area of MM segment with access: %d\n", current -> mms[MM_SEG_CODE].access_flags);
        	ret_value = -1;
		if (bit_to_check == 1)								// Only read access is allowed.
			ret_value = 1;
		return ret_value;

	}

	// If range is in RO Data Area of MM segment then only read is allowed (use of next_free).
	printk ("RO data segment start: %x, RO data segment end: %x\n", current -> mms[MM_SEG_RODATA].start, current -> mms[MM_SEG_RODATA].end);
	if ((current -> mms[MM_SEG_RODATA].start <= start) && (current -> mms[MM_SEG_RODATA].next_free >= end)) {
	
		printk ("Inside ROData Area of MM segment with access: %d\n", current -> mms[MM_SEG_RODATA].access_flags);
		ret_value = -1;
		if (bit_to_check == 1)								// Only read access is allowed.
			ret_value = 1;
		return ret_value;

	}

	// If range is in Data Area of MM segment then both read and write can be allowed (use of next_free).
	printk ("data segment start: %x, data segment end: %x\n", current -> mms[MM_SEG_DATA].start, current -> mms[MM_SEG_DATA].end);
	if ((current -> mms[MM_SEG_DATA].start <= start) && (current -> mms[MM_SEG_DATA].next_free >= end)) {
		
		printk ("Inside Data Area of MM segment with access: %d\n", current -> mms[MM_SEG_DATA].access_flags);
		ret_value = -1;
		// Valid range is checked in that seg.
		if (find_bit_from_lsb(current -> mms[MM_SEG_DATA].access_flags, bit_to_check))	// So, we have proper access.
			ret_value = 1;
		return ret_value;

	}
	
	// If range is in Stack Area of MM segment then both read and write can be allowed.
	// And here page faults are handled, so we should not bother about next_fee.
	printk ("stack segment start: %x, stack segment end: %x\n", current -> mms[MM_SEG_STACK].start, current -> mms[MM_SEG_STACK].end);
	if ((current -> mms[MM_SEG_STACK].start <= start) && (current -> mms[MM_SEG_STACK].end >= end)) {
		
		printk ("Inside Stack Area of MM segment with access: %d\n", current -> mms[MM_SEG_STACK].access_flags);
		ret_value = -1;
		if (find_bit_from_lsb(current -> mms[MM_SEG_STACK].access_flags, bit_to_check))		// So, we have proper access.
			ret_value = 1;
		return ret_value;

	}
		
	// If range is not in the MM segment.
    	printk ("start: %x and end: %x, not found in any MM seg\n", start, end);
	return ret_value;
}


/**
*  Check whether it belongs to VMA segment.
*  Return value 0 -> does not belongs.
*  Return value 1 -> Belongs to vma and have proper access.
*  Return value -1 -> Belongs to vma but don't have proper access.
*  bit_to_check is 1 for read and 2 for write.
*/
int check_in_vma_segment (unsigned long start, unsigned long end, int bit_to_check) {
	
    int i;
    int belongs_to_vma = 0;
    int has_access = 0;

	printk("Inside check_in_vma_segment\n");
    // Get current context and search in its vma segments.
    struct exec_context *current = get_current_ctx ();
	struct vm_area *vma = current -> vm_area;
	while (vma) {
		printk("vma->vm_start: %x, start: %x, vma -> vm_end: %x, end: %x\n", vma->vm_start, start, vma->vm_end, end); 
		if ((vma -> vm_start <= start) & (vma -> vm_end >= end)) {
		    	belongs_to_vma = 1;
			printk ("Buffer belongs to this vma\n");
			printk ("VMA Access flag: %d, checking permission for flag: %d\n", vma -> access_flags, bit_to_check);
			if (find_bit_from_lsb(vma -> access_flags, bit_to_check))
			{
				has_access = 1;
				printk ("Access matches\n");
			}
			break;
        	}

		vma = vma -> vm_next;

	}

	// Return finding.
	if (belongs_to_vma) {
		if (has_access)
			return 1;
		else
			return -1;
	} else {
		return 0;
	}
}


// Check whether passed buffer is valid memory location for read.
int is_valid_mem_range (unsigned long buff, u32 count, int access_bit) {

	/**
	*  First check whether it belongs to mm segment.
	*  Return value 0 -> does not belongs.
	*  Return value 1 -> Belongs to mm and have proper access.
	*  Return value -1 -> Belongs to mm but don't have proper access.
	*  bit_to_check is 1 for read and 2 for write.
	*/
	int mm_seg_ret = check_in_mm_segment (buff, (unsigned long)(buff + (unsigned long)count), access_bit);
	if (mm_seg_ret == -1) {
		printk ("Found inside MM seg but access is not allowed\n");
		return -EBADMEM;
	}
	if (mm_seg_ret == 1) {
		printk ("Found inside MM seg with proper access flag\n");
		return 1;
	}
	
	// printk ("Not found inside MM segments.\n");
	/**
	*  Check whether it belongs to VMA segment.
	*  Return value 0 -> does not belongs.
	*  Return value 1 -> Belongs to vma and have proper access.
	*  Return value -1 -> Belongs to vma but don't have proper access.
	*  bit_to_check is 1 for read and 2 for write.
	*/
	int vma_seg_ret = check_in_vma_segment (buff, (unsigned long)(buff + (unsigned long)count), access_bit);
	if (vma_seg_ret == 1) {
		printk ("Found inside VMA seg with proper access flag\n");
		return 1;
	} else {
		printk ("Either access flag or not found inside VMA\n");
		return -EBADMEM;
	}

	printk ("Not found anywhere. NOT POSSIBLE!!!!\n");
	return -1;

}

//Inside pipe_read()
// Check whether supplied buffer has valid range and access in the memory segments.
//if (is_valid_mem_range ((unsigned long) buff, (count - 1), 2) != 1)
//	return -EBADMEM;


//Inside pipe_write()
// Check whether supplied buffer has valid range and access in the memory segments.
//if (is_valid_mem_range ((unsigned long) buff, (count - 1), 1) != 1)
//	return -EBADMEM;





long trace_buffer_close(struct file *filep){
//	if(!filep) return NULL;
	os_page_free(USER_REG, filep -> trace_buffer -> buffer);
	os_free(filep -> trace_buffer, sizeof(struct trace_buffer_info));
	os_free(filep -> fops, sizeof(struct fileops));
	os_free(filep, sizeof(struct file));

	return 0;	
}



int trace_buffer_read(struct file *filep, char *buff, u32 count)
{
//    if(!filep) return NULL;
    char* source_buffer;
    int read_pos = filep -> trace_buffer -> read_pos;
    // remaining data is the data to be read after the write happens
    int remaining_data = filep -> trace_buffer -> remaining_data;
    int i = read_pos;

    //if((filep -> mode != O_READ) && (filep -> mode != O_WRITE)) return -EINVAL;
    if(remaining_data == 0) return 0;
    if(count <= 0) return 0;
    if (is_valid_mem_range ((unsigned long) buff, (count) , 2) != 1){
          return -EBADMEM;
    }

    count = (count >= remaining_data)? remaining_data: count;
    source_buffer = (filep -> trace_buffer -> buffer);
    for(int j = 0; j<count; j++){
	buff[j] = source_buffer[i];
	i = (i+1) % TRACE_BUFFER_MAX_SIZE;
    }
    filep -> trace_buffer -> read_pos = i;
    if(count < remaining_data) 
	filep -> trace_buffer -> remaining_data -= count;
    else
	filep -> trace_buffer -> remaining_data = 0;


    return count;

}


int trace_buffer_write(struct file *filep, char *buff, u32 count)
{
  //  if(!filep) return NULL;
//printk("count(40, 40) : %d\n", count);
    char* destination_buffer;
    int write_pos = filep -> trace_buffer -> write_pos;
    int remaining_data = filep -> trace_buffer -> remaining_data;
    int free_space = TRACE_BUFFER_MAX_SIZE - remaining_data;
    int i = write_pos;
    

    if((filep -> mode != O_RDWR) && (filep -> mode != O_WRITE)) return -EINVAL;
    if(count <= 0) return 0;
    if(free_space <= 0) return 0;
    if (is_valid_mem_range ((unsigned long) buff, (count), 1) != 1){
          return -EBADMEM;
    }
    count = (count >= free_space)? free_space: count;
    destination_buffer = (filep -> trace_buffer -> buffer);
    for(int j = 0; j<count; j++){
	destination_buffer[i] = buff[j];
	i = (i+1) % TRACE_BUFFER_MAX_SIZE;
    }
    filep -> trace_buffer -> write_pos = i;
    filep -> trace_buffer -> remaining_data += count;
    return count;
}

int sys_create_trace_buffer(struct exec_context *current, int mode)
{
        if((mode & ~(O_READ | O_WRITE | O_RDWR | O_EXEC | O_CREAT)) != 0){
	return -EINVAL;
    }
    int ret_fd = 0;
    for(; ret_fd < MAX_OPEN_FILES; ret_fd++){
        if((current -> files[ret_fd] == NULL)){
	    break;
	}    		
    }
    if(ret_fd >= MAX_OPEN_FILES) return -EINVAL;

    struct file *filep1 = os_alloc(sizeof(struct file));
    if (filep1 == NULL)
	return -EINVAL;
    current->files[ret_fd] = filep1;

    struct trace_buffer_info *trace_buffer_struct = os_alloc(sizeof(struct trace_buffer_info));
    if(trace_buffer_struct == NULL) return -EINVAL;

    struct fileops * fops = os_alloc(sizeof(struct fileops));
    if (fops == NULL)
	return -EINVAL;

    filep1 -> type = TRACE_BUFFER;
    filep1 -> mode = mode;
    filep1 -> ref_count = 1;
    filep1 -> inode = NULL;
    filep1 -> trace_buffer = trace_buffer_struct;
    filep1 -> fops  = fops;
    filep1 -> fops -> read = trace_buffer_read;
    filep1 -> fops -> write = trace_buffer_write;
    filep1 -> fops -> close = trace_buffer_close;
    filep1 -> ref_count = 1; 
    trace_buffer_struct -> buffer = os_page_alloc(USER_REG); 
    trace_buffer_struct -> read_pos = 0;
    trace_buffer_struct -> write_pos = 0;
    trace_buffer_struct -> remaining_data = 0;
    return ret_fd;
}

///////////////////////////////////////////////////////////////////////////
//// 		Start of strace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////


int strace_file_write(struct exec_context *ctx,u64 fd,u64 buff,u64 count){
        int write_size;
        struct file *filep = ctx->files[fd];
        if(!filep){
                return -EINVAL; //file is not opened
        }
        if(!(filep->mode & O_WRITE)){
                return -EACCES; // file is not opened in write mode
        }
        if(filep->fops->write){
                write_size = filep->fops->write(filep, (char*)buff, count);
                dprintk("write size:%d\n",write_size);
                return write_size;
        }
        return -EINVAL;
}



int perform_tracing(u64 syscall, u64 param1, u64 param2, u64 param3, u64 param4){

	struct exec_context *current = get_current_ctx();

	if(current->st_md_base && current->st_md_base->is_traced){
        int param_size = 0, total_size = 2;
        if(syscall == 1) param_size = 1;
        else if(syscall == 2) param_size = 0;
        else if(syscall == 4) param_size = 2;
        else if(syscall == 5) param_size = 2;
        else if(syscall == 6) param_size = 1;
        else if(syscall == 7) param_size = 1;
        else if(syscall == 8) param_size = 2;
        else if(syscall == 9) param_size = 2;
        else if(syscall == 10) param_size = 0;
        else if(syscall == 11) param_size = 0;
        else if(syscall == 12) param_size = 3;
        else if(syscall == 13) param_size = 0;
        else if(syscall == 14) param_size = 1;
        else if(syscall == 15) param_size = 0;
        else if(syscall == 16) param_size = 4;
        else if(syscall == 17) param_size = 2;
        else if(syscall == 18) param_size = 3;
        else if(syscall == 19) param_size = 1;
        else if(syscall == 20) param_size = 0;
        else if(syscall == 21) param_size = 0;
        else if(syscall == 22) param_size = 0;
        else if(syscall == 23) param_size = 3;
        else if(syscall == 24) param_size = 3;
        else if(syscall == 25) param_size = 3;
        else if(syscall == 27) param_size = 1;
        else if(syscall == 28) param_size = 2;
        else if(syscall == 29) param_size = 1;
        else if(syscall == 30) param_size = 3;
        else if(syscall == 35) param_size = 3;
        else if(syscall == 36) param_size = 1;
        else if(syscall == 37) param_size = 2;
        else if(syscall == 38) param_size = 1;
        else if(syscall == 39) param_size = 3;
        else if(syscall == 40) param_size = 2;
        else if(syscall == 61) param_size = 0;


	total_size += param_size;
        u64 strace_buff[total_size];
        if(current->st_md_base->tracing_mode == FILTERED_TRACING){
                printk("Filtered tracing mode\n");
                struct strace_info *info = current -> st_md_base -> next;
                while(info && info->syscall_num != syscall){
                        printk("info-> syscall_num  : %d\n", info -> syscall_num);
                        info = info->next;
                }
                if(info){
                        strace_buff[0] = (total_size-1)*8;
                        strace_buff[1] = syscall;

                        if(param_size == 1){
                                strace_buff[2] = param1;
                        }
                        else if(param_size == 2){
                                strace_buff[2] = param1;
                                strace_buff[3] = param2;
                        }
                        else if(param_size == 3){
                                strace_buff[2] = param1;
                                strace_buff[3] = param2;
                                strace_buff[4] = param3;
                        }
                        else if(param_size == 4){
                                strace_buff[2] = param1;
                                strace_buff[3] = param2;
                                strace_buff[4] = param3;
                                strace_buff[5] = param4;
                        }
                        int ret = strace_file_write(current, current -> st_md_base->strace_fd, (u64)strace_buff, total_size*8);
                        if(ret != (total_size*8)) return -EINVAL;
                }
                else{
                        printk("info not found in partial list\n");
                }

        }

	else if(syscall != 38){
                printk("Full tracing mode\n");
                strace_buff[0] = (total_size-1)*8;
                strace_buff[1] = syscall;

                if(param_size == 1){
                        strace_buff[2] = param1;
                }
                else if(param_size == 2){
                        strace_buff[2] = param1;
                        strace_buff[3] = param2;
                }
                else if(param_size == 3){
                        strace_buff[2] = param1;
                        strace_buff[3] = param2;
                        strace_buff[4] = param3;
                }
                else if(param_size == 4){
                        strace_buff[2] = param1;
                        strace_buff[3] = param2;
                        strace_buff[4] = param3;
                        strace_buff[5] = param4;
                }
		printk("strace_buff[0] : %d\n", strace_buff[0]);
		printk("strace_buff[1] : %d\n", strace_buff[1]);
                int ret = strace_file_write(current, current->st_md_base->strace_fd, (u64)strace_buff, total_size*8);
                if(ret != (total_size*8)) return -EINVAL;
        }
    }
    return 0;
}


static int add_strace(struct exec_context *current, int syscall_num)
{
	printk("entered add_strace\n");
    struct strace_info *info;
    struct strace_head *head;
    //current -> tracing_mode = FILTERED_TRACING;
    if(!current->st_md_base){
	printk("entered it is empty list\n");
        current->st_md_base = os_alloc(sizeof(struct strace_head));
        bzero((char *)current->st_md_base, sizeof(struct strace_head));
    }
    head = current->st_md_base;
    info = current -> st_md_base -> next;
	while(info){
		printk("@@@@@@@@@@@@@@@ info-> syscall_num  : %d\n", info -> syscall_num);
		info = info->next;
	}
    info = os_alloc(sizeof(struct strace_info));
    if(!info){
            return -1;
    }
	printk("info allocated\n");
    bzero((char *)info, sizeof(struct strace_info));
	printk("head-> count : %d \n", head-> count);
    if(head->count >= STRACE_MAX){
	printk("head-> count more than STRACE_MAX\n");
           return -EINVAL;    
    }
    if(!head->count){
	printk("head-> count is zero before insertion\n");
        head->next = info;
    }
    else{
	printk("head-> count is non-zero. Performing new insertion.\n");
        head->last->next = info;
    }
    head->last = info;
    head->count++;
    info->syscall_num = syscall_num;
printk("syscall_num : %d\n", info->syscall_num);


    info = current -> st_md_base -> next;
	while(info){
		printk("###################### info-> syscall_num  : %d\n", info -> syscall_num);
		info = info->next;
	}
		



    return 0;
}

static struct strace_info* find_strace_info(struct exec_context *current, int syscall_num)
{
	struct strace_info *info = current -> st_md_base ->next;
	while(info && info->syscall_num != syscall_num)
		info = info->next;
	return info;
}

static int remove_strace(struct exec_context *current, int syscall_num)
{
	struct strace_info *info = find_strace_info(current, syscall_num);
	if(!info)
            return -1;
	struct strace_head *head = current->st_md_base;
   	struct strace_info *tmp = head->next;
   	if(tmp == info){
           head->next = info->next;
           head->count -= 1;
   	   os_free(info, sizeof(struct strace_info));
           return 0;
   	}
  	while(tmp && tmp->next != info)
	{
          tmp = tmp->next;
	}
   	tmp->next = info->next;
	if(head->last == info)
	{
		head->last = tmp;
	}
   	os_free(info, sizeof(struct strace_info));
   	head->count--;
	return 0;
}

int sys_strace(struct exec_context *current, int syscall_num, int action)
{
	switch(action){
		case ADD_STRACE:
                	return add_strace(current, syscall_num);
            	case REMOVE_STRACE:
                        return remove_strace(current, syscall_num);
		default:
                        break;
	}
	return -1;
}

int sys_read_strace(struct file *filep, char *buff, u64 count)
{
    int ret = 0;
    while(count > 0){
    	    char* source_buffer = (filep -> trace_buffer -> buffer);
    	    int read_pos = filep -> trace_buffer -> read_pos;
    	    int remaining_data = filep -> trace_buffer -> remaining_data;
		printk("remaining_data(80, 40) : %d\n", remaining_data);
            if(remaining_data == 0) return ret;
		printk("read_pos(0, 32) : %d\n", read_pos);
            int size = ((u64 *)source_buffer)[read_pos/8];
printk("source_buffer[read_pos/8] : %d\n", ((u64 *)source_buffer)[5]);
	printk("size(40, 40) : %d\n", size);
            read_pos += 8;
     
            for(int j = 0; j<size; j++){
                buff[j] = source_buffer[read_pos];
                read_pos = (read_pos+1) % 4096;
            }
            filep -> trace_buffer -> read_pos = read_pos;
            if((size+8) < remaining_data)
                filep -> trace_buffer -> remaining_data -= (size+8);
            else{
                filep -> trace_buffer -> remaining_data = 0;
	    }
            count--;
            buff += size;
            ret += size;
		printk("ret size from read_strace : %d\n", ret);
    }
    return ret;

}

int sys_start_strace(struct exec_context *current, int fd, int tracing_mode)
{
	if(current -> st_md_base == NULL){
		current -> st_md_base = os_alloc(sizeof(struct strace_head));
		if(current -> st_md_base == NULL){
			return -EINVAL;
		}
	}
	current -> st_md_base -> tracing_mode = tracing_mode;
	current -> st_md_base -> strace_fd = fd;
	current -> st_md_base -> is_traced = 1;
	return 0;
}

int sys_end_strace(struct exec_context *current){
	
	struct strace_info *tmp = 0;
	struct strace_head *head = current->st_md_base;
	if(head){
		while(head->next)
		{ 
		   tmp = head->next;
		   head->next = tmp->next;
		   head->count -= 1;
		   os_free(tmp, sizeof(struct strace_info));
		}
		os_free(head, sizeof(struct strace_head));
	}
        current->st_md_base = NULL;
	return 0;
}



///////////////////////////////////////////////////////////////////////////
//// 		Start of ftrace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////


//Helper routines

static u64 get_func_arg(struct user_regs *regs, int arg_num)
{
    u64 value;	
    switch(arg_num){
	    case 1: 
		    value = regs->rdi;
		    break;
	    case 2:
		    value = regs->rsi;
		    break;
	    case 3:
		    value = regs->rdx;
		    break;
	    case 4:
		    value = regs->rcx;
		    break;
	    case 5:
		    value = regs->r8;
		    break;
            default:
		    printk("Argument number %d\n", arg_num);
		    //gassert(0, "Wrong argument number");
		    break;
		     
    }	    
    return value;
}	
static struct ftrace_info *add_ftrace_md(struct exec_context *ctx)
{
   struct ftrace_info *info;	
   struct ftrace_head *head;
   
   if(!ctx->ft_md_base){
        ctx->ft_md_base = os_alloc(sizeof(struct ftrace_head));
        bzero((char *)ctx->ft_md_base, sizeof(struct ftrace_head));
   }           
   
   head = ctx->ft_md_base;

   if(head->count >= FTRACE_MAX)
	   return NULL;
   info = os_alloc(sizeof(struct ftrace_info));
   bzero((char *)info, sizeof(struct ftrace_info));
   
   if(!head->count)
	head->next = info;  
   else
	head->last->next = info;	

    head->last = info;
    head->count++;
    return info;
}

static void remove_ftrace_md(struct exec_context *ctx, struct ftrace_info *info)
{
   struct ftrace_head *head = ctx->ft_md_base;
   struct ftrace_info *tmp = head->next;
   if(tmp == info){
	   //gassert(head->count == 1, "Ftrace list is corrupted!");
	   head->next = info->next;
	   head->count -= 1;
           os_free(info, sizeof(struct ftrace_info));
	   return;
   }
   while(tmp && tmp->next != info)
	  tmp = tmp->next;
   //gassert(tmp->next == info, "Not found in ftrace list. Corrupted!");
   tmp->next = info->next;
   if(head->last == info)
        {
                head->last = tmp;
        }
   os_free(info, sizeof(struct ftrace_info));
   head->count--;
   return; 
}
static struct ftrace_info* find_ftrace_info(struct exec_context *ctx, unsigned long faddr)
{
    struct ftrace_info *info = ctx->ft_md_base->next;
    while(info && info->faddr != faddr)
	 info = info->next;
    return info;    
}
//Handlers for tracer operations

static int add_ftrace(struct exec_context *ctx, unsigned long faddr, long args, int fd_trace_buffer)
{
    struct ftrace_info *info;

	printk("add_ftrace: faddr: %x, args: %d\n", faddr, args);
    
    if(args < 0 || args > MAX_ARGS)
	     return -1;
    if(find_ftrace_info(ctx, faddr))
	    return -1;
    info = add_ftrace_md(ctx);
    
    if(!info)
	    return -1;
    info->faddr = faddr;
    info->num_args = args;
    info->fd = fd_trace_buffer;
    info -> capture_backtrace = 0;
    printk("add_ftrace: ftrace_info struct created\n");
    return 0; 
}


static int enable_ftrace(struct exec_context *ctx, unsigned long faddr)
{
    u8 *ptr;	
    printk("enable_ftrace: faddr: %x\n", faddr);
    struct ftrace_info *info = find_ftrace_info(ctx, faddr);
    if(!info)
	    return -1;

    printk("enable_ftrace: ftrace_info found. info->faddr: %x, info->num_args: %d\n", info->faddr, info->num_args);

    /////////////////////////////////////////////////////////////
    //Note: First instruction is push rbp in this container setup
    //ptr = (u8 *) (info->faddr + 4);
    /////////////////////////////////////////////////////////////
    ptr = (u8 *) (info->faddr);
    //gassert(*ptr == (u8)PUSH_RBP_OPCODE, "!Expected. The ins is not 'push rbp'");
    if(*ptr != (u8)PUSH_RBP_OPCODE)
    {
	printk("enable_ftrace: BUG!! 'push rbp' not found\n");
    }

    for(int i = 0; i < 4; ++i){
       info->code_backup[i] = ptr[i];	    
       ptr[i] = INV_OPCODE;
    }
    printk("enable_ftrace: ftrace_info found. Took code backup. info->code_backup: %d, %d, %d, %d, info->num_args: %d\n", info->code_backup[0], info->code_backup[1], info->code_backup[2], info->code_backup[3], info->num_args);
    return 0;
}

static int disable_ftrace(struct exec_context *ctx, unsigned long faddr, u8 check_invalid)
{
    u8 *ptr;	
    printk("disable_ftrace: faddr: %x\n", faddr);
    struct ftrace_info *info = find_ftrace_info(ctx, faddr);
    if(!info)
	    return -1;

    /////////////////////////////////////////////////////////////
    //Note: First instruction is push rbp in this container setup
    //ptr = (u8 *) (info->faddr + 4);
    /////////////////////////////////////////////////////////////
    ptr = (u8 *) (info->faddr);
    if(*ptr != (u8)INV_OPCODE){
	    if(check_invalid){
                 //gassert(0, "!Expected. The ins is not inv opcode");
	    }else{
		 return 0;   
	    }
    }
    for(int i = 0; i < 4; ++i){
       ptr[i] = info->code_backup[i];	    
    }
    return 0;
   	
}

static int remove_ftrace(struct exec_context *ctx, unsigned long faddr)
{
    struct ftrace_info *info = find_ftrace_info(ctx, faddr);
    
    if(!info)
	    return -1;
    disable_ftrace(ctx, faddr, 0);
    remove_ftrace_md(ctx, info);
    return 0;
}

static int enable_backtrace(struct exec_context *ctx, unsigned long faddr)
{
    struct ftrace_info *info = find_ftrace_info(ctx, faddr);
    if(!info)
            return -1;	
    info->capture_backtrace = 1;
    return 0;
}

static int disable_backtrace(struct exec_context *ctx, unsigned long faddr)
{
    struct ftrace_info *info = find_ftrace_info(ctx, faddr);
    if(!info)
            return -1;
    info->capture_backtrace = 0;
    return 0;
}

//Syscall handler
long do_ftrace(struct exec_context *ctx, unsigned long faddr, long action, long nargs, int fd_trace_buffer)
{
    printk("\n\ndo_ftrace: faddr: %x, action: %d, nargs: %d, fd_trace_buffer: %d\n", faddr, action, nargs, fd_trace_buffer);
    switch(action)
    {
	    case ADD_FTRACE:
	                     return add_ftrace(ctx, faddr, nargs, fd_trace_buffer);
	    case REMOVE_FTRACE:	     
	                     return remove_ftrace(ctx, faddr);
	    case ENABLE_FTRACE:	     
	                     return enable_ftrace(ctx, faddr);
	    case DISABLE_FTRACE:	     
	                     return disable_ftrace(ctx, faddr, 1);
	    case ENABLE_BACKTRACE:	     
	                     return enable_backtrace(ctx, faddr);
	    case DISABLE_BACKTRACE:	     
	                     return disable_backtrace(ctx, faddr);
	    default:
			      break;
    }	    
    return -1;
}

int ftrace_file_write(struct exec_context *ctx,u64 fd,u64 buff,u64 count){

       int write_size;
        struct file *filep = ctx->files[fd];
        if(!filep){
                return -EINVAL; //file is not opened
        }
        if(!(filep->mode & O_WRITE)){
                return -EACCES; // file is not opened in write mode
        }
        if(filep->fops->write){
                write_size = filep->fops->write(filep, (char*)buff, count);
                dprintk("write size:%d\n",write_size);
                return write_size;
        }
        return -EINVAL;
}



//Fault handler
long handle_ftrace_fault(struct user_regs *regs)
{
    struct exec_context *current = get_current_ctx();	 
     //Why doing regs->entry_rip - 4??
    //struct ftrace_info *info = find_ftrace_info(current, regs->entry_rip - 4);
    struct ftrace_info *info = find_ftrace_info(current, regs->entry_rip);
    //gassert(info, "Invalid opcode fault from unknown address");
    if(!info)
    {
    	printk("\nhandle_ftrace_fault: regs->entry_rip: %x", regs->entry_rip);
	printk("handle_ftrace_fault: Invalid opcode fault from unknown address\n");
	return -1;
    }
    printk("\nhandle_ftrace_fault: regs->entry_rip: %x, info->faddr: %x\n", regs->entry_rip, info->faddr);
//////////////////////////////////////////////////////

	int param_size = 0, total_size = 2, ctr = 0, cur_index = 0;
	u64 ret_addr = 0;	
	u64 rbp_addr = 0;

	*((unsigned long *)(regs->entry_rsp - 8)) = regs->rbp;
	regs->entry_rsp -= 8;
	regs->rbp = regs->entry_rsp;
	regs->entry_rip += 4;

	total_size += info->num_args; 
	if(info->capture_backtrace){

		ret_addr = info->faddr;
		rbp_addr = regs->rbp;

		while(ret_addr != END_ADDR)
		{
			ctr++;
			//find earlier return addresses
			ret_addr = *((u64*)(rbp_addr+8));
			rbp_addr = *((u64*)rbp_addr);
			//printk("Found return address: %x\n", ret_addr);
		}
		total_size += ctr;
	}
	printk("total_size: %d, num backtrace: %d \n", total_size, ctr);

	u64 ftrace_buff[total_size];
	ftrace_buff[cur_index++] = (total_size-1)*8;
	ftrace_buff[cur_index++] = info->faddr;
	for(int i=1; i <= info->num_args; ++i){
		printk("Arg: %d-->%x\n", i, get_func_arg(regs, i));
		ftrace_buff[cur_index++] = get_func_arg(regs, i); 
	}
	
	if(info->capture_backtrace){
		printk("Backtrace capturing enabled\n");
		ret_addr = info->faddr;
		rbp_addr = regs->rbp;
		while(ret_addr != END_ADDR)
                {
                        printk("Return address: %x\n", ret_addr);
			ftrace_buff[cur_index++] = ret_addr;

                        //find earlier return addresses
                        ret_addr = *((u64*)(rbp_addr+8));
                        rbp_addr = *((u64*)rbp_addr);
                }
        }

	int ret = ftrace_file_write(current, info->fd, (u64)ftrace_buff, total_size*8);
	if(ret != (total_size*8)) return -EINVAL;
    
        return 0;
}


int sys_read_ftrace(struct file *filep, char *buff, u64 count)
{
    int ret = 0;
    printk("\nsys_read_ftrace called, count: %d\n", count);
    while(count > 0){
            char* source_buffer = (filep -> trace_buffer -> buffer);
            int read_pos = filep -> trace_buffer -> read_pos;
            int remaining_data = filep -> trace_buffer -> remaining_data;
            printk("\nremaining_data : %d\n", remaining_data);
            if(remaining_data == 0) return ret;
            printk("read_pos : %d\n", read_pos);
            int size = ((u64 *)source_buffer)[read_pos/8];
            printk("size : %d\n", size);
            read_pos += 8;

            for(int j = 0; j<(size/8); j++){
                ((u64*)buff)[j] = ((u64*)source_buffer)[read_pos/8];
		printk("j: %d, buff[%d]: %x\n", j, j*8, ((u64*)buff)[j]);
                read_pos = (read_pos+8) % 4096;
            }
            filep -> trace_buffer -> read_pos = read_pos;
            //if((size+8) < remaining_data)
                filep -> trace_buffer -> remaining_data -= (size+8);
            //else{
            //    filep -> trace_buffer -> remaining_data = 0;
            //}
            count--;
            buff += size;
            ret += size;
            printk("ret size from read_ftrace : %d\n", ret);
    }
    printk("Returning from sys_read_ftrace , count: %d\n", count);

    return ret;

}


