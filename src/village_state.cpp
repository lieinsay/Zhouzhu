#include "village_state.h"

#include <cstdio>
#include <sstream>

#include "crops.h"

uint64_t layout_signature(const SettlementLayout &p_layout) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t p_v) { h = (h ^ p_v) * 1099511628211ull; };
    for (const Building &b : p_layout.get_items()) {
        for (char ch : b.kind) {
            mix(uint64_t(uint8_t(ch)));
        }
        mix(uint64_t(int64_t(b.x * 100.0)));
        mix(uint64_t(int64_t(b.z * 100.0)));
        mix(uint64_t(int64_t(b.yaw * 1000.0)));
    }
    return h;
}

int village_grain(const VillageState &p_state) {
    auto is_grain = [](const std::string &p_item) {
        for (int k = 0; k < catalog::CROP_COUNT; k++) {
            if (p_item == catalog::CROPS[k].product) {
                return true;
            }
        }
        return false;
    };
    int total = 0;
    for (const ItemStack &s : p_state.store.get_stacks()) {
        total += is_grain(s.item) ? s.count : 0;
    }
    for (const PersonState &p : p_state.people) {
        total += is_grain(p.mind.carrying) ? p.mind.carry_count : 0;
    }
    return total;
}

// ---------------------------------------------------------------- 文本
//
// 一行一条，第一个词说这行是什么；空串写成「-」（物品 id、名字里都没有空格与「-」）。
//   village 1
//   head <指纹> <开过账> <记到第几秒> <在近处> <水缸> <缸容量>
//   store <物品> <份数>
//   tally <按活计的 16 个数> <收> <播> <吃> <饿> <挑进> <用掉> <找不到路>
//   claim <i> <j> <谁>
//   held <i> <j> <状态> <长势> <作物> <墒情>
//   person <下标> <名字> <职业> <家> <望楼> <x> <z> <背着> <几份> <还要巡几座> <随机流> <阶段> <计时> <步速> <估着走> <还剩几米> <下一个路点> <再找荒地> <打水处：有无 站 x 站 z 水 x 水 z>
//   task <活计> <x> <z> <面朝 x> <面朝 z> <几秒> <i> <j> <哪座> <种子>
//   path <x> <z> …（跟在它的 person 后面，可以没有）
//   arable <i0> <j0> <行> <列> <位图的十六进制>（开账时勘察的能垦的格；可以没有）
//   end

namespace {

std::string word(const std::string &p_s) {
    return p_s.empty() ? std::string("-") : p_s;
}

std::string unword(const std::string &p_s) {
    return p_s == "-" ? std::string() : p_s;
}

std::string num(double p_v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", p_v);
    return buf;
}

} // namespace

std::string save_village_state(const VillageState &p_state) {
    std::ostringstream out;
    out << "village 1\n";
    out << "head " << p_state.layout_sig << ' ' << int(p_state.founded) << ' ' << num(p_state.clock) << ' ' << int(p_state.deployed) << ' '
        << num(p_state.water) << ' ' << num(p_state.water_cap) << '\n';
    for (const ItemStack &s : p_state.store.get_stacks()) {
        out << "store " << s.item << ' ' << s.count << '\n';
    }
    const VillageTally &t = p_state.tally;
    out << "tally";
    for (int d : t.done) {
        out << ' ' << d;
    }
    out << ' ' << t.harvested << ' ' << t.sown << ' ' << t.eaten << ' ' << t.hungry << ' ' << num(t.water_in) << ' ' << num(t.water_used) << ' ' << t.path_failures << '\n';
    for (const Claim &c : p_state.claims) {
        out << "claim " << c.i << ' ' << c.j << ' ' << c.owner << '\n';
    }
    for (const HeldPlot &h : p_state.held) {
        out << "held " << h.i << ' ' << h.j << ' ' << int(h.plot.state) << ' ' << num(h.plot.growth) << ' ' << word(h.plot.crop ? h.plot.crop->id : "") << ' '
            << num(h.moisture) << '\n';
    }
    for (const PersonState &p : p_state.people) {
        const VillagerMind &m = p.mind;
        out << "person " << m.index << ' ' << word(m.who.name) << ' ' << villagers::job(m.who.job).id << ' ' << m.who.home << ' ' << m.who.post << ' '
            << num(m.x) << ' ' << num(m.z) << ' ' << word(m.carrying) << ' ' << m.carry_count << ' ' << m.patrol_left << ' ' << p.rng.get_state() << ' '
            << int(p.phase) << ' ' << num(p.timer) << ' ' << num(p.speed) << ' ' << int(p.estimated) << ' ' << num(p.walk_left) << ' ' << p.next << ' '
            << num(m.wild_retry_at) << ' ' << m.water_spot << ' ' << num(m.water_x) << ' ' << num(m.water_z) << ' ' << num(m.water_face_x) << ' '
            << num(m.water_face_z) << '\n';
        const Task &k = p.task;
        out << "task " << villagers::task(k.kind).id << ' ' << num(k.x) << ' ' << num(k.z) << ' ' << num(k.face_x) << ' ' << num(k.face_z) << ' '
            << num(k.seconds) << ' ' << k.cell_i << ' ' << k.cell_j << ' ' << k.building << ' ' << word(k.seed) << '\n';
        if (!p.path.empty()) {
            out << "path";
            for (const NavPoint &q : p.path) {
                out << ' ' << num(q.x) << ' ' << num(q.z);
            }
            out << '\n';
        }
    }
    if (p_state.arable.rows > 0) {
        const ArableMask &a = p_state.arable;
        static const char *HEX = "0123456789abcdef";
        out << "arable " << a.i0 << ' ' << a.j0 << ' ' << a.rows << ' ' << a.cols << ' ';
        for (uint8_t b : a.bits) {
            out << HEX[b >> 4] << HEX[b & 15];
        }
        out << '\n';
    }
    out << "end\n";
    return out.str();
}

