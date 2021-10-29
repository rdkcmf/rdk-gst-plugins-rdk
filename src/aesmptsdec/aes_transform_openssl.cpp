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

#include "aes_transform_openssl.h"

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#define EVP_CIPHER_CTX_reset EVP_CIPHER_CTX_cleanup
#endif
aes_transform_openssl::aes_transform_openssl(aes_mode mode): aes_transform_iface(mode)
{
    m_mode = mode;
    m_ctx = EVP_CIPHER_CTX_new();
    TRACE("%s: constructor\n", __func__);
}

aes_transform_openssl::~aes_transform_openssl()
{
    EVP_CIPHER_CTX_free(m_ctx);
    m_ctx = nullptr;
    TRACE("%s: destructor\n", __func__);
}
int aes_transform_openssl::process_buffer(unsigned char * in_data, const unsigned int in_len, unsigned char * out_data, unsigned int *out_len)
{
    int ret = 0;
    if(op_type_t::DECRYPT == m_operation)
    {
        int decrypted_length = 0;
        if (1 != EVP_DecryptUpdate(m_ctx, out_data, &decrypted_length, in_data, in_len))
        {
            ERROR("%s: openssl decryption failed.\n", __func__);
            ERR_print_errors_fp(stdout);
            ret = -1;
        }
        *out_len = decrypted_length;

        if(decrypted_length != static_cast <int> (in_len)) //length is expected to be an integer multiple of cipher block size, so this should match.
        {
            ERROR("%s: openssl decrypted length doesn't match input length. Unexpected condition.\n", __func__);
            ret = -2;
        }
    }
    else
    {
        int encrypted_length = 0;
        if (1 != EVP_EncryptUpdate(m_ctx, out_data, &encrypted_length, in_data, in_len))
        {
            ERROR("%s: openssl encryption failed.\n", __func__);
            ERR_print_errors_fp(stdout);
            ret = -1;
        }
        *out_len = encrypted_length;

        if(encrypted_length != static_cast <int> (in_len)) //length is expected to be an integer multiple of cipher block size, so this should match.
        {
            ERROR("%s: openssl encrypted length doesn't match input length. Unexpected condition.\n", __func__);
            ret = -2;
        }        
    }
    return ret;
}
int aes_transform_openssl::process_buffer_in_place(unsigned char * data, const unsigned int length)
{
    int ret = 0;
    if(op_type_t::DECRYPT == m_operation)
    {
        int decrypted_length = 0;
        if (1 != EVP_DecryptUpdate(m_ctx, data, &decrypted_length, data, length)) //in-place decryption
        {
            ERROR("%s: openssl decryption failed.\n", __func__);
            ERR_print_errors_fp(stdout);
            ret = -1;
        }
        if(decrypted_length != static_cast <int> (length)) //length is expected to be an integer multiple of cipher block size, so this should match.
        {
            ERROR("%s: openssl decrypted length doesn't match input length. Unexpected condition.\n", __func__);
            ret = -2;
        }
    }
    else
    {
        int encrypted_length = 0;
        if (1 != EVP_EncryptUpdate(m_ctx, data, &encrypted_length, data, length)) //in-place decryption
        {
            ERROR("%s: openssl encryption failed.\n", __func__);
            ERR_print_errors_fp(stdout);
            ret = -1;
        }
        if(encrypted_length != static_cast <int> (length)) //length is expected to be an integer multiple of cipher block size, so this should match.
        {
            ERROR("%s: openssl encrypted length doesn't match input length. Unexpected condition.\n", __func__);
            ret = -2;
        }
    }
    return ret;
}

int aes_transform_openssl::configure(const op_type_t operation, const param_array_t &key, const param_array_t &iv)
{
    int ret = 0;
    m_operation = operation;
    if(op_type_t::DECRYPT == operation)
    {
        if(1 != EVP_DecryptInit_ex(m_ctx, EVP_aes_128_cbc(), nullptr, key.data(), iv.data())) //TODO: use m_mode to take a decision here.
        {
            ERROR("%s: failed to set openssl parameters.\n", __func__);
            ERR_print_errors_fp(stdout);
            ret = -1;
        }
    }
    else
    {
        if(1 != EVP_EncryptInit_ex(m_ctx, EVP_aes_128_cbc(), nullptr, key.data(), iv.data()))
        {
            ERROR("%s: failed to set openssl parameters.\n", __func__);
            ERR_print_errors_fp(stdout);
            ret = -1;
        }
    }
    EVP_CIPHER_CTX_set_padding(m_ctx, 0); //TODO: Is this relevant for encrypt operations?
    return ret;
}

void aes_transform_openssl::reset()
{
    EVP_CIPHER_CTX_reset(m_ctx);
}
