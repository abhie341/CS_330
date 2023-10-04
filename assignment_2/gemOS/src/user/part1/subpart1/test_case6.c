#include<ulib.h>

int main (u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {

        int fd = create_trace_buffer(O_RDWR);
        char buff[4196];
        char buff2[4196];

        for(int i = 0, j = 0; i< 4096; i++){
                j = i % 26;
                buff[i] = 'A' + j;
        }

        int ret = write(fd, buff, 4096);
        if(ret != 4096){
                printf("1.Test case failed\n");
                return -1;
        }

        int ret2 = read(fd, buff2, 4096);
        if(ret2 != 4096){
                printf("2.Test case failed\n");
                return -1;
        }
        int ret3 = write(fd, buff+4096, 100);
        if(ret3 != 100){
                printf("3.Test case failed\n");
                return -1;
        }

        int ret4 = read(fd, buff2+4096, 100);
        if(ret4 != 100){
                printf("2.Test case failed\n");
                return -1;
        }
        int ret5 = ustrncmp(buff, buff2, 4196);
        if(ret5 != 0){
                printf("3.Test case failed\n");
                return -1;
	}
        close(fd);
        printf("Test case passed\n");
        return 0;
}
