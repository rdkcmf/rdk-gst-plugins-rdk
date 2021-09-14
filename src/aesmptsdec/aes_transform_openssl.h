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
