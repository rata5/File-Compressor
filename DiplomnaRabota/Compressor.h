#ifndef COMPRESSOR_H
#define COMPRESSOR_H

#include <string>

class Compressor {
public:
    static void compress(const std::string& inPath, const std::string& outPath);
    static void decompress(const std::string& inPath, const std::string& outPath);
};

#endif