bool load_village_state(const std::string &p_text, VillageState &r_state, std::string &r_error) {
    r_state = VillageState();
    std::istringstream in(p_text);
    std::string line;
    int line_no = 0;
    bool header = false, ended = false;
    auto fail = [&](const std::string &p_why) {
        r_error = "第 " + std::to_string(line_no) + " 行：" + p_why;
        return false;
    };
    while (std::getline(in, line)) {
        line_no++;
        if (line.empty()) {
            continue;
        }
        std::istringstream row(line);
        std::string tag;
        row >> tag;
        if (!header) {
            int version = 0;
            if (tag != "village" || !(row >> version) || version != 1) {
                return fail("不是村落账本（第一行应为 village 1）");
            }
            header = true;
            continue;
        }
        if (tag == "head") {
            int founded = 0, deployed = 0;
            if (!(row >> r_state.layout_sig >> founded >> r_state.clock >> deployed >> r_state.water >> r_state.water_cap)) {
                return fail("head 读不全");
            }
            r_state.founded = founded != 0;
            r_state.deployed = deployed != 0;
        } else if (tag == "store") {
            std::string item;
            int count = 0;
            if (!(row >> item >> count)) {
                return fail("store 读不全");
            }
            r_state.store.add(item, count);
        } else if (tag == "tally") {
            VillageTally &t = r_state.tally;
            for (int &d : t.done) {
                row >> d;
            }
            if (!(row >> t.harvested >> t.sown >> t.eaten >> t.hungry >> t.water_in >> t.water_used >> t.path_failures)) {
                return fail("tally 读不全");
            }
        } else if (tag == "claim") {
            Claim c;
            if (!(row >> c.i >> c.j >> c.owner)) {
                return fail("claim 读不全");
            }
            r_state.claims.push_back(c);
        } else if (tag == "held") {
            HeldPlot h;
            int state = 0;
            std::string crop;
            if (!(row >> h.i >> h.j >> state >> h.plot.growth >> crop >> h.moisture) || state < 0 || state > int(PlotState::RIPE)) {
                return fail("held 读不全");
            }
            h.plot.state = PlotState(state);
            h.plot.crop = catalog::crop(unword(crop));
            if ((h.plot.state == PlotState::GROWING || h.plot.state == PlotState::RIPE) && !h.plot.crop) {
                return fail("held 的作物不认识：" + crop);
            }
            r_state.held.push_back(h);
        } else if (tag == "person") {
            PersonState p;
            VillagerMind &m = p.mind;
            std::string name, job, carrying;
            uint64_t rng = 0;
            int phase = 0, estimated = 0;
            if (!(row >> m.index >> name >> job >> m.who.home >> m.who.post >> m.x >> m.z >> carrying >> m.carry_count >> m.patrol_left >> rng >> phase >> p.timer >>
                        p.speed >> estimated >> p.walk_left >> p.next >> m.wild_retry_at >> m.water_spot >> m.water_x >> m.water_z >>
                        m.water_face_x >> m.water_face_z)) {
                return fail("person 读不全");
            }
            const JobDef *def = villagers::job_by_id(job);
            if (!def || phase < 0 || phase > int(FolkPhase::WORKING)) {
                return fail("person 的职业或阶段不认识");
            }
            m.who.name = unword(name);
            m.who.job = def->job;
            m.carrying = unword(carrying);
            p.rng = Rng(rng);
            p.phase = FolkPhase(phase);
            p.estimated = estimated != 0;
            r_state.people.push_back(p);
        } else if (tag == "task") {
            if (r_state.people.empty()) {
                return fail("task 前面没有 person");
            }
            Task &k = r_state.people.back().task;
            std::string kind, seed;
            if (!(row >> kind >> k.x >> k.z >> k.face_x >> k.face_z >> k.seconds >> k.cell_i >> k.cell_j >> k.building >> seed)) {
                return fail("task 读不全");
            }
            bool known = false;
            for (int t = 0; t <= int(TaskKind::WAIT) && !known; t++) {
                if (kind == villagers::task(TaskKind(t)).id) {
                    k.kind = TaskKind(t);
                    known = true;
                }
            }
            if (!known) {
                return fail("活计不认识：" + kind);
            }
            k.seed = unword(seed);
        } else if (tag == "path") {
            if (r_state.people.empty()) {
                return fail("path 前面没有 person");
            }
            NavPoint q;
            while (row >> q.x >> q.z) {
                r_state.people.back().path.push_back(q);
            }
        } else if (tag == "arable") {
            ArableMask &a = r_state.arable;
            std::string hex;
            if (!(row >> a.i0 >> a.j0 >> a.rows >> a.cols >> hex) || a.rows <= 0 || a.cols <= 0 || hex.size() != 2 * ((size_t(a.rows) * size_t(a.cols) + 7) / 8)) {
                return fail("arable 读不全");
            }
            a.bits.resize(hex.size() / 2);
            for (size_t k = 0; k < a.bits.size(); k++) {
                a.bits[k] = uint8_t(std::stoi(hex.substr(2 * k, 2), nullptr, 16));
            }
        } else if (tag == "end") {
            ended = true;
            break;
        } else {
            return fail("不认识的一行：" + tag);
        }
    }
    if (!ended) {
        return fail("没有 end（文本被截断了）");
    }
    return true;
}
