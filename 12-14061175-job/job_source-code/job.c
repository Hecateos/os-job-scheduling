#include <stdio.h>
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

int jobid=0;
int siginfo=1;
int fifo;
int fifo_0;
int globalfd;
int time_temp;

struct waitqueue *head1=NULL,*head2=NULL,*head3=NULL;
struct waitqueue *next=NULL,*current =NULL;

/* 调度程序 */
void scheduler()
{
	struct jobinfo *newjob=NULL;
	struct jobcmd cmd;
	int  count = 0;
	bzero(&cmd,DATALEN);
	if((count=read(fifo,&cmd,DATALEN))<0)
		error_sys("read fifo failed");
#ifdef DEBUG

	if(count){
		printf("cmd cmdtype\t%d\ncmd defpri\t%d\ncmd data\t%s\n",cmd.type,cmd.defpri,cmd.data);
	}
	else
		printf("no data read\n");
#endif

	/* 更新等待队列中的作业 */

	updateall();

	switch(cmd.type){
	case ENQ:
		do_enq(newjob,cmd);
		break;
	case DEQ:
		do_deq(cmd);
		break;
	case STAT:
		do_stat(cmd);
		break;
	default:
		break;
	}

	if(current == NULL){
		/* 选择高优先级作业 */
		time_temp = 0;
		next=jobselect();
		/* 作业切换 */
		jobswitch();
	}
	else{
		if(current->job->state == DONE 
		||
	   	((current->job->curpri == 3 && time_temp >= 1) ||
	   	 (current->job->curpri == 2 && time_temp >= 2) || 
	  	 (current->job->curpri == 1 && time_temp >= 5))
		||
	   	((current->job->curpri == 3) ||
	   	 (current->job->curpri == 2 && head3) ||
		 (current->job->curpri == 1 && (head2 || head3)))
		){
		/* 选择高优先级作业 */
		if(head2 !=NULL){
			printf("heda2 not NULL");		
		}
		time_temp = 0;
		next=jobselect();
		/* 作业切换 */
		jobswitch();
		}
	}
}

int allocjid()
{
	return ++jobid;
}

void updateall()
{
	struct waitqueue *p1,*p2,*p3,*prev1,*prev2,*prev3,*temp;

	/* 更新作业运行时间 */
	if(current){
		current->job->run_time += 1; /* 加1代表1000ms */
		time_temp++;	
	}

	/* 更新作业等待时间及优先级 */
	for(p3 = head3, prev3 = head3; p3 != NULL; prev3=p3,p3 = p3->next){
		p3->job->wait_time += 1000;
	}

	for(p2 = head2,prev2 = head2; p2 != NULL;){
		p2->job->wait_time += 1000;
		if(p2->job->wait_time >= 10000){
			p2->job->curpri++;
			p2->job->wait_time = 0;
			temp = p2;
			if(p2 == head2){
				head2 = p2->next;
				p2 = head2;
				prev2 = head2;
			}
			else{
				prev2->next = p2->next;
				p2 = p2->next;
			}
			temp->next = NULL;
			if(head3 == NULL)
				head3 = temp;
			else {
				prev3->next = temp;
				prev3 = temp;
			}
		}
		else {/*attention!*/
			prev2 = p2; 
			p2 = p2->next;
		}
	}

	for(p1 = head1,prev1 = head1; p1 != NULL;){
		p1->job->wait_time += 1000;
		if(p1->job->wait_time >= 10000){
			p1->job->curpri++;
			p1->job->wait_time = 0;
			temp = p1;
			if(p1 == head1){
				head1 = p1->next;
				p1 = head1;
				prev1 = head1;			
			}
			else{
				prev1->next = p1->next;
				p1 = p1->next;
			}
			temp->next = NULL;
			if(head2 == NULL){
				head2 = temp;}
			else {
				prev2->next = temp;
				prev2 = temp;
			}
		}
		else{
			prev1 = p1;
 			p1 = p1->next;
		}
	}
}

