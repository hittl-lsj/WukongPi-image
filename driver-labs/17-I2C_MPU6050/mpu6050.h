#ifndef __MPU6050_H__
#define __MPU6050_H__

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/timer.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

#define MPU6050_DEV_NAME "mpu6050_dev"

//mpu6050寄存器地址
#define PMPU6050_PWR_MGMT_1 0x6B    //电源管理寄存器1
#define PMPU6050_SMPLRT_DIV 0x19    //采样率寄存器
#define PMPU6050_CONFIG 0x1A        //配置寄存器
#define PMPU6050_GYRO_CONFIG 0x1B   //陀螺仪配置寄存器
#define PMPU6050_ACCEL_CONFIG 0x1C  //加速度计配置寄存器
#define PMPU6050_ACCEL_XOUT_H 0x3B  //加速度计X轴高8位输出寄存器
#define PMPU6050_ACCEL_XOUT_L 0x3C  //加速度计X轴低8位输出寄存器
#define PMPU6050_ACCEL_YOUT_H 0x3D  //加速度计Y轴高8位输出寄存器
#define PMPU6050_ACCEL_YOUT_L 0x3E  //加速度计Y轴低8位输出寄存器
#define PMPU6050_ACCEL_ZOUT_H 0x3F  //加速度计Z轴高8位输出寄存器
#define PMPU6050_ACCEL_ZOUT_L 0x40  //加速度计Z轴低8位输出寄存器
#define PMPU6050_FF_THR 0x1D                   //自由落体阈值寄存器
#define PMPU6050_FF_DUR 0x1E                   //自由落体持续时间寄存器
#define PMPU6050_MOT_DETECT 0x1F    //运动检测寄存器


#define PMPU6050_GYRO_XOUT_H 0x43  //陀螺仪X轴高8位输出寄存器
#define PMPU6050_GYRO_XOUT_L 0x44  //陀螺仪X轴低8位输出寄存器
#define PMPU6050_GYRO_YOUT_H 0x45  //陀螺仪Y轴高8位输出寄存器   
#define PMPU6050_GYRO_YOUT_L 0x46  //陀螺仪Y轴低8位输出寄存器
#define PMPU6050_GYRO_ZOUT_H 0x47  //陀螺仪Z轴高8位输出寄存器
#define PMPU6050_GYRO_ZOUT_L 0x48  //陀螺仪Z轴低8位输出寄存器
#define PMPU6050_PWR_MGMT_2   0x6C //电源管理寄存器2
#define MPU6050_SAMPLE_BYTES    14 //采样字节数量


//mpu6050设备结构体
struct mpu6050_dev
{
    dev_t devt; //设备号
    struct cdev mpu6050_cdev;//字符设备结构体
    struct class *mpu6050_class;//类结构体指针
    struct device *mpu6050_device;//设备结构体指针
    int major; //主设备号
    int minor; //次设备号
    struct i2c_client *client; //私有数据指针
    struct mutex lock;// 互斥锁
};
#endif
