// SPDX-License-Identifier: GPL-2.0-only
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/fb.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/vmalloc.h>

struct ssd1306fb
{
	struct i2c_client *client; // I2C 客户端
	struct mutex io_lock;	   // 串行化内核写回调和 I2C 刷新
	struct fb_deferred_io defio; // mmap 写入后的延迟刷新状态
};

#define SSD1306_WIDTH 128							// 屏幕宽度
#define SSD1306_HEIGHT 64							// 屏幕高度
#define SSD1306_PAGES (SSD1306_HEIGHT / 8)			// 页数
#define SSD1306_FB_STRIDE ((SSD1306_WIDTH + 7) / 8) /* 每行字节数 */
#define SSD1306_FB_SIZE \
	(SSD1306_FB_STRIDE * SSD1306_HEIGHT) /* 整屏缓冲区字节数 */
#define SSD1306_I2C_CHUNK 16			 // 每次最多发送的字节数
#define SSD1306_REFRESH_DELAY_MS 50	 /* 合并短时间内的多次像素修改 */

static ssize_t ssd1306_fb_read(struct fb_info *info,
							   char __user *buf,
							   size_t count, loff_t *ppos)
{
	struct ssd1306fb *par = info->par;
	ssize_t read_count;

	/* 只与内核 write/刷新串行化；mmap 用户写入不会取得此锁。 */
	mutex_lock(&par->io_lock);

	read_count = simple_read_from_buffer(buf, count, ppos,
										 info->screen_buffer,
										 info->screen_size);

	mutex_unlock(&par->io_lock);

	return read_count;
}

/*
 * SSD1306 I2C 控制字节：
 * 0x00 表示后续字节是命令，0x40 表示后续字节是显示数据。
 * 每次最多发送 16 字节负载，避免依赖适配器支持很长的传输。
 */
static int ssd1306_write(struct i2c_client *client, u8 control,
						 const u8 *data, size_t len)
{
	char packet[1 + SSD1306_I2C_CHUNK];
	size_t chunk;
	int ret;

	while (len)
	{
		chunk = len < SSD1306_I2C_CHUNK ? len : SSD1306_I2C_CHUNK;
		packet[0] = control;
		memcpy(packet + 1, data, chunk);

		ret = i2c_master_send(client, packet, (int)chunk + 1);
		if (ret < 0)
			return ret;
		if (ret != (int)chunk + 1)
			return -EIO;

		data += chunk;
		len -= chunk;
	}

	return 0;
}
static const u8 ssd1306_init_cmds[] = {
	0xAE, /* 显示关闭，初始化期间避免显示旧数据 */
	0xD5,
	0x80, /* 显示时钟分频 */
	0xA8,
	0x3F, /* 复用率：64 行 */
	0xD3,
	0x00, /* 显示偏移 */
	0x40, /* 起始行 */
	0x8D,
	0x14, /* 启用内部电荷泵 */
	0x20,
	0x02, /* 页寻址模式 */
	0xA1, /* 列地址映射 */
	0xC8, /* COM 扫描方向 */
	0xDA,
	0x12, /* 128x64 面板配置 */
	0x81,
	0x7F, /* 对比度 */
	0xD9,
	0xF1, /* 预充电周期 */
	0xDB,
	0x40, /* VCOMH 电平 */
	0xA4, /* 显示 GDDRAM 内容 */
	0xA6, /* 正常显示，非反色 */
};
/*
 * fix 描述不会在运行期间改变的硬件属性。
 */
static const struct fb_fix_screeninfo ssd1306_fix = {
	/* 固定屏幕信息 */
	.id = "wukong-ssd1306",			  // 屏幕 ID// 用于设备树匹配
	.type = FB_TYPE_PACKED_PIXELS,	  // 像素类型// 0 表示每个像素占用一个字节，1 表示每个像素占用一个位
	.visual = FB_VISUAL_MONO10,		  // 颜色深度// 1 位灰度，0 表示黑色，1 表示白色
	.line_length = SSD1306_FB_STRIDE, // 每行字节数/
	.smem_len = SSD1306_FB_SIZE,	  // 显示缓冲区大小/字节
	.accel = FB_ACCEL_NONE,			  // 加速器类型// 0 表示没有加速器
};

/*
 * var 描述分辨率、颜色深度等显示模式。
 */
static const struct fb_var_screeninfo ssd1306_var = {
	.xres = SSD1306_WIDTH,
	.yres = SSD1306_HEIGHT,
	.xres_virtual = SSD1306_WIDTH,	 // 虚拟分辨率
	.yres_virtual = SSD1306_HEIGHT,	 // 虚拟分辨率
	.bits_per_pixel = 1,			 // 每个像素占用 1 位
	.grayscale = 1,					 // 灰度模式
	.red = {.length = 1},			 // 红色通道
	.green = {.length = 1},			 // 绿色通道
	.blue = {.length = 1},			 // 蓝色通道
	.activate = FB_ACTIVATE_NOW,	 // 立即激活
	.vmode = FB_VMODE_NONINTERLACED, // 非隔行扫描
};
/**
 * @ssd1306_flush: 刷新显示缓冲区
 * @client: I2C 客户端
 * @fb: 显示缓冲区
 * @return: 0 成功，其他值 失败
 */
