
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_DEVICE       "/dev/mpu6050_dev"
#define MPU6050_SAMPLE_BYTES 14

/* 必须与驱动 mpu6050_hw_init() 设置的 ±2g、±250 deg/s 量程一致。 */
#define ACCEL_LSB_PER_G      16384.0
#define GYRO_LSB_PER_DPS     131.0

static volatile sig_atomic_t running = 1;

static void stop_sampling(int signo)
{
    (void)signo;
    /* 信号处理函数只修改标志，关闭设备交给 main() 完成。 */
    running = 0;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s [device [count]]\n"
                    "  device: default %s\n"
                    "  count : 0 = continuous (default); Ctrl+C to stop\n"
                    "Example: %s %s 10\n",
            program, DEFAULT_DEVICE, program, DEFAULT_DEVICE);
}

/* MPU6050 高字节在前；显式解析，不能依赖 CPU 的字节序。 */
static int decode_be16(const uint8_t *bytes)
{
    unsigned int value = ((unsigned int)bytes[0] << 8) | bytes[1];

    /* 16 位补码的最高位为 1 时，要按负数解释。 */
    return (value & 0x8000U) ? (int)value - 65536 : (int)value;
}

static void print_sample(const uint8_t *data, unsigned long number)
{
    /* 从寄存器 0x3b 连读：AX、AY、AZ、温度、GX、GY、GZ。 */
    int ax = decode_be16(data + 0);
    int ay = decode_be16(data + 2);
    int az = decode_be16(data + 4); // 加速度 Z 轴
    int temperature = decode_be16(data + 6);
    int gx = decode_be16(data + 8); // 陀螺仪 X 轴
    int gy = decode_be16(data + 10); // 陀螺仪 Y 轴
    int gz = decode_be16(data + 12); // 陀螺仪 Z 轴

    printf("[%lu] raw: A=(%d,%d,%d) T=%d G=(%d,%d,%d)\n",
           number, ax, ay, az, temperature, gx, gy, gz);
    printf("    accel(g): X=% .3f Y=% .3f Z=% .3f\n", 
           ax / ACCEL_LSB_PER_G, ay / ACCEL_LSB_PER_G,
           az / ACCEL_LSB_PER_G);
    printf("    gyro(deg/s): X=% .3f Y=% .3f Z=% .3f  temp=%.2f C\n",
           gx / GYRO_LSB_PER_DPS, gy / GYRO_LSB_PER_DPS,
           gz / GYRO_LSB_PER_DPS, temperature / 340.0 + 36.53);
}

int main(int argc, char *argv[])
{
    const char *device = DEFAULT_DEVICE;
    unsigned long limit = 0;
    unsigned long samples = 0;
    struct sigaction action = { .sa_handler = stop_sampling };
    uint8_t data[MPU6050_SAMPLE_BYTES];
    int status = EXIT_SUCCESS;
    int fd;

    if (argc == 2 && (!strcmp(argv[1], "-h") ||
                      !strcmp(argv[1], "--help"))) {
        usage(argv[0]);
        return EXIT_SUCCESS;
    }
    if (argc > 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (argc >= 2)
        device = argv[1];
    if (argc == 3) {
        char *end;

        errno = 0;
        limit = strtoul(argv[2], &end, 10);
        if (errno || argv[2][0] < '0' || argv[2][0] > '9' || *end) {
            fprintf(stderr, "count must be a non-negative integer\n");
            return EXIT_FAILURE;
        }
    }

    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGTERM, &action, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    fd = open(device, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        perror(device);
        return EXIT_FAILURE;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("Device: %s; frame: 14 bytes; read interval: 200 ms\n", device);
    printf("Conversion assumes accel +/-2g and gyro +/-250 deg/s\n");

    while (running && (limit == 0 || samples < limit)) {
        struct timespec delay = { .tv_sec = 0, .tv_nsec = 200000000L };
        ssize_t bytes = read(fd, data, sizeof(data));

        if (bytes < 0) {
            if (errno == EINTR)
                continue;
            perror("read MPU6050");
            status = EXIT_FAILURE;
            break;
        }
        /* 驱动约定一次 read 返回完整一组，不拼接不同 read 的采样。 */
        if (bytes != (ssize_t)sizeof(data)) {
            fprintf(stderr, "Incomplete sample: expected %zu bytes, got %zd\n",
                    sizeof(data), bytes);
            status = EXIT_FAILURE;
            break;
        }

        print_sample(data, ++samples);
        if (limit != 0 && samples == limit)
            break;

        /* 控制终端刷新频率，不是在配置芯片内部的采样率。 */
        while (running && nanosleep(&delay, &delay) < 0) {
            if (errno == EINTR)
                continue;
            perror("nanosleep");
            status = EXIT_FAILURE;
            goto out;
        }
    }

out:
    if (close(fd) < 0) {
        perror("close");
        status = EXIT_FAILURE;
    }
    return status;
}
