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

int main(int argc,char *argv[])
{
    struct jobcmd statcmd;
    int fd;

    statcmd.type=STAT;
    statcmd.defpri=0;
    statcmd.owner=getuid();

    if((fd=open(FIFO_PATH,O_WRONLY))<0)
        err(1, FIFO_PATH);

    if(write(fd, &statcmd, sizeof statcmd)<0)
        err(1, "write");

    close(fd);
    return 0;
}
