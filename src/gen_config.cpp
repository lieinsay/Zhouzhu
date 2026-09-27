#include "gen_config.h"

#include <cstdlib>
#include <vector>

#include "py_format.h"

namespace {

std::vector<std::string> split(const std::string &p_s, char p_sep) {
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        const size_t b = p_s.find(p_sep, a);
        if (b == std::string::npos) {
            out.push_back(p_s.substr(a));
            return out;
        }
        out.push_back(p_s.substr(a, b - a));
        a = b + 1;
    }
}

std::string unescape(const std::string &p_s) {
    std::string out;
    out.reserve(p_s.size());
    for (size_t k = 0; k < p_s.size(); k++) {
        if (p_s[k] == '\\' && k + 1 < p_s.size()) {
            const char c = p_s[++k];
            out.push_back(c == 't' ? '\t' : (c == 'n' ? '\n' : (c == 'r' ? '\r' : c)));
        } else {
            out.push_back(p_s[k]);
        }
    }
    return out;
}

bool parse_num(const std::string &p_s, double &r_v) {
    if (p_s.empty()) {
        return false;
    }
    char *end = nullptr;
    r_v = std::strtod(p_s.c_str(), &end); // Python 的 repr 读回逐位相同（UCRT 的 strtod 正确舍入）
    return end && *end == '\0';
}

} // namespace

int GenConfig::threads() const {
    const int t = int(engine.get("threads", 4.0));
    return t < 1 ? 1 : t;
}

bool parse_gen_config(const std::string &p_raw, GenConfig &r_config, std::string &r_error) {
    // 换行一律按 \n（git 的 autocrlf 在 Windows 上检出成 \r\n；导出脚本写的是 \n——新建世界照抄进 planet/ 的要与它逐字节相同）
    std::string p_text;
    p_text.reserve(p_raw.size());
    for (size_t k = 0; k < p_raw.size(); k++) {
        if (p_raw[k] == '\r' && k + 1 < p_raw.size() && p_raw[k + 1] == '\n') {
            continue;
        }
        p_text.push_back(p_raw[k]);
    }
    GenConfig c;
    c.text = p_text;
    c.sha1 = pyfmt::sha1_hex(p_text);
    std::string island_lines;
    int line_no = 0;
    size_t a = 0;
    while (a < p_text.size()) {
        size_t b = p_text.find('\n', a);
        if (b == std::string::npos) {
            b = p_text.size();
        }
        std::string line = p_text.substr(a, b - a);
        a = b + 1;
        line_no++;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::vector<std::string> f = split(line, '\t');
        if (f.size() != 4) {
            r_error = "config.tsv 第 " + std::to_string(line_no) + " 行不是 4 列";
            return false;
        }
        skyisle::Config *t = f[0] == "planet" ? &c.planet : (f[0] == "island" ? &c.island : (f[0] == "engine" ? &c.engine : nullptr));
        if (!t) {
            r_error = "config.tsv 第 " + std::to_string(line_no) + " 行：不认识的表 " + f[0];
            return false;
        }
        const std::string key = unescape(f[2]);
        if (f[1] == "num") {
            double v = 0.0;
            if (!parse_num(f[3], v)) {
                r_error = "config.tsv 第 " + std::to_string(line_no) + " 行：数读不出来";
                return false;
            }
            t->num[key] = v;
        } else if (f[1] == "vec") {
            std::vector<double> vs;
            if (!f[3].empty()) {
                for (const std::string &x : split(f[3], ',')) {
                    double v = 0.0;
                    if (!parse_num(x, v)) {
                        r_error = "config.tsv 第 " + std::to_string(line_no) + " 行：列表里的数读不出来";
                        return false;
                    }
                    vs.push_back(v);
                }
            }
            t->vec[key] = vs;
        } else if (f[1] == "str") {
            t->str[key] = unescape(f[3]);
        } else {
            r_error = "config.tsv 第 " + std::to_string(line_no) + " 行：不认识的类型 " + f[1];
            return false;
        }
        if (t == &c.island) {
            island_lines += line;
            island_lines += '\n';
        }
    }
    if (c.planet.num.empty() || c.island.num.empty()) {
        r_error = "config.tsv 里没有 planet / island 表";
        return false;
    }
    c.island_sha1 = pyfmt::sha1_hex(island_lines);
    r_config = std::move(c);
    return true;
}