struct waitqueue* jobselect()
{
	struct waitqueue *p,*prev,*select,*selectprev;

	select = NULL;
	selectprev = NULL;
	if(head3){
		if(current == NULL || (current != NULL &&(current->job->state == DONE||current->job->curpri <=3))){
			select = head3;
			head3 = head3->next;
			select->next = NULL;
		}
	}
	else if(head2){
		if(current == NULL || (current != NULL &&(current->job->state == DONE||current->job->curpri <=2))){
			select = head2;
			head2 = head2->next;
			select->next = NULL;
		}
	}
	else if(head1){
		if(current == NULL || (current != NULL &&(current->job->state == DONE||current->job->curpri <=1))){
			select = head1;
			head1 = head1->next;
			select->next = NULL;
		}
	}
	return select;
}

void jobswitch()
{
	struct waitqueue *p;
	int i;

	if(current && current->job->state == DONE){ /* 当前作业完成 */
		/* 作业完成，删除它 */
		for(i = 0;(current->job->cmdarg)[i] != NULL; i++){
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i] = NULL;
		}
		/* 释放空间 */
		free(current->job->cmdarg);
		free(current->job);
		free(current);

		current = NULL;
	}

	if(next == NULL && current == NULL) /* 没有作业要运行 */

		return;
	else if (next != NULL && current == NULL){ /* 开始新的作业 */

		printf("begin start new job\n");
		current = next;
		next = NULL;
		current->job->state = RUNNING;
		kill(current->job->pid,SIGCONT);
		return;
	}
	else if (next != NULL && current != NULL){ /* 切换作业 */

		printf("switch to Pid: %d\n",next->job->pid);
		kill(current->job->pid,SIGSTOP);
		current->job->wait_time = 0;
		current->job->state = READY;

		if(current->job->curpri-1>=current->job->defpri){
			current->job->curpri--;
		}
		/* 放回等待队列 */
		if(current->job->curpri == 3){
			if(head3){
				for(p = head3; p->next != NULL; p = p->next);
				p->next = current;
			}else{
				head3 = current;
			}
		}
		
		if(current->job->curpri == 2){
			if(head2){
				for(p = head2; p->next != NULL; p = p->next);
				p->next = current;
			}else{
				head2 = current;
			}
		}

		if(current->job->curpri == 1){
			if(head1){
				for(p = head1; p->next != NULL; p = p->next);
				p->next = current;
			}else{
				head1 = current;
			}
		}
		current = next;
		next = NULL;
		current->job->state = RUNNING;
		current->job->wait_time = 0;
		kill(current->job->pid,SIGCONT);
		return;
	}else{ /* next == NULL且current != NULL，不切换 */
		return;
	}
}

void sig_handler(int sig,siginfo_t *info,void *notused)
{
	int status;
	int ret;

	switch (sig) {
case SIGVTALRM: /* 到达计时器所设置的计时间隔 */
	scheduler();
	return;
case SIGCHLD: /* 子进程结束时传送给父进程的信号 */
	ret = waitpid(-1,&status,WNOHANG);
	if (ret == 0)
		return;
	if(WIFEXITED(status)){
		current->job->state = DONE;
		printf("normal termation, exit status = %d\n",WEXITSTATUS(status));
	}else if (WIFSIGNALED(status)){
		printf("abnormal termation, signal number = %d\n",WTERMSIG(status));
	}else if (WIFSTOPPED(status)){
		printf("child stopped, signal number = %d\n",WSTOPSIG(status));
	}
	return;
	default:
		return;
	}
}

