#include "house_template.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace furniture {

const FurnitureDef DEFS[] = {
    { "bed", "床", 2.0, 1.5, 0.6, true },
    { "stove", "灶", 1.8, 1.0, 0.85, true }, // 烟囱在后头一角，画得高，占地不变
    { "table", "桌", 1.4, 0.9, 0.8, true },
    { "bench", "长凳", 1.2, 0.35, 0.45, true },
    { "chest", "柜", 1.2, 0.6, 1.2, true },
    { "vat", "水缸", 0.9, 0.9, 0.8, true },
    { "bin", "粮囤", 1.4, 1.4, 1.6, true },
    { "altar", "供桌", 1.8, 0.6, 1.0, true },
    { "loom", "织机", 1.6, 1.3, 1.5, true },
    { "shelf", "架子", 1.6, 0.5, 1.9, true },
    { "sacks", "麻袋", 1.2, 0.9, 0.7, true },
    { "drum", "鼓", 1.0, 1.0, 1.3, true },
    { "mat", "席", 2.0, 1.4, 0.03, false },
};
const int COUNT = int(sizeof(DEFS) / sizeof(DEFS[0]));

const FurnitureDef *by_id(const std::string &p_id) {
    const int k = index_of(p_id);
    return k < 0 ? nullptr : &DEFS[k];
}

int index_of(const std::string &p_id) {
    for (int k = 0; k < COUNT; k++) {
        if (p_id == DEFS[k].id) {
            return k;
        }
    }
    return -1;
}

} // namespace furniture

namespace {

const char *SIDE_NAMES[4] = { "front", "right", "back", "left" };
const char *SIDE_LABELS[4] = { "正面", "右山墙", "背面", "左山墙" };

std::string number(double p_value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", p_value);
    std::string text(buffer);
    // 去掉尾巴上的 0：1.30 → 1.3，2.00 → 2
    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    if (text == "-0") {
        text = "0";
    }
    return text;
}

std::string at_level(int p_level) {
    return p_level > 0 ? " @" + std::to_string(p_level) : std::string();
}

bool parse_number(const std::string &p_text, double &r_value) {
    if (p_text.empty()) {
        return false;
    }
    char *end = nullptr;
    r_value = std::strtod(p_text.c_str(), &end);
    return end && *end == '\0';
}

bool parse_point(const std::string &p_text, double &r_x, double &r_z) {
    const size_t comma = p_text.find(',');
    return comma != std::string::npos && parse_number(p_text.substr(0, comma), r_x) && parse_number(p_text.substr(comma + 1), r_z);
}

bool parse_quarter(const std::string &p_text, int &r_quarter) {
    double q = 0.0;
    if (!parse_number(p_text, q) || q != std::floor(q)) {
        return false;
    }
    r_quarter = ((int(q) % 4) + 4) % 4;
    return true;
}

bool same(double p_a, double p_b) {
    return std::abs(p_a - p_b) < 1e-6;
}

} // namespace

std::string HouseTemplate::shell() const {
    const size_t slash = id.find('/');
    return slash == std::string::npos ? id : id.substr(0, slash);
}

std::string HouseTemplate::style() const {
    const size_t slash = id.find('/');
    return slash == std::string::npos ? std::string() : id.substr(slash + 1);
}

bool HouseTemplate::operator==(const HouseTemplate &p_other) const {
    if (id != p_other.id || name != p_other.name || auto_pick != p_other.auto_pick || openings.size() != p_other.openings.size() || partitions.size() != p_other.partitions.size() ||
            doorways.size() != p_other.doorways.size() || items.size() != p_other.items.size() || ladders.size() != p_other.ladders.size()) {
        return false;
    }
    for (size_t k = 0; k < openings.size(); k++) {
        const Opening &a = openings[k], &b = p_other.openings[k];
        if (a.window != b.window || a.side != b.side || !same(a.at, b.at) || !same(a.width, b.width) || a.level != b.level) {
            return false;
        }
    }
    for (size_t k = 0; k < partitions.size(); k++) {
        const Partition &a = partitions[k], &b = p_other.partitions[k];
        if (!same(a.x0, b.x0) || !same(a.z0, b.z0) || !same(a.x1, b.x1) || !same(a.z1, b.z1) || a.level != b.level) {
            return false;
        }
    }
    for (size_t k = 0; k < doorways.size(); k++) {
        const Doorway &a = doorways[k], &b = p_other.doorways[k];
        if (!same(a.x, b.x) || !same(a.z, b.z) || a.level != b.level) {
            return false;
        }
    }
    for (size_t k = 0; k < items.size(); k++) {
        const Furnishing &a = items[k], &b = p_other.items[k];
        if (a.kind != b.kind || !same(a.x, b.x) || !same(a.z, b.z) || a.quarter != b.quarter || a.level != b.level) {
            return false;
        }
    }
    for (size_t k = 0; k < ladders.size(); k++) {
        const LadderSpot &a = ladders[k], &b = p_other.ladders[k];
        if (!same(a.x, b.x) || !same(a.z, b.z) || a.quarter != b.quarter || a.level != b.level) {
            return false;
        }
    }
    return true;
}