static int ssd1306_flush(struct i2c_client *client, const u8 *fb)
{
	u8 page_data[SSD1306_WIDTH]; // 每页像素数
	u8 position[3];				 // 页地址、列地址、高字节
	u8 display_on = 0xAF;		 // 显示开启
	unsigned int page, x, bit;
	int ret;

	for (page = 0; page < SSD1306_PAGES; page++)
	{
		memset(page_data, 0, sizeof(page_data)); // 初始化每页像素数为 0
		/*
		 * 遍历每页像素数，将显示缓冲区中的像素值转换为二进制值
		 * 每个像素占用 1 位，1 表示点亮，0 表示熄灭
		 * 每个字节表示 8 个像素，每个像素占用 1 位
		 */
		for (x = 0; x < SSD1306_WIDTH; x++)
		{
			for (bit = 0; bit < 8; bit++)
			{
				unsigned int y = page * 8 + bit;
				u8 source = fb[y * SSD1306_FB_STRIDE + x / 8];

				if (source & (0x80U >> (x % 8)))
					page_data[x] |= 1U << bit;
			}
		}
		// 设置页地址、列地址、高字节
		position[0] = 0xB0 | page;// 页地址/页号// 0xB0 表示页寻址模式
		position[1] = 0x00;// 列地址/列号// 0x00 表示列地址映射
		position[2] = 0x10;// 高字节// 0x10 表示高字节
		// 发送高字节
		ret = ssd1306_write(client, 0x00,
							position, sizeof(position));
		if (ret)
			return ret;
		// 发送每页像素数
		ret = ssd1306_write(client, 0x40,
							page_data, sizeof(page_data));
		if (ret)
			return ret;
	}

	return ssd1306_write(client, 0x00, &display_on, 1);
}

/*
 * fb_deferred_io 在 mmap 页面被写入后延迟调用这里。
 * pagereflist 记录哪些 framebuffer 页面变脏；本屏只有 1024 字节，
 * 小于一个内存页，因此教学版直接刷新整屏。
 */
static void ssd1306_deferred_io(struct fb_info *info,
				struct list_head *pagereflist)
{
	struct ssd1306fb *par = info->par;// 获取驱动私有数据指针
	int ret;

	(void)pagereflist;// 忽略未使用的参数

	mutex_lock(&par->io_lock);
	ret = ssd1306_flush(par->client, info->screen_buffer);
	mutex_unlock(&par->io_lock);

	if (ret)
		dev_err_ratelimited(&par->client->dev,// 打印错误日志
				    "deferred framebuffer flush failed: %d\n",
				    ret);
}

static void ssd1306_schedule_refresh(struct fb_info *info)
{
	/*
	 * 如果已有刷新任务等待执行，schedule_delayed_work() 不会再创建
	 * 第二个任务，因此连续的小写入会合并成一次 I2C 整屏刷新。
	 */
	schedule_delayed_work(&info->deferred_work, info->fbdefio->delay);// 刷新显示缓冲区
}

static ssize_t ssd1306_fb_write(struct fb_info *info,
				const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct ssd1306fb *par = info->par;
	ssize_t written;

	mutex_lock(&par->io_lock);
	written = simple_write_to_buffer(info->screen_buffer,// 写入 framebuffer 缓冲区
					 info->screen_size,
					 ppos, buf, count);
	if (written > 0)// 写入成功
		ssd1306_schedule_refresh(info);// 刷新显示缓冲区

	mutex_unlock(&par->io_lock);
	return written;
}

