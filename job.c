#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include "job.h"

int fifo;
int cont;

struct waitqueue *head, *next, *current;

/* 调度程序 */
void schedule(void)
{
    struct jobinfo *newjob=NULL;
    struct jobcmd cmd;
    int count = 0;
    bzero(&cmd, sizeof cmd);
    if ((count = read(fifo, &cmd, sizeof cmd)) < 0)
        err(1, "read");
#ifdef DEBUG

    if (count) {
        printf("cmd cmdtype\t%d\n"
               "cmd defpri\t%d\n"
               "cmd cmdline\t%s\n",
               cmd.type, cmd.defpri, cmd.cmdline);
    } else {
        printf("no data read\n");
    }
#endif

    /* 更新等待队列中的作业 */
    updateall();

    switch (cmd.type) {
    case ENQ:
        do_enq(newjob, &cmd);
        break;
    case DEQ:
        do_deq(&cmd);
        break;
    case STAT:
        do_stat(&cmd);
    }

    /* 选择高优先级作业 */
    next = jobselect();
    /* 作业切换 */
    jobswitch();
}

int allocjid(void)
{
    static jobid;
    return ++jobid;
}

void updateall(void)
{
    struct waitqueue *p;

    /* 更新作业运行时间 */
    if (current)
        current->job->run_time += 1; /* 加1代表1000ms */

    /* 更新作业等待时间及优先级 */
    for (p = head; p; p = p->next) {
        p->job->wait_time += 1000;
        if (p->job->wait_time >= 5000 && p->job->curpri < 3) {
            p->job->curpri++;
            p->job->wait_time = 0;
        }
    }
}

struct waitqueue *
jobselect(void)
{
    struct waitqueue *p,*prev,*select,*selectprev;
    int highest = -1;

    select = NULL;
    selectprev = NULL;
    if (head) {
        /* 遍历等待队列中的作业，找到优先级最高的作业 */
        for (prev = head, p = head; p; prev = p,p = p->next)
            if (p->job->curpri > highest) {
                select = p;
                selectprev = prev;
                highest = p->job->curpri;
            }
        selectprev->next = select->next;
        if (select == selectprev)
            head = NULL;
    }
    return select;
}

void jobswitch(void)
{
    struct waitqueue *p;
    int i;

    if (current && current->job->state == DONE) { /* 当前作业完成 */
        /* 作业完成，删除它 */
        free(current->job);
        free(current);
        current = NULL;
    }

    if (next) {
        if (!current) { /* 开始新的作业 */
            printf("begin start new job\n");
            current = next;
            next = NULL;
            current->job->state = RUNNING;
            kill(current->job->pid, SIGCONT);
        } else { /* 切换作业 */
            printf("switch to Pid: %d\n",next->job->pid);
            kill(current->job->pid,SIGSTOP);
            current->job->curpri = current->job->defpri;
            current->job->wait_time = 0;
            current->job->state = READY;
            /* 放回等待队列 */
            if (head) {
                for (p = head; p->next; p = p->next);
                p->next = current;
            } else {
                head = current;
            }
            current = next;
            next = NULL;
            current->job->state = RUNNING;
            current->job->wait_time = 0;
            kill(current->job->pid,SIGCONT);
        }
    }
}