double level_height(const BuildingDef &p_def) {
    return p_def.wall_m / double(p_def.levels > 0 ? p_def.levels : 1);
}

double level_floor(const BuildingDef &p_def, int p_level) {
    return p_level * level_height(p_def);
}

double interior_half_x(const BuildingDef &p_def) {
    return 0.5 * p_def.width_m - house_dims::WALL_T;
}

double interior_half_z(const BuildingDef &p_def) {
    return 0.5 * p_def.depth_m - house_dims::WALL_T;
}

void side_axes(WallSide p_side, double &r_along_x, double &r_along_z, double &r_out_x, double &r_out_z) {
    switch (p_side) {
        case WallSide::FRONT:
            r_along_x = 1.0, r_along_z = 0.0, r_out_x = 0.0, r_out_z = 1.0;
            break;
        case WallSide::BACK:
            r_along_x = 1.0, r_along_z = 0.0, r_out_x = 0.0, r_out_z = -1.0;
            break;
        case WallSide::RIGHT:
            r_along_x = 0.0, r_along_z = 1.0, r_out_x = 1.0, r_out_z = 0.0;
            break;
        case WallSide::LEFT:
            r_along_x = 0.0, r_along_z = 1.0, r_out_x = -1.0, r_out_z = 0.0;
            break;
    }
}

double side_length(const BuildingDef &p_def, WallSide p_side) {
    return p_side == WallSide::FRONT || p_side == WallSide::BACK ? p_def.width_m : p_def.depth_m;
}

void opening_point(const BuildingDef &p_def, const Opening &p_opening, double p_inset, double &r_x, double &r_z) {
    double ax = 0.0, az = 0.0, ox = 0.0, oz = 0.0;
    side_axes(p_opening.side, ax, az, ox, oz);
    // 墙内皮离中心多远
    const double inner = (ox != 0.0 ? interior_half_x(p_def) : interior_half_z(p_def)) - p_inset;
    r_x = ax * p_opening.at + ox * inner;
    r_z = az * p_opening.at + oz * inner;
}

void furniture_extent(const FurnitureDef &p_def, int p_quarter, double &r_sx, double &r_sz) {
    const bool turned = (p_quarter & 1) != 0;
    r_sx = turned ? p_def.d : p_def.w;
    r_sz = turned ? p_def.w : p_def.d;
}

void ladder_outward(int p_quarter, double &r_x, double &r_z) {
    static const double X[4] = { 0.0, 1.0, 0.0, -1.0 }, Z[4] = { 1.0, 0.0, -1.0, 0.0 };
    const int q = ((p_quarter % 4) + 4) % 4;
    r_x = X[q];
    r_z = Z[q];
}

const char *side_name(WallSide p_side) {
    return SIDE_NAMES[int(p_side)];
}

const char *side_label(WallSide p_side) {
    return SIDE_LABELS[int(p_side)];
}

bool parse_side(const std::string &p_text, WallSide &r_side) {
    for (int k = 0; k < 4; k++) {
        if (p_text == SIDE_NAMES[k]) {
            r_side = WallSide(k);
            return true;
        }
    }
    return false;
}

HouseTemplate bare_template(const BuildingDef &p_def) {
    HouseTemplate t;
    t.id = std::string(p_def.id) + "/bare";
    t.name = p_def.name;
    t.openings.push_back(Opening{ false, WallSide::FRONT, 0.0, 1.3, 0 });
    return t;
}

std::vector<std::string> template_lines(const HouseTemplate &p_template) {
    std::vector<std::string> lines;
    lines.push_back("id " + p_template.id);
    lines.push_back("name " + p_template.name);
    if (!p_template.auto_pick) {
        lines.push_back("auto no");
    }
    for (const Opening &o : p_template.openings) {
        lines.push_back(std::string(o.window ? "window " : "door ") + side_name(o.side) + " " + number(o.at) + " " + number(o.width) + at_level(o.level));
    }
    for (const Partition &p : p_template.partitions) {
        lines.push_back("wall " + number(p.x0) + "," + number(p.z0) + " " + number(p.x1) + "," + number(p.z1) + at_level(p.level));
    }
    for (const Doorway &d : p_template.doorways) {
        lines.push_back("doorway " + number(d.x) + "," + number(d.z) + at_level(d.level));
    }
    for (const Furnishing &f : p_template.items) {
        lines.push_back("item " + f.kind + " " + number(f.x) + "," + number(f.z) + " " + std::to_string(f.quarter) + at_level(f.level));
    }
    for (const LadderSpot &l : p_template.ladders) {
        lines.push_back("ladder " + number(l.x) + "," + number(l.z) + " " + std::to_string(l.quarter) + at_level(l.level));
    }
    return lines;
}

