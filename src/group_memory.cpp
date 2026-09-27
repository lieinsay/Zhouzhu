#include "group_memory.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "crops.h"

// 文本格式（UTF-8，一行一条，字段用制表符分开）：
//   # 注释
//   ledgers_at <秒>                      各村的账存下时的钟
//   ledger <村下标> <字节数>              下一行起正好这么多字节是 save_village_state 的文本，其后一个换行
//   world <0/1>                          近景 → 远景时存过（下面几种才作数）
//   plots_at <秒>
//   plot <键> <状态 0–3> <长势> <作物 id 或 ->
//   stocks <条数>                        采场的存量，与 resource_plan.sites 一一对应
//   stock <下标> <份数> <钟头>
//   scatter <键> <份数> <钟头> <倒没倒 0/1>
// 数都按 %.17g 写，读回逐位一样。

namespace {

std::string num(double p_v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", p_v);
    return buf;
}

} // namespace

std::string save_group_memory(const GroupMemory &p_memory) {
    std::string out = "# 州渚 · 离开的岛群的账（行星计划 P4）\n";
    out += "ledgers_at\t" + num(p_memory.ledgers_at) + "\n";
    for (const auto &kv : p_memory.ledgers) {
        out += "ledger\t" + std::to_string(kv.first) + "\t" + std::to_string(kv.second.size()) + "\n";
        out += kv.second;
        out += "\n";
    }
    out += "world\t" + std::string(p_memory.has_world ? "1" : "0") + "\n";
    if (p_memory.has_world) {
        out += "plots_at\t" + num(p_memory.plots_at) + "\n";
        for (const auto &kv : p_memory.plots) {
            const Plot &p = kv.second;
            out += "plot\t" + std::to_string(kv.first) + "\t" + std::to_string(int(p.state)) + "\t" + num(p.growth) + "\t" + (p.crop ? p.crop->id : "-") + "\n";
        }
        out += "stocks\t" + std::to_string(p_memory.stocks.size()) + "\n";
        for (size_t k = 0; k < p_memory.stocks.size(); k++) {
            out += "stock\t" + std::to_string(k) + "\t" + num(p_memory.stocks[k].amount) + "\t" + num(p_memory.stocks[k].hour) + "\n";
        }
        p_memory.scatter.for_each([&](uint64_t p_key, const ResourceStock &p_stock, bool p_felled) {
            out += "scatter\t" + std::to_string(p_key) + "\t" + num(p_stock.amount) + "\t" + num(p_stock.hour) + "\t" + (p_felled ? "1" : "0") + "\n";
        });
    }
    return out;
}

bool load_group_memory(const std::string &p_text, GroupMemory &r_memory, std::string &r_error) {
    r_memory = GroupMemory();
    size_t pos = 0;
    int line_no = 0;
    auto fail = [&](const std::string &p_why) {
        r_error = "第 " + std::to_string(line_no) + " 行：" + p_why;
        return false;
    };
    while (pos < p_text.size()) {
        size_t end = p_text.find('\n', pos);
        if (end == std::string::npos) {
            end = p_text.size();
        }
        std::string line = p_text.substr(pos, end - pos);
        pos = end + 1;
        line_no++;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::vector<std::string> f;
        std::stringstream ss(line);
        std::string item;
        while (std::getline(ss, item, '\t')) {
            f.push_back(item);
        }
        const std::string &kind = f[0];
        if (kind == "ledgers_at" && f.size() >= 2) {
            r_memory.ledgers_at = std::strtod(f[1].c_str(), nullptr);
        } else if (kind == "ledger" && f.size() >= 3) {
            const int index = std::atoi(f[1].c_str());
            const size_t bytes = size_t(std::strtoull(f[2].c_str(), nullptr, 10));
            if (pos + bytes > p_text.size()) {
                return fail("村 " + f[1] + " 的账截断了");
            }
            r_memory.ledgers[index] = p_text.substr(pos, bytes);
            // 账里的换行也要数进行号
            for (size_t k = pos; k < pos + bytes; k++) {
                line_no += p_text[k] == '\n' ? 1 : 0;
            }
            pos += bytes;
            if (pos < p_text.size() && p_text[pos] == '\r') {
                pos++;
            }
            if (pos < p_text.size() && p_text[pos] == '\n') {
                pos++;
                line_no++;
            }
        } else if (kind == "world" && f.size() >= 2) {
            r_memory.has_world = f[1] == "1";
        } else if (kind == "plots_at" && f.size() >= 2) {
            r_memory.plots_at = std::strtod(f[1].c_str(), nullptr);
        } else if (kind == "plot" && f.size() >= 5) {
            Plot p;
            const int state = std::atoi(f[2].c_str());
            if (state < 0 || state > int(PlotState::RIPE)) {
                return fail("地的状态 " + f[2] + " 不认识");
            }
            p.state = PlotState(state);
            p.growth = std::strtod(f[3].c_str(), nullptr);
            p.crop = f[4] == "-" ? nullptr : catalog::crop(f[4]);
            if (f[4] != "-" && !p.crop) {
                return fail("作物 " + f[4] + " 不认识");
            }
            r_memory.plots.emplace_back(uint64_t(std::strtoull(f[1].c_str(), nullptr, 10)), p);
        } else if (kind == "stocks" && f.size() >= 2) {
            r_memory.stocks.assign(size_t(std::strtoull(f[1].c_str(), nullptr, 10)), ResourceStock());
        } else if (kind == "stock" && f.size() >= 4) {
            const size_t k = size_t(std::strtoull(f[1].c_str(), nullptr, 10));
            if (k >= r_memory.stocks.size()) {
                return fail("采场下标 " + f[1] + " 超出 stocks 的条数");
            }
            r_memory.stocks[k].amount = std::strtod(f[2].c_str(), nullptr);
            r_memory.stocks[k].hour = std::strtod(f[3].c_str(), nullptr);
        } else if (kind == "scatter" && f.size() >= 5) {
            ResourceStock s;
            s.amount = std::strtod(f[2].c_str(), nullptr);
            s.hour = std::strtod(f[3].c_str(), nullptr);
            r_memory.scatter.put(uint64_t(std::strtoull(f[1].c_str(), nullptr, 10)), s, f[4] == "1");
        } else {
            return fail("认不出「" + kind + "」");
        }
    }
    return true;
}
