// SPDX-License-Identifier: GPL-2.0-only
/* 第一课：用 RAM 模拟固定分辨率的 framebuffer，不操作显示硬件。 */
#include <linux/fb.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#define RAMFB_WIDTH	320U
#define RAMFB_HEIGHT	240U
#define RAMFB_BPP	16U
#define RAMFB_STRIDE	(RAMFB_WIDTH * (RAMFB_BPP / 8))
#define RAMFB_SIZE	(RAMFB_STRIDE * RAMFB_HEIGHT)

static struct fb_info *ramfb;

static ssize_t ramfb_read(struct fb_info *info, char __user *buf,
			 size_t count, loff_t *pos)
{
	/* 普通 RAM 可用通用拷贝函数；它同时处理用户地址、偏移和边界。 */
	return simple_read_from_buffer(buf, count, pos, info->screen_buffer,
				       info->screen_size);
}

static ssize_t ramfb_write(struct fb_info *info, const char __user *buf,
			  size_t count, loff_t *pos)
{
	return simple_write_to_buffer(info->screen_buffer, info->screen_size,
				      pos, buf, count);
}

static int ramfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	u32 activate = var->activate;

	/* 本课不支持改分辨率、平移或改变像素格式，避免显存大小不匹配。 */
	if (var->xres != RAMFB_WIDTH || var->yres != RAMFB_HEIGHT ||
	    var->xres_virtual != RAMFB_WIDTH ||
	    var->yres_virtual != RAMFB_HEIGHT ||
	    var->bits_per_pixel != RAMFB_BPP || var->xoffset || var->yoffset ||
	    var->grayscale || var->nonstd ||
	    var->vmode != FB_VMODE_NONINTERLACED)
		return -EINVAL;

	/* 位域和其余参数规范化为当前固定模式，保留核心层使用的 activate。 */
	*var = info->var;
	var->activate = activate;
	return 0;
}

static int ramfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	unsigned long size = PAGE_ALIGN(info->screen_size);
	unsigned long length = vma->vm_end - vma->vm_start;
	unsigned long offset;

	if (!(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	/* 先检查页偏移，再移位，防止溢出后绕过范围检查。 */
	if (vma->vm_pgoff >= (size >> PAGE_SHIFT))
		return -EINVAL;
	offset = vma->vm_pgoff << PAGE_SHIFT;
	if (length > size - offset)
		return -EINVAL;

	/* vmalloc 的物理页未必连续，不能拿 virt_to_phys() 去映射。 */
	return remap_vmalloc_range(vma, info->screen_buffer, vma->vm_pgoff);
}

static void ramfb_destroy(struct fb_info *info)
{
	vfree(info->screen_buffer);
	framebuffer_release(info);
}

static const struct fb_ops ramfb_ops = {
	.owner = THIS_MODULE,
	.fb_read = ramfb_read,
	.fb_write = ramfb_write,
	.fb_check_var = ramfb_check_var,
	.fb_mmap = ramfb_mmap,
	.fb_destroy = ramfb_destroy,
};

static int __init ramfb_init(void)
{
	struct fb_info *info;// framebuffer 结构体指针
	int ret;

	/* 本课只实现用户接口，尚未实现供 fbcon 使用的绘图回调。 */
	if (IS_ENABLED(CONFIG_FRAMEBUFFER_CONSOLE)) {// 检查是否启用了 framebuffer 控制台
		pr_err("ramfb: this lesson requires CONFIG_FRAMEBUFFER_CONSOLE=n\n");
		return -EOPNOTSUPP;
	}

	info = framebuffer_alloc(0, NULL);
	if (!info)
		return -ENOMEM;

	/* 清零包括最后一页的填充区，避免 mmap 向用户暴露旧内存。 */
	info->screen_buffer = vmalloc_user(PAGE_ALIGN(RAMFB_SIZE));// 分配用户空间内存
	// PAGE_ALIGN 用于内存对齐，确保内存分配的大小是页面大小的整数倍
	// 例如，RAMFB_SIZE = 320 * 240 * 2 = 153600，PAGE_ALIGN(RAMFB_SIZE) = 155648
	// 分配 155648 字节的内核用户可映射内存，用于存储 framebuffer 数据
	if (!info->screen_buffer) {
		framebuffer_release(info);
		return -ENOMEM;
	}

	info->screen_size = RAMFB_SIZE;// 显存大小
	info->flags = FBINFO_VIRTFB;// 虚拟 framebuffer 标志
	// FBINFO_VIRTFB 表示它不对应显卡的物理显存，但仍占用普通 RAM
	info->skip_vt_switch = true;// 跳过 vt 切换
	info->fbops = &ramfb_ops;// 指向 framebuffer 操作函数的指针
	strscpy(info->fix.id, "ramfb-lab", sizeof(info->fix.id));// 固定信息的 ID 字符串
	info->fix.type = FB_TYPE_PACKED_PIXELS;// 像素类型为打包像素/位域
	info->fix.visual = FB_VISUAL_TRUECOLOR;// 真彩色像素
	info->fix.smem_len = RAMFB_SIZE;// 显存大小
	info->fix.line_length = RAMFB_STRIDE;// 行长度
	info->fix.accel = FB_ACCEL_NONE;// 无加速
	/* smem_start 保持 0：这块 vmalloc RAM 没有一个连续的显存物理地址。 */

	info->var.xres = RAMFB_WIDTH;// 水平分辨率
	info->var.yres = RAMFB_HEIGHT;// 垂直分辨率
	info->var.xres_virtual = RAMFB_WIDTH;// 虚拟水平分辨率
	info->var.yres_virtual = RAMFB_HEIGHT;// 虚拟垂直分辨率
	info->var.bits_per_pixel = RAMFB_BPP;//	 每个像素的位数
	info->var.red.offset = 11;// 红色位偏移量
	info->var.red.length = 5;// 红色位长度
	info->var.green.offset = 5;// 绿色位偏移量
	info->var.green.length = 6;// 绿色位长度
	info->var.blue.length = 5;// 蓝色位长度
	info->var.width = -1;// 物理宽度，-1 表示未知
	info->var.height = -1;// 物理高度，-1 表示未知
	info->var.activate = FB_ACTIVATE_NOW;// 立即激活
	info->var.vmode = FB_VMODE_NONINTERLACED;// 非隔行扫描模式

	/* 全部准备好后才注册；用户可以在注册后立即打开设备。 */
	ret = register_framebuffer(info);
	if (ret) {
		ramfb_destroy(info);
		return ret;
	}

	ramfb = info;
	pr_info("ramfb: /dev/fb%d, %ux%u RGB565, %u bytes (RAM only)\n",
		info->node, RAMFB_WIDTH, RAMFB_HEIGHT, RAMFB_SIZE);
	return 0;
}

static void __exit ramfb_exit(void)
{
	/* 核心放下最后一个 fb_info 引用后调用 fb_destroy，不在这里重复释放。 */
	unregister_framebuffer(ramfb);
	pr_info("ramfb: unloaded\n");
}

module_init(ramfb_init);
module_exit(ramfb_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RAM framebuffer lesson: fixed RGB565 userspace interface");
