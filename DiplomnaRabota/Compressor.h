#ifndef COMPRESSOR_H
#define COMPRESSOR_H

#include <string>

class Compressor {
public:
    static void compress(const std::string& in, const std::string& out);
    static void decompress(const std::string& in, const std::string& out);
};

#endif