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

/* ���ȳ��� */
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

	/* ���µȴ������е���ҵ */

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
		/* ѡ������ȼ���ҵ */
		time_temp = 0;
		next=jobselect();
		/* ��ҵ�л� */
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
		/* ѡ������ȼ���ҵ */
		if(head2 !=NULL){
			printf("heda2 not NULL");		
		}
		time_temp = 0;
		next=jobselect();
		/* ��ҵ�л� */
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

	/* ������ҵ����ʱ�� */
	if(current){
		current->job->run_time += 1; /* ��1����1000ms */
		time_temp++;	
	}

	/* ������ҵ�ȴ�ʱ�估���ȼ� */
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

	if(current && current->job->state == DONE){ /* ��ǰ��ҵ��� */
		/* ��ҵ��ɣ�ɾ���� */
		for(i = 0;(current->job->cmdarg)[i] != NULL; i++){
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i] = NULL;
		}
		/* �ͷſռ� */
		free(current->job->cmdarg);
		free(current->job);
		free(current);

		current = NULL;
	}

	if(next == NULL && current == NULL) /* û����ҵҪ���� */

		return;
	else if (next != NULL && current == NULL){ /* ��ʼ�µ���ҵ */

		printf("begin start new job\n");
		current = next;
		next = NULL;
		current->job->state = RUNNING;
		kill(current->job->pid,SIGCONT);
		return;
	}
	else if (next != NULL && current != NULL){ /* �л���ҵ */

		printf("switch to Pid: %d\n",next->job->pid);
		kill(current->job->pid,SIGSTOP);
		current->job->wait_time = 0;
		current->job->state = READY;

		if(current->job->curpri-1>=current->job->defpri){
			current->job->curpri--;
		}
		/* �Żصȴ����� */
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
	}else{ /* next == NULL��current != NULL�����л� */
		return;
	}
}

void sig_handler(int sig,siginfo_t *info,void *notused)
{
	int status;
	int ret;

	switch (sig) {
case SIGVTALRM: /* �����ʱ�������õļ�ʱ��� */
	scheduler();
	return;
case SIGCHLD: /* �ӽ��̽���ʱ���͸������̵��ź� */
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

	/* ��װjobinfo���ݽṹ */
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

	/*��ȴ������������µ���ҵ*/
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
	/*Ϊ��ҵ��������*/
	if((pid=fork())<0)
		error_sys("enq fork failed");

	if(pid==0){
		newjob->pid =getpid();
		/*�����ӽ���,�ȵ�ִ��*/
		raise(SIGSTOP);
#ifdef DEBUG

		printf("begin running\n");
		for(i=0;arglist[i]!=NULL;i++)
			printf("arglist %s\n",arglist[i]);
#endif

		/*�����ļ�����������׼���*/
		dup2(globalfd,1);
		/* ִ������ */
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

	/*current jodid==deqid,��ֹ��ǰ��ҵ*/
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
	else{ /* �����ڵȴ������в���deqid */
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
	*��ӡ������ҵ��ͳ����Ϣ:
	*1.��ҵID
	*2.����ID
	*3.��ҵ������
	*4.��ҵ����ʱ��
	*5.��ҵ�ȴ�ʱ��
	*6.��ҵ����ʱ��
	*7.��ҵ״̬
	*/

	/* ��ӡ��Ϣͷ�� */
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
		/* ���FIFO�ļ�����,ɾ�� */
		if(remove("/tmp/server")<0)
			error_sys("remove failed");
	}

	if(mkfifo("/tmp/server",0666)<0)
		error_sys("mkfifo failed");

	if(stat("/tmp/server_0",&statbuf_0)==0){
		/* ���FIFO�ļ�����,ɾ�� */
		if(remove("/tmp/server_0")<0)
			error_sys("remove failed");
	}

	if(mkfifo("/tmp/server_0",0666)<0)
		error_sys("mkfifo failed_0");
	/* �ڷ�����ģʽ�´�FIFO */
	if((fifo=open("/tmp/server",O_RDONLY|O_NONBLOCK))<0)
		error_sys("open fifo failed");

	/* �����źŴ����� */
	newact.sa_sigaction=sig_handler;
	sigemptyset(&newact.sa_mask);
	newact.sa_flags=SA_SIGINFO;
	sigaction(SIGCHLD,&newact,&oldact1);
	sigaction(SIGVTALRM,&newact,&oldact2);

	/* ����ʱ����Ϊ1000���� */
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
