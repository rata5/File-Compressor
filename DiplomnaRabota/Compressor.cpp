#include "Compressor.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <numeric>
#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

static constexpr size_t BLOCK_SIZE = 100 * 1024;

static std::pair<std::string, uint32_t> bwtTransform(const std::string& s) {
    int n = int(s.size());
    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        for (int k = 0; k < n; ++k) {
            char ca = s[(a + k) % n];
            char cb = s[(b + k) % n];
            if (ca != cb) return ca < cb;
        }
        return false;
        });
    std::string last(n, '\0');
    uint32_t primary = 0;
    for (int i = 0; i < n; ++i) {
        int j = idx[i];
        last[i] = s[(j + n - 1) % n];
        if (j == 0) primary = i;
    }
    return { last, primary };
}

static std::vector<uint8_t> mtfEncode(const std::string& bwt) {
    std::vector<uint8_t> symbols(256);
    std::iota(symbols.begin(), symbols.end(), 0);
    std::vector<uint8_t> out;
    out.reserve(bwt.size());
    for (unsigned char c : bwt) {
        auto it = std::find(symbols.begin(), symbols.end(), c);
        int idx = int(it - symbols.begin());
        out.push_back(idx);
        symbols.erase(it);
        symbols.insert(symbols.begin(), c);
    }
    return out;
}

static std::vector<uint8_t> rleZero(const std::vector<uint8_t>& mtf) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < mtf.size();) {
        if (mtf[i] == 0) {
            size_t run = 1;
            while (i + run < mtf.size() && mtf[i + run] == 0 && run < 255) ++run;
            out.push_back(0);
            out.push_back(static_cast<uint8_t>(run));
            i += run;
        }
        else {
            out.push_back(mtf[i++]);
        }
    }
    return out;
}

static std::vector<uint8_t> rleZeroDecode(const std::vector<uint8_t>& rle) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < rle.size();) {
        if (rle[i] == 0 && i + 1 < rle.size()) {
            size_t run = rle[i + 1];
            out.insert(out.end(), run, 0);
            i += 2;
        }
        else {
            out.push_back(rle[i++]);
        }
    }
    return out;
}

static std::string mtfDecode(const std::vector<uint8_t>& mtf) {
    std::vector<uint8_t> symbols(256);
    std::iota(symbols.begin(), symbols.end(), 0);
    std::string out;
    out.reserve(mtf.size());
    for (auto idx : mtf) {
        uint8_t c = symbols[idx];
        out.push_back(static_cast<char>(c));
        symbols.erase(symbols.begin() + idx);
        symbols.insert(symbols.begin(), c);
    }
    return out;
}

static std::string bwtInverse(const std::string& last, uint32_t primary) {
    int n = int(last.size());
    std::vector<int> count(256, 0), pos(256, 0), next(n);

    for (unsigned char c : last) ++count[c];
    for (int c = 1; c < 256; ++c)
        pos[c] = pos[c - 1] + count[c - 1];

    for (int i = 0; i < n; ++i) {
        unsigned char c = last[i];
        next[pos[c]++] = i;
    }

    int idx = next[primary];
    std::string out(n, '\0');
    for (int i = 0; i < n; ++i) {
        out[i] = last[idx];
        idx = next[idx];
    }
    return out;
}

class IModel {
public:
    virtual ~IModel() = default;
    virtual uint16_t predict() const = 0;
    virtual void updateBit(int bit) = 0;
    virtual void updateByte(uint8_t b) = 0;
};

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
        auto& entry = table[key];
        if (bit) ++entry.second;
        else     ++entry.first;
    }

    void updateByte(uint8_t b) override {
        if (history.size() == order) history.pop_front();
        history.push_back(b);
    }
};

class BitContextModel : public IModel {
    size_t order;
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> table;
    std::deque<bool> history;
public:
    explicit BitContextModel(size_t ord)
        : order(ord) {
    }

    uint16_t predict() const override {
        if (history.size() < order) return 0x8000;
        uint32_t key = 0;
        for (bool b : history) key = (key << 1) | (b ? 1 : 0);
        auto it = table.find(key);
        uint32_t c0 = 1, c1 = 1;
        if (it != table.end()) {
            c0 = it->second.first + 1;
            c1 = it->second.second + 1;
        }
        return static_cast<uint16_t>((uint64_t(c1) * 0xFFFF) / (c0 + c1));
    }

