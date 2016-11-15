#include<stdio.h>
#include<stdlib.h>
#include<fcntl.h>
#include<string.h>

int main(int argc, char *argv[]){
/*    int i, fd;
    char *file = argv[1];
    fd = open(file, O_RDWR);
    if(fd == -1){
        printf("open failed for file %s\n", file);
    }

    char *fc = (char *)malloc(228);
    read(fd, fc, 228);
    for(i = 0; i < 228; i++){
        printf("%d ", *fc);
        fc++;
    }
    close(file);
*/

    int i, fsize;
    if(argc == 1){
        printf("please input the file name\n");
        return -1;
    }
    char *name = argv[1];
    FILE *file = fopen(name, "rb");
    fseek(file, 0, SEEK_END);
    fsize = ftell(file);
    rewind(file);
    printf("file size: %d\n", fsize);
    unsigned char *fd = (unsigned char*)malloc(fsize);
    memset(fd, 0, fsize);
    for(i = 0; i < fsize; i++){
        fscanf(file, "%c", fd);
        printf("%d ", *fd);
        fd++;
    }
    fclose(file);
    return 0;
}
