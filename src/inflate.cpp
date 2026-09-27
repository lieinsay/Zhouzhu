#include "inflate.h"

#include <algorithm>
#include <cstring>

namespace {

constexpr int LUT_BITS = 10;
constexpr int MAX_BITS = 15;

// 位读取：DEFLATE 的位从每个字节的最低位起
struct Bits {
    const uint8_t *p = nullptr;
    size_t size = 0, pos = 0;
    uint64_t buf = 0;
    int count = 0;

    void refill() {
        while (count <= 56) {
            const uint64_t b = pos < size ? p[pos] : 0; // 过了尾巴补 0（预读）；真读过头由 past_end 判
            pos++;
            buf |= b << count;
            count += 8;
        }
    }
    // 读掉的位超出了流：坏了（截断或乱码）
    bool past_end() const { return pos * 8 - size_t(count) > size * 8; }
    uint32_t peek(int p_n) {
        if (count < p_n) {
            refill();
        }
        return uint32_t(buf & ((uint64_t(1) << p_n) - 1));
    }
    void drop(int p_n) {
        buf >>= p_n;
        count -= p_n;
    }
    uint32_t get(int p_n) {
        if (p_n == 0) {
            return 0;
        }
        const uint32_t v = peek(p_n);
        drop(p_n);
        return v;
    }
    // 丢到字节边界；返回下一个没读的字节的位置
    size_t align() {
        drop(count & 7);
        return pos - size_t(count / 8);
    }
};

// 一套规范霍夫曼码：短码查表，长码逐位走
struct Huffman {
    uint16_t lut[1 << LUT_BITS]; // (符号 << 4) | 码长；0 = 不在表里（长码或没有）
    uint16_t counts[MAX_BITS + 1];
    uint16_t symbols[320];
    bool ok = false;

    bool build(const uint8_t *p_lengths, int p_n) {
        std::memset(lut, 0, sizeof(lut));
        std::memset(counts, 0, sizeof(counts));
        for (int k = 0; k < p_n; k++) {
            counts[p_lengths[k]]++;
        }
        counts[0] = 0;
        // 码长合不合法（过满不行；不满可以：只有一个距离码时常见）
        int left = 1;
        for (int len = 1; len <= MAX_BITS; len++) {
            left <<= 1;
            left -= counts[len];
            if (left < 0) {
                return false;
            }
        }
        uint16_t offs[MAX_BITS + 2];
        offs[1] = 0;
        for (int len = 1; len < MAX_BITS; len++) {
            offs[len + 1] = uint16_t(offs[len] + counts[len]);
        }
        for (int k = 0; k < p_n; k++) {
            if (p_lengths[k] != 0) {
                symbols[offs[p_lengths[k]]++] = uint16_t(k);
            }
        }
        // 查表：规范码（高位在前）按位反过来，填满所有以它结尾的下标
        int code = 0, index = 0;
        for (int len = 1; len <= MAX_BITS; len++) {
            for (int k = 0; k < counts[len]; k++) {
                if (len <= LUT_BITS) {
                    int rev = 0;
                    for (int b = 0; b < len; b++) {
                        rev |= ((code >> b) & 1) << (len - 1 - b);
                    }
                    const uint16_t entry = uint16_t((symbols[index] << 4) | len);
                    for (int i = rev; i < (1 << LUT_BITS); i += 1 << len) {
                        lut[i] = entry;
                    }
                }
                code++;
                index++;
            }
            code <<= 1;
        }
        ok = true;
        return true;
    }

    // 解一个符号；坏了返回 −1
    int decode(Bits &r_bits) const {
        const uint32_t look = r_bits.peek(LUT_BITS);
        const uint16_t e = lut[look];
        if (e != 0) {
            r_bits.drop(e & 15);
            return e >> 4;
        }
        // 长码（或非法）：逐位走规范码（同 zlib 的 puff.c）
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= MAX_BITS; len++) {
            code |= int(r_bits.get(1));
            const int count = counts[len];
            if (code - count < first) {
                return symbols[index + (code - first)];
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

const uint16_t LEN_BASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const uint8_t LEN_EXTRA[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const uint16_t DIST_BASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289,
    16385, 24577 };
const uint8_t DIST_EXTRA[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

bool codes(Bits &r_bits, const Huffman &p_lit, const Huffman &p_dist, std::vector<uint8_t> &r_out, std::string &r_error) {
    for (;;) {
        int sym = p_lit.decode(r_bits);
        if (sym < 0 || r_bits.past_end()) {
            r_error = "DEFLATE：字面量 / 长度码坏了";
            return false;
        }
        if (sym < 256) {
            r_out.push_back(uint8_t(sym));
            continue;
        }
        if (sym == 256) {
            return true;
        }
        sym -= 257;
        if (sym >= 29) {
            r_error = "DEFLATE：长度码越界";
            return false;
        }
        const size_t len = size_t(LEN_BASE[sym]) + r_bits.get(LEN_EXTRA[sym]);
        const int dsym = p_dist.decode(r_bits);
        if (dsym < 0 || dsym >= 30) {
            r_error = "DEFLATE：距离码坏了";
            return false;
        }
        const size_t dist = size_t(DIST_BASE[dsym]) + r_bits.get(DIST_EXTRA[dsym]);
        if (dist > r_out.size()) {
            r_error = "DEFLATE：距离超出已解出的";
            return false;
        }
        const size_t from = r_out.size() - dist;
        if (dist >= len) {
            const size_t at = r_out.size();
            r_out.resize(at + len);
            std::memcpy(r_out.data() + at, r_out.data() + from, len);
        } else {
            for (size_t k = 0; k < len; k++) {
                r_out.push_back(r_out[from + k]); // 重叠：一个一个抄
            }
        }
    }
}

bool dynamic_tables(Bits &r_bits, Huffman &r_lit, Huffman &r_dist, std::string &r_error) {
    static const uint8_t ORDER[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    const int nlen = int(r_bits.get(5)) + 257, ndist = int(r_bits.get(5)) + 1, ncode = int(r_bits.get(4)) + 4;
    if (nlen > 286 || ndist > 30) {
        r_error = "DEFLATE：码表太长";
        return false;
    }
    uint8_t lengths[320] = {};
    for (int k = 0; k < ncode; k++) {
        lengths[ORDER[k]] = uint8_t(r_bits.get(3));
    }
    Huffman lencode;
    if (!lencode.build(lengths, 19)) {
        r_error = "DEFLATE：码长的码表不对";
        return false;
    }
    int index = 0;
    std::memset(lengths, 0, sizeof(lengths));
    while (index < nlen + ndist) {
        const int sym = lencode.decode(r_bits);
        if (sym < 0) {
            r_error = "DEFLATE：码长坏了";
            return false;
        }
        if (sym < 16) {
            lengths[index++] = uint8_t(sym);
            continue;
        }
        uint8_t value = 0;
        int repeat = 0;
        if (sym == 16) {
            if (index == 0) {
                r_error = "DEFLATE：重复前一个码长，可前面没有";
                return false;
            }
            value = lengths[index - 1];
            repeat = 3 + int(r_bits.get(2));
        } else if (sym == 17) {
            repeat = 3 + int(r_bits.get(3));
        } else {
            repeat = 11 + int(r_bits.get(7));
        }
        if (index + repeat > nlen + ndist) {
            r_error = "DEFLATE：码长重复越界";
            return false;
        }
        while (repeat-- > 0) {
            lengths[index++] = value;
        }
    }
    if (lengths[256] == 0) {
        r_error = "DEFLATE：没有块尾码";
        return false;
    }
    if (!r_lit.build(lengths, nlen) || !r_dist.build(lengths + nlen, ndist)) {
        r_error = "DEFLATE：码表过满";
        return false;
    }
    return true;
}

// 固定码表（块类型 1）
struct FixedTables {
    Huffman lit, dist;
    FixedTables() {
        uint8_t l[288];
        for (int k = 0; k < 288; k++) {
            l[k] = k < 144 ? 8 : (k < 256 ? 9 : (k < 280 ? 7 : 8));
        }
        lit.build(l, 288);
        uint8_t d[30];
        std::memset(d, 5, sizeof(d));
        dist.build(d, 30);
    }
};

} // namespace

bool raw_inflate(const uint8_t *p_data, size_t p_size, std::vector<uint8_t> &r_out, std::string &r_error, size_t *r_consumed) {
    Bits bits;
    bits.p = p_data;
    bits.size = p_size;
    static const FixedTables fixed; // 函数里的静态量：C++11 起头一回构造是线程安全的
    bool last = false;
    while (!last) {
        last = bits.get(1) != 0;
        const uint32_t type = bits.get(2);
        if (type == 0) {
            // 不压的块：对齐到字节，LEN、NLEN
            size_t at = bits.align();
            bits.buf = 0;
            bits.count = 0;
            if (at + 4 > p_size) {
                r_error = "DEFLATE：不压的块截断";
                return false;
            }
            const size_t len = size_t(p_data[at]) | (size_t(p_data[at + 1]) << 8);
            const size_t nlen = size_t(p_data[at + 2]) | (size_t(p_data[at + 3]) << 8);
            if ((len ^ 0xFFFF) != nlen || at + 4 + len > p_size) {
                r_error = "DEFLATE：不压的块长度不对";
                return false;
            }
            r_out.insert(r_out.end(), p_data + at + 4, p_data + at + 4 + len);
            bits.pos = at + 4 + len;
        } else if (type == 1) {
            if (!codes(bits, fixed.lit, fixed.dist, r_out, r_error)) {
                return false;
            }
        } else if (type == 2) {
            Huffman lit, dist;
            if (!dynamic_tables(bits, lit, dist, r_error) || !codes(bits, lit, dist, r_out, r_error)) {
                return false;
            }
        } else {
            r_error = "DEFLATE：块类型 3 不存在";
            return false;
        }
        if (bits.past_end()) {
            r_error = "DEFLATE：流截断";
            return false;
        }
    }
    if (r_consumed) {
        *r_consumed = bits.align();
    }
    return true;
}

bool zlib_inflate(const uint8_t *p_data, size_t p_size, std::vector<uint8_t> &r_out, size_t p_expected, std::string &r_error) {
    if (p_size < 6) {
        r_error = "zlib：太短";
        return false;
    }
    const uint8_t cmf = p_data[0], flg = p_data[1];
    if ((cmf & 15) != 8 || ((unsigned(cmf) << 8) | flg) % 31 != 0 || (flg & 32) != 0) {
        r_error = "zlib：头不对（只认 DEFLATE、不带预设字典）";
        return false;
    }
    r_out.clear();
    if (p_expected > 0) {
        r_out.reserve(p_expected);
    }
    size_t used = 0;
    if (!raw_inflate(p_data + 2, p_size - 2, r_out, r_error, &used)) {
        return false;
    }
    const size_t tail = 2 + used;
    if (tail + 4 > p_size) {
        r_error = "zlib：没有 Adler-32";
        return false;
    }
    uint32_t a = 1, b = 0;
    const size_t n = r_out.size();
    size_t k = 0;
    while (k < n) {
        const size_t block = std::min<size_t>(n - k, 5552); // 5552 字节之内 b 不溢出
        for (size_t j = 0; j < block; j++) {
            a += r_out[k + j];
            b += a;
        }
        a %= 65521u;
        b %= 65521u;
        k += block;
    }
    const uint32_t want = (uint32_t(p_data[tail]) << 24) | (uint32_t(p_data[tail + 1]) << 16) | (uint32_t(p_data[tail + 2]) << 8) | uint32_t(p_data[tail + 3]);
    if (((b << 16) | a) != want) {
        r_error = "zlib：Adler-32 对不上";
        return false;
    }
    if (p_expected > 0 && r_out.size() != p_expected) {
        r_error = "zlib：解开的字节数 " + std::to_string(r_out.size()) + " ≠ " + std::to_string(p_expected);
        return false;
    }
    return true;
}
