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

    class MatchPredictor {
        static constexpr int HISTORY_SIZE = 8192;
        std::vector<uint8_t> history;
        int historyPos = 0;
        uint8_t matchByte = 0;
        bool matchFound = false;
        int bitPos = 7;

    public:
        MatchPredictor() : history(HISTORY_SIZE, 0) {}

        void findMatch(uint8_t prev3, uint8_t prev2, uint8_t prev1) {
            matchFound = false;
            for (int i = 3; i < HISTORY_SIZE - 4; ++i) {
                int idx0 = (historyPos - i + HISTORY_SIZE) % HISTORY_SIZE;
                int idx1 = (idx0 + 1) % HISTORY_SIZE;
                int idx2 = (idx0 + 2) % HISTORY_SIZE;

                if (history[idx0] == prev3 &&
                    history[idx1] == prev2 &&
                    history[idx2] == prev1) {

                    int idx3 = (idx0 + 3) % HISTORY_SIZE;
                    matchByte = history[idx3];
                    matchFound = true;
                    bitPos = 7;
                    break;
                }
            }
        }

        int p() const {
            if (!matchFound) return 32768; // neutral
            return ((matchByte >> bitPos) & 1) ? 45000 : 20000; // softer prediction
        }

        void update(int bit) {
            if (matchFound && bitPos > 0) --bitPos;
        }

        void addByte(uint8_t b) {
            history[historyPos] = b;
            historyPos = (historyPos + 1) % HISTORY_SIZE;
        }

        bool isMatch() const {
            return matchFound;
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

    // Compression with Text + Match Predictors
    TextPredictor textPredictor;
    MatchPredictor matchPredictor;

    BitEncoder encoder(out);
    uint8_t prev1 = 0, prev2 = 0, prev3=0;

    for (uint8_t byte : data) {
        matchPredictor.findMatch(prev1, prev2, prev3);

        for (int i = 7; i >= 0; --i) {
            int bit = (byte >> i) & 1;

            int p1 = textPredictor.p();
            int p2 = matchPredictor.p();
            int mixed = matchPredictor.isMatch() ? (p1 + p2) / 2 : p1;

            encoder.encode(bit, mixed);
            textPredictor.update(bit);
            matchPredictor.update(bit);
        }

        matchPredictor.addByte(byte);
        prev3 = prev2;
        prev2 = prev1;
        prev1 = byte;
    }

    encoder.flush();
    return true;
}

bool paq_decompress(const std::string& inPath, const std::string& outPath) {

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

    // Write UTF-8 BOM if present
    if (bomFlag == 1) {
        out.put(static_cast<char>(0xEF));
        out.put(static_cast<char>(0xBB));
        out.put(static_cast<char>(0xBF));
    }

    // Predictors
    TextPredictor textPredictor;
    MatchPredictor matchPredictor;
    BitDecoder decoder(in);

    // Previous byte context
    uint8_t prev1 = 0, prev2 = 0, prev3 = 0;

    for (uint64_t i = 0; i < size; ++i) {
        uint8_t byte = 0;

        matchPredictor.findMatch(prev3, prev2, prev1);

        for (int j = 7; j >= 0; --j) {
            int p1 = textPredictor.p();
            int p2 = matchPredictor.p();
            int mixed = matchPredictor.isMatch() ? (p1 + p2) / 2 : p1;

            int bit = decoder.decode(mixed);
            textPredictor.update(bit);
            matchPredictor.update(bit);
            byte |= (bit << j);
        }

        out.put(static_cast<char>(byte));
        matchPredictor.addByte(byte);

        // Update context
        prev3 = prev2;
        prev2 = prev1;
        prev1 = byte;
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
