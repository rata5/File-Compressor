#include "ArithmeticCoder.h"
#include <iostream>

ArithmeticCoder::ArithmeticCoder(std::ostream* output, std::istream* input)
    : out(output), in(input), low(0), high(0xFFFFFFFF), code(0), underflow_bits(0), buffer(0), bits_to_go(8) {
    if (in) {
        for (int i = 0; i < 4; ++i) {
            code = (code << 8) | input->get();
        }
    }
}

void ArithmeticCoder::encode(uint16_t symbol, const std::vector<uint32_t>& cum_freq, uint32_t total) {
    uint32_t range = high - low + 1;
    high = low + (range * cum_freq[symbol + 1]) / total - 1;
    low = low + (range * cum_freq[symbol]) / total;

    while (true) {
        if (high < 0x80000000) {
            output_bit(0);
            while (underflow_bits > 0) {
                output_bit(1);
                --underflow_bits;
            }
        }
        else if (low >= 0x80000000) {
            output_bit(1);
            while (underflow_bits > 0) {
                output_bit(0);
                --underflow_bits;
            }
            low -= 0x80000000;
            high -= 0x80000000;
        }
        else if (low >= 0x40000000 && high < 0xC0000000) {
            ++underflow_bits;
            low -= 0x40000000;
            high -= 0x40000000;
        }
        else {
            break;
        }
        low <<= 1;
        high = (high << 1) | 1;
    }
}

uint16_t ArithmeticCoder::decode(const std::vector<uint32_t>& cum_freq, uint32_t total) {
    uint32_t range = high - low + 1;
    uint32_t value = ((code - low + 1) * total - 1) / range;
    uint16_t symbol = 0;
    while (cum_freq[symbol + 1] <= value) {
        ++symbol;
    }

    high = low + (range * cum_freq[symbol + 1]) / total - 1;
    low = low + (range * cum_freq[symbol]) / total;

    while (true) {
        if (high < 0x80000000) {
            // nothing
        }
        else if (low >= 0x80000000) {
            code -= 0x80000000;
            low -= 0x80000000;
            high -= 0x80000000;
        }
        else if (low >= 0x40000000 && high < 0xC0000000) {
            code -= 0x40000000;
            low -= 0x40000000;
            high -= 0x40000000;
        }
        else {
            break;
        }
        low <<= 1;
        high = (high << 1) | 1;
        code = (code << 1) | input_bit();
    }

    return symbol;
}

void ArithmeticCoder::flush() {
    ++underflow_bits;
    if (low < 0x40000000) {
        output_bit(0);
        while (underflow_bits-- > 0) {
            output_bit(1);
        }
    }
    else {
        output_bit(1);
        while (underflow_bits-- > 0) {
            output_bit(0);
        }
    }

    if (bits_to_go != 8) {
        buffer <<= bits_to_go;
        out->put(static_cast<char>(buffer));
    }
    out->flush();
}

void ArithmeticCoder::output_bit(int bit) {
    buffer = (buffer << 1) | bit;
    --bits_to_go;

    if (bits_to_go == 0) {
        out->put(static_cast<char>(buffer));
        bits_to_go = 8;
        buffer = 0;
    }
}

int ArithmeticCoder::input_bit() {
    if (bits_to_go == 0) {
        buffer = in->get();
        bits_to_go = 8;
    }
    int bit = (buffer >> 7) & 1;
    buffer <<= 1;
    --bits_to_go;
    return bit;
}
