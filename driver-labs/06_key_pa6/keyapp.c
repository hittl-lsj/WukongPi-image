/* 每 20ms 读取一个二进制 int，只在按键状态变化时打印。 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	ssize_t ret;
	int fd;
	int last_value = -1;
	int value;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s /dev/dts_key\n", argv[0]);
		return 1;
	}

	/* 本程序只读取按键，不需要写权限。 */
	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	while (1) {
		ret = read(fd, &value, sizeof(value));
		if (ret < 0) {
			perror("read");
			break;
		}
		/* 0 表示 EOF，短读表示协议不匹配，不能使用不完整的 value。 */
		if (ret != (ssize_t)sizeof(value)) {
			fprintf(stderr, "read: expected %zu bytes, got %zd\n",
				sizeof(value), ret);
			break;
		}
		if (value != last_value) {
			printf("key %s (%d)\n", value ? "pressed" : "released",
			       value);
			fflush(stdout);
			last_value = value;
		}
		/* 等待放在应用中，让出 CPU；20ms 采样间隔不等于消抖。 */
		usleep(20 * 1000);
	}
	close(fd);

	return 1;
}
