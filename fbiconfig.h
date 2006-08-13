#include "parseconfig.h"

#define O_CMDLINE		"cmdline", "options"
#define O_OPTIONS		"config",  "options"

#define O_HELP		        O_CMDLINE, "help"
#define O_VERSION		O_CMDLINE, "version"
#define O_WRITECONF		O_CMDLINE, "writeconf"
#define O_FILE_LIST		O_CMDLINE, "file-list"
#define O_TEXT_MODE		O_CMDLINE, "text-mode"
#define O_AUTO_ZOOM		O_CMDLINE, "auto-zoom"

#define O_AUTO_UP		O_OPTIONS, "auto-up"
#define O_AUTO_DOWN		O_OPTIONS, "auto-down"
#define O_FIT_WIDTH		O_OPTIONS, "fit-width"
#define O_QUIET		        O_OPTIONS, "quiet"
#define O_VERBOSE		O_OPTIONS, "verbose"
#define O_RANDOM		O_OPTIONS, "random"
#define O_ONCE		        O_OPTIONS, "once"
#define O_COMMENTS		O_OPTIONS, "comments"
#define O_EDIT		        O_OPTIONS, "edit"
#define O_BACKUP		O_OPTIONS, "backup"
#define O_PRESERVE		O_OPTIONS, "preserve"
#define O_READ_AHEAD		O_OPTIONS, "read-ahead"

#define O_CACHE_MEM    	        O_OPTIONS, "cache-mem"
#define O_BLEND_MSECS		O_OPTIONS, "blend-msecs"
#define O_VT		        O_OPTIONS, "vt"
#define O_SCROLL		O_OPTIONS, "scroll"
#define O_TIMEOUT		O_OPTIONS, "timeout"
#define O_PCD_RES		O_OPTIONS, "photocd-res"

#define O_GAMMA		        O_OPTIONS, "gamma"

#define O_DEVICE                O_OPTIONS, "device"
#define O_FONT                  O_OPTIONS, "font"
#define O_VIDEO_MODE            O_OPTIONS, "video-mode"

#define GET_HELP()		cfg_get_bool(O_HELP,          0)
#define GET_VERSION()		cfg_get_bool(O_VERSION,       0)
#define GET_WRITECONF()		cfg_get_bool(O_WRITECONF,     0)
#define GET_TEXT_MODE()		cfg_get_bool(O_TEXT_MODE,     0)
#define GET_AUTO_ZOOM()		cfg_get_bool(O_AUTO_ZOOM,     0)

#define GET_AUTO_UP()		cfg_get_bool(O_AUTO_UP,       0)
#define GET_AUTO_DOWN()		cfg_get_bool(O_AUTO_DOWN,     0)
#define GET_FIT_WIDTH()		cfg_get_bool(O_FIT_WIDTH,     0)
#define GET_QUIET()		cfg_get_bool(O_QUIET,         0)
#define GET_VERBOSE()		cfg_get_bool(O_VERBOSE,       1)
#define GET_RANDOM()		cfg_get_bool(O_RANDOM,        0)
#define GET_ONCE()		cfg_get_bool(O_ONCE,          0)
#define GET_COMMENTS()		cfg_get_bool(O_COMMENTS,      0)
#define GET_EDIT()		cfg_get_bool(O_EDIT,          0)
#define GET_BACKUP()		cfg_get_bool(O_BACKUP,        0)
#define GET_PRESERVE()		cfg_get_bool(O_PRESERVE,      0)
#define GET_READ_AHEAD()       	cfg_get_bool(O_READ_AHEAD,    0)

#define GET_CACHE_MEM()         cfg_get_int(O_CACHE_MEM,    256)
#define GET_BLEND_MSECS()       cfg_get_int(O_BLEND_MSECS,    0)
#define GET_VT()                cfg_get_int(O_VT,             0)
#define GET_SCROLL()            cfg_get_int(O_SCROLL,        50)
#define GET_TIMEOUT()           cfg_get_int(O_TIMEOUT,        0)
#define GET_PCD_RES()           cfg_get_int(O_PCD_RES,        3)

#define GET_GAMMA()             cfg_get_float(O_GAMMA,        1)

/* -------------------------------------------------------------------------- */

extern struct cfg_cmdline fbi_cmd[];
extern struct cfg_cmdline fbi_cfg[];
void fbi_read_config(void);
void fbi_write_config(void);

