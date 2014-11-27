#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

#include <lirc/lirc_client.h>
#include "lirc.h"

/*-----------------------------------------------------------------------*/

static int debug = 0;
static struct lirc_config *config = NULL;

int lirc_fbi_init()
{
    int fd;
    if (-1 == (fd = lirc_init("fbi",debug))) {
	if (debug)
	    fprintf(stderr,"lirc: no infrared remote support available\n");
	return -1;
    }
    if (0 != lirc_readconfig(NULL,&config,NULL)) {
	config = NULL;
    }
    if (debug)
	fprintf(stderr, "lirc: ~/.lircrc file %sfound\n",
		config ? "" : "not ");
  
    fcntl(fd,F_SETFL,O_NONBLOCK);
    fcntl(fd,F_SETFD,FD_CLOEXEC);
    if (debug)
	fprintf(stderr,"lirc: init ok\n");
  
    return fd;
}

int lirc_fbi_havedata(int* rc, char key[11])
{
    char *code,*cmd;
    int ret=-1;
    
    while (lirc_nextcode(&code) == 0  &&  code != NULL) {
	ret = 0;
	if (config) {
	    /* use ~/.lircrc */
	    while (lirc_code2char(config,code,&cmd)==0 && cmd != NULL) {
		memset(key,0,11);
		strncpy(key,cmd,10);
		*rc = strlen(cmd);
		if (debug)
		    fprintf(stderr,"lirc: cmd \"%s\"\n", cmd);
	    }
	}
	free(code);
    }
    return ret;
}
