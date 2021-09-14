#ifndef _GSTAESDEC_TYPES_H_
#define _GSTAESDEC_TYPES_H_
#include <vector>
#include <cstdint>
#include <stdio.h> //for LOG/printf

typedef enum
{
    AES_128_CBC = 0,
    AES_MODE_MAX
} aes_mode;

typedef std::vector <uint8_t> param_array_t;

#define ERROR_LEVEL "ERROR: "
#define INFO_LEVEL "Info: "
#define WARN_LEVEL "WARN: "
#define TRACE_LEVEL "Trace: "

#define _LOG(level, ...) do { printf("<aesdec> " level __VA_ARGS__); } while(0);
#define WARN(...) _LOG(WARN_LEVEL, __VA_ARGS__)
#define ERROR(...) _LOG(ERROR_LEVEL, __VA_ARGS__)
#define INFO(...) _LOG(INFO_LEVEL, __VA_ARGS__)
#define TRACE(...) _LOG(TRACE_LEVEL, __VA_ARGS__)

#define TS_PACKET_SIZE 188
#endif //_GSTAESDEC_TYPES_H_