void sig(int sig)
{
    int status;
    int ret;

    switch (sig) {
    case SIGALRM: /* 到达计时器所设置的计时间隔 */
        schedule();
        break;
    case SIGCHLD: /* 子进程结束时传送给父进程的信号 */
        /* is this correct?? (without WUNTRACED) */
        ret = waitpid(-1,&status,WNOHANG);
        if (ret) {
            if (WIFEXITED(status)) {
                current->job->state = DONE;
                printf("exited (%d)\n", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                printf("signaled (%s)\n",
                       sys_siglist[WTERMSIG(status)]);
            } else if (WIFSTOPPED(status)) {
                printf("stopped (%s)\n",
                       sys_siglist[WSTOPSIG(status)]);
            }
        }
    }
    cont = 1;
}

void do_enq(struct jobinfo *newjob, struct jobcmd *enqcmd)
{
    struct waitqueue *newnode, *p;
    int i=0, pid;
    char **arglist;
    sigset_t zeromask;

    sigemptyset(&zeromask);

    /* 封装jobinfo数据结构 */
    newjob = malloc(sizeof *newjob);
    newjob->jid = allocjid();
    newjob->defpri = enqcmd->defpri;
    newjob->curpri = enqcmd->defpri;
    newjob->ownerid = enqcmd->owner;
    newjob->state = READY;
    newjob->create_time = time(NULL);
    newjob->wait_time = 0;
    newjob->run_time = 0;
    newjob->cmdline = enqcmd->cmdline;

    /*向等待队列中增加新的作业*/
    newnode = malloc(sizeof *newnode);
    newnode->next =NULL;
    newnode->job = newjob;

    if (head) {
        for (p=head;p->next != NULL; p=p->next);
        p->next = newnode;
    } else {
        head = newnode;
    }

    /*为作业创建进程*/
    if ((pid=fork())<0)
        err(1, "fork");

    if (!pid) {
        /* child process */
        newjob->pid = getpid();
        /* wait for command */
        raise(SIGSTOP);

        /* 执行命令 */
        execl("/bin/sh", "sh", "-c", newjob->cmdline);
        err(1, "exec");
    } else {
        newjob->pid = pid;
    }
}

void do_deq(struct jobcmd *deqcmd)
{
    int deqid, i;
    struct waitqueue *p, *prev, *select, *selectprev;
    deqid = deqcmd->deqid;

#ifdef DEBUG
    printf("deq jid %d\n", deqid);
#endif

    /*current jodid==deqid,终止当前作业*/
    if (current && current->job->jid == deqid) {
        printf("terminate current job\n");
        kill(current->job->pid, SIGKILL);
        free(current->job);
        free(current);
        current=NULL;
    }
    else { /* 或者在等待队列中查找deqid */
        select=NULL;
        selectprev=NULL;
        if (head) {
            for (prev=head,p=head;p!=NULL;prev=p,p=p->next)
                if (p->job->jid==deqid) {
                    select=p;
                    selectprev=prev;
                    break;
                }
            selectprev->next=select->next;
            if (select==selectprev)
                head=NULL;
        }
        if (select) {
            free(select->job);
            free(select);
            select=NULL;
        }
    }
}

static void
print_job(struct jobinfo *job, const char *state)
{
    /*
     *打印所有作业的统计信息:
     *1.作业ID
     *2.进程ID
     *3.作业所有者
     *4.作业运行时间
     *5.作业等待时间
     *6.作业创建时间
     *7.作业状态
     */
    char *time = ctime(&job->create_time);
    printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
           job->jid,
           job->pid,
           job->ownerid,
           job->run_time,
           job->wait_time,
           time,
           state);
}

void do_stat(struct jobcmd *statcmd)
{
    struct waitqueue *p;

    /* print header */
    printf("JOB\tPID\tOWNER\tRUN\tWAIT\tCREATE\tSTATE\n");

    if (current)
        print_job(current->job, "RUNNING");

    for (p=head; p; p=p->next)
        print_job(p->job, "READY");
}

int main()
{
    struct timeval interval;
    struct itimerval it;
    struct stat statbuf;
    struct sigaction newact;

    /* FIXME check then act... */
    if (stat(FIFO_PATH,&statbuf)==0) {
        /* 如果FIFO文件存在,删掉 */
        if (remove(FIFO_PATH)<0)
            err(1, "remove");
    }

    if (mkfifo(FIFO_PATH,0666) < 0)
        err(1, "mkfifo");
    /* 在非阻塞模式下打开FIFO */
    if ((fifo = open(FIFO_PATH,O_RDONLY|O_NONBLOCK)) < 0)
        err(1, "open");

    /* 建立信号处理函数 */
    signal(SIGCHLD, sig);
    signal(SIGALRM, sig);

    /* 设置时间间隔为1000毫秒 */
    interval.tv_sec = 1;
    interval.tv_usec = 0;

    it.it_interval = interval;
    it.it_value = interval;
    setitimer(ITIMER_REAL, &it, 0);

    for (;;) {
        pause();
        if (!cont)
            break;
        cont = 0;
    }

    return 0;
}
