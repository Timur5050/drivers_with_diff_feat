#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#define SIMPLE_CHAR_IOC_MAGIC 'k'
#define SIMPLE_CHAR_IOC_MAXNR 4
#define SIMPLE_CHAR_CLEAR _IO(SIMPLE_CHAR_IOC_MAGIC, 0)
#define SIMPLE_CHAR_SET_SIZE _IOW(SIMPLE_CHAR_IOC_MAGIC, 1, int)
#define SIMPLE_CHAR_GET_SIZE _IOR(SIMPLE_CHAR_IOC_MAGIC, 2, int)
#define SIMPLE_CHAR_TELL_SIZE _IOW(SIMPLE_CHAR_IOC_MAGIC, 3, int)
#define SIMPLE_CHAR_EXCHANGE_SIZE _IOWR(SIMPLE_CHAR_IOC_MAGIC, 4, int)

int main()
{
    int fd, ret, size;
    char buf[100];

    fd = open("/dev/simple_char", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    strcpy(buf, "це тест!");
    ret = write(fd, buf, strlen(buf));
    printf("Записано: %d байт\n", ret);

    memset(buf, 0, sizeof(buf));
    ret = read(fd, buf, sizeof(buf));
    printf("Прочитано: %d байт, дані: %s\n", ret, buf);

    printf("Спроба читати з порожнього буфера (засне)...\n");
    ret = read(fd, buf, sizeof(buf));
    if (ret < 0)
        printf("Прочитано: %d байт\n", ret);
    else
        perror("read");

    int fd_nonblock = open("/dev/simple_char", O_RDWR | O_NONBLOCK);
    if (fd_nonblock < 0) {
        perror("open nonblock");
        close(fd);
        return 1;
    }
    ret = read(fd_nonblock, buf, sizeof(buf));
    if (ret < 0 && errno == EAGAIN)
        printf("Неблокуюче читання: EAGAIN, як і треба\n");
    else
        printf("Неблокуюче читання: %d байт\n", ret);
    close(fd_nonblock);

    ret = ioctl(fd, SIMPLE_CHAR_CLEAR, 0);
    if (ret)
        perror("SIMPLE_CHAR_CLEAR");
    else
        printf("Буфер очищено\n");

    size = 2048;
    ret = ioctl(fd, SIMPLE_CHAR_SET_SIZE, &size);
    if (ret)
        perror("SIMPLE_CHAR_SET_SIZE");
    else
        printf("Встановлено розмір: %d\n", size);


    ret = ioctl(fd, SIMPLE_CHAR_GET_SIZE, &size);
    if (ret)
        perror("SIMPLE_CHAR_GET_SIZE");
    else
        printf("Поточний розмір: %d\n", size);

    ret = ioctl(fd, SIMPLE_CHAR_TELL_SIZE, 1024);
    if (ret)
        perror("SIMPLE_CHAR_TELL_SIZE");
    else
        printf("Встановлено розмір через TELL_SIZE: %d\n", 1024);

    size = 512;
    ret = ioctl(fd, SIMPLE_CHAR_EXCHANGE_SIZE, &size);
    if (ret)
        perror("SIMPLE_CHAR_EXCHANGE_SIZE");
    else
        printf("Обміняно: старий=%d, новий=%d\n", size, 512);

    close(fd);
    return 0;
}