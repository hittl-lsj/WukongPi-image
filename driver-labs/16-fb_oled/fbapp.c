// SPDX-License-Identifier: GPL-2.0-only
/*
 * SSD1306 framebuffer 用户程序。
 *
 * 本程序通过 mmap 修改 128x64、1bpp 的 framebuffer RAM。
 * 驱动的 deferred-I/O 回调会在延迟到期后通过 I2C 刷新 OLED。
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_STRIDE (OLED_WIDTH / 8)
#define OLED_FB_SIZE (OLED_STRIDE * OLED_HEIGHT)

/*
 * 用户态 framebuffer 的布局必须和内核驱动约定一致：
 * 每行 16 字节，每个字节从最高位到最低位表示从左到右的 8 个像素。
 */
static void set_pixel(uint8_t *fb, unsigned int x, unsigned int y, int on)
{
	size_t offset;
	uint8_t mask;

	if (x >= OLED_WIDTH || y >= OLED_HEIGHT)
		return;

	offset = y * OLED_STRIDE + x / 8;
	mask = (uint8_t)(0x80U >> (x % 8));

	if (on)
		fb[offset] |= mask;
	else
		fb[offset] &= (uint8_t)~mask;
}

struct glyph5x7 {
	char character;
	uint8_t columns[5];
};

/* 每字节表示一列，bit 0 是字符最上面一行。 */
static const struct glyph5x7 font[] = {
	{ ' ', { 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ '3', { 0x41, 0x49, 0x49, 0x49, 0x36 } },
	{ 'D', { 0x7f, 0x41, 0x41, 0x41, 0x3e } },
	{ 'E', { 0x7f, 0x49, 0x49, 0x49, 0x49 } },
	{ 'H', { 0x7f, 0x08, 0x08, 0x08, 0x7f } },
	{ 'L', { 0x7f, 0x40, 0x40, 0x40, 0x40 } },
	{ 'O', { 0x3e, 0x41, 0x41, 0x41, 0x3e } },
};

static const uint8_t *find_glyph(char character)
{
	size_t i;

	for (i = 0; i < sizeof(font) / sizeof(font[0]); i++) {
		if (font[i].character == character)
			return font[i].columns;
	}

	return font[0].columns;
}

static void draw_char(uint8_t *fb, unsigned int x, unsigned int y,
		      char character)
{
	const uint8_t *columns = find_glyph(character);
	unsigned int col, row;

	/* 6x8 cell: 5x7 glyph plus one blank spacing column. */
	for (col = 0; col < 6; col++) {
		for (row = 0; row < 8; row++) {
			int on = col < 5 && row < 7 &&
				(columns[col] & (1U << row));

			set_pixel(fb, x + col, y + row, on);
		}
	}
}

static void draw_text(uint8_t *fb, unsigned int x, unsigned int y,
		      const char *text)
{
	while (*text) {
		draw_char(fb, x, y, *text++);
		x += 6;
	}
}

static void draw_frame(uint8_t *fb)
{
	unsigned int x, y;

	memset(fb, 0, OLED_FB_SIZE);

	/* 外框：确认四个边界坐标都能正确显示。 */
	for (x = 0; x < OLED_WIDTH; x++) {
		set_pixel(fb, x, 0, 1);// 上边界
		set_pixel(fb, x, OLED_HEIGHT - 1, 1);// 下边界
	}
	for (y = 0; y < OLED_HEIGHT; y++) {
		set_pixel(fb, 0, y, 1);// 左边界
		set_pixel(fb, OLED_WIDTH - 1, y, 1);// 右边界
	}

	/* 两条对角线：确认 x/y 坐标方向。 */
	for (x = 0; x < OLED_WIDTH; x++) {
		y = x * (OLED_HEIGHT - 1) / (OLED_WIDTH - 1);// 计算对角线上的像素坐标
		set_pixel(fb, x, y, 1);// 绘制对角线上的像素
		set_pixel(fb, x, OLED_HEIGHT - 1 - y, 1);// 绘制对角线上的像素
	}

	/* 中央棋盘格：确认字节内的位顺序和行间步长。 */
	for (y = 16; y < 48; y++) {
		for (x = 16; x < 112; x++) {
			if (((x / 8) + (y / 8)) % 2 == 0)// 检查是否为偶数行或偶数列
				set_pixel(fb, x, y, 1);// 绘制像素
		}
	}

	/* 7 个 6 像素宽字符共 42 像素，水平居中显示。 */
	draw_text(fb, (OLED_WIDTH - 7 * 6) / 2, 4, "H3 OLED");// 绘制文本
}

/**
 * @main: 主函数
 * @argc: 命令行参数数量
 * @argv: 命令行参数数组

 * @return: 0 成功，其他值 失败
 */
int main(int argc, char **argv)
{
	struct fb_fix_screeninfo fix;// 固定屏幕信息
	struct fb_var_screeninfo var;// 变量屏幕信息
	uint8_t *framebuffer;
	const char *device = "/dev/fb0";
	int fd;

	if (argc == 2)// 传入设备路径
		device = argv[1];
	else if (argc != 1) {
		fprintf(stderr, "Usage: %s [/dev/fb0]\n", argv[0]);
		return 1;
	}

	fd = open(device, O_RDWR);
	if (fd < 0) {
		perror("open framebuffer");
		return 1;
	}

	if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0 ||// 获取固定屏幕信息
	    ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0)// 获取变量屏幕信息
		perror("query framebuffer");
		close(fd);
		return 1;
	}

	printf("%-.16s: %ux%u, %u bpp, stride=%u, bytes=%u\n",
	       fix.id, var.xres, var.yres, var.bits_per_pixel,
	       fix.line_length, fix.smem_len);

	/* 拒绝写入其他格式的 framebuffer，避免静默显示错误。 */
	if (strncmp(fix.id, "wukong-ssd1306", sizeof(fix.id)) != 0 ||// 检查固定屏幕信息是否匹配
	    fix.type != FB_TYPE_PACKED_PIXELS ||// 检查固定屏幕信息是否匹配
	    fix.visual != FB_VISUAL_MONO10 ||// 检查固定屏幕信息是否匹配
	    var.xres != OLED_WIDTH || var.yres != OLED_HEIGHT ||// 检查变量屏幕信息是否匹配
	    var.bits_per_pixel != 1 || fix.line_length != OLED_STRIDE ||// 检查固定屏幕信息是否匹配
	    fix.smem_len != OLED_FB_SIZE) {
		fprintf(stderr, "unexpected framebuffer format\n");
		close(fd);
		return 1;
	}

	framebuffer = mmap(NULL, OLED_FB_SIZE, PROT_READ | PROT_WRITE,
			   MAP_SHARED, fd, 0);
	if (framebuffer == MAP_FAILED) {
		perror("mmap framebuffer");
		close(fd);
		return 1;
	}

	/*
	 * 这里只修改映射到用户空间的 RAM，没有调用 write()。
	 * 第一次写页面会触发 page_mkwrite，驱动约 50 ms 后刷新 OLED。
	 */
	draw_frame(framebuffer);
	puts("mmap image modified; waiting for deferred refresh");
	usleep(200000);

	/* fsync 强制等待仍未执行的延迟刷新，便于确定性测试。 */
	if (fsync(fd) < 0) {
		perror("fsync framebuffer");
		munmap(framebuffer, OLED_FB_SIZE);
		close(fd);
		return 1;
	}

	munmap(framebuffer, OLED_FB_SIZE);
	puts("deferred framebuffer refresh completed");
	close(fd);
	return 0;
}
