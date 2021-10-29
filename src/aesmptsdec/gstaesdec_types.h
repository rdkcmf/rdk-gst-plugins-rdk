/*
 * Copyright 2021 RDK Management
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation, version 2
 * of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
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