    void updateBit(int bit) override {
        if (history.size() < order) {
            history.push_back(bit);
            return;
        }
        uint32_t key = 0;
        for (bool b : history) key = (key << 1) | (b ? 1 : 0);
        auto& entry = table[key];
        if (bit) ++entry.second;
        else     ++entry.first;
        history.pop_front();
        history.push_back(bit);
    }

    void updateByte(uint8_t) override {
    }
};

class MatchModel : public IModel {
    const size_t contextSize;
    static constexpr size_t WINDOW_SIZE = 1 << 20;

    std::vector<uint8_t> buffer;
    size_t bufPos = 0;

    std::unordered_map<uint64_t, size_t> lastPos;
    size_t matchPos = std::string::npos;
    int matchLen = 0;
    int bitPos = 0;

public:
    MatchModel(size_t ctxSize = 4)
        : contextSize(ctxSize), buffer(WINDOW_SIZE, 0) {
    }

    uint16_t predict() const override {
        if (matchPos == std::string::npos || matchLen < 1)
            return 32768;

        uint8_t nextByte = buffer[(matchPos + matchLen) % WINDOW_SIZE];
        int nextBit = (nextByte >> (7 - bitPos)) & 1;

        int confidence;
        if (matchLen == 1)        confidence = 256;
        else if (matchLen == 2)   confidence = 1024;
        else if (matchLen == 3)   confidence = 4096;
        else                      confidence = 8192;

        int p = nextBit
            ? 32768 + confidence
            : 32768 - confidence;

        return static_cast<uint16_t>(std::clamp(p, 1, 65534));
    }

    void updateBit(int bit) override {
        if (++bitPos == 8) {
            bitPos = 0;
            if (matchLen > 0 && matchPos != std::string::npos) {
                matchPos = (matchPos + 1) % WINDOW_SIZE;
                ++matchLen;
                if (matchLen >= WINDOW_SIZE)
                    matchLen = 0, matchPos = std::string::npos;
            }
        }
    }

    void updateByte(uint8_t b) override {
        buffer[bufPos] = b;

        if (contextSize <= bufPos || bufPos >= contextSize) {
            size_t base = bufPos >= contextSize
                ? bufPos - contextSize
                : WINDOW_SIZE + bufPos - contextSize;

            uint64_t key = 0;
            for (size_t i = 0; i < contextSize; ++i) {
                key = (key << 8) | buffer[(base + i) % WINDOW_SIZE];
            }

            auto it = lastPos.find(key);
            if (it != lastPos.end()) {
                matchPos = it->second;
                matchLen = 1;
                bitPos = 0;
            }
            else {
                matchPos = std::string::npos;
                matchLen = 0;
                bitPos = 0;
            }

            lastPos[key] = bufPos;
        }

        bufPos = (bufPos + 1) % WINDOW_SIZE;
    }
};

class LZPModel : public IModel {
    static constexpr size_t N = 1 << 20;
    std::vector<uint8_t> buf;
    std::vector<size_t>  nxt;
    size_t pos = 0;
    uint8_t prev = 0;

public:
    LZPModel()
        : buf(N),
        nxt(N, std::string::npos)
    {
    }

    uint16_t predict() const override {
        size_t p = nxt[pos];
        if (p == std::string::npos) return 32768;
        uint8_t nb = buf[(p + 1) % N];
        return (nb & 0x80) ? 49152 : 16384;
    }

    void updateBit(int) override {
    }

    void updateByte(uint8_t b) override {
        buf[pos] = b;
        size_t key = (size_t(prev) << 8) | b;
        nxt[pos] = nxt[key % N];
        nxt[key % N] = pos;
        prev = b;
        pos = (pos + 1) % N;
    }
};

class Mixer {
    std::vector<IModel*> mods;
    std::vector<double> w;
    double lr;
public:
    Mixer(const std::vector<IModel*>& M, double learningRate = 0.005)
        : mods(M), w(M.size(), 1.0), lr(learningRate) {
        if (w.empty()) w.back() = 2.0;
    }

    uint16_t mix() const {
        double sum = 0.0;
        for (size_t i = 0; i < mods.size(); ++i) {
            double pi = std::clamp(mods[i]->predict() / 65535.0, 0.0001, 0.9999);
            sum += w[i] * std::log(pi / (1.0 - pi));
        }
        double p = 1.0 / (1.0 + std::exp(-sum));
        return static_cast<uint16_t>(p * 65535.0 + 0.5);
    }

