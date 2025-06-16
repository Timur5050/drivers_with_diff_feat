#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/fasync.h>
#include "simple_char.h"

struct simple_dev {
    char buffer[BUFFER_SIZE]; // Простий буфер
    int data_size;            // Скільки даних у буфері
    struct fasync_struct *fasync_queue; // Черга для SIGIO
    struct cdev cdev;
};

static struct simple_dev dev;
static dev_t dev_num;
static struct class *class;

static int simple_open(struct inode *inode, struct file *file)
{
    file->private_data = &dev;
    return 0;
}

static int simple_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t simple_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    struct simple_dev *dev = file->private_data;

    if (dev->data_size == 0)
        return 0; // Немає даних

    count = min(count, (size_t)dev->data_size);
    if (copy_to_user(buf, dev->buffer, count))
        return -EFAULT;

    dev->data_size -= count;
    if (dev->fasync_queue)
        kill_fasync(&dev->fasync_queue, SIGIO, POLL_OUT); // Сповіщаємо, що можна писати
    return count;
}

static ssize_t simple_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    struct simple_dev *dev = file->private_data;

    if (dev->data_size == BUFFER_SIZE)
        return 0; // Буфер повний

    count = min(count, (size_t)(BUFFER_SIZE - dev->data_size));
    if (copy_from_user(dev->buffer + dev->data_size, buf, count))
        return -EFAULT;

    dev->data_size += count;
    if (dev->fasync_queue)
        kill_fasync(&dev->fasync_queue, SIGIO, POLL_IN); // Сповіщаємо, що є дані
    return count;
}

static int simple_fasync(int fd, struct file *file, int on)
{
    struct simple_dev *dev = file->private_data;
    return fasync_helper(fd, file, on, &dev->fasync_queue);
}

static struct file_operations simple_fops = {
    .owner = THIS_MODULE,
    .open = simple_open,
    .release = simple_release,
    .read = simple_read,
    .write = simple_write,
    .fasync = simple_fasync,
};

static int __init simple_init(void)
{
    int ret;

    dev.data_size = 0;
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "simple_char: failed to allocate major\n");
        return ret;
    }

    cdev_init(&dev.cdev, &simple_fops);
    dev.cdev.owner = THIS_MODULE;
    ret = cdev_add(&dev.cdev, dev_num, 1);
    if (ret < 0) {
        unregister_chrdev_region(&dev_num, 1);
        printk(KERN_ERR "simple_char: failed to add cdev\n");
        return ret;
    }

    class = class_create(DEVICE_NAME);
    if (IS_ERR(class)) {
        ret = PTR_ERR(class);
        cdev_del(&dev.cdev);
        unregister_chrdev_region(&dev_num, 1);
        return ret;
    }

    device_create(class, NULL, dev_num, NULL, DEVICE_NAME);
    printk(KERN_INFO "simple_char: initialized\n");
    return 0;
}

static void __exit simple_exit(void)
{
    device_destroy(class, dev_num);
    class_destroy(class);
    cdev_del(&dev.cdev);
    unregister_chrdev_region(&dev_num, 1);
    printk(KERN_INFO "simple_char: unloaded\n");
}

module_init(simple_init);
module_exit(simple_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Grok & Bro");
MODULE_DESCRIPTION("Minimal char driver with async notification");