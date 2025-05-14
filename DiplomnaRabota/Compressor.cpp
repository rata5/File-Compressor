#include "Compressor.h"
#include <fstream>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <filesystem>
#include <deque>
#include <unordered_map>
#include <utility> 

namespace fs = std::filesystem;

// --- Model interface ---
class IModel {
public:
    virtual ~IModel() = default;
    virtual uint16_t predict() const = 0;      // probability of bit=1
    virtual void updateBit(int bit) = 0;       // after encoding/decoding a bit
    virtual void updateByte(uint8_t b) = 0;    // after full byte
};

// --- Byte-order context model (unordered_map) ---
class ByteContextModel : public IModel {
    size_t order;
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> table;
    std::deque<uint8_t> history;
public:
    explicit ByteContextModel(size_t ord)
        : order(ord) {
    }

    uint16_t predict() const override {
        if (history.size() < order) return 0x8000;
        uint32_t key = 0;
        for (auto b : history) key = (key << 8) | b;
        auto it = table.find(key);
        // Laplace smoothing
        uint32_t c0 = 1, c1 = 1;
        if (it != table.end()) {
            c0 = it->second.first + 1;
            c1 = it->second.second + 1;
        }
        return static_cast<uint16_t>((uint64_t(c1) * 0xFFFF) / (c0 + c1));
    }

    void updateBit(int bit) override {
        if (history.size() < order) return;
        uint32_t key = 0;
        for (auto b : history) key = (key << 8) | b;
        auto& entry = table[key];  // default-constructs to {0,0}
        if (bit) ++entry.second;
        else     ++entry.first;
    }

    void updateByte(uint8_t b) override {
        if (history.size() == order) history.pop_front();
        history.push_back(b);
    }
};

// --- Bit-prefix context model ---
class BitContextModel : public IModel {
    size_t order;
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> table;
    std::deque<bool> history;
public:
    explicit BitContextModel(size_t ord)
        : order(ord) {
    }

    uint16_t predict() const override {
        if (history.size() < order) return 0x8000;  // uniform until enough bits
        // build a bit‑sequence key
        uint32_t key = 0;
        for (bool b : history) key = (key << 1) | (b ? 1 : 0);
        auto it = table.find(key);
        // Laplace smoothing: pretend we’ve seen each outcome once
        uint32_t c0 = 1, c1 = 1;
        if (it != table.end()) {
            c0 = it->second.first + 1;
            c1 = it->second.second + 1;
        }
        return static_cast<uint16_t>((uint64_t(c1) * 0xFFFF) / (c0 + c1));
    }

    void updateBit(int bit) override {
        if (history.size() < order) {
            // slide in the bit into history, but don’t update counts yet
            history.push_back(bit);
            return;
        }
        // history is already size==order
        uint32_t key = 0;
        for (bool b : history) key = (key << 1) | (b ? 1 : 0);
        auto& entry = table[key];  // default-constructs to {0,0}
        if (bit) ++entry.second;
        else     ++entry.first;
        // now slide window
        history.pop_front();
        history.push_back(bit);
    }

    void updateByte(uint8_t) override {
        // nothing to do here for bit model
    }
};

// --- Mixer ---
class Mixer {
    std::vector<IModel*> mods;
    std::vector<double> w;
    double lr;
public:
    Mixer(const std::vector<IModel*>& M, double learningRate = 0.05)
        : mods(M), w(M.size(), 1.0), lr(learningRate) {
    }

    uint16_t mix() const {
        double num = 0, den = 0;
        for (size_t i = 0; i < mods.size(); ++i) {
            double p = mods[i]->predict() / 65535.0;
            num += w[i] * p;
            den += w[i];
        }
        double p = (den > 0 ? num / den : 0.5);
        return static_cast<uint16_t>(p * 0xFFFF);
    }

    void update(uint16_t p1, int bit) {
        double p = p1 / 65535.0;
        double err = bit - p;
        for (size_t i = 0; i < mods.size(); ++i) {
            double m = mods[i]->predict() / 65535.0;
            w[i] = std::max(0.0, w[i] + lr * err * (m - p));
        }
    }
};

