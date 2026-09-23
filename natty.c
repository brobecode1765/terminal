#include <stdio.h>
#include <stdlib.h>
#include <pty.h>
#include <sys/select.h>
#include <unisrd.h>
#include <stdbool.h>

static int32_t masterfd
int main(void) {

    if(forkpty(&masterfd, NULL, NULL, NULL) == 0) {
        execlp("/usr/bin/bash", "bash", NULL);
        perror("excplp");
        exit(1);
    }

    bool running = true;

    fd_set fdset;

    while(running) {
        FD_ZERO(&fdset);
        FD_SET(masterfd, &fdset);

        
    }
    printf("Hello, World!\n");
    return EXIT_SUCCESS;
}