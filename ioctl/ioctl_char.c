#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/device.h>
#include "simple_char.h"

#define DEVICE_NAME "simple_char"

struct simple_dev {
    char *device_buffer; 
    int buffer_len;      
    int max_buffer_size;
    struct cdev cdev;
};

static struct simple_dev dev;
static dev_t dev_num;
static struct class *class;

static int simple_open(struct inode *inode, struct file *file)
{
    file->private_data = &dev;
    printk(KERN_INFO "simple_char: пристрій відкрито\n");
    return 0;
}

static int simple_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "simple_char: пристрій закрито\n");
    return 0;
}

static ssize_t simple_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
    struct simple_dev *dev = file->private_data;

    if (*off >= dev->buffer_len)
        return 0; // Кінець даних

    if (len > dev->buffer_len - *off)
        len = dev->buffer_len - *off;

    if (copy_to_user(buf, dev->device_buffer + *off, len))
        return -EFAULT;

    *off += len;
    printk(KERN_INFO "simple_char: прочитано %zu байтів\n", len);
    return len;
}

static ssize_t simple_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
    struct simple_dev *dev = file->private_data;

    if (*off >= dev->max_buffer_size)
        return -ENOSPC; 

    if (len > dev->max_buffer_size - *off)
        len = dev->max_buffer_size - *off;

    if (copy_from_user(dev->device_buffer + *off, buf, len))
        return -EFAULT;

    if (*off + len > dev->buffer_len)
        dev->buffer_len = *off + len;

    *off += len;
    printk(KERN_INFO "simple_char: записано %zu байтів\n", len);
    return len;
}


static long simple_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct simple_dev *dev = file->private_data;
    int err = 0, tmp;
    int retval = 0;

    if (_IOC_TYPE(cmd) != SIMPLE_CHAR_IOC_MAGIC)
        return -ENOTTY;
    if (_IOC_NR(cmd) > SIMPLE_CHAR_IOC_MAXNR)
        return -ENOTTY;

    if (_IOC_DIR(cmd) & _IOC_READ)
        err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
    else if (_IOC_DIR(cmd) & _IOC_WRITE)
        err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
    if (err)
        return -EFAULT;

    switch (cmd) {
    case SIMPLE_CHAR_CLEAR:
        dev->buffer_len = 0;
        memset(dev->device_buffer, 0, dev->max_buffer_size);
        printk(KERN_INFO "simple_char: буфер очищено\n");
        break;

    case SIMPLE_CHAR_SET_SIZE:
        if (!capable(CAP_SYS_ADMIN))
            return -EPERM;
        if (get_user(tmp, (int __user *)arg))
            return -EFAULT;
        if (tmp <= 0 || tmp > 4096)
            return -EINVAL;
        dev->max_buffer_size = tmp;
        dev->device_buffer = krealloc(dev->device_buffer, tmp, GFP_KERNEL);
        if (!dev->device_buffer)
            return -ENOMEM;
        if (dev->buffer_len > tmp)
            dev->buffer_len = tmp;
        printk(KERN_INFO "simple_char: встановлено розмір буфера %d\n", dev->max_buffer_size);
        break;

    case SIMPLE_CHAR_GET_SIZE:
        if (put_user(dev->max_buffer_size, (int __user *)arg))
            return -EFAULT;
        printk(KERN_INFO "simple_char: повернуто розмір буфера %d\n", dev->max_buffer_size);
        break;

    case SIMPLE_CHAR_TELL_SIZE:
        if (!capable(CAP_SYS_ADMIN))
            return -EPERM;
        if (arg <= 0 || arg > 4096)
            return -EINVAL;
        dev->max_buffer_size = arg;
        dev->device_buffer = krealloc(dev->device_buffer, arg, GFP_KERNEL);
        if (!dev->device_buffer)
            return -ENOMEM;
        if (dev->buffer_len > arg)
            dev->buffer_len = arg;
        printk(KERN_INFO "simple_char: встановлено розмір буфера %ld (через значення)\n", arg);
        break;

    case SIMPLE_CHAR_EXCHANGE_SIZE:
        if (!capable(CAP_SYS_ADMIN))
            return -EPERM;
        tmp = dev->max_buffer_size;
        if (get_user(dev->max_buffer_size, (int __user *)arg))
            return -EFAULT;
        if (dev->max_buffer_size <= 0 || dev->max_buffer_size > 4096)
            return -EINVAL;
        dev->device_buffer = krealloc(dev->device_buffer, dev->max_buffer_size, GFP_KERNEL);
        if (!dev->device_buffer)
            return -ENOMEM;
        if (dev->buffer_len > dev->max_buffer_size)
            dev->buffer_len = dev->max_buffer_size;
        if (put_user(tmp, (int __user *)arg))
            return -EFAULT;
        printk(KERN_INFO "simple_char: обмінено розмір буфера: старе %d, нове %d\n", tmp, dev->max_buffer_size);
        break;

    default:
        return -ENOTTY;
    }

    return retval;
}

static struct file_operations simple_fops = {
    .owner = THIS_MODULE,
    .open = simple_open,
    .release = simple_release,
    .read = simple_read,
    .write = simple_write,
    .unlocked_ioctl = simple_ioctl,
};

static int __init simple_init(void)
{
    int ret;

    dev.device_buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!dev.device_buffer)
        return -ENOMEM;
    dev.max_buffer_size = BUFFER_SIZE;
    dev.buffer_len = 0;

    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        kfree(dev.device_buffer);
        return ret;
    }

    cdev_init(&dev.cdev, &simple_fops);
    dev.cdev.owner = THIS_MODULE;
    ret = cdev_add(&dev.cdev, dev_num, 1);
    if (ret < 0)
        goto err_chrdev;

    class = class_create(DEVICE_NAME);
    if (IS_ERR(class)) {
        ret = PTR_ERR(class);
        goto err_cdev;
    }


    device_create(class, NULL, dev_num, NULL, DEVICE_NAME);
    printk(KERN_INFO "simple_char: ініціалізовано\n");
    return 0;

err_cdev:
    cdev_del(&dev.cdev);
err_chrdev:
    kfree(dev.device_buffer);
    unregister_chrdev_region(dev_num, 1);
    return ret;
}

static void __exit simple_exit(void)
{
    device_destroy(class, dev_num);
    class_destroy(class);
    cdev_del(&dev.cdev);
    kfree(dev.device_buffer);
    unregister_chrdev_region(dev_num, 1);
    printk(KERN_INFO "simple_char: вивантажено\n");
}

module_init(simple_init);
module_exit(simple_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Grok & Bro");
MODULE_DESCRIPTION("Символьний драйвер із розширеним ioctl");