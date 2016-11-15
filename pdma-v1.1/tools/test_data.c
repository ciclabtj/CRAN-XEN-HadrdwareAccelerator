#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

int main(int argc, char *argv[])
{
	unsigned char c;
    unsigned char* buf, *tmp_buf, *sys_buf, *s_buf;
    int i,j, byte_size, data_size;
    FILE *fp;

    fp = fopen("encode","r+");
    if(fp == NULL)
        printf("open encode fail\n");
    
    /*get file size*/
    fseek(fp, 0, SEEK_END);
    byte_size = ftell(fp) - 1;
	data_size = byte_size/3;
    rewind(fp);
    printf("encode file byte_Size: %d, data_size: %d\n", byte_size, data_size);

    /* malloc buf */
    buf = (unsigned char *)malloc(byte_size * 4);
	s_buf = (unsigned char *)malloc(byte_size);
    tmp_buf = buf;
    sys_buf = s_buf;

    if (!buf) {
        printf("alloc buffer failed\n");
        return -1;
    }
    j = 0; 
    while(j < byte_size){
        c = fgetc(fp);
        if(c == '0')
            *sys_buf = 0x80;
        else *sys_buf = 0x7f;
	    printf("%x", *sys_buf);
        sys_buf++;
        j++;
    }
	printf("\n");
    printf("j is: %d, should be equal to byte_size\n", j);
//	printf("s_buf is: %s\n", s_buf);
	
    j = 0; i = 0;
    while(j < byte_size){
        *(tmp_buf++) = s_buf[i];
		*(tmp_buf++) = s_buf[i + data_size];
		*(tmp_buf++) = s_buf[i + data_size + data_size];
		tmp_buf++;
		i++;        
        j++;
    }
    
    unsigned char * p = buf;
    for(j = 0; j < 176; j++)
	    printf("buf%d: 0x%x\n", j, *p++);
    fclose(fp);

}
