/* 每 20ms 读取一个二进制 int，只在按键状态变化时打印。 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>

/* 必须和驱动里的定义完全一致 */
#define MAGIC           'j'
#define TIME_ON         _IO(MAGIC,  0x01)
#define TIME_OFF        _IO(MAGIC,  0x02)
#define TIME_SET_PERIOD _IOW(MAGIC, 0x03, unsigned long)

int main(int argc, char *argv[])
{
	ssize_t ret;
	int fd;

	/* 本程序只读取按键，不需要写权限。 */
	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror("Cannot open device");
		return 1;
	}
    int cmd;
	while (1) {
		printf("please CMD:");
		scanf("%d", &cmd);
        if(cmd ==1){
            ioctl(fd, TIME_ON, 0);
        }else if(cmd ==2){
            ioctl(fd, TIME_OFF, 0);
        }else if(cmd ==3){
            unsigned long period;
            printf("please period:");
            ret = scanf("%lu", &period);
            if(ret == EOF){
                printf("input error\n");
                continue;
            }
            if(ret == 1){
                ioctl(fd, TIME_SET_PERIOD, &period);
            }
        }

	}
	close(fd);

	return 1;
}
