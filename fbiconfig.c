#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#include "fbiconfig.h"

/* ------------------------------------------------------------------------ */

struct cfg_cmdline fbi_cmd[] = {
    {
	.letter   = 'h',
	.cmdline  = "help",
	.option   = { O_HELP },
	.value    = "1",
	.desc     = "print this help text",
    },{
	.letter   = 'V',
	.cmdline  = "version",
	.option   = { O_VERSION },
	.value    = "1",
	.desc     = "print fbi version number",
    },{
	.cmdline  = "store",
	.option   = { O_WRITECONF },
	.value    = "1",
	.desc     = "write cmd line args to config file",
    },{
	.letter   = 'l',
	.cmdline  = "list",
	.option   = { O_FILE_LIST },
	.needsarg = 1,
	.desc     = "read image filelist from file <arg>",
    },{
	.letter   = 'P',
	.cmdline  = "text",
	.option   = { O_TEXT_MODE },
	.value    = "1",
	.desc     = "switch into text reading mode",
    },{
	.letter   = 'a',
	.cmdline  = "autozoom",
	.option   = { O_AUTO_ZOOM },
	.value    = "1",
	.desc     = "automagically pick useful zoom factor",
    },{
	/* end of list */
    }
};

struct cfg_cmdline fbi_cfg[] = {
    {
	.cmdline  = "autoup",
	.option   = { O_AUTO_UP },
	.yesno    = 1,
	.desc     = "  like the above, but upscale only",
    },{
	.cmdline  = "autodown",
	.option   = { O_AUTO_DOWN },
	.yesno    = 1,
	.desc     = "  like the above, but downscale only",
    },{
	.cmdline  = "fitwidth",
	.option   = { O_FIT_WIDTH },
	.yesno    = 1,
	.desc     = "  use width only for autoscaling",

    },{
	.letter   = 'v',
	.cmdline  = "verbose",
	.option   = { O_VERBOSE },
	.yesno    = 1,
	.desc     = "show filenames all the time",
    },{
	.letter   = 'u',
	.cmdline  = "random",
	.option   = { O_RANDOM },
	.yesno    = 1,
	.desc     = "show files in a random order",
    },{
	.letter   = '1',
	.cmdline  = "once",
	.option   = { O_ONCE },
	.yesno    = 1,
	.desc     = "don't loop (for use with -t)",
    },{
	.cmdline  = "comments",
	.option   = { O_COMMENTS },
	.yesno    = 1,
	.desc     = "display image comments",
    },{
	.letter   = 'e',
	.cmdline  = "edit",
	.option   = { O_EDIT },
	.yesno    = 1,
	.desc     = "enable editing commands (see man page)",
    },{
	.cmdline  = "backup",
	.option   = { O_BACKUP },
	.yesno    = 1,
	.desc     = "  create backup files when editing",
    },{
	.cmdline  = "preserve",
	.option   = { O_PRESERVE },
	.yesno    = 1,
	.desc     = "  preserve timestamps when editing",
    },{
	.cmdline  = "readahead",
	.option   = { O_READ_AHEAD },
	.yesno    = 1,
	.desc     = "read ahead images into cache",

    },{
	.cmdline  = "cachemem",
	.option   = { O_CACHE_MEM },
	.needsarg = 1,
	.desc     = "image cache size in megabytes",
    },{
	.cmdline  = "blend",
	.option   = { O_BLEND_MSECS },
	.needsarg = 1,
	.desc     = "image blend time in miliseconds",
    },{
	.letter   = 'T',
	.cmdline  = "vt",
	.option   = { O_VT },
	.needsarg = 1,
	.desc     = "start on virtual console <arg>",
    },{
	.letter   = 's',
	.cmdline  = "scroll",
	.option   = { O_SCROLL },
	.needsarg = 1,
	.desc     = "scroll image by <arg> pixels",
    },{
	.letter   = 't',
	.cmdline  = "timeout",
	.option   = { O_TIMEOUT },
	.needsarg = 1,
	.desc     = "load next image after <arg> sec without user input",
    },{
	.letter   = 'r',
	.cmdline  = "resolution",
	.option   = { O_PCD_RES },
	.needsarg = 1,
	.desc     = "pick PhotoCD resolution (1..5)",
    },{
	.letter   = 'g',
	.cmdline  = "gamma",
	.option   = { O_GAMMA },
	.needsarg = 1,
	.desc     = "set display gamma (doesn't work on all hardware)",
    },{
	.letter   = 'f',
	.cmdline  = "font",
	.option   = { O_FONT },
	.needsarg = 1,
	.desc     = "use font <arg> (anything fontconfig accepts)",
    },{
	.letter   = 'd',
	.cmdline  = "device",
	.option   = { O_DEVICE },
	.needsarg = 1,
	.desc     = "use framebuffer device <arg>",
    },{
	.letter   = 'm',
	.cmdline  = "mode",
	.option   = { O_VIDEO_MODE },
	.needsarg = 1,
	.desc     = "use video mode <arg> (from /etc/fb.modes)",

    },{
	/* end of list */
    }
};

/* ------------------------------------------------------------------------ */

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
