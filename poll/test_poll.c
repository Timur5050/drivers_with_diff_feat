#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <errno.h>

#define DEVICE_NAME "/dev/simple_char"

int main() {
    int fd = open(DEVICE_NAME, O_RDWR);
    if (fd < 0) {
        perror("Не вдалося відкрити пристрій");
        return 1;
    }

    struct pollfd fds[1];
    fds[0].fd = fd;
    fds[0].events = POLLIN | POLLOUT;

    char buf[100];
    
    printf("\n[Тест 1] poll з timeout (2 секунди), буфер порожній...\n");
    int ret = poll(fds, 1, 2000);
    if (ret == 0) {
        printf("-> [poll] Таймаут: дані не готові\n");
    } else if (ret > 0) {
        if (fds[0].revents & POLLIN)
            printf("-> [poll] Можна читати (POLLIN)\n");
        if (fds[0].revents & POLLOUT)
            printf("-> [poll] Можна писати (POLLOUT)\n");
    } else {
        perror("poll");
    }

    printf("\n[Тест 2] Пишемо, якщо можна...\n");
    strcpy(buf, "Привіт з poll!");

    fds[0].events = POLLOUT;
    ret = poll(fds, 1, 1000);
    if (ret > 0 && (fds[0].revents & POLLOUT)) {
        int written = write(fd, buf, strlen(buf));
        if (written > 0)
            printf("-> Записано: %s\n", buf);
        else
            perror("write");
    } else {
        printf("-> Нельзя писати, POLLOUT не спрацював\n");
    }

    printf("\n[Тест 3] Читаємо, якщо можна...\n");
    fds[0].events = POLLIN;
    ret = poll(fds, 1, 1000);
    if (ret > 0 && (fds[0].revents & POLLIN)) {
        memset(buf, 0, sizeof(buf));
        int read_bytes = read(fd, buf, sizeof(buf));
        if (read_bytes > 0)
            printf("-> Прочитано: %s\n", buf);
        else
            perror("read");
    } else {
        printf("-> Немає даних для читання (POLLIN не спрацював)\n");
    }

    printf("\n[Тест 4] Знову poll без запису, перевірка порожнього буфера...\n");
    fds[0].events = POLLIN;
    ret = poll(fds, 1, 1000);
    if (ret > 0 && (fds[0].revents & POLLIN)) {
        printf("-> [poll] Все ще можна читати — ймовірно не дочитали все\n");
    } else {
        printf("-> [poll] Буфер порожній — читати не можна\n");
    }

    close(fd);
    return 0;
}
