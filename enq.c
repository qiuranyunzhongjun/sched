#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include <signal.h>
#include "job.h"

void usage()
{
    fputs("Usage: enq [-p priority] command\n", stderr);
    exit(2);
}

int main(int argc, char *argv[])
{
    int pri = 0;
    int fd;
    char *cmd;
    struct jobcmd enqcmd;

    if (argc < 2)
        usage();

    if (!strcmp(argv[1], "-p")) {
        if (argc < 4)
            usage();
        pri = atoi(argv[2]);
        cmd = argv[3];
    } else {
        cmd = argv[1];
    }

    if (pri < 0 || pri > 3) {
        fputs("priority must be between 0 and 3\n", stderr);
        return 1;
    }

    enqcmd.type = ENQ;
    enqcmd.defpri = pri;
    enqcmd.owner = getuid();

    if ((fd = open(FIFO_PATH, O_WRONLY)) < 0)
        err(1, FIFO_PATH);

    if (write(fd, &enqcmd, sizeof enqcmd) < 0)
        err(1, "write");

    close(fd);
    return 0;
}

