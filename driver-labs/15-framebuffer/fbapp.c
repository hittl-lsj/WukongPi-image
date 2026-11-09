// SPDX-License-Identifier: GPL-2.0-only
/* 查询模式、用 mmap 画彩条、通过 read/write 验证同一块 RAM。 */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static int save_ppm(const char *path, const uint8_t *pixels,
		    const struct fb_fix_screeninfo *fix,
		    const struct fb_var_screeninfo *var)
{
	FILE *file = fopen(path, "wb");
	unsigned int x, y;
	int ret = -1;

	if (!file) {
		perror("fopen PPM");
		return -1;
	}
	if (fprintf(file, "P6\n%u %u\n255\n", var->xres, var->yres) < 0)
		goto out;

	for (y = 0; y < var->yres; y++) {
		for (x = 0; x < var->xres; x++) {
			const uint16_t *pixel = (const uint16_t *)(pixels +
						y * fix->line_length + x * 2);
			uint8_t rgb[3] = {
				((*pixel >> 11) & 31) * 255 / 31,
				((*pixel >> 5) & 63) * 255 / 63,
				(*pixel & 31) * 255 / 31,
			};

			if (fwrite(rgb, 1, sizeof(rgb), file) != sizeof(rgb))
				goto out;
		}
	}
	ret = 0;
out:
	if (fclose(file))
		ret = -1;
	if (ret)
		fprintf(stderr, "Failed to write PPM file\n");
	return ret;
}

int main(int argc, char *argv[])
{
	const uint16_t colors[] = { 0xf800, 0x07e0, 0x001f };
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	uint16_t row[320], pixel = 0xffff;
	uint8_t *pixels;
	unsigned int x, y;
	ssize_t count;
	int fd, status = 1;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "Usage: %s /dev/fbX [output.ppm]\n", argv[0]);
		return 1;
	}
	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		perror("open framebuffer");
		return 1;
	}
	if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0 ||
	    ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
		perror("query framebuffer");
		goto out_close;
	}
	/* 只写本课设备，避免编号不同而改写其他显示设备。 */
	if (strncmp(fix.id, "ramfb-lab", sizeof(fix.id)) ||
	    fix.type != FB_TYPE_PACKED_PIXELS ||
	    fix.visual != FB_VISUAL_TRUECOLOR ||
	    var.xres != 320 || var.yres != 240 || var.bits_per_pixel != 16 ||
	    var.xoffset || var.yoffset || fix.line_length != sizeof(row) ||
	    fix.smem_len != sizeof(row) * var.yres ||
	    var.red.offset != 11 || var.red.length != 5 ||
	    var.green.offset != 5 || var.green.length != 6 ||
	    var.blue.offset || var.blue.length != 5) {
		fprintf(stderr, "Expected this lesson's 320x240 RGB565 ramfb-lab\n");
		goto out_close;
	}
	printf("%.16s: %ux%u, %u bpp, stride=%u, bytes=%u\n", fix.id,
	       var.xres, var.yres, var.bits_per_pixel, fix.line_length,
	       fix.smem_len);
	pixels = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE,
		      MAP_SHARED, fd, 0);
	if (pixels == MAP_FAILED) {
		perror("mmap framebuffer");
		goto out_close;
	}

	/* 三根竖向彩条：行偏移用查询到的 line_length，不能假设无填充。 */
	for (x = 0; x < var.xres; x++)
		row[x] = colors[x * 3 / var.xres];
	for (y = 0; y < var.yres; y++)
		memcpy(pixels + y * fix.line_length, row, sizeof(row));

	/* mmap 写入后，read 应读到同一块 RAM；校验每一行。 */
	for (y = 0; y < var.yres; y++) {
		do {
			count = read(fd, row, sizeof(row));
		} while (count < 0 && errno == EINTR);
		if (count != (ssize_t)sizeof(row) ||
		    memcmp(row, pixels + y * fix.line_length, sizeof(row))) {
			fprintf(stderr, "read/mmap mismatch at row %u\n", y);
			goto out_unmap;
		}
	}
	puts("mmap -> read: all rows match");

	/* 反过来，write 写第一个像素，mmap 也必须立即读到该值。 */
	if (lseek(fd, 0, SEEK_SET) < 0 ||
	    write(fd, &pixel, sizeof(pixel)) != (ssize_t)sizeof(pixel) ||
	    *(uint16_t *)pixels != pixel) {
		fprintf(stderr, "write/mmap mismatch\n");
		goto out_unmap;
	}
	puts("write -> mmap: first pixel is white (0xffff)");
	if (argc == 3) {
		if (save_ppm(argv[2], pixels, &fix, &var))
			goto out_unmap;
		printf("Saved %s (RGB bars with one white pixel)\n", argv[2]);
	}
	status = 0;
out_unmap:
	munmap(pixels, fix.smem_len);
out_close:
	close(fd);
	return status;
}