bool parse_template(const std::vector<std::string> &p_lines, HouseTemplate &r_template, std::vector<std::string> *r_errors) {
    HouseTemplate t;
    auto fail = [&](size_t p_line, const std::string &p_why) {
        if (r_errors) {
            r_errors->push_back("第 " + std::to_string(p_line + 1) + " 行：" + p_why);
        }
    };
    for (size_t n = 0; n < p_lines.size(); n++) {
        std::string line = p_lines[n];
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        const size_t begin = line.find_first_not_of(" \t");
        if (begin == std::string::npos || line[begin] == '#') {
            continue;
        }
        std::istringstream in(line.substr(begin));
        std::vector<std::string> words;
        for (std::string w; in >> w;) {
            words.push_back(w);
        }
        const std::string &cmd = words[0];
        if (cmd == "id" || cmd == "name") {
            // 名字里可以有空格：取命令之后的整段
            const size_t rest = line.find_first_not_of(" \t", line.find(cmd, begin) + cmd.size());
            const std::string value = rest == std::string::npos ? std::string() : line.substr(rest);
            if (value.empty()) {
                fail(n, cmd + " 后面是空的");
            } else if (cmd == "id") {
                t.id = value;
            } else {
                t.name = value;
            }
            continue;
        }
        if (cmd == "auto") {
            if (words.size() != 2 || (words[1] != "yes" && words[1] != "no")) {
                fail(n, "应是「auto yes」或「auto no」");
            } else {
                t.auto_pick = words[1] == "yes";
            }
            continue;
        }
        int level = 0;
        if (words.size() > 1 && words.back().size() > 1 && words.back()[0] == '@') {
            double l = 0.0;
            if (!parse_number(words.back().substr(1), l) || l < 0 || l != std::floor(l)) {
                fail(n, "层号认不出：" + words.back());
                continue;
            }
            level = int(l);
            words.pop_back();
        }
        if (cmd == "door" || cmd == "window") {
            Opening o;
            o.window = cmd == "window";
            o.level = level;
            if (words.size() != 4 || !parse_side(words[1], o.side) || !parse_number(words[2], o.at) || !parse_number(words[3], o.width)) {
                fail(n, "应是「" + cmd + " 哪一面 位置 宽」");
                continue;
            }
            t.openings.push_back(o);
        } else if (cmd == "wall") {
            Partition p;
            p.level = level;
            if (words.size() != 3 || !parse_point(words[1], p.x0, p.z0) || !parse_point(words[2], p.x1, p.z1)) {
                fail(n, "应是「wall x,z x,z」");
                continue;
            }
            t.partitions.push_back(p);
        } else if (cmd == "doorway") {
            Doorway d;
            d.level = level;
            if (words.size() != 2 || !parse_point(words[1], d.x, d.z)) {
                fail(n, "应是「doorway x,z」");
                continue;
            }
            t.doorways.push_back(d);
        } else if (cmd == "item") {
            Furnishing f;
            f.level = level;
            if (words.size() != 4 || !furniture::by_id(words[1]) || !parse_point(words[2], f.x, f.z) || !parse_quarter(words[3], f.quarter)) {
                fail(n, words.size() > 1 && !furniture::by_id(words[1]) ? "没有这种家具：" + words[1] : std::string("应是「item 种类 x,z 朝向」"));
                continue;
            }
            f.kind = words[1];
            t.items.push_back(f);
        } else if (cmd == "ladder") {
            LadderSpot l;
            l.level = level;
            if (words.size() != 3 || !parse_point(words[1], l.x, l.z) || !parse_quarter(words[2], l.quarter)) {
                fail(n, "应是「ladder x,z 朝向」");
                continue;
            }
            t.ladders.push_back(l);
        } else {
            fail(n, "认不出的命令：" + cmd);
        }
    }
    if (t.id.empty() || !buildings::by_id(t.shell()) || t.style().empty()) {
        if (r_errors) {
            r_errors->push_back(t.id.empty() ? std::string("缺 id 行") : "id 应是「外壳/款式」，外壳是建筑表里的一种：" + t.id);
        }
        return false;
    }
    if (t.name.empty()) {
        t.name = t.style();
    }
    r_template = t;
    return true;
}
