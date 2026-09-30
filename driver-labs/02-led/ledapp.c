#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
/* H3 PA17 
argc: 命令行参数个数
argv: 命令行参数数组字符
*./ledAPP <filename> <command>
./ledapp / 1 
./ledapp /dev/led 0 
./ledapp /dev/led 2 is toggle
*/
#define LED_NAME  "wukong_led"
#define LED_ON  1
#define LED_OFF 0
#define LED_TOGGLE 2
int main(int argc, char *argv[]){
    if(argc!=3){
        printf("error usage!!!/r/n");
        return -1;  
    }
    int ret=0;
    char data_buf[2] = {0};
    int fd = open(argv[1], O_RDWR); 
    if (fd < 0) {
        printf("open %s failed\n", argv[1]);
        return -1;
    }

    data_buf[0] = atoi(argv[2]);

    // 写入命令
    ret = write(fd, data_buf, sizeof(data_buf[0]));
    if (ret < 0) {
        printf("write %s failed\n", argv[1]);
        close(fd);
        return -1;
    }
    
    close(fd);
    return 0;
}