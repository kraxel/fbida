#include "parseconfig.h"

#define O_OPTIONS		"config", "options"
#define O_BOOKMARKS		"config", "bookmarks"

#define O_AUTOZOOM		O_OPTIONS, "autozoom"
#define O_PHOTOCD_RES		O_OPTIONS, "photocd_res"
#define O_SANE_RES		O_OPTIONS, "sane_res"
#define O_ICON_SMALL		O_OPTIONS, "icon_small"
#define O_ICON_LARGE		O_OPTIONS, "icon_large"

#define GET_AUTOZOOM()		cfg_get_bool(O_AUTOZOOM,      1)
#define GET_PHOTOCD_RES()      	cfg_get_int(O_PHOTOCD_RES,    3)
#define GET_SANE_RES()		cfg_get_int(O_SANE_RES,     300)
#define GET_ICON_SMALL()     	cfg_get_int(O_ICON_SMALL,    32)
#define GET_ICON_LARGE()     	cfg_get_int(O_ICON_LARGE,    96)

/* -------------------------------------------------------------------------- */

char *ida_lists;

void ida_init_config(void);
void ida_read_config(void);
void ida_write_config(void);
