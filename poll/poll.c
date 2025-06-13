#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/device.h>
#include "simple_char.h"

struct simple_dev {
    char *buffer;           // Кільцевий буфер
    int buffer_size;        // Максимальний розмір
    int data_size;          // Поточна кількість даних
    int read_pos, write_pos;// Позиції читання/запису
    wait_queue_head_t read_queue;
    wait_queue_head_t write_queue;
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

    while (dev->data_size == 0) {
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        if (wait_event_interruptible(dev->read_queue, dev->data_size > 0))
            return -ERESTARTSYS;
    }

    count = min(count, (size_t)dev->data_size);
    if (dev->read_pos + count > dev->buffer_size)
        count = dev->buffer_size - dev->read_pos;

    if (copy_to_user(buf, dev->buffer + dev->read_pos, count))
        return -EFAULT;

    dev->read_pos = (dev->read_pos + count) % dev->buffer_size;
    dev->data_size -= count;

    wake_up_interruptible(&dev->write_queue);
    return count;
}

static ssize_t simple_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    struct simple_dev *dev = file->private_data;

    while (space_free(dev) == 0) {
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        if (wait_event_interruptible(dev->write_queue, space_free(dev) > 0))
            return -ERESTARTSYS;
    }

    count = min(count, (size_t)space_free(dev));
    if (dev->write_pos + count > dev->buffer_size)
        count = dev->buffer_size - dev->write_pos;

    if (copy_from_user(dev->buffer + dev->write_pos, buf, count))
        return -EFAULT;

    dev->write_pos = (dev->write_pos + count) % dev->buffer_size;
    dev->data_size += count;

    wake_up_interruptible(&dev->read_queue);
    return count;
}

static __poll_t simple_poll(struct file *file, struct poll_table_struct *wait)
{
    struct simple_dev *dev = file->private_data;
    __poll_t mask = 0;

    poll_wait(file, &dev->read_queue, wait);
    poll_wait(file, &dev->write_queue, wait);

    if (dev->data_size > 0)
        mask |= POLLIN | POLLRDNORM; // Можна читати
    if (space_free(dev) > 0)
        mask |= POLLOUT | POLLWRNORM; // Можна записати

    return mask;
}

static struct file_operations simple_fops = {
    .owner = THIS_MODULE,
    .open = simple_open,
    .release = simple_release,
    .read = simple_read,
    .write = simple_write,
    .poll = simple_poll,
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
MODULE_DESCRIPTION("Simple char driver with poll support");