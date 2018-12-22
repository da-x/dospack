#ifndef _DOSPACK_COMMON_H__
#define _DOSPACK_COMMON_H__

#include <endian.h>
#include <stddef.h>

typedef unsigned long ulong;
typedef signed long slong;

typedef unsigned int u32;
typedef signed int s32;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned char u8;
typedef signed char s8;

typedef unsigned long long u64;
typedef signed long long s64;

enum dp_bool {
	DP_FALSE = 0,
	DP_TRUE = 1,
};

#if __BYTE_ORDER == __LITTLE_ENDIAN
#  define DP_LITTLE_ENDIAN
#  ifndef htobe16
#    define htobe16(x) __bswap_16 (x)
#  endif
#  define be16toh(x) __bswap_16 (x)

#  ifndef htobe32
#     define htobe32(x) __bswap_32 (x)
#  endif
#  define be32toh(x) __bswap_32 (x)

#  ifndef htobe64
#    define htobe64(x) __bswap_64 (x)
#  endif

#  define be64toh(x) __bswap_64 (x)
#else
#  define DP_BIG_ENDIAN
#  define htobe16(x) (x)
#  define htole16(x) __bswap_16 (x)
#  define be16toh(x) (x)
#  define le16toh(x) __bswap_16 (x)

#  define htobe32(x) (x)
#  define htole32(x) __bswap_32 (x)
#  define be32toh(x) (x)
#  define le32toh(x) __bswap_32 (x)

#  define htobe64(x) (x)
#  define htole64(x) __bswap_64 (x)
#  define be64toh(x) (x)
#  define le64toh(x) __bswap_64 (x)
#endif

#endif