static const struct fb_ops ssd1306_fb_ops = {
	.owner = THIS_MODULE,
	.fb_read = ssd1306_fb_read,
	.fb_write = ssd1306_fb_write,
	.fb_mmap = fb_deferred_io_mmap,
};
static int ssd1306_probe(struct i2c_client *client)
{
	struct fb_info *info;
	struct ssd1306fb *par;
	int ret;
	/**
	 * 检查 I2C 适配器是否支持原始 I2C 功能
	 */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
	{
		dev_err(&client->dev,
				"adapter does not support raw I2C\n");
		return -EOPNOTSUPP;
	}

	/*
	 * 分配 fb_info，并在其尾部同时分配驱动私有数据。
	 * 之后 info->par 就指向 struct ssd1306fb。
	 */
	info = framebuffer_alloc(sizeof(*par), &client->dev);// 分配 fb_info 并在其尾部同时分配驱动私有数据
	if (!info)
		return -ENOMEM;

	par = info->par;// 获取驱动私有数据指针
	par->client = client;// 初始化 I2C 客户端指针
	mutex_init(&par->io_lock);// 初始化互斥锁
	par->defio.delay = msecs_to_jiffies(SSD1306_REFRESH_DELAY_MS);// 设置刷新延迟
	par->defio.deferred_io = ssd1306_deferred_io;// 设置延迟 I/O 函数指针

	/*
	 * 把 framebuffer 的操作函数和显示参数交给 fbdev 核心。
	 */
	info->fbops = &ssd1306_fb_ops;
	info->fix = ssd1306_fix;// 设置固定屏幕信息
	info->var = ssd1306_var;// 设置变量屏幕信息
	info->fbdefio = &par->defio;

	/*
	 * 表示 framebuffer 位于普通系统 RAM。
	 * deferred-I/O 据此按系统内存页面处理 mmap。
	 */
	info->flags = FBINFO_VIRTFB;// 设置虚拟内存标志
	info->screen_size = SSD1306_FB_SIZE;// 设置显示缓冲区大小

	/*
	 * deferred-I/O 需要取得缓冲区背后的 struct page。vzalloc()
	 * 分配的页面可以通过 vmalloc_to_page() 找到并映射到用户空间。
	 */
	info->screen_buffer = vzalloc(SSD1306_FB_SIZE);// 分配显示缓冲区内存
	if (!info->screen_buffer)
	{
		ret = -ENOMEM;
		goto err_release_fb;
	}

	ret = fb_deferred_io_init(info);
	if (ret)
	{
		dev_err(&client->dev,
				"failed to initialize deferred I/O: %d\n", ret);
		goto err_free_buffer;
	}

	msleep(100);

	ret = ssd1306_write(client, 0x00, ssd1306_init_cmds,
						sizeof(ssd1306_init_cmds));
	if (ret)
	{
		dev_err(&client->dev,
				"display init failed: %d\n", ret);
		goto err_cleanup_defio;
	}

	/*
	 * 先在真正的 framebuffer 中生成测试图案。
	 * 0xAA = 10101010，因此会形成竖向黑白条纹。
	 */
	memset(info->screen_buffer, 0xAA, SSD1306_FB_SIZE);

	ret = ssd1306_flush(client, info->screen_buffer);
	if (ret)
	{
		dev_err(&client->dev,
				"initial framebuffer flush failed: %d\n", ret);
		goto err_cleanup_defio;
	}

	/*
	 * 保存 fb_info，remove() 时通过 client 取回来。
	 */
	i2c_set_clientdata(client, info);// 设置客户端数据指针

	/*
	 * 注册成功后，内核才会创建 /dev/fb0。
	 */
	ret = register_framebuffer(info);
	if (ret)
	{
		dev_err(&client->dev,
				"failed to register framebuffer: %d\n", ret);
		i2c_set_clientdata(client, NULL);
		goto err_cleanup_defio;
	}

	dev_info(&client->dev,
			 "fb%d registered: %ux%u, %u bytes\n",
			 info->node,
			 SSD1306_WIDTH,
			 SSD1306_HEIGHT,
			 SSD1306_FB_SIZE);

	return 0;

err_cleanup_defio:
	fb_deferred_io_cleanup(info);

err_free_buffer:
	vfree(info->screen_buffer);

err_release_fb:
	framebuffer_release(info);
	return ret;
}
static void ssd1306_remove(struct i2c_client *client)
{
	struct fb_info *info = i2c_get_clientdata(client);// 获取客户端数据指针
	struct ssd1306fb *par;// 获取驱动私有数据指针
	u8 display_off = 0xAE;// 显示关闭命令

	if (!info)
		return;

	par = info->par;// 获取驱动私有数据指针

	/*
	 * 首先注销设备，阻止用户空间继续通过 /dev/fb0 写入。
	 */
	unregister_framebuffer(info);
	i2c_set_clientdata(client, NULL);// 设置客户端数据指针为 NULL

	/* 先执行并取消所有待处理刷新，避免释放缓冲区后工作队列再访问它。 */
	fb_deferred_io_cleanup(info);// 清理延迟 I/O 状态标志

	mutex_lock(&par->io_lock);
	ssd1306_write(client, 0x00, &display_off, 1);// 发送显示关闭命令
	mutex_unlock(&par->io_lock);

	vfree(info->screen_buffer);// 释放显示缓冲区内存
	framebuffer_release(info);// 释放 framebuffer

	dev_info(&client->dev, "framebuffer removed\n");
}

static const struct of_device_id ssd1306_of_match[] = {
	{.compatible = "wukong,ssd1306-fb"},
	{}};
MODULE_DEVICE_TABLE(of, ssd1306_of_match); // 注册 OF 匹配表
/**
 * @ssd1306_driver: SSD1306 I2C 驱动
 * @name: 设备名称
 * @of_match_table: OF 匹配表
 * @probe: 探针函数
 * @remove: 移除函数
 * wukong,ssd1306-fb
 */
static struct i2c_driver ssd1306_driver = {
	.driver = {
		.name = "wukong-ssd1306-fb",
		.of_match_table = ssd1306_of_match,
	},
	.probe = ssd1306_probe,
	.remove = ssd1306_remove,
};
module_i2c_driver(ssd1306_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("WuKong Pi SSD1306 framebuffer with deferred I/O");
