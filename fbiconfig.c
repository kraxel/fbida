#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#include "parseconfig.h"
#include "fbiconfig.h"

static char *fbi_config = NULL;

static void init_config(void)
{
    char *home;
    
    home = getenv("HOME");
    if (NULL == home)
	return;

    fbi_config = malloc(strlen(home) + 16);
    sprintf(fbi_config,"%s/.fbirc", home);
}

void fbi_read_config(void)
{
    init_config();
    if (fbi_config)
	cfg_parse_file("config", fbi_config);
}

void fbi_write_config(void)
{
    if (fbi_config)
	cfg_write_file("config", fbi_config);
}
