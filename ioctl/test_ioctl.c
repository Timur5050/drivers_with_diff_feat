#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>

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

    // Тест 1: Запис даних
    strcpy(buf, "Привіт, це тест!");
    ret = write(fd, buf, strlen(buf));
    if (ret < 0)
        perror("write");
    else
        printf("Записано %d байтів: %s\n", ret, buf);

    // Тест 2: Читання даних
    memset(buf, 0, sizeof(buf));
    ret = read(fd, buf, sizeof(buf));
    if (ret < 0)
        perror("read");
    else
        printf("Прочитано %d байтів: %s\n", ret, buf);

    // Тест 3: Очистка буфера
    ret = ioctl(fd, SIMPLE_CHAR_CLEAR, 0);
    if (ret < 0)
        perror("SIMPLE_CHAR_CLEAR");
    else
        printf("Буфер очищено\n");

    // Тест 4: Встановлення розміру (потрібен root)
    size = 2048;
    ret = ioctl(fd, SIMPLE_CHAR_SET_SIZE, &size);
    if (ret < 0)
        perror("SIMPLE_CHAR_SET_SIZE");
    else
        printf("Встановлено розмір через SET_SIZE: %d\n", size);

    // Тест 5: Отримання розміру
    ret = ioctl(fd, SIMPLE_CHAR_GET_SIZE, &size);
    if (ret < 0)
        perror("SIMPLE_CHAR_GET_SIZE");
    else
        printf("Поточний розмір: %d\n", size);

    // Тест 6: Встановлення через TELL_SIZE (потрібен root)
    ret = ioctl(fd, SIMPLE_CHAR_TELL_SIZE, 1024);
    if (ret < 0)
        perror("SIMPLE_CHAR_TELL_SIZE");
    else
        printf("Встановлено розмір через TELL_SIZE: %d\n", 1024);

    // Тест 7: Обмін розміром (потрібен root)
    size = 512;
    ret = ioctl(fd, SIMPLE_CHAR_EXCHANGE_SIZE, &size);
    if (ret < 0)
        perror("SIMPLE_CHAR_EXCHANGE_SIZE");
    else
        printf("Обміняно: старий розмір %d, новий %d\n", size, 512);

    // Тест 8: Перевірка буфера після очищення
    memset(buf, 0, sizeof(buf));
    ret = read(fd, buf, sizeof(buf));
    if (ret < 0)
        perror("read after clear");
    else
        printf("Прочитано після очищення: %d байтів\n", ret);

    close(fd);
    return 0;
}