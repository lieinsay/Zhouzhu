#include "ground_works.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

// 文本格式（UTF-8，一行一条，字段用制表符分开；数按 %.17g 写，读回逐位一样）：
//   # 注释
//   grid / chunk …                                     改地形（同 terrain_edits.cpp 的两种行）
//   structure <座 id> <x> <y> <z> <yaw>                一座：原点与朝向
//   piece <座 id> <件 id> <物品> <x> <z> <层> <阶> <向> <门开没开> <台基底>
//   prop <id> <物品> <x> <y> <z> <low> <quarter> <yaw_extra> <格数> [<格>:<物品>:<份数> …]
// 件按它的座挂回去（座行要在件行之前）。

namespace {

std::string num(double p_v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", p_v);
    return buf;
}

std::vector<std::string> fields(const std::string &p_line) {
    std::vector<std::string> f;
    std::stringstream ss(p_line);
    std::string item;
    while (std::getline(ss, item, '\t')) {
        f.push_back(item);
    }
    return f;
}

} // namespace

std::string save_ground_works(const GroundWorks &p_works) {
    std::string out = "# 州渚 · 营造（切片十一）：改地形、搭建件、摆在地上的东西\n";
    if (p_works.edits && !p_works.edits->empty()) {
        const std::string edits = save_terrain_edits(*p_works.edits);
        // 去掉它自己的注释行
        size_t pos = 0;
        while (pos < edits.size()) {
            size_t end = edits.find('\n', pos);
            if (end == std::string::npos) {
                end = edits.size();
            }
            const std::string line = edits.substr(pos, end - pos);
            if (!line.empty() && line[0] != '#') {
                out += line + "\n";
            }
            pos = end + 1;
        }
    }
    for (const Structure &s : p_works.structures.get()) {
        out += "structure\t" + std::to_string(s.id) + "\t" + num(s.x) + "\t" + num(s.y) + "\t" + num(s.z) + "\t" + num(s.yaw) + "\n";
        for (const Piece &p : s.pieces) {
            out += "piece\t" + std::to_string(s.id) + "\t" + std::to_string(p.id) + "\t" + p.item + "\t" + std::to_string(p.spot.x) + "\t" + std::to_string(p.spot.z) + "\t" +
                    std::to_string(p.spot.level) + "\t" + std::to_string(p.spot.tier) + "\t" + std::to_string(p.spot.dir) + "\t" + std::to_string(p.open) + "\t" + num(p.bottom) + "\n";
        }
    }
    for (const PlacedProp &p : p_works.props.get_props()) {
        out += "prop\t" + std::to_string(p.id) + "\t" + p.item + "\t" + num(p.x) + "\t" + num(p.y) + "\t" + num(p.z) + "\t" + num(p.low) + "\t" + std::to_string(p.quarter) + "\t" +
                num(p.yaw_extra) + "\t" + std::to_string(p.contents.size());
        for (int k = 0; k < p.contents.size(); k++) {
            const ItemStack &st = p.contents.at(k);
            if (st.count > 0) {
                out += "\t" + std::to_string(k) + ":" + st.item + ":" + std::to_string(st.count);
            }
        }
        out += "\n";
    }
    return out;
}

bool load_ground_works(const std::string &p_text, GroundWorks &r_works, std::string &r_error) {
    std::string edits_text;
    std::vector<Structure> structures;
    std::vector<PlacedProp> props;
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
        const std::vector<std::string> f = fields(line);
        const std::string &kind = f[0];
        if (kind == "grid" || kind == "chunk") {
            edits_text += line + "\n";
        } else if (kind == "structure" && f.size() >= 6) {
            Structure s;
            s.id = std::atoi(f[1].c_str());
            s.x = std::strtod(f[2].c_str(), nullptr);
            s.y = std::strtod(f[3].c_str(), nullptr);
            s.z = std::strtod(f[4].c_str(), nullptr);
            s.yaw = std::strtod(f[5].c_str(), nullptr);
            structures.push_back(s);
        } else if (kind == "piece" && f.size() >= 11) {
            const int sid = std::atoi(f[1].c_str());
            Structure *owner = nullptr;
            for (Structure &s : structures) {
                owner = s.id == sid ? &s : owner;
            }
            const PieceDef *def = pieces::find(f[3]);
            if (!owner) {
                return fail("件的座 " + f[1] + " 还没出现");
            }
            if (!def) {
                return fail("不是搭建件：" + f[3]);
            }
            Piece p;
            p.id = std::atoi(f[2].c_str());
            p.item = f[3];
            p.spot.slot = def->slot;
            p.spot.x = std::atoi(f[4].c_str());
            p.spot.z = std::atoi(f[5].c_str());
            p.spot.level = std::atoi(f[6].c_str());
            p.spot.tier = std::atoi(f[7].c_str());
            p.spot.dir = std::atoi(f[8].c_str());
            p.open = std::atoi(f[9].c_str());
            p.bottom = std::strtod(f[10].c_str(), nullptr);
            owner->pieces.push_back(p);
        } else if (kind == "prop" && f.size() >= 10) {
            const PropDef *def = props::find(f[2]);
            if (!def) {
                return fail("不是能摆的东西：" + f[2]);
            }
            PlacedProp p;
            p.id = std::atoi(f[1].c_str());
            p.item = f[2];
            p.x = std::strtod(f[3].c_str(), nullptr);
            p.y = std::strtod(f[4].c_str(), nullptr);
            p.z = std::strtod(f[5].c_str(), nullptr);
            p.low = std::strtod(f[6].c_str(), nullptr);
            p.quarter = std::atoi(f[7].c_str());
            p.yaw_extra = std::strtod(f[8].c_str(), nullptr);
            p.contents = ItemSlots(std::atoi(f[9].c_str()));
            for (size_t k = 10; k < f.size(); k++) {
                const size_t a = f[k].find(':'), b = f[k].rfind(':');
                if (a == std::string::npos || b == a) {
                    return fail("格子写错了：" + f[k]);
                }
                const int slot = std::atoi(f[k].substr(0, a).c_str());
                if (slot < 0 || slot >= p.contents.size()) {
                    return fail("格号越界：" + f[k]);
                }
                p.contents.set(slot, f[k].substr(a + 1, b - a - 1), std::atoi(f[k].substr(b + 1).c_str()));
            }
            props.push_back(p);
        } else {
            return fail("认不得：" + kind);
        }
    }
    GroundWorks works;
    if (!edits_text.empty()) {
        TerrainEdits edits;
        std::string error;
        if (!load_terrain_edits(edits_text, edits, error)) {
            r_error = "改地形：" + error;
            return false;
        }
        works.edits = std::make_shared<const TerrainEdits>(edits);
    }
    // 空了的座不收
    structures.erase(std::remove_if(structures.begin(), structures.end(), [](const Structure &s) { return s.pieces.empty(); }), structures.end());
    works.structures.assign(structures);
    works.props.assign(props);
    r_works = std::move(works);
    return true;
}
