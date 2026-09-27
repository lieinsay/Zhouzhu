#include "structure_styles.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>

using namespace piece_dims;

namespace {

constexpr double TAU = 6.283185307179586;

std::vector<std::string> split_fields(const std::string &p_line) {
    std::vector<std::string> f;
    std::stringstream ss(p_line);
    std::string item;
    while (ss >> item) {
        f.push_back(item);
    }
    return f;
}

} // namespace

bool parse_structure_style(const std::string &p_text, StructureStyle &r_style, std::string &r_error) {
    StructureStyle style;
    style.id = r_style.id;
    size_t pos = 0;
    int line_no = 0;
    int next_id = 1;
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
        if (line.rfind("name", 0) == 0) {
            const size_t start = line.find_first_not_of(" \t", 4);
            style.name = start == std::string::npos ? std::string() : line.substr(start);
            continue;
        }
        const std::vector<std::string> f = split_fields(line);
        if (f[0] == "piece" && f.size() >= 7) {
            const PieceDef *def = pieces::find(f[1]);
            if (!def) {
                r_error = "第 " + std::to_string(line_no) + " 行：不是搭建件：" + f[1];
                return false;
            }
            Piece p;
            p.id = next_id++;
            p.item = f[1];
            p.spot.slot = def->slot;
            p.spot.x = std::atoi(f[2].c_str());
            p.spot.z = std::atoi(f[3].c_str());
            p.spot.level = std::atoi(f[4].c_str());
            p.spot.tier = std::atoi(f[5].c_str());
            p.spot.dir = std::atoi(f[6].c_str());
            style.pieces.push_back(p);
        } else {
            r_error = "第 " + std::to_string(line_no) + " 行：认不得：" + f[0];
            return false;
        }
    }
    if (style.pieces.empty()) {
        r_error = "一件都没有";
        return false;
    }
    if (style.name.empty()) {
        style.name = style.id;
    }
    r_style = style;
    return true;
}

std::string save_structure_style(const StructureStyle &p_style) {
    std::string out = "# 州渚 · 整座样式（切片十一）：piece <物品> <x> <z> <层> <阶> <向>\n";
    out += "name " + p_style.name + "\n";
    for (const Piece &p : p_style.pieces) {
        out += "piece " + p.item + " " + std::to_string(p.spot.x) + " " + std::to_string(p.spot.z) + " " + std::to_string(p.spot.level) + " " +
                std::to_string(p.spot.tier) + " " + std::to_string(p.spot.dir) + "\n";
    }
    return out;
}

StructureStyle style_from_structure(const Structure &p_structure, const std::string &p_id, const std::string &p_name) {
    StructureStyle style;
    style.id = p_id;
    style.name = p_name;
    int x0 = std::numeric_limits<int>::max(), z0 = std::numeric_limits<int>::max();
    for (const Piece &p : p_structure.pieces) {
        x0 = std::min(x0, p.spot.x);
        z0 = std::min(z0, p.spot.z);
    }
    for (const Piece &p : p_structure.pieces) {
        Piece q = p;
        q.spot.x -= x0;
        q.spot.z -= z0;
        q.open = 0;
        q.bottom = 0.0;
        style.pieces.push_back(q);
    }
    return style;
}

std::vector<std::pair<std::string, int>> style_bill(const StructureStyle &p_style) {
    std::vector<std::pair<std::string, int>> out;
    for (int k = 0; k < pieces::COUNT; k++) {
        int n = 0;
        for (const Piece &p : p_style.pieces) {
            n += p.item == pieces::DEFS[k].item ? 1 : 0;
        }
        if (n > 0) {
            out.push_back({ pieces::DEFS[k].item, n });
        }
    }
    return out;
}

