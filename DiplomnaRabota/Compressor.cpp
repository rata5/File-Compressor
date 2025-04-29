#include "Compressor.h"
#include "ArithmeticCoder.h"
#include <fstream>
#include <vector>
#include <cstdint>

void Compressor::compress(const std::string& inputFile, const std::string& outputFile) {
    std::ifstream in(inputFile, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open input file!");

    std::ofstream out(outputFile, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot open output file!");

    ArithmeticCoder coder(&out, nullptr);

    std::vector<uint32_t> freq(257, 1);
    std::vector<uint32_t> cum_freq(258, 0);
    uint32_t total = 257;

    auto update_cum_freq = [&]() {
        cum_freq[0] = 0;
        for (int i = 0; i < 257; ++i) {
            cum_freq[i + 1] = cum_freq[i] + freq[i];
        }
        };

    update_cum_freq();

    int ch;
    while ((ch = in.get()) != EOF) {
        coder.encode(static_cast<uint16_t>(ch), cum_freq, total);

        ++freq[ch];
        ++total;

        if (total > 0x00FFFFFF) { // limit frequency growth
            for (auto& f : freq) {
                f = (f + 1) / 2;
            }
            total = 0;
            for (auto f : freq) {
                total += f;
            }
            update_cum_freq();
        }
        update_cum_freq();
    }

    coder.encode(256, cum_freq, total); // special EOF symbol
    coder.flush();
    out.flush();
}

void Compressor::decompress(const std::string& inputFile, const std::string& outputFile) {
    std::ifstream in(inputFile, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open input file!");

    std::ofstream out(outputFile, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot open output file!");

    ArithmeticCoder coder(nullptr, &in);

    std::vector<uint32_t> freq(257, 1);
    std::vector<uint32_t> cum_freq(258, 0);
    uint32_t total = 257;

    auto update_cum_freq = [&]() {
        cum_freq[0] = 0;
        for (int i = 0; i < 257; ++i) {
            cum_freq[i + 1] = cum_freq[i] + freq[i];
        }
        };

    update_cum_freq();

    while (true) {
        uint16_t symbol = coder.decode(cum_freq, total);
        if (symbol == 256) break; // EOF symbol
        out.put(static_cast<char>(symbol));

        ++freq[symbol];
        ++total;
        if (total > 0x00FFFFFF) {
            for (auto& f : freq) {
                f = (f + 1) / 2;
            }
            total = 0;
            for (auto f : freq) {
                total += f;
            }
        }
        update_cum_freq();
    }
}
