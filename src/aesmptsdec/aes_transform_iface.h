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