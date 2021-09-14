#include <stdio.h>
#include "scte52_processor.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <string.h>
#include <stdio.h>
#include <string.h>

#define CHECK_SANITY 1
namespace
{
    const int SIZE_PREFIX = 1000 * 50; //Operate in large blocks
    const int CIPHER_BLOCK_SIZE = 16; //bytes
}

int main(int argc, char * argv[])
{
    std::cout<<"Syntax: in-file-path out-file-path megabytes-to-process\n";
    std::cout<<"decryptor\n";

    if(4 > argc)
    {
        std::cout<<"Insufficient args.\n";
        return -1;
    }

    auto in_buffer = new std::array <char, (SIZE_PREFIX * TS_PACKET_SIZE)>;

    double read_limit_d = std::stod(argv[3]);
    std::cout<<"Will read max " << read_limit_d << " megabytes\n";
    unsigned long long read_limit = (unsigned long long) (read_limit_d * 1024 * 1024); //Convert to bytes.

    std::ifstream infile(argv[1], std::ios::binary);
    std::ofstream outfile(argv[2], std::ios::binary);

    if((!infile.is_open()) || (!outfile.is_open()))
    {
        std::cout<<"Could not open one or more of the files.\n";
        delete in_buffer;
        return -1;
    }

    //Find file length of input file.
    std::filesystem::path in_path{argv[1]};
    auto infile_len = std::filesystem::file_size(in_path);
    std::cout << "The size of infile is " << infile_len << " bytes.\n";

    if((read_limit > infile_len) || (0 == read_limit))
    {        
        read_limit = infile_len;
        std::cout<<"Adjusting read limit to match file length.\n";
    }

    bool dothings = true;
    unsigned long long bytes_read = 0;
    std::unique_ptr <param_array_t> mykey = std::make_unique<param_array_t>(CIPHER_BLOCK_SIZE);
    std::unique_ptr <param_array_t> myiv = std::make_unique<param_array_t>(CIPHER_BLOCK_SIZE);
#warn "Add your key and iv here."
    auto processor = std::make_unique<scte52_processor>(AES_128_CBC);
    processor->set_parameters(std::move(mykey), std::move(myiv));

    while(dothings)
    {
        infile.read(in_buffer->data(), in_buffer->size());
        bytes_read += infile.gcount();
        std::cout<<"Bytes read: " << bytes_read << std::endl;
        if(0 != infile.gcount() % TS_PACKET_SIZE)
        {
            std::cout << "Unhandled condition. Buffer not an integer multiple of packet size.\n";
        }

        // /* Start conversion operation*/
        unsigned char * ptr = reinterpret_cast<unsigned char *> (in_buffer->data());
        int bytes_remaining = infile.gcount();
        processor->process_buffer(ptr, bytes_remaining);
        // while(TS_PACKET_SIZE <= bytes_remaining) //as long as there is at least one packet remaining
        // {
        //     process_packet(openssl_dec_ctx, openssl_rtb_ctx, ptr);
        //     ptr += 188;
        //     bytes_remaining -= 188;
        // }
        outfile.write(in_buffer->data(), bytes_remaining);

        //Stop conversion operation.
        if(bytes_read >= read_limit)
        {
            std::cout<<"Hit the limit. Stopping read.\n";
            break;
        }
        if(infile.eof())
        {
            std::cout<<"Finished reading file\n";
            break;
        }
    }
    std::cout<<"Exiting convertor.\n";
    delete in_buffer;
    return 0;
}
