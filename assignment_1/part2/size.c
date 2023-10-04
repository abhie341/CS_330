#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <libgen.h>
struct dirent *entry;
struct stat file_info;
struct stat directory_info;

long long get_directory_size(char * directory_path){
	long long size = 0;
	DIR* dir_stream = opendir(directory_path);
	if(dir_stream){
		while((entry = readdir(dir_stream)) != NULL){
			if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                		continue;
            		}
			else if(entry->d_type == DT_DIR){
				char sub_directory_path[512];
                                snprintf(sub_directory_path, sizeof(sub_directory_path), "%s/%s", directory_path, entry->d_name);
                                size += get_directory_size(sub_directory_path);
			}
			else if(entry -> d_type == DT_LNK){
				char symbolic_file_path[512];
                                snprintf(symbolic_file_path, sizeof(symbolic_file_path), "%s/%s", directory_path, entry->d_name);
                                //if(stat(symbolic_file_path, &file_info) == 0){
                                 //       size += file_info.st_size;
                               // }
				char symbolic_path[512];
                                snprintf(symbolic_path, sizeof(symbolic_path), "%s/%s", directory_path, entry->d_name);
				if (lstat(symbolic_path, &file_info) == 0) {
        				if (S_ISDIR(file_info.st_mode)) {
						char sub_directory_path[512];
						readlink(symbolic_path, sub_directory_path, sizeof(sub_directory_path) - 1);
						if(stat(sub_directory_path, &file_info) == 0){
                                                        //size += file_info.st_size;
                                                }
						//size += get_directory_size(sub_directory_path);
			
					}
					else{
						char file_path[512];
						readlink(symbolic_path, file_path, sizeof(file_path) - 1);
                                		if(stat(file_path, &file_info) == 0){
							size += file_info.st_size;
                                		}
					}
				}
			}
			else{
				char file_path[512];
				snprintf(file_path, sizeof(file_path), "%s/%s", directory_path, entry->d_name);
				if(stat(file_path, &file_info) == 0){
					size += file_info.st_size;
				}
			}
		}
		if(stat(directory_path, &file_info) == 0){
                        size += file_info.st_size;
                }
		closedir(dir_stream);
		return size;
	}
	else{
		perror("opendir");
	}
}

int main(int argc, char * argv[]) {
	long long size = 0;
	long long child_size = 0;
        if(argc!=2)
                {
                        printf("syntax: $./exec path?\n");
                        exit(-1);
                }
        char *directory_path = argv[1];
	char *dir_name = basename(directory_path);
	int pipefd[2];
	if (pipe(pipefd) == -1) {
               perror("pipe");
               exit(EXIT_FAILURE);
           }

	DIR *dir_stream = opendir(directory_path);
	if (dir_stream) {
        	while ((entry = readdir(dir_stream)) != NULL) {
			if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                                continue;
                        }
			if (entry->d_type == DT_DIR) {
				int pid = fork();
				if(pid < 0){
					printf("fork was unsuccessful\n");
                        		exit(-1);
				}
				else if(pid == 0){
					long long size = 0;
					char sub_directory_path[512]; // Adjust buffer size as needed
                			snprintf(sub_directory_path, sizeof(sub_directory_path), "%s/%s", directory_path, entry->d_name);
					size = get_directory_size(sub_directory_path);
               				write(pipefd[1], &size, sizeof(size));
					exit(EXIT_SUCCESS);	
				}
				else{
					read(pipefd[0], &child_size, sizeof(child_size));
					size += child_size;
					printf("%s %lld\n", entry -> d_name, child_size);
					wait(NULL);	
				}

			}
			else if(entry -> d_type == DT_LNK){
                                char symbolic_file_path[512];
                                snprintf(symbolic_file_path, sizeof(symbolic_file_path), "%s/%s", directory_path, entry->d_name);
                                if(stat(symbolic_file_path, &file_info) == 0){
                                        size += file_info.st_size;
                                }
                                char symbolic_path[512];
                                snprintf(symbolic_path, sizeof(symbolic_path), "%s/%s", directory_path, entry->d_name);
                                if (lstat(symbolic_path, &file_info) == 0) {
                                        if (S_ISDIR(file_info.st_mode)) {
                                                char sub_directory_path[512];
                                                readlink(symbolic_path, sub_directory_path, sizeof(sub_directory_path) - 1);
                                                size += get_directory_size(sub_directory_path);

                                        }
                                        else{
                                                char file_path[512];
                                                readlink(symbolic_path, file_path, sizeof(file_path) - 1);
                                                if(stat(file_path, &file_info) == 0){
                                                        size += file_info.st_size;
                                                }
                                        }
                                }
                        }

			else{
				 char file_path[512];
	                         snprintf(file_path, sizeof(file_path), "%s/%s", directory_path, entry->d_name);
         	                 if(stat(file_path, &file_info) == 0){
					size += file_info.st_size;
                        	 }
	
			}
		
		}

		if(stat(directory_path, &file_info) == 0){
                	size += file_info.st_size;
		}
		printf("%s %lld\n",dir_name, size);
	}
	close(pipefd[0]);
	close(pipefd[1]);

    return 0;
}

