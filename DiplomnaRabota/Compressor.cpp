#include "Compressor.h"
#include <fstream>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace {
    class Predictor {
        int cxt = 1;
        std::vector<int> t;
    public:
        Predictor() : t(512, 32768) {}
        int p() const { return t[cxt] >> 4; }
        void update(int y) {
            int& pr = t[cxt];
            pr += ((y << 16) - pr) >> 5;
            cxt = (cxt << 1 | y) & 511;
        }
    };

    class BitEncoder {
        uint32_t x1 = 0, x2 = 0xFFFFFFFF;
        std::ostream& out;
    public:
        BitEncoder(std::ostream& o) : out(o) {}
        void encode(int y, int p) {
            const uint32_t xmid = x1 + ((x2 - x1) >> 8) * p;
            y ? x2 = xmid : x1 = xmid + 1;
            while (((x1 ^ x2) & 0xFF000000) == 0) {
                out.put(x2 >> 24);
                x1 <<= 8;
                x2 = (x2 << 8) + 255;
            }
        }
        void flush() {
            for (int i = 0; i < 4; ++i) out.put(x1 >> 24), x1 <<= 8;
        }
    };

    class BitDecoder {
        uint32_t x1 = 0, x2 = 0xFFFFFFFF, x = 0;
        std::istream& in;
    public:
        BitDecoder(std::istream& i) : in(i) {
            for (int j = 0; j < 4; ++j) x = (x << 8) + (in.get() & 255);
        }
        int decode(int p) {
            const uint32_t xmid = x1 + ((x2 - x1) >> 8) * p;
            int y = x <= xmid;
            y ? x2 = xmid : x1 = xmid + 1;
            while (((x1 ^ x2) & 0xFF000000) == 0) {
                x1 <<= 8;
                x2 = (x2 << 8) + 255;
                x = (x << 8) + (in.get() & 255);
            }
            return y;
        }
    };
}

void Compressor::compress(const std::string& inPath, const std::string& outPath) {
    std::ifstream in(inPath, std::ios::binary);
    std::ofstream out(outPath, std::ios::binary);
    if (!in || !out) throw std::runtime_error("Failed to open files for compression");

    // Write header
    uint32_t magic = 0x53525200; // 'SRR\0'
    uint32_t version = 1;
    in.seekg(0, std::ios::end);
    uint64_t originalSize = in.tellg();
    in.seekg(0);

    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&originalSize), sizeof(originalSize));

    // Encode file content
    Predictor predictor;
    BitEncoder encoder(out);
    int c;
    while ((c = in.get()) != EOF) {
        for (int i = 7; i >= 0; --i) {
            int bit = (c >> i) & 1;
            encoder.encode(bit, predictor.p());
            predictor.update(bit);
        }
    }
    encoder.flush();
}

void Compressor::decompress(const std::string& inPath, const std::string& outPath) {
    std::ifstream in(inPath, std::ios::binary);
    std::ofstream out(outPath, std::ios::binary);
    if (!in || !out) throw std::runtime_error("Failed to open files for decompression");

    // Read header
    uint32_t magic, version;
    uint64_t originalSize;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&originalSize), sizeof(originalSize));

    if (magic != 0x53525200) throw std::runtime_error("Invalid file format");
    if (version != 1) throw std::runtime_error("Unsupported version");

    // Decode file content
    Predictor predictor;
    BitDecoder decoder(in);
    for (uint64_t j = 0; j < originalSize; ++j) {
        int c = 0;
        for (int i = 0; i < 8; ++i) {
            int bit = decoder.decode(predictor.p());
            predictor.update(bit);
            c = (c << 1) | bit;
        }
        out.put(static_cast<char>(c));
    }
}
