/* 用户空间测试：发送一个二进制命令，再读取文本状态。 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	unsigned char command;
	char state[3];
	ssize_t ret;
	int fd;
	int result = 1;

	/* 不使用 atoi，避免把非法参数也当成关闭命令 0。 */
	if (argc != 3 || argv[2][0] < '0' || argv[2][0] > '2' ||
	    argv[2][1] != '\0') {
		fprintf(stderr, "Usage: %s /dev/dts_led 0|1|2\n", argv[0]);
		return 1;
	}

	/* 字符 '1' 是 49，减去 '0' 后才得到二进制数值 1。 */
	command = (unsigned char)(argv[2][0] - '0');
	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	ret = write(fd, &command, sizeof(command));
	if (ret < 0) {
		perror("write");
		goto out_close;
	}
	if (ret != (ssize_t)sizeof(command)) {
		fprintf(stderr, "write: expected one byte, got %zd\n", ret);
		goto out_close;
	}

	ret = read(fd, state, sizeof(state) - 1);
	if (ret < 0) {
		perror("read");
		goto out_close;
	}
	if (!ret) {
		fprintf(stderr, "read: unexpected EOF\n");
		goto out_close;
	}
	state[ret] = '\0';
	printf("LED state: %s", state);
	result = 0;

out_close:
	if (close(fd) < 0) {
		perror("close");
		result = 1;
	}
	return result;
}
