#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/ioctl.h>
#include <linux/device.h>
#include "simple_char.h"

#define DEVICE_NAME "simple_char"

struct simple_dev {
    char *buffer;           // Кільцевий буфер
    int buffer_size;        // Максимальний розмір
    int data_size;          // Поточна кількість даних
    int read_pos, write_pos;// Позиції читання/запису
    wait_queue_head_t read_queue;
    wait_queue_head_t write_queue;
    struct semaphore sem;
    struct cdev cdev;
};

static struct simple_dev dev;
static dev_t dev_num;
static struct class *class;

static int space_free(struct simple_dev *dev)
{
    return dev->buffer_size - dev->data_size - 1;
}

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

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    while (dev->data_size == 0) {
        up(&dev->sem);
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        DEFINE_WAIT(wait);
        prepare_to_wait(&dev->read_queue, &wait, TASK_INTERRUPTIBLE);
        if (dev->data_size == 0)
            schedule();
        finish_wait(&dev->read_queue, &wait);
        if (signal_pending(current))
            return -ERESTARTSYS;
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
    }

    count = min(count, (size_t)dev->data_size);
    if (dev->read_pos + count > dev->buffer_size)
        count = dev->buffer_size - dev->read_pos;

    if (copy_to_user(buf, dev->buffer + dev->read_pos, count)) {
        up(&dev->sem);
        return -EFAULT;
    }

    dev->read_pos = (dev->read_pos + count) % dev->buffer_size;
    dev->data_size -= count;

    up(&dev->sem);
    wake_up_interruptible(&dev->write_queue);
    return count;
}

static ssize_t simple_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    struct simple_dev *dev = file->private_data;

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    while (space_free(dev) == 0) {
        up(&dev->sem);
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        DEFINE_WAIT(wait);
        prepare_to_wait_exclusive(&dev->write_queue, &wait, TASK_INTERRUPTIBLE);
        if (space_free(dev) == 0)
            schedule();
        finish_wait(&dev->write_queue, &wait);
        if (signal_pending(current))
            return -ERESTARTSYS;
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
    }

    count = min(count, (size_t)space_free(dev));
    if (dev->write_pos + count > dev->buffer_size)
        count = dev->buffer_size - dev->write_pos;

    if (copy_from_user(dev->buffer + dev->write_pos, buf, count)) {
        up(&dev->sem);
        return -EFAULT;
    }

    dev->write_pos = (dev->write_pos + count) % dev->buffer_size;
    dev->data_size += count;

    up(&dev->sem);
    wake_up_interruptible(&dev->read_queue);
    return count;
}

static long simple_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct simple_dev *dev = file->private_data;
    int tmp, old_size;

    if (_IOC_TYPE(cmd) != SIMPLE_CHAR_IOC_MAGIC || _IOC_NR(cmd) > SIMPLE_CHAR_IOC_MAXNR)
        return -ENOTTY;

    if (_IOC_DIR(cmd) & _IOC_READ && !access_ok((void __user *)arg, _IOC_SIZE(cmd)))
        return -EFAULT;
    if (_IOC_DIR(cmd) & _IOC_WRITE && !access_ok((void __user *)arg, _IOC_SIZE(cmd)))
        return -EFAULT;

    switch (cmd) {
    case SIMPLE_CHAR_CLEAR:
        if (!capable(CAP_SYS_ADMIN))
            return -EPERM;
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
        dev->data_size = 0;
        dev->read_pos = dev->write_pos = 0;
        up(&dev->sem);
        wake_up_interruptible(&dev->read_queue);
        wake_up_interruptible(&dev->write_queue);
        break;

    case SIMPLE_CHAR_SET_SIZE:
    case SIMPLE_CHAR_TELL_SIZE:
    case SIMPLE_CHAR_EXCHANGE_SIZE:
        if (!capable(CAP_SYS_ADMIN))
            return -EPERM;
        if (cmd == SIMPLE_CHAR_SET_SIZE || cmd == SIMPLE_CHAR_EXCHANGE_SIZE) {
            if (get_user(tmp, (int __user *)arg))
                return -EFAULT;
        } else {
            tmp = arg;
        }
        if (tmp <= 0 || tmp > 4096)
            return -EINVAL;
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
        old_size = dev->buffer_size;
        dev->buffer_size = tmp;
        if (dev->data_size > tmp)
            dev->data_size = tmp;
        if (dev->read_pos >= tmp)
            dev->read_pos = 0;
        if (dev->write_pos >= tmp)
            dev->write_pos = 0;
        up(&dev->sem);
        wake_up_interruptible(&dev->write_queue);
        if (cmd == SIMPLE_CHAR_EXCHANGE_SIZE) {
            if (put_user(old_size, (int __user *)arg))
                return -EFAULT;
            printk(KERN_INFO "simple_char: exchanged size: old=%d, new=%d\n", old_size, tmp);
        } else {
            printk(KERN_INFO "simple_char: set size to %d\n", tmp);
        }
        break;

    case SIMPLE_CHAR_GET_SIZE:
        if (put_user(dev->buffer_size, (int __user *)arg))
            return -EFAULT;
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

    dev.buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!dev.buffer)
        return -ENOMEM;
    dev.buffer_size = BUFFER_SIZE;
    dev.data_size = dev.read_pos = dev.write_pos = 0;
    init_waitqueue_head(&dev.read_queue);
    init_waitqueue_head(&dev.write_queue);
    sema_init(&dev.sem, 1);

    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0)
        goto err_buf;

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
    printk(KERN_INFO "simple_char: initialized\n");
    return 0;

err_cdev:
    cdev_del(&dev.cdev);
err_chrdev:
    unregister_chrdev_region(&dev_num, 1);
err_buf:
    kfree(dev.buffer);
    return ret;
}

static void __exit simple_exit(void)
{
    device_destroy(class, dev_num);
    class_destroy(class);
    cdev_del(&dev.cdev);
    unregister_chrdev_region(&dev_num, 1);
    kfree(dev.buffer);
    printk(KERN_INFO "simple_char: unloaded\n");
}

module_init(simple_init);
module_exit(simple_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Grok & Bro");
MODULE_DESCRIPTION("Simple char driver with low-level blocking I/O");