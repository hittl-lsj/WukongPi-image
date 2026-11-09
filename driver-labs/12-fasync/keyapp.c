/* 每 20ms 读取一个二进制 int，只在按键状态变化时打印。 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "poll.h"
#include "sys/select.h"
#include "sys/time.h"
#include "linux/ioctl.h"
#include <stdio.h>      // 用于 printf, perror, fprintf
#include <stdlib.h>     // 用于 exit, getpid
#include <unistd.h>     // 用于 read, close, pause, open
#include <fcntl.h>      // ★ 核心：用于 open, fcntl, O_ASYNC, F_SETOWN, F_SETFL
#include <signal.h>     // ★ 核心：用于 signal, SIGIO, sigaction
#include <errno.h>      // 用于 errno, strerror
#include <string.h>     // 用于 strcmp (如果解析命令行参数)

static int fd;
/* 信号处理函数（当收到 SIGIO 时被内核调用） */
static void sigio_handler(int signum)
{
    int state = -1;
    
    if (signum == SIGIO) {
        // ★ 注意：在 fasync 机制下，App 必须用非阻塞读取，
        // 因为如果连续触发信号，可能有1次读不到数据（返回 EAGAIN）
        int ret = read(fd, &state, sizeof(state));
        if (ret > 0) {
            printf("收到异步通知！按键状态 = %d\n", state);
        } else if (ret < 0 && errno != EAGAIN) {
            perror("read");
        }
    }
}
int main(int argc, char *argv[])
{
	ssize_t ret;
	struct pollfd pfds;


	int value;

	/* 本程序只读取按键，不需要写权限。 */
	fd = open(argv[1], O_RDONLY | O_NONBLOCK);//异步模式
	if (fd < 0) {
		perror("open");
		return 1;
	}
	// 2. 注册信号处理函数
	if(signal(SIGIO, sigio_handler) == SIG_ERR){//注册信号处理函数
		perror("signal");
		close(fd);
		return 1;
	}
	// 3. 设置文件描述符的拥有者为当前进程
	if(fcntl(fd, F_SETOWN, getpid()) < 0){//设置文件描述符的拥有者为当前进程
		perror("fcntl");
		close(fd);
		return 1;
	}
	/* 4. 开启异步通知模式（驱动底层的 .fasync 会被调用） */
    int flags = fcntl(fd, F_GETFL);
	//
	if (fcntl(fd, F_SETFL, flags | O_ASYNC) < 0) {
        perror("fcntl F_SETFL");
        close(fd);
        return 1;
    }
	    printf("进程 %d 已开启异步通知，等待按键 (Ctrl+C 退出)...\n", getpid());

    /* 5. 死循环睡眠，把 CPU 让给其他进程，直到收到信号 */
    while (1) {
        pause(); 
    }

	close(fd);

	return 1;
}