    void update(uint16_t p1, int bit) {
        double p = std::clamp(p1 / 65535.0, 0.0001, 0.9999);
        double error = bit - p;
        for (size_t i = 0; i < mods.size(); ++i) {
            double pi = std::clamp(mods[i]->predict() / 65535.0, 0.0001, 0.9999);
            double grad = std::log(pi / (1.0 - pi));
            w[i] += lr * error * grad;
        }
    }
};

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
    namespace fs = std::filesystem;
    if (fs::exists(outPath))
        throw std::runtime_error("Output already exists");
    std::ifstream fin(inPath, std::ios::binary);
    if (!fin) throw std::runtime_error("Cannot open input");
    std::string input((std::istreambuf_iterator<char>(fin)), {});
    fin.close();
    std::ofstream out(outPath, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot open output");
    uint64_t fullSize = input.size();
    out.write(reinterpret_cast<const char*>(&fullSize), sizeof(fullSize));
    ByteContextModel bcm1(1), bcm2(2), bcm3(3), bcm4(4);
    BitContextModel bitm(24);
    MatchModel match4(4), match8(8);
    LZPModel lzp;
    std::vector<IModel*> mods = { &bcm1, &bcm2, &bcm3, &bcm4, &bitm, &match4, &match8, &lzp };
    Mixer mixer(mods, 0.001);
    auto [bwtLast, primary] = bwtTransform(input);
    auto mtf = mtfEncode(bwtLast);
    auto rle = rleZero(mtf);
    std::ostringstream tmp(std::ios::binary);
    RangeCoder coder(tmp);
    for (uint8_t byte : rle) {
        for (int b = 7; b >= 0; --b) {
            int bit = (byte >> b) & 1;
            uint16_t p1 = mixer.mix();
            coder.encode(bit, p1);
            mixer.update(p1, bit);
            for (IModel* m : mods)
                m->updateBit(bit);
        }
        for (auto* m : mods) m->updateByte(byte);
    }
    coder.finish();
    std::string compData = tmp.str();
    uint32_t blockLen = uint32_t(input.size());
    uint32_t rleCount = uint32_t(rle.size());
    uint32_t compSize = uint32_t(compData.size());
    out.write(reinterpret_cast<const char*>(&blockLen), sizeof(blockLen));
    out.write(reinterpret_cast<const char*>(&primary), sizeof(primary));
    out.write(reinterpret_cast<const char*>(&rleCount), sizeof(rleCount));
    out.write(reinterpret_cast<const char*>(&compSize), sizeof(compSize));
    out.write(compData.data(), compData.size());
}

void Compressor::decompress(const std::string& inPath, const std::string& outPath) {
    namespace fs = std::filesystem;
    if (!fs::exists(inPath))
        throw std::runtime_error("Input missing");
    std::ifstream in(inPath, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open input");
    uint64_t fullSize;
    in.read(reinterpret_cast<char*>(&fullSize), sizeof(fullSize));
    ByteContextModel bcm1(1), bcm2(2), bcm3(3), bcm4(4);
    BitContextModel bitm(24);
    MatchModel match4(4), match8(8);
    LZPModel lzp;
    std::vector<IModel*> mods = { &bcm1, &bcm2, &bcm3, &bcm4, &bitm, &match4, &match8, &lzp };
    Mixer mixer(mods, 0.001);
    std::ofstream out(outPath, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot open output");
    while (true) {
        uint32_t blockLen, primary, rleCount, compSize;
        if (!in.read(reinterpret_cast<char*>(&blockLen), sizeof(blockLen)))
            break;
        in.read(reinterpret_cast<char*>(&primary), sizeof(primary));
        in.read(reinterpret_cast<char*>(&rleCount), sizeof(rleCount));
        in.read(reinterpret_cast<char*>(&compSize), sizeof(compSize));
        std::vector<char> buf(compSize);
        in.read(buf.data(), compSize);
        std::istringstream tmpIn(std::string(buf.data(), compSize), std::ios::binary);
        RangeDecoder dec(tmpIn);
        std::vector<uint8_t> rle;
        rle.reserve(rleCount);
        for (uint32_t i = 0; i < rleCount; ++i) {
            uint8_t c = 0;
            for (int b = 7; b >= 0; --b) {
                uint16_t p1 = mixer.mix();
                int bit = dec.decode(p1);
                mixer.update(p1, bit);
                for (IModel* model : mods)
                    model->updateBit(bit);
                c |= (uint8_t(bit) << b);
            }
            rle.push_back(c);
            for (IModel* model : mods)
                model->updateByte(c);
        }
        auto mtf = rleZeroDecode(rle);
        auto bwt = mtfDecode(mtf);
        auto block = bwtInverse(bwt, primary);
        out.write(block.data(), blockLen);
    }
}