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

        matchPredictor.addByte(byte);
        prev3 = prev2;
        prev2 = prev1;
        prev1 = byte;
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
