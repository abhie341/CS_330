
/* Checking with system call for different number of arguments */

#include<ulib.h>

int main (u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {

        int strace_fd = create_trace_buffer(O_RDWR);
        if(strace_fd != 3){
                printf("create trace buffer failed\n");
                return -1;
        }
        int rdwr_fd = create_trace_buffer(O_RDWR);
        if(rdwr_fd != 4){
                printf("create trace buffer failed for read and write\n");
                return -1;
        }
        u64 strace_buff[4096];
        int read_buff[4096];
        int write_buff[4096];

        for(int i = 0, j = 0; i< 4096; i++){
                j = i % 26;
                write_buff[i] = 'A' + j;
        }

        start_strace(strace_fd, FULL_TRACING);
        int write_ret = write(rdwr_fd, write_buff, 10);
        if(write_ret != 10){
                printf("1. Test case failed\n");
                return -1;
        }
        int read_ret = read(rdwr_fd, read_buff, 10);
        if(read_ret != 10){
                printf("2. Test case failed\n");
                return -1;
        }
	sleep(10);
	get_stats();
        end_strace();

        int strace_ret = read_strace(strace_fd, strace_buff, 4);
        if(strace_ret != 88){
                printf("3. Test case failed\n");
                return -1;
	}
        if(strace_buff[0] != 25){
                printf("4. Test case failed\n");
                return -1;
        }
	if(strace_buff[2] != (u64)&write_buff){
                printf("5. Test case failed\n");
                return -1;
        }	


	if(strace_buff[3] != 10){
                printf("6. Test case failed\n");
                return -1;
        }
        if(strace_buff[4] != 24){
                printf("7. Test case failed\n");
                return -1;
        }
	if(strace_buff[8] != 7){
                printf("8. Test case failed\n");
                return -1;
        }
	if(strace_buff[9] != 10){
                printf("9. Test case failed\n");
                return -1;
        }
	if(strace_buff[10] != 11){
                printf("10. Test case failed\n");
                return 0;
        }
	printf("Test case passed\n");
        close(strace_fd);
        close(rdwr_fd);

        return 0;
}
