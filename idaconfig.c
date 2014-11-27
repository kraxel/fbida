#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#include "idaconfig.h"

char        *ida_lists;
static char *ida_config;

void ida_init_config(void)
{
    char *home;
    char *conf;
    struct stat st;
    
    home = getenv("HOME");
    if (NULL == home)
	return;

    conf       = malloc(strlen(home) + 16);
    ida_lists  = malloc(strlen(home) + 16);
    ida_config = malloc(strlen(home) + 16);
    sprintf(conf,      "%s/.ida",        home);
    sprintf(ida_lists, "%s/.ida/lists",  home);
    sprintf(ida_config,"%s/.ida/config", home);

    if (-1 == stat(ida_lists,&st)) {
	if (-1 == stat(conf,&st))
	    mkdir(conf,0777);
	mkdir(ida_lists,0777);
    }
    free(conf);
}

void ida_read_config(void)
{
    int rc;

    rc = cfg_parse_file("config", ida_config);
    if (-1 == rc) {
	/* set some defaults */
	cfg_set_str(O_BOOKMARKS, "Home", getenv("HOME"));
    }
}

void ida_write_config(void)
{
    cfg_write_file("config", ida_config);
}