StylePlan plan_structure_style(const Structures &p_existing, const StructureStyle &p_style, const PieceAim &p_aim, const LandingGround &p_ground, const PieceRules &p_rules) {
    StylePlan out;
    const double G = GRID_M;
    const double step = p_rules.new_turn_deg * TAU / 360.0;
    double yaw = std::fmod(std::round(p_aim.look_yaw / step) * step + p_aim.turns * step, TAU);
    if (yaw < 0.0) {
        yaw += TAU;
    }
    // 占地（按件的格号算外包）的中心落在准星上
    int x0 = std::numeric_limits<int>::max(), z0 = x0, x1 = std::numeric_limits<int>::min(), z1 = x1;
    for (const Piece &p : p_style.pieces) {
        x0 = std::min(x0, p.spot.x);
        z0 = std::min(z0, p.spot.z);
        x1 = std::max(x1, p.spot.x);
        z1 = std::max(z1, p.spot.z);
    }
    const double cx = 0.5 * (x0 + x1 + 1) * G, cz = 0.5 * (z0 + z1 + 1) * G;
    Structure s;
    s.yaw = yaw;
    const double c = std::cos(yaw), sn = std::sin(yaw);
    s.x = p_aim.x - (c * cx + sn * cz);
    s.z = p_aim.z - (-sn * cx + c * cz);
    s.y = p_aim.y;
    s.pieces = p_style.pieces;
    auto refuse = [&](const std::string &p_why) {
        out.ok = false;
        out.why = p_why;
        out.structure = s;
        return out;
    };
    // 地坪：所有地基格里最高的地面 + 露出的一截
    double hi = -std::numeric_limits<double>::infinity();
    std::vector<double> lows(s.pieces.size(), 0.0);
    std::string why;
    for (size_t k = 0; k < s.pieces.size(); k++) {
        const Piece &p = s.pieces[k];
        const PieceDef *def = pieces::find(p.item);
        if (!def || def->role != PieceRole::FOUNDATION) {
            continue;
        }
        double lo = std::numeric_limits<double>::infinity();
        for (double u : { 0.05, 0.5 * G, G - 0.05 }) {
            for (double v : { 0.05, 0.5 * G, G - 0.05 }) {
                double hx, hy, hz;
                s.to_host(p.spot.x * G + u, 0.0, p.spot.z * G + v, hx, hy, hz);
                double y = 0.0;
                if (!p_ground.height(hx, hz, y)) {
                    return refuse("有一格底下没有地");
                }
                if (p_ground.water(hx, hz)) {
                    return refuse("有一格在水里");
                }
                hi = std::max(hi, y);
                lo = std::min(lo, y);
            }
        }
        lows[k] = lo;
    }
    if (!std::isfinite(hi)) {
        return refuse("样式里没有地基");
    }
    s.y = hi + p_rules.min_plinth_m;
    for (size_t k = 0; k < s.pieces.size(); k++) {
        const PieceDef *def = pieces::find(s.pieces[k].item);
        if (def && def->role == PieceRole::FOUNDATION) {
            if (s.y - lows[k] > p_rules.max_plinth_m) {
                return refuse("地太陡：有一格离地太高，先垒土或挖平");
            }
            s.pieces[k].bottom = lows[k] - s.y - p_rules.bury_m;
        }
    }
    // 样式自己得站得住
    for (const Piece &p : s.pieces) {
        if (!piece_supported(s, p)) {
            return refuse("这个样式有件没撑着：" + p.item);
        }
    }
    // 离人够得着、离别的一座隔开
    if (std::hypot(p_aim.x - p_aim.from_x, p_aim.z - p_aim.from_z) > p_rules.reach_m + 0.5 * std::hypot(x1 - x0 + 1, z1 - z0 + 1) * G) {
        return refuse("够不着那儿");
    }
    for (const Structure &other : p_existing.get()) {
        for (const Piece &op : other.pieces) {
            double ox, oy, oz;
            other.to_host((op.spot.x + 0.5) * G, 0.0, (op.spot.z + 0.5) * G, ox, oy, oz);
            for (const Piece &p : s.pieces) {
                double px, py, pz;
                s.to_host((p.spot.x + 0.5) * G, 0.0, (p.spot.z + 0.5) * G, px, py, pz);
                if (std::hypot(ox - px, oz - pz) < G * 1.2) {
                    return refuse("挨着别的一座了");
                }
            }
        }
    }
    out.ok = true;
    out.structure = s;
    return out;
}
