#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include "job.h"

/* 
 * √¸¡Ó”Ô∑®∏Ò Ω
 *     stat
 */
void usage()
{
	printf("Usage: stat\n");		
}

int main(int argc,char *argv[])
{
	struct jobcmd statcmd;
	int fd,fd_0;
	char buffer[10000];

	if(argc!=1)
	{
		usage();
		return 1;
	}

	statcmd.type=STAT;
	statcmd.defpri=0;
	statcmd.owner=getuid();
	statcmd.argnum=0;
	
	if((fd_0=open("/tmp/server_0",O_RDONLY|O_NONBLOCK))<0)
		error_sys("open fifo failed");
	
	if((fd=open("/tmp/server",O_WRONLY))<0)
		error_sys("stat open fifo failed");

	if(write(fd,&statcmd,DATALEN)<0)
		error_sys("stat write failed");

	close(fd);

	while(buffer[0] =='\0')
		read(fd_0,&buffer,10000);

	close(fd_0);
	printf("%s",buffer);
	return 0;
}