void do_enq(struct jobinfo *newjob,struct jobcmd enqcmd)
{
	struct waitqueue *newnode,*p;
	int i=0,pid;
	char *offset,*argvec,*q;
	char **arglist;
	sigset_t zeromask;

	sigemptyset(&zeromask);

	/* 封装jobinfo数据结构 */
	newjob = (struct jobinfo *)malloc(sizeof(struct jobinfo));
	newjob->jid = allocjid();
	newjob->defpri = enqcmd.defpri;
	newjob->curpri = enqcmd.defpri;
	newjob->ownerid = enqcmd.owner;
	newjob->state = READY;
	newjob->create_time = time(NULL);
	newjob->wait_time = 0;
	newjob->run_time = 0;
	arglist = (char**)malloc(sizeof(char*)*(enqcmd.argnum+1));
	newjob->cmdarg = arglist;
	offset = enqcmd.data;
	argvec = enqcmd.data;
	while (i < enqcmd.argnum){
		if(*offset == ':'){
			*offset++ = '\0';
			q = (char*)malloc(offset - argvec);
			strcpy(q,argvec);
			arglist[i++] = q;
			argvec = offset;
		}else
			offset++;
	}

	arglist[i] = NULL;

#ifdef DEBUG

	printf("enqcmd argnum %d\n",enqcmd.argnum);
	for(i = 0;i < enqcmd.argnum; i++)
		printf("parse enqcmd:%s\n",arglist[i]);

#endif

	/*向等待队列中增加新的作业*/
	newnode = (struct waitqueue*)malloc(sizeof(struct waitqueue));
	newnode->next =NULL;
	newnode->job=newjob;

	if(newnode->job->defpri == 1){

		if(head1)
		{
			for(p=head1;p->next != NULL; p=p->next);
			p->next =newnode;
		}else
			head1=newnode;
	}

	else{
		if(newnode->job->defpri == 2){
			if(head2)
			{
				for(p=head2;p->next != NULL; p=p->next);
				p->next =newnode;
			}else
				head2=newnode;
		}
		else{
			if(newnode->job->defpri == 3){
				if(head3){
					for(p=head3;p->next != NULL; p=p->next);
					p->next =newnode;
			}else
				head3=newnode;
			}		
		}
	}
	/*为作业创建进程*/
	if((pid=fork())<0)
		error_sys("enq fork failed");

	if(pid==0){
		newjob->pid =getpid();
		/*阻塞子进程,等等执行*/
		raise(SIGSTOP);
#ifdef DEBUG

		printf("begin running\n");
		for(i=0;arglist[i]!=NULL;i++)
			printf("arglist %s\n",arglist[i]);
#endif

		/*复制文件描述符到标准输出*/
		dup2(globalfd,1);
		/* 执行命令 */
		if(execv(arglist[0],arglist)<0)
			printf("exec failed\n");
		exit(1);
	}else{
		newjob->pid=pid;
		waitpid(pid,NULL,WUNTRACED);
	}
}

void do_deq(struct jobcmd deqcmd)
{
	int deqid,i;
	struct waitqueue *p,*prev,*select,*selectprev;
	deqid=atoi(deqcmd.data);

#ifdef DEBUG
	printf("deq jid %d\n",deqid);
#endif

	/*current jodid==deqid,终止当前作业*/
	if (current && current->job->jid ==deqid){
		printf("teminate current job\n");
		kill(current->job->pid,SIGKILL);
		for(i=0;(current->job->cmdarg)[i]!=NULL;i++){
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i]=NULL;
		}
		free(current->job->cmdarg);
		free(current->job);
		free(current);
		current=NULL;
	}
	else{ /* 或者在等待队列中查找deqid */
		select=NULL;
		selectprev=NULL;
		int zhi = 0;
		if(head3){
			for(prev=head3,p=head3;p!=NULL;prev=p,p=p->next)
				if(p->job->jid==deqid){
					select=p;
					selectprev=prev;
					zhi = 1;
					break;
				}
				if(select==selectprev){
					head3 = select->next;
					select=NULL;
				}
				else{
					selectprev->next=select->next;
					select->next = NULL;
				}
		}
		if(head2 && zhi == 0){
			for(prev=head2,p=head2;p!=NULL;prev=p,p=p->next)
				if(p->job->jid==deqid){
					select=p;
					selectprev=prev;
					zhi = 1;
					break;
				}
				if(select==selectprev){
					head2 = select->next;
					select=NULL;
				}
				else{
					selectprev->next=select->next;
					select->next = NULL;
				}
		}
		if(head1 && zhi == 0){
			for(prev=head1,p=head1;p!=NULL;prev=p,p=p->next)
				if(p->job->jid==deqid){
					select=p;
					selectprev=prev;
					zhi = 1;
					break;
				}
				if(select==selectprev){
					head1 = select->next;
					select=NULL;
				}
				else{
					selectprev->next=select->next;
					select->next = NULL;
				}
		}
		if(select){
			for(i=0;(select->job->cmdarg)[i]!=NULL;i++){
				free((select->job->cmdarg)[i]);
				(select->job->cmdarg)[i]=NULL;
			}
			free(select->job->cmdarg);
			free(select->job);
			free(select);
			select=NULL;
		}
	}
}

