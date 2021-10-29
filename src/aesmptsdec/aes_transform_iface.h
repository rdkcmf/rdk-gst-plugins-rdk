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
#ifndef _AES_DEC_IFACE_H_
#define _AES_DEC_IFACE_H_
#include "gstaesdec_types.h"
enum class op_type_t {ENCRYPT, DECRYPT};

class aes_transform_iface
{
    public:
    aes_transform_iface(const aes_mode mode) {}
    virtual ~aes_transform_iface() {}
    virtual int process_buffer(unsigned char * in_data, const unsigned int in_len, unsigned char * out_data, unsigned int *out_len) = 0; //Length must be an integer multiple of cipher block size.
    virtual int process_buffer_in_place(unsigned char * data, const unsigned int length) = 0; //Length must be an integer multiple of cipher block size.
    virtual int configure(const op_type_t operation, const param_array_t &key, const param_array_t &iv) = 0;
    virtual void reset() = 0;
};
#endif //_AES_DEC_IFACE_H_