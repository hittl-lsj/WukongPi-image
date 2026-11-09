// SPDX-License-Identifier: GPL-2.0
/* evdev 提供阻塞读取，不再读取自定义 int 或每 20 ms 轮询。 */
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <unistd.h>


/*
 * 本程序读取按键事件，打印按键状态。
 * 例如：./keyapp /dev/input/event0
 * 例如：./keyapp /dev/input/event1
*/
int main(int argc, char *argv[])
{
	struct input_event event;
	ssize_t ret;
	int fd;
	int status = 0;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s /dev/input/eventX\n", argv[0]);
		return 1;
	}

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	while (1) {
		/* 整个结构体包含时间戳、type、code、value，不能只读 int。 */
		ret = read(fd, &event, sizeof(event));
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			perror("read");
			status = 1;
			break;
		}
		if (ret == 0)
			break;
		if (ret != (ssize_t)sizeof(event)) {
			fprintf(stderr, "read: expected %zu bytes, got %zd\n",
				sizeof(event), ret);
			status = 1;
			break;
		}

		if (event.type == EV_KEY) {
			printf("EV_KEY code=%u value=%d (%s)\n", event.code,
			       event.value, event.value == 0 ? "released" :
			       event.value == 1 ? "pressed" : "repeat");
		} else if (event.type == EV_SYN && event.code == SYN_REPORT) {
			printf("SYN_REPORT\n");
		} else if (event.type == EV_SYN && event.code == SYN_DROPPED) {
			/* 本课只演示低速单按键；不继续使用丢失事件后的状态。 */
			fprintf(stderr, "input queue overrun; restart the reader\n");
			status = 1;
			break;
		}
		fflush(stdout);
	}
	close(fd);
	return status;
}
