/* 每 20ms 读取一个二进制 int，只在按键状态变化时打印。 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	ssize_t ret;
	int fd;

	int value;


	/* 本程序只读取按键，不需要写权限。 */
	fd = open(argv[1], O_RDONLY );//阻塞模式
	if (fd < 0) {
		perror("open");
		return 1;
	}

	while (1) {
		ret = read(fd, &value, sizeof(value));
		if(value == 0){
			printf("key released\n");
		}
		else{
			printf("key pressed\n");
		}
	}
	close(fd);

	return 1;
}
