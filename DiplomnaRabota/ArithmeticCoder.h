#ifndef ARITHMETIC_CODER_H
#define ARITHMETIC_CODER_H

#include <iostream>
#include <vector>
#include <cstdint>

class ArithmeticCoder {
public:
    ArithmeticCoder(std::ostream* output, std::istream* input);
    void encode(uint16_t symbol, const std::vector<uint32_t>& cum_freq, uint32_t total);
    uint16_t decode(const std::vector<uint32_t>& cum_freq, uint32_t total);
    void flush();

private:
    std::ostream* out;
    std::istream* in;
    uint32_t low, high, code;
    int underflow_bits;
    void output_bit(int bit);
    int input_bit();
    int buffer = 0;
    int bits_to_go = 8;
};

#endif 