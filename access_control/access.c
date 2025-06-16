#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/ioctl.h>
#include <linux/cred.h>
#include "simple_char.h"

struct simple_dev {
    char buffer[BUFFER_SIZE]; // Буфер
    int data_size;            // Скільки даних
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
        return 0;

    count = min(count, (size_t)dev->data_size);
    if (copy_to_user(buf, dev->buffer, count))
        return -EFAULT;

    dev->data_size -= count;
    return count;
}

static ssize_t simple_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    struct simple_dev *dev = file->private_data;

    // Тільки root може писати
    if (!uid_eq(current->cred->uid, GLOBAL_ROOT_UID))
        return -EPERM;

    if (dev->data_size == BUFFER_SIZE)
        return 0;

    count = min(count, (size_t)(BUFFER_SIZE - dev->data_size));
    if (copy_from_user(dev->buffer + dev->data_size, buf, count))
        return -EFAULT;

    dev->data_size += count;
    return count;
}

static long simple_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct simple_dev *dev = file->private_data;

    if (_IOC_TYPE(cmd) != SIMPLE_CHAR_IOC_MAGIC)
        return -ENOTTY;

    switch (cmd) {
    case SIMPLE_CHAR_CLEAR:
        // Тільки група adm (GID=4) може очищати
        if (!in_group_p(4))
            return -EPERM;
        dev->data_size = 0;
        break;
    default:
        return -ENOTTY;
    }
    return 0;
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
MODULE_DESCRIPTION("Minimal char driver with access control");