// --- Range coder ---
class RangeCoder {
    uint32_t low = 0, high = 0xFFFFFFFF, follow = 0;
    std::ostream& os;
public:
    explicit RangeCoder(std::ostream& o) : os(o) {}
    void encode(int bit, uint16_t p1) {
        uint32_t range = high - low + 1;
        uint32_t bound = low + (uint64_t(range) * (0xFFFF - p1) >> 16);
        if (bit) low = bound + 1; else high = bound;
        for (;;) {
            if ((high & 0xFF000000) == (low & 0xFF000000)) {
                os.put(static_cast<char>(high >> 24));
                for (; follow; --follow) os.put(static_cast<char>(~(high >> 24)));
                low <<= 8; high = (high << 8) | 0xFF;
            }
            else if ((low & 0x80000000) && !(high & 0x80000000)) {
                ++follow;
                low = (low << 1) & 0x7FFFFFFF;
                high = ((high ^ 0x80000000) << 1) | 1;
            }
            else break;
        }
    }
    void finish() {
        for (int i = 0; i < 4; ++i) {
            os.put(static_cast<char>(low >> 24));
            low <<= 8;
        }
    }
};

// --- Range decoder ---
class RangeDecoder {
    uint32_t low = 0, high = 0xFFFFFFFF, code = 0;
    std::istream& is;
public:
    explicit RangeDecoder(std::istream& i) : is(i) {
        for (int k = 0; k < 4; ++k) code = (code << 8) | static_cast<uint8_t>(is.get());
    }
    int decode(uint16_t p1) {
        uint32_t range = high - low + 1;
        uint32_t bound = low + (uint64_t(range) * (0xFFFF - p1) >> 16);
        int bit;
        if (code <= bound) { bit = 0; high = bound; }
        else { bit = 1; low = bound + 1; }
        for (;;) {
            if ((high & 0xFF000000) == (low & 0xFF000000)) {
                low <<= 8; high = (high << 8) | 0xFF;
                code = (code << 8) | static_cast<uint8_t>(is.get());
            }
            else if ((low & 0x80000000) && !(high & 0x80000000)) {
                low = (low << 1) & 0x7FFFFFFF;
                high = ((high ^ 0x80000000) << 1) | 1;
                code = ((code ^ 0x80000000) << 1) | (static_cast<uint8_t>(is.get()) & 1);
            }
            else break;
        }
        return bit;
    }
};

static bool exists(const std::string& p) { return fs::exists(p); }

void Compressor::compress(const std::string& inPath, const std::string& outPath) {
    if (fs::exists(outPath)) throw std::runtime_error("Output already exists");
    std::ifstream in(inPath, std::ios::binary);
    std::ofstream out(outPath, std::ios::binary);
    if (!in || !out) throw std::runtime_error("Cannot open files");

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
    uint64_t n = data.size();
    out.write(reinterpret_cast<const char*>(&n), sizeof(n));  // Header

    ByteContextModel bcm1(1), bcm2(2);
    BitContextModel bitm(16);
    std::vector<IModel*> mods = { &bcm1, &bcm2, &bitm };
    Mixer mixer(mods, 0.01);
    RangeCoder coder(out);

    for (size_t i = 0; i < n; ++i) {
        uint8_t c = data[i];
        for (int b = 7; b >= 0; --b) {
            int bit = (c >> b) & 1;
            uint16_t p1 = mixer.mix();
            coder.encode(bit, p1);
            mixer.update(p1, bit);
            bitm.updateBit(bit);
            bcm1.updateBit(bit);
            bcm2.updateBit(bit);
        }
        bitm.updateByte(c);
        bcm1.updateByte(c);
        bcm2.updateByte(c);
    }

    coder.finish();
}

void Compressor::decompress(const std::string& inPath, const std::string& outPath) {
    if (!fs::exists(inPath)) throw std::runtime_error("Input missing");
    std::ifstream in(inPath, std::ios::binary);
    std::ofstream out(outPath, std::ios::binary);
    if (!in || !out) throw std::runtime_error("Cannot open files");

    uint64_t n;
    in.read(reinterpret_cast<char*>(&n), sizeof(n));  // Header

    ByteContextModel bcm1(1), bcm2(2);
    BitContextModel bitm(16);
    std::vector<IModel*> mods = { &bcm1, &bcm2, &bitm };
    Mixer mixer(mods, 0.01);
    RangeDecoder dec(in);

    for (size_t i = 0; i < n; ++i) {
        uint8_t c = 0;
        for (int b = 7; b >= 0; --b) {
            uint16_t p1 = mixer.mix();
            int bit = dec.decode(p1);
            mixer.update(p1, bit);
            bitm.updateBit(bit);
            bcm1.updateBit(bit);
            bcm2.updateBit(bit);
            c |= (bit << b);
        }
        bitm.updateByte(c);
        bcm1.updateByte(c);
        bcm2.updateByte(c);
        out.put(static_cast<char>(c));
    }
}