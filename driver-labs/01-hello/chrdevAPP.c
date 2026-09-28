#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/*
 * 字符设备驱动测试程序
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * ./chrdevbaseAPP /dev/chrdevbase 1 is read
 * ./chrdevbaseAPP /dev/chrdevbase 2 is write
 * @return 0 成功
 * @return -1 失败
 */
int main(int argc, char *argv[]){
    if(argc!=3){
      printf("error usage!!!/r/n");
      return -1;  
    }
    int ret=0;
    char read_buf[128] = {0};
    char write_buf[128] = {0};
    int fd = open(argv[1], O_RDWR); 
    char usr_data[]={"userdata"};

    if(fd < 0){
        printf("open is failed\n");
        return -1;
    }
    printf("open is success\n"); 

    if(atoi(argv[2])==1){
    ret = read(fd, read_buf, sizeof(read_buf));
    if(ret < 0){
        printf("read %s failed\n", argv[1]);
        return -1;
    }
    printf("read: %s\n", read_buf);
    }


    if(atoi(argv[2])==2){
    memcpy(write_buf,usr_data,sizeof(usr_data));
    ret = write(fd, write_buf, sizeof(write_buf));
    if(ret < 0){ 
        printf("write %s failed\n", argv[1]);
        return -1;
    }
    printf("write: %s\n", write_buf);

    }
    /*
    // 关闭文件描述符
    close(fd);
    */
    ret = close(fd);

    if(ret < 0){
        printf("close %s failed\n", argv[1]);
        return -1;
    }
    printf("close is success\n");
    return 0;
}