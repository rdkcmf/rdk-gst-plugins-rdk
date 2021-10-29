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
#include "aes_transform_iface.h"
#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>

class aes_transform_openssl: public aes_transform_iface
{
    private:
    EVP_CIPHER_CTX * m_ctx;
    aes_mode m_mode;
    op_type_t m_operation;

    public:
    aes_transform_openssl(const aes_mode mode);
    ~aes_transform_openssl() override;
    int process_buffer(unsigned char * in_data, const unsigned int in_len, unsigned char * out_data, unsigned int *out_len) override;
    int process_buffer_in_place(unsigned char * data, const unsigned int length) override;
    int configure(const op_type_t operation, const param_array_t &key, const param_array_t &iv) override;
    void reset() override;
};
