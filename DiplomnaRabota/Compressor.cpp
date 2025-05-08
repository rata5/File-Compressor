#include "Compressor.h"
#include <fstream>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace {

    class TextPredictor {
        int cxt = 1;
        std::vector<int> table;
    public:
        TextPredictor() : table(4096, 32768) {}
        int p() const { return table[cxt] >> 4; }
        void update(int bit) {
            int& pr = table[cxt];
            pr += ((bit << 16) - pr) >> 5;
            cxt = ((cxt << 1) | bit) & 4095;
        }
    };

    class BitEncoder {
        uint32_t x1 = 0;
        uint32_t x2 = 0xFFFFFFFF;
        std::ostream& out;
    public:
        BitEncoder(std::ostream& o) : out(o) {}

        void encode(int bit, int p) {
            uint32_t range = x2 - x1 + 1;
            uint32_t xmid = x1 + ((static_cast<uint64_t>(range) * p) >> 13);
            if (bit) x2 = xmid;
            else x1 = xmid + 1;

            // Output common leading bytes
            while ((x1 ^ x2) < 0x1000000) {
                out.put(static_cast<char>(x2 >> 24));  // PAQ-style: write MSB of x2
                x1 <<= 8;
                x2 = (x2 << 8) | 0xFF;
            }
        }

        void flush() {
            // Emit enough bytes to distinguish x1 from any other range
            for (int i = 0; i < 4; ++i) {
                out.put(static_cast<char>(x1 >> 24));
                x1 <<= 8;
            }
        }
    };

    class BitDecoder {
        uint32_t x1 = 0;
        uint32_t x2 = 0xFFFFFFFF;
        uint32_t x = 0;
        std::istream& in;
    public:
        BitDecoder(std::istream& i) : in(i) {
            for (int j = 0; j < 4; ++j) {
                int byte = in.get();
                if (byte == EOF) throw std::runtime_error("Unexpected EOF during decoder init");
                x = (x << 8) | (byte & 0xFF);
            }
        }

        int decode(int p) {
            uint32_t range = x2 - x1 + 1;
            uint32_t xmid = x1 + ((static_cast<uint64_t>(range) * p) >> 13);
            int bit = (x <= xmid);
            if (bit) x2 = xmid;
            else x1 = xmid + 1;

            while ((x1 ^ x2) < 0x1000000) {
                int byte = in.get();
                if (byte == EOF) throw std::runtime_error("Unexpected EOF during decode");
                x1 <<= 8;
                x2 = (x2 << 8) | 0xFF;
                x = (x << 8) | (byte & 0xFF);
            }
            return bit;
        }
    };


    bool file_exists(const std::string& path) {
        return fs::exists(fs::u8path(path));
    }

} // namespace

bool paq_compress(const std::string& inPath, const std::string& outPath) {
    if (file_exists(outPath)) return false;

    std::ifstream in(inPath, std::ios::binary);
    std::ofstream out(outPath, std::ios::binary);
    if (!in || !out) return false;

    // Detect UTF-8 BOM
    char bom[3] = {};
    in.read(bom, 3);
    bool hasBom = (uint8_t)bom[0] == 0xEF && (uint8_t)bom[1] == 0xBB && (uint8_t)bom[2] == 0xBF;

    std::vector<uint8_t> data;
    if (!hasBom) {
        in.seekg(0);
    }
    else {
        // Skip BOM — will be restored later
    }

    // Read full file
    char c;
    while (in.get(c)) {
        data.push_back(static_cast<uint8_t>(c));
    }

    // Write header
    uint32_t magic = 0x50515130;
    uint64_t size = data.size();
    uint8_t bomFlag = hasBom ? 1 : 0;

    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&size), sizeof(size));
    out.write(reinterpret_cast<const char*>(&bomFlag), sizeof(bomFlag));

    // Compress
    TextPredictor predictor;
    BitEncoder encoder(out);
    for (uint8_t byte : data) {
        for (int i = 7; i >= 0; --i) {
            int bit = (byte >> i) & 1;
            encoder.encode(bit, predictor.p());
            predictor.update(bit);
        }
    }

    encoder.flush();
    return true;
}

bool paq_decompress(const std::string& inPath, const std::string& outPath) {
    if (file_exists(outPath)) return false;

    std::ifstream in(inPath, std::ios::binary);
    std::ofstream out(outPath, std::ios::binary);
    if (!in || !out) return false;

    // Read header
    uint32_t magic;
    uint64_t size;
    uint8_t bomFlag;

    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&size), sizeof(size));
    in.read(reinterpret_cast<char*>(&bomFlag), sizeof(bomFlag));

    if (magic != 0x50515130) {
        throw std::runtime_error("Invalid file format.");
    }

    // Write BOM if needed
    if (bomFlag == 1) {
        out.put(static_cast<char>(0xEF));
        out.put(static_cast<char>(0xBB));
        out.put(static_cast<char>(0xBF));
    }


    TextPredictor predictor;
    BitDecoder decoder(in);
    std::vector<uint8_t> outData;

    // Write final output
    for (uint64_t i = 0; i < size; ++i) {
        int byte = 0; // <--- FIXED: reset each time
        for (int j = 7; j >= 0; --j) {
            int bit = decoder.decode(predictor.p());
            predictor.update(bit);
            byte |= (bit << j);
        }
        out.put(static_cast<char>(byte));
    }

    return true;
}

void Compressor::compress(const std::string& inPath, const std::string& outPath) {
    if (!paq_compress(inPath, outPath))
        throw std::runtime_error("Compression failed or output file exists.");
}

void Compressor::decompress(const std::string& inPath, const std::string& outPath) {
    if (!paq_decompress(inPath, outPath))
        throw std::runtime_error("Decompression failed or output file exists.");
}
