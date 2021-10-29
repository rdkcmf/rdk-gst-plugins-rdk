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
#ifndef _SCTE52_PROCESSOR_H_
#define _SCTE52_PROCESSOR_H_
#include <cstdint>
#include <vector>
#include <memory>

#include "gstaesdec_types.h"
#include "aes_transform_iface.h"

//#define SCTE52_DEBUG 1
class scte52_processor
{
    private:
    aes_mode m_mode;
    std::unique_ptr <param_array_t> m_key;
    std::unique_ptr <param_array_t> m_iv;
    param_array_t m_srtb_xor_factor;
#ifdef SCTE52_DEBUG
    mutable unsigned long long m_encrypted_packet_counter;
    mutable unsigned long long m_clear_packet_counter;
#endif
    std::unique_ptr <aes_transform_iface> m_aes_transformer;
    std::size_t m_cipher_block_size;

    void compute_srtb_xor_factor();
    int process_solitary_residual_termination_block(unsigned char * residue_start, unsigned int residue_len) const;
    int process_residual_termination_block(unsigned char * residue_start, unsigned int residue_len) const;
    int process_packet(unsigned char * data) const;

    public:
    scte52_processor(aes_mode);
    ~scte52_processor();
    int set_parameters(std::unique_ptr <param_array_t> key, std::unique_ptr <param_array_t> iv);
    int process_buffer(unsigned char * data, const unsigned int length) const;
};
#endif //_SCTE52_PROCESSOR_H_