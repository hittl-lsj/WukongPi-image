/* 每 20ms 读取一个二进制 int，只在按键状态变化时打印。 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "poll.h"
#include "sys/select.h"
#include "sys/time.h"
#include "linux/ioctl.h"
int main(int argc, char *argv[])
{
	ssize_t ret;
	int fd;
	struct pollfd pfds;
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 20000;
	fd_set readfds;

	int value;

	/* 本程序只读取按键，不需要写权限。 */
	fd = open(argv[1], O_RDONLY | O_NONBLOCK);//非阻塞模式
	if (fd < 0) {
		perror("open");
		return 1;
	}
#if 0
	while (1) {
		pfds.fd = fd;
		pfds.events = POLLIN | POLLPRI;
		ret = poll(&pfds, 1, -1);
		if(ret < 0){
			printf("error read\n");
			}
		else if(ret == 0){
			printf("timeout\n");
		}
		else if(ret > 0){
			printf("key value is %d\n", value);
			printf("1\n");
		}
	}
#endif
	while (1) {
		FD_ZERO(&readfds);
		FD_SET(fd, &readfds);
		ret = select(fd + 1, &readfds, NULL, NULL, &tv);
		if(ret < 0){
			printf("error select\n");
			}
		else if(ret == 0){
			printf("timeout\n");
		}
		else if(ret > 0){
			ret = read(fd, &value, sizeof(int));
			if(ret < 0){
				printf("error read\n");
				}
			else{
				printf("key value is %d\n", value);
				printf("1\n");
			}
		}
	}
	close(fd);

	return 1;
}
