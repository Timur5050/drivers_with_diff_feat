#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "simple_char.h"

#define DEVICE_NAME "/dev/simple_char"

int main()
{
    int fd, ret;
    char buf[64];

    fd = open(DEVICE_NAME, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // Тест 1: Спроба запису (тільки root)
    strcpy(buf, "Тест!");
    ret = write(fd, buf, strlen(buf));
    if (ret < 0)
        perror("write (очікується помилка, якщо не root)");
    else
        printf("Записано: %s\n", buf);

    // Тест 2: Читання (дозволено всім)
    memset(buf, 0, sizeof(buf));
    ret = read(fd, buf, sizeof(buf));
    if (ret < 0)
        perror("read");
    else if (ret == 0)
        printf("Буфер порожній\n");
    else
        printf("Прочитано: %s\n", buf);

    // Тест 3: Очищення буфера (тільки група adm)
    ret = ioctl(fd, SIMPLE_CHAR_CLEAR, 0);
    if (ret < 0)
        perror("ioctl CLEAR (очікується помилка, якщо не в групі adm)");
    else
        printf("Буфер очищено\n");

    close(fd);
    return 0;
}