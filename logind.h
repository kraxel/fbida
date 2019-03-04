#include <stdbool.h>
#include <inttypes.h>

#include <libudev.h>
#include <libinput.h>

extern const struct libinput_interface libinput_if_logind;

int logind_init(void);
int logind_take_control(void);
int logind_open(const char *path, int flags, void *user_data);
void logind_close(int fd, void *user_data);
