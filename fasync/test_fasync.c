#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

#define DEVICE_NAME "/dev/simple_char"

int fd;

void sigio_handler(int sig, siginfo_t *info, void *ucontext)
{
    printf("SIGIO від fd=%d, si_code=%d\n", info->si_fd, info->si_code);

    if (info->si_code == POLL_IN)
        printf("Є дані для читання\n");
    else if (info->si_code == POLL_OUT)
        printf("Можна писати\n");
    else
        printf("Інший код: %d\n", info->si_code);
}

int main()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigio_handler;
    sa.sa_flags = SA_SIGINFO;

    sigemptyset(&sa.sa_mask);
    sigaction(SIGIO, &sa, NULL);

    fd = open(DEVICE_NAME, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    fcntl(fd, F_SETOWN, getpid());                    // хто отримує SIGIO
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_ASYNC); // вмикаємо async

    printf("Чекаємо сигнали...\n");

    while (1)
        pause(); // чекаємо сигналів

    return 0;
}
