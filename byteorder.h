#include <sys/types.h>

#ifndef __BYTE_ORDER
# ifndef BYTE_ORDER
#  error unknown byteorder
# endif
# define __BYTE_ORDER     BYTE_ORDER
# define __LITTLE_ENDIAN  LITTLE_ENDIAN
# define __BIG_ENDIAN     BIG_ENDIAN
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
# define le16_to_cpu(x) (x)
# define le32_to_cpu(x) (x)
#elif BYTE_ORDER == BIG_ENDIAN
# define le16_to_cpu(x) (((x>>8) & 0x00ff) |\
                         ((x<<8) & 0xff00))
# define le32_to_cpu(x) (((x>>24) & 0x000000ff) |\
                         ((x>>8)  & 0x0000ff00) |\
                         ((x<<8)  & 0x00ff0000) |\
                         ((x<<24) & 0xff000000))
#endif
