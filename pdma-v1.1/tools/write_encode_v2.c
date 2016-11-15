#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include "pdma-lib.h"
#include "../pdma-ioctl.h"


void usage()
{
    printf("pdma-write device [-cnt value] [-dma value] [-d value] [-pt value] [-inc value]\n");
    printf("device: device name\n");
    printf("-cnt:  total write count\n");
    printf("-dma:  1: send dma-start command before write; 0: don't send\n");
    printf("-d:    delay between two write system call, measured by us\n");
    printf("-pt:   start pattern\n");
    printf("-inc:  pattern increase\n");
    printf("example: ./pdma-write /dev/pdma -cnt 1000000\n");
}

int main(int argc, char *argv[])
{
    struct pdma_info info;
    char *fl = argv[1];
    int fd, ret=0, check_pt=0;
    unsigned int dma_start=0, delay=0, pattern=0, inc=1;
    unsigned long long count=(unsigned long long)(30);
    unsigned long long total_write_num=0, total_write_size=0;
    unsigned long long tm_beg, tm_end, tm_total;
    unsigned long long k; 
    char *pval;
	unsigned char c;
    unsigned char* buf, *tmp_buf, *sys_buf, *s_buf, *rev_buf, *ag_buf;
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

    /* parse argument */
    if (argc == 1 || !strcmp("-h", argv[1])) {
        usage();
        return 0;
    }

    pval = arg_get_next(argc, argv, "-cnt");
    if (pval) {
        count = memparse(pval, &pval);
    }
    pval = arg_get_next(argc, argv, "-dma");
    if (pval) {
        dma_start = memparse(pval, &pval);
    }
    pval = arg_get_next(argc, argv, "-d");
    if (pval) {
        delay = memparse(pval, &pval);
    }
    pval = arg_get_next(argc, argv, "-pt");
    if (pval) {
        pattern = memparse(pval, &pval);
        check_pt = 1;
    }
    pval = arg_get_next(argc, argv, "-inc");
    if (pval) {
        inc = memparse(pval, &pval);
    }

    /* open */
    fd = open(fl, O_RDWR);
    if (fd == -1) {
        printf("open failed for pdma device %s \n", fl);
        return -1;
    }

    /* get info */
    ret = ioctl(fd, PDMA_IOC_INFO, (unsigned long)&info);
    if (ret == -1) {
        printf("get info failed\n");
        close(fd);
        return -1;
    }

    /* malloc buf */
	rev_buf == (unsigned char *)malloc(byte_size);
    buf = (unsigned char *)malloc(info.wt_block_sz);
	s_buf = (unsigned char *)malloc(byte_size);
    tmp_buf = rev_buf;
	ag_buf = buf;
    sys_buf = s_buf;

    if (!buf) {
        printf("alloc buffer failed\n");
        close(fd);
        return -1;
    }
    
    /* start dma */
    if (dma_start != 0) {
        ret = ioctl(fd, PDMA_IOC_START_DMA);
        if (ret == -1) {
            printf("start dma failed\n");
            free(buf);
            close(fd);
            return -1;
        }
    }
    
    /*read data from encode file*/
    sys_tm_get_us(&tm_beg);
          
    j = 0; 
    while(j < byte_size){
        c = fgetc(fp);
        if(c == '0')
            *sys_buf = 0x00;
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
		*tmp_buf++ = 0x00;
		i++;        
        j++;
    }
    
	int q;
	for(j = 0, i = 0; j < 22; j++){
		if(i == 0){
			i++;
			rev_buf += 8;
		}
		else rev_buf += 16;
		for( q = 0; q < 8; q++ ){		
			*ag_buf++ = *(--rev_buf);	
	    }
    }
	
    unsigned char * p = buf;
    for(j = 0; j < 176; j++)
	    printf("buf%d: 0x%x\n", j, *p++);
		
	

	/* start write */
    //struct pdma_rw_reg ctrl;
    //ctrl.type = 1;   //write
    //ctrl.addr = 0;
    //ctrl.val = data_size;
    //ret = ioctl(fd, PDMA_IOC_RW_REG, (unsigned long)&ctrl);
    //if(ret == -1){
     //   printf("pdma_ioc_rw_reg error\n");
        //return -1;
    //}

    while (count--) 
    {
        if (check_pt) 
       {
            unsigned int start_pt = pattern + inc*(info.wt_block_sz/4)*total_write_num;
            unsigned int *val32 = (unsigned int *)buf;

       }
         
         
       // ret = write(fd, buf, byte_size);
        ret = write(fd, buf, info.wt_block_sz);
        tmp_buf = buf;
     //   printf("block szie: %d\n",info.wt_block_sz);
        printf("count:%d\n",count);
       
        if (ret != info.wt_block_sz) {
            printf("write failed\n");
            ret = -1;
            break;

        if (delay != 0) {
            sys_tm_wait_us(delay);
        }

        total_write_num++;
    }
    }
	
	//change the position for size
	struct pdma_rw_reg ctrl;
    ctrl.type = 1;   //write
    ctrl.addr = 0;
    ctrl.val = data_size;
	//ret = ioctl(fd, PDMA_IOC_RW_REG, (unsigned long)&ctrl);
    ret = 0;
    if(ret == -1){
        printf("pdma_ioc_rw_reg error\n");
        return -1;
    }

    sys_tm_get_us(&tm_end); 
    tm_total = tm_end - tm_beg;
    if (tm_total == 0) tm_total = 1;
    total_write_size = total_write_num * info.wt_block_sz;


    printf("block size %d\n",info.wt_block_sz);
    printf("total write %lldKB, elapsed %lldus\n", total_write_size/1024, tm_total);
    printf("write performance is %lldMB/s\n", total_write_size/tm_total);

    free(buf);
    close(fd);
    fclose(fp);
    return ret;
}
