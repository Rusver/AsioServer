#pragma once
#include <cstdint>


#pragma pack(push, 1)
struct RequestHeader {
    uint32_t user_id;   // 4 bytes
    uint8_t  version;   // 1 byte
    uint8_t  op;        // 1 byte
    uint16_t name_len;  // 2 bytes
    // filename follows (variable size)
};
#pragma pack(pop)

#pragma pack(push, 1)
struct PayloadHeader {
    uint32_t size;  // 4 bytes
    // binary data follows (variable)
};
#pragma pack(pop)

// Helper endian conversion (cross-platform)
#if defined(_WIN32)
    #include <windows.h>
    #define htole16(x) (x)
    #define le16toh(x) (x)
    #define htole32(x) (x)
    #define le32toh(x) (x)
#else
    #include <endian.h>
// Convert host <-> little endian (no change on little-endian Windows)
#endif