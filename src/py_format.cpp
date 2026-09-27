#include "py_format.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace pyfmt {

std::string repr(double p_x) {
    if (std::isnan(p_x)) {
        return "nan";
    }
    if (std::isinf(p_x)) {
        return p_x > 0 ? "inf" : "-inf";
    }
    if (p_x == 0.0) {
        return std::signbit(p_x) ? "-0.0" : "0.0";
    }
    // 最短的读回逐位相同的数字串（Ryu，与 CPython 的 dtoa 模式 0 同：最短、并列时取离真值最近的）
    char buf[64];
    const std::to_chars_result r = std::to_chars(buf, buf + sizeof(buf), p_x, std::chars_format::scientific);
    std::string s(buf, r.ptr);
    const bool neg = s[0] == '-';
    if (neg) {
        s.erase(0, 1);
    }
    const size_t e = s.find('e');
    const std::string mant = s.substr(0, e);
    const int exp10 = std::atoi(s.c_str() + e + 1);
    std::string digits;
    for (char c : mant) {
        if (c != '.') {
            digits.push_back(c);
        }
    }
    while (digits.size() > 1 && digits.back() == '0') {
        digits.pop_back();
    }
    const int decpt = exp10 + 1; // 值 = 0.d1d2… × 10^decpt
    const int nd = int(digits.size());
    std::string out;
    if (decpt <= -4 || decpt > 16) {
        out = digits.substr(0, 1);
        if (nd > 1) {
            out += '.';
            out += digits.substr(1);
        }
        out += 'e';
        out += exp10 < 0 ? '-' : '+';
        const int ae = exp10 < 0 ? -exp10 : exp10;
        if (ae < 10) {
            out += '0';
        }
        out += std::to_string(ae);
    } else if (decpt <= 0) {
        out = "0." + std::string(size_t(-decpt), '0') + digits;
    } else if (decpt >= nd) {
        out = digits + std::string(size_t(decpt - nd), '0') + ".0";
    } else {
        out = digits.substr(0, size_t(decpt)) + "." + digits.substr(size_t(decpt));
    }
    return neg ? "-" + out : out;
}

std::string str(int64_t p_v) {
    return std::to_string(p_v);
}

std::string fmt_g(double p_x) {
    if (std::isnan(p_x)) {
        return "nan";
    }
    if (std::isinf(p_x)) {
        return p_x > 0 ? "inf" : "-inf";
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", p_x);
    return buf;
}

std::string fmt_f(double p_x, int p_digits) {
    if (std::isnan(p_x)) {
        return "nan";
    }
    if (std::isinf(p_x)) {
        return p_x > 0 ? "inf" : "-inf";
    }
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%.*f", p_digits, p_x);
    return buf;
}

namespace {

void escape_to(std::string &r_out, const std::string &p_s) {
    r_out += '"';
    for (unsigned char c : p_s) {
        switch (c) {
            case '"':
                r_out += "\\\"";
                break;
            case '\\':
                r_out += "\\\\";
                break;
            case '\n':
                r_out += "\\n";
                break;
            case '\r':
                r_out += "\\r";
                break;
            case '\t':
                r_out += "\\t";
                break;
            case '\b':
                r_out += "\\b";
                break;
            case '\f':
                r_out += "\\f";
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", unsigned(c));
                    r_out += buf;
                } else {
                    r_out += char(c);
                }
        }
    }
    r_out += '"';
}

std::string json_float(double p_x) {
    if (std::isnan(p_x)) {
        return "NaN";
    }
    if (std::isinf(p_x)) {
        return p_x > 0 ? "Infinity" : "-Infinity";
    }
    return repr(p_x);
}

void dump_to(std::string &r_out, const skyisle::Json &p_v, int p_indent, int p_level, const char *p_nl) {
    using skyisle::Json;
    switch (p_v.type()) {
        case Json::NUL:
            r_out += "null";
            return;
        case Json::BOOL:
            r_out += p_v.as_bool() ? "true" : "false";
            return;
        case Json::INT:
            r_out += std::to_string(p_v.as_int());
            return;
        case Json::NUM:
            r_out += json_float(p_v.as_num());
            return;
        case Json::STR:
            escape_to(r_out, p_v.as_str());
            return;
        case Json::ARR: {
            if (p_v.items().empty()) {
                r_out += "[]";
                return;
            }
            const std::string inner = std::string(p_nl) + std::string(size_t(p_indent * (p_level + 1)), ' ');
            r_out += '[';
            bool first = true;
            for (const Json &x : p_v.items()) {
                if (!first) {
                    r_out += ',';
                }
                first = false;
                r_out += inner;
                dump_to(r_out, x, p_indent, p_level + 1, p_nl);
            }
            r_out += p_nl;
            r_out += std::string(size_t(p_indent * p_level), ' ');
            r_out += ']';
            return;
        }
        case Json::OBJ: {
            if (p_v.fields().empty()) {
                r_out += "{}";
                return;
            }
            const std::string inner = std::string(p_nl) + std::string(size_t(p_indent * (p_level + 1)), ' ');
            r_out += '{';
            bool first = true;
            for (const auto &kv : p_v.fields()) {
                if (!first) {
                    r_out += ',';
                }
                first = false;
                r_out += inner;
                escape_to(r_out, kv.first);
                r_out += ": ";
                dump_to(r_out, kv.second, p_indent, p_level + 1, p_nl);
            }
            r_out += p_nl;
            r_out += std::string(size_t(p_indent * p_level), ' ');
            r_out += '}';
            return;
        }
    }
}

} // namespace

std::string json_dumps(const skyisle::Json &p_value, int p_indent, bool p_crlf) {
    std::string out;
    dump_to(out, p_value, p_indent, 0, p_crlf ? "\r\n" : "\n");
    return out;
}

// ---------------------------------------------------------------- SHA-1（FIPS 180-4）

namespace {

inline uint32_t rol(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

} // namespace

std::string sha1_hex(const std::string &p_data) {
    uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
    std::string msg = p_data;
    const uint64_t bits = uint64_t(p_data.size()) * 8u;
    msg.push_back(char(0x80));
    while (msg.size() % 64 != 56) {
        msg.push_back('\0');
    }
    for (int k = 7; k >= 0; k--) {
        msg.push_back(char((bits >> (8 * k)) & 0xFFu));
    }
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[80];
        for (int t = 0; t < 16; t++) {
            const unsigned char *p = reinterpret_cast<const unsigned char *>(msg.data() + off + 4 * t);
            w[t] = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
        }
        for (int t = 16; t < 80; t++) {
            w[t] = rol(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int t = 0; t < 80; t++) {
            uint32_t f, k;
            if (t < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999u;
            } else if (t < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (t < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const uint32_t tmp = rol(a, 5) + f + e + k + w[t];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = tmp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    char out[41];
    for (int k = 0; k < 5; k++) {
        std::snprintf(out + 8 * k, 9, "%08x", h[k]);
    }
    return std::string(out, 40);
}

} // namespace pyfmt
