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