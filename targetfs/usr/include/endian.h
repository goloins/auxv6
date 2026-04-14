#ifndef _ENDIAN_H
#define _ENDIAN_H

#include "stdint.h"

#define LITTLE_ENDIAN 1234
#define BIG_ENDIAN    4321
#define PDP_ENDIAN    3412

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define BYTE_ORDER LITTLE_ENDIAN
#else
#define BYTE_ORDER BIG_ENDIAN
#endif
#else
#define BYTE_ORDER LITTLE_ENDIAN
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
#define htobe16(x) __builtin_bswap16((uint16_t)(x))
#define htole16(x) ((uint16_t)(x))
#define be16toh(x) __builtin_bswap16((uint16_t)(x))
#define le16toh(x) ((uint16_t)(x))

#define htobe32(x) __builtin_bswap32((uint32_t)(x))
#define htole32(x) ((uint32_t)(x))
#define be32toh(x) __builtin_bswap32((uint32_t)(x))
#define le32toh(x) ((uint32_t)(x))

#define htobe64(x) __builtin_bswap64((uint64_t)(x))
#define htole64(x) ((uint64_t)(x))
#define be64toh(x) __builtin_bswap64((uint64_t)(x))
#define le64toh(x) ((uint64_t)(x))
#else
#define htobe16(x) ((uint16_t)(x))
#define htole16(x) __builtin_bswap16((uint16_t)(x))
#define be16toh(x) ((uint16_t)(x))
#define le16toh(x) __builtin_bswap16((uint16_t)(x))

#define htobe32(x) ((uint32_t)(x))
#define htole32(x) __builtin_bswap32((uint32_t)(x))
#define be32toh(x) ((uint32_t)(x))
#define le32toh(x) __builtin_bswap32((uint32_t)(x))

#define htobe64(x) ((uint64_t)(x))
#define htole64(x) __builtin_bswap64((uint64_t)(x))
#define be64toh(x) ((uint64_t)(x))
#define le64toh(x) __builtin_bswap64((uint64_t)(x))
#endif

#ifndef betoh16
#define betoh16(x) be16toh(x)
#endif
#ifndef betoh32
#define betoh32(x) be32toh(x)
#endif
#ifndef betoh64
#define betoh64(x) be64toh(x)
#endif

#endif
