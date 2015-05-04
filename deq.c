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
    fputs("Usage: deq job-id\n", stderr);
    exit(2);
}

int main(int argc, char *argv[])
{
    struct jobcmd deqcmd;
    int fd;

    if (argc < 2)
        usage();

    deqcmd.type = DEQ;
    deqcmd.defpri = 0;
    deqcmd.owner = getuid();
    deqcmd.deqid = atoi(argv[1]);

    if ((fd = open(FIFO_PATH, O_WRONLY)) < 0)
        err(1, FIFO_PATH);

    if (write(fd, &deqcmd, sizeof deqcmd) < 0)
        err(1, "write");

    close(fd);
    return 0;
}