void do_stat(struct jobcmd statcmd)
{
	struct waitqueue *p;
	char timebuf[BUFLEN];
	int i;
	char buffer[10000];
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

	/* 打印信息头部 */
	if((fifo_0=open("/tmp/server_0",O_WRONLY))<0)
		error_sys("open failed");

	sprintf(buffer,"JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");
	if(current){
		strcpy(timebuf,ctime(&(current->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		sprintf(buffer + strlen(buffer),"%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			current->job->jid,
			current->job->pid,
			current->job->ownerid,
			current->job->run_time,
			current->job->wait_time,
			timebuf,"RUNNING");
	}

	for(p=head3;p!=NULL;p=p->next){
		strcpy(timebuf,ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		sprintf(buffer + strlen(buffer),"%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			p->job->jid,
			p->job->pid,
			p->job->ownerid,
			p->job->run_time,
			p->job->wait_time,
			timebuf,
			"READY");
	}
	for(p=head2;p!=NULL;p=p->next){
		strcpy(timebuf,ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		sprintf(buffer + strlen(buffer),"%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			p->job->jid,
			p->job->pid,
			p->job->ownerid,
			p->job->run_time,
			p->job->wait_time,
			timebuf,
			"READY");
	}
	for(p=head1;p!=NULL;p=p->next){
		strcpy(timebuf,ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		sprintf(buffer + strlen(buffer),"%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			p->job->jid,
			p->job->pid,
			p->job->ownerid,
			p->job->run_time,
			p->job->wait_time,
			timebuf,
			"READY");
	}
	if(write(fifo_0,buffer,10000)<0)
		error_sys("write failed");

	close(fifo_0);

}

int main()
{
	struct timeval interval;
	struct itimerval new,old;
	struct stat statbuf,statbuf_0;
	struct sigaction newact,oldact1,oldact2;

	time_temp = 0;
	if(stat("/tmp/server",&statbuf)==0){
		/* 如果FIFO文件存在,删掉 */
		if(remove("/tmp/server")<0)
			error_sys("remove failed");
	}

	if(mkfifo("/tmp/server",0666)<0)
		error_sys("mkfifo failed");

	if(stat("/tmp/server_0",&statbuf_0)==0){
		/* 如果FIFO文件存在,删掉 */
		if(remove("/tmp/server_0")<0)
			error_sys("remove failed");
	}

	if(mkfifo("/tmp/server_0",0666)<0)
		error_sys("mkfifo failed_0");
	/* 在非阻塞模式下打开FIFO */
	if((fifo=open("/tmp/server",O_RDONLY|O_NONBLOCK))<0)
		error_sys("open fifo failed");

	/* 建立信号处理函数 */
	newact.sa_sigaction=sig_handler;
	sigemptyset(&newact.sa_mask);
	newact.sa_flags=SA_SIGINFO;
	sigaction(SIGCHLD,&newact,&oldact1);
	sigaction(SIGVTALRM,&newact,&oldact2);

	/* 设置时间间隔为1000毫秒 */
	interval.tv_sec=1;
	interval.tv_usec=0;

	new.it_interval=interval;
	new.it_value=interval;
	setitimer(ITIMER_VIRTUAL,&new,&old);

	while(siginfo==1);

	close(fifo);
	close(globalfd);
	return 0;
}
