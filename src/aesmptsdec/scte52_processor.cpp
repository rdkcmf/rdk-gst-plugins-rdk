#include <stdio.h>
#include <cassert>
#include "aes_transform_openssl.h"
#include "scte52_processor.h"

typedef enum
{
    ENCRYPTED_PLUS_RESIDUAL_TERMINATION_BLOCK = 0,
    ENCRYPTED_ONLY,
    SOLITARY_TERMINATION_BLOCK
} ts_payload_classification;

static std::size_t get_cipher_block_size(aes_mode mode)
{
    size_t size = 0;
    switch(mode)
    {
        case AES_128_CBC:
            size = 16;
            break;
        
        default:
            break;
    }
    return size;
}

scte52_processor::scte52_processor(aes_mode mode): m_mode(mode)
{
    INFO("Constructing scte52_processor\n");
    if(AES_MODE_MAX <= mode)
    {
        ERROR("Unsupported AES mode\n");
        return;
    }
    m_cipher_block_size = get_cipher_block_size(mode);
    m_srtb_xor_factor.resize(m_cipher_block_size);
    m_aes_transformer = std::make_unique <aes_transform_openssl>(mode);
#ifdef SCTE52_DEBUG
    m_encrypted_packet_counter = 0;
    m_clear_packet_counter = 0;
#endif
}
scte52_processor::~scte52_processor()
{
    INFO("Destroying scte52_processor.\n");
#ifdef SCTE52_DEBUG
    INFO("Encrypted packets processed: %llu, clear packets processed: %llu\n", m_encrypted_packet_counter, m_clear_packet_counter);
#endif
}

void scte52_processor::compute_srtb_xor_factor()
{
    unsigned int decrypted_len = 0;
    m_aes_transformer->configure(op_type_t::ENCRYPT, *m_key, *m_iv);
    m_aes_transformer->process_buffer(m_iv->data(), m_cipher_block_size, m_srtb_xor_factor.data(), &decrypted_len);
    m_aes_transformer->reset();
}

int scte52_processor::set_parameters(std::unique_ptr <param_array_t> key, std::unique_ptr <param_array_t> iv)
{
    INFO("Updating parameters.\n");
    m_key = std::move(key);
    m_iv = std::move(iv);

    compute_srtb_xor_factor(); //Compute once. Reuse as long as key and iv are unchanged.
    m_aes_transformer->configure(op_type_t::ENCRYPT, *m_key, *m_iv);
    return 0;
}

int scte52_processor::process_buffer(unsigned char * data, const unsigned int length) const
{
    int ret = 0;
    unsigned int bytes_remaining = length;
    if(0 != (length % TS_PACKET_SIZE))
    {
        WARN("%s: Buffer contains a non-integer number of packets.\n", __func__);
        //Do nothing.
        ret = -1;
    }

    while(TS_PACKET_SIZE <= bytes_remaining) //as long as there is at least one packet remaining
    {
        process_packet(data);
        data += 188;
        bytes_remaining -= 188;
    }    
    return ret;
}

int scte52_processor::process_packet(unsigned char * in) const //assumes in points to the beginning of a TS packet.
{
    //Verify sync byte.
    if(0x47 != in[0])
    {
        WARN("Sync byte not detected in packet\n");
        return -1;
    }

    //int pid = ((in[1] & 0x1f) << 8) | in[2];
    int scrambling_control = (in[3] & 0xC0) >> 6;
    int adaptation_field_control = (in[3] & 0x30) >> 4;

    if((0 == scrambling_control) || (2 ==  adaptation_field_control /*indicates there is no payload*/))
    {
        //do nothing.
#ifdef SCTE52_DEBUG
        m_clear_packet_counter++;
#endif        
    }
    else
    {
#ifdef SCTE52_DEBUG        
        m_encrypted_packet_counter++;
#endif        
        //Figure out where payload starts.
        std::size_t payload_offset = 0;
        if(1 == adaptation_field_control) //Payload only
            payload_offset = 4;
        else //Adaptation field followed by payload.
        {
            //Range-check adaptation field size for bad data
            static const unsigned char MAX_AF_LENGTH = TS_PACKET_SIZE - 4 /* Standard TS header */ - 1 /*Adaptation field size byte*/
                - 1 /*minimum payload, since adaptation field control says this packet has a payload*/;
            if(in[4] > MAX_AF_LENGTH)
            {
                WARN("Bad adaptation field size\n");
                return -1;
            }
            payload_offset = 4 /* Standard TS header */ + 1 /*Adaptation field size byte*/ + in[4] /*Size of adaptation field itself*/;
        }


        in[3] &= 0x3F; //Clear scrambling control bits.

        std::size_t payload_size  = TS_PACKET_SIZE - payload_offset;
        ts_payload_classification packaging_classification = ENCRYPTED_PLUS_RESIDUAL_TERMINATION_BLOCK; //arbitrary default.

        if(payload_size < m_cipher_block_size)
            packaging_classification = SOLITARY_TERMINATION_BLOCK;
        else if(0 != (payload_size % m_cipher_block_size))
            packaging_classification = ENCRYPTED_PLUS_RESIDUAL_TERMINATION_BLOCK;
        else
            packaging_classification = ENCRYPTED_ONLY;

        std::size_t ciphertext_length = int(payload_size / m_cipher_block_size) * m_cipher_block_size; //TODO: optimization opportunity using LUT perhaps?

        if((ENCRYPTED_PLUS_RESIDUAL_TERMINATION_BLOCK == packaging_classification) || (ENCRYPTED_ONLY == packaging_classification))
        {
            //Out of order decryption. Process the termination block first, as it needs preceding ciphertext (not cleartext).
            if(ENCRYPTED_PLUS_RESIDUAL_TERMINATION_BLOCK == packaging_classification)
                process_residual_termination_block((in + payload_offset + ciphertext_length), (payload_size - ciphertext_length));

            m_aes_transformer->configure(op_type_t::DECRYPT, *m_key, *m_iv);
            if (0 != ciphertext_length)
                m_aes_transformer->process_buffer_in_place((in + payload_offset), ciphertext_length);

            m_aes_transformer->reset();
        }
        else //This is a solitary termination block.
            process_solitary_residual_termination_block((in + payload_offset), payload_size);
    }
    return 0;
}

int scte52_processor::process_solitary_residual_termination_block(unsigned char * residue_start, unsigned int residue_len) const
{
    int ret = 0;
    unsigned char * out = residue_start; //in-place operation.
    for(unsigned int i = 0; i < residue_len; i++)
    {
        out[i] = residue_start[i] ^ m_srtb_xor_factor[i];
    }
    return ret;
}

int scte52_processor::process_residual_termination_block(unsigned char * residue_start, unsigned int residue_len) const
{
    int ret = 0;
    unsigned char * in = residue_start - m_cipher_block_size;
    unsigned char * out = residue_start;
    unsigned char xor_factor[m_cipher_block_size];

    m_aes_transformer->configure(op_type_t::ENCRYPT, *m_key, *m_iv);

    unsigned int encrypted_len = 0;
    m_aes_transformer->process_buffer(in, m_cipher_block_size, xor_factor, &encrypted_len);
    assert(m_cipher_block_size == encrypted_len);

    for(unsigned int i = 0; i < residue_len; i++)
    {
        out[i] = residue_start[i] ^ xor_factor[i];
    }
    m_aes_transformer->reset();
    return ret;    
}