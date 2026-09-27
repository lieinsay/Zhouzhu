#include "hinterland.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "farm.h"
#include "local_season.h"
#include "site_stream.h"
#include "world.h"
#include "world_clock.h"

using namespace godot;

namespace {

std::string to_std(const String &p_string) {
    return std::string(p_string.utf8().get_data());
}

uint64_t ticks_us() {
    return Time::get_singleton()->get_ticks_usec();
}

} // namespace

void Hinterland::_bind_methods() {
    ClassDB::bind_method(D_METHOD("find_site", "site_id", "hamlet"), &Hinterland::find_site);
    ClassDB::bind_method(D_METHOD("get_ledger_summary", "site_id", "hamlet"), &Hinterland::get_ledger_summary);
    ClassDB::bind_method(D_METHOD("get_stats"), &Hinterland::get_stats);
    ClassDB::bind_method(D_METHOD("get_ledger_text", "site_id", "hamlet"), &Hinterland::get_ledger_text);

    ClassDB::bind_method(D_METHOD("set_world_path", "value"), &Hinterland::set_world_path);
    ClassDB::bind_method(D_METHOD("get_world_path"), &Hinterland::get_world_path);
    ClassDB::bind_method(D_METHOD("set_sites_path", "value"), &Hinterland::set_sites_path);
    ClassDB::bind_method(D_METHOD("get_sites_path"), &Hinterland::get_sites_path);
    ClassDB::bind_method(D_METHOD("set_budget_ms", "value"), &Hinterland::set_budget_ms);
    ClassDB::bind_method(D_METHOD("get_budget_ms"), &Hinterland::get_budget_ms);
    ClassDB::bind_method(D_METHOD("set_settle_hours", "value"), &Hinterland::set_settle_hours);
    ClassDB::bind_method(D_METHOD("get_settle_hours"), &Hinterland::get_settle_hours);
    ClassDB::bind_method(D_METHOD("set_slice_hours", "value"), &Hinterland::set_slice_hours);
    ClassDB::bind_method(D_METHOD("get_slice_hours"), &Hinterland::get_slice_hours);
    ClassDB::bind_method(D_METHOD("set_max_openings", "value"), &Hinterland::set_max_openings);
    ClassDB::bind_method(D_METHOD("get_max_openings"), &Hinterland::get_max_openings);

    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "world_path"), "set_world_path", "get_world_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sites_path"), "set_sites_path", "get_sites_path");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "budget_ms", PROPERTY_HINT_RANGE, "0.05,10,0.05,suffix:ms"), "set_budget_ms", "get_budget_ms");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "settle_hours", PROPERTY_HINT_RANGE, "0.05,6,0.05,suffix:h"), "set_settle_hours", "get_settle_hours");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "slice_hours", PROPERTY_HINT_RANGE, "0.01,2,0.01,suffix:h"), "set_slice_hours", "get_slice_hours");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "max_openings", PROPERTY_HINT_RANGE, "1,8,1"), "set_max_openings", "get_max_openings");
}

void Hinterland::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_physics_process(false);
        return;
    }
    world = Object::cast_to<GroupWorld>(get_node_or_null(world_path));
    stream = Object::cast_to<SiteStream>(get_node_or_null(sites_path));
    ERR_FAIL_NULL_MSG(world, "Hinterland 找不到 World（world_path）");
    ERR_FAIL_NULL_MSG(stream, "Hinterland 找不到 SiteStream（sites_path）");
    taken = std::make_shared<ClaimRegistry>();
}

void Hinterland::_notification(int p_what) {
    if (p_what == NOTIFICATION_EXIT_TREE) {
        collect_openings(false); // 后台还在读地形：离树时（此刻谁都还没删）等它们做完
    }
}

bool Hinterland::ready_to_work() {
    if (!world || !stream || !world->is_loaded() || !world->host_farm() || !WorldClock::get_singleton()) {
        return false;
    }
    if (started_at < 0.0) {
        started_at = WorldClock::get_singleton()->get_seconds();
        ledgers.resize(world->get_sites().settlements.size());
    }
    return true;
}

WorldLedgerSpec Hinterland::spec_of(int p_index) const {
    WorldLedgerSpec spec;
    spec.site = world->get_sites().settlements[size_t(p_index)];
    spec.seed = uint64_t(stream->get_seed());
    spec.per_house = stream->get_per_house();
    spec.max_folk = stream->get_max_folk();
    const FarmModel &farm = world->host_farm()->get_model();
    spec.lattice = farm.get_lattice();
    spec.farm_rules = farm.get_rules();
    spec.day = WorldClock::get_singleton()->get_rules();
    spec.now = started_at;
    // 布置模式存下的改动：与 SiteStream 给 WorldVillage 的是同一个文件（后台不碰文件系统，这里先读好）
    const String dir = stream->get_layout_dir();
    const String path = dir.is_empty() ? String() : dir.path_join(vformat("%s_%d.txt", spec.site.hamlet ? "hamlet" : "village", spec.site.id));
    if (!path.is_empty() && FileAccess::file_exists(path)) {
        Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
        if (file.is_valid()) {
            const PackedStringArray text = file->get_as_text().split("\n");
            for (int64_t k = 0; k < text.size(); k++) {
                spec.edits.push_back(to_std(text[k]));
            }
        }
    }
    return spec;
}

void Hinterland::install(int p_index, WorldLedger &&p_ledger) {
    const auto found = saved.find(p_index);
    if (found != saved.end()) {
        // 上回离开时存下的账：照文本补回（人、仓、水缸、认领的地、流水），认领的格子重新登记到全图认领表
        VillageState state;
        std::string error;
        if (load_village_state(found->second, state, error)) {
            *p_ledger.state = std::move(state);
            for (const Claim &c : p_ledger.state->claims) {
                taken->add(c.i, c.j);
            }
            for (const HeldPlot &h : p_ledger.state->held) {
                taken->add(h.i, h.j);
            }
            // 离开超过一天：只补最后一天，前面的视为按汇总过了（钟直接挪到一天前）
            if (const WorldClock *clock = WorldClock::get_singleton()) {
                const double resume = resume_clock(p_ledger.state->clock, clock->get_seconds(), clock->get_rules().day_seconds);
                if (resume > p_ledger.state->clock) {
                    skipped_hours += game_hours(resume - p_ledger.state->clock, clock->get_rules());
                    p_ledger.state->clock = resume;
                }
            }
            restored++;
        } else {
            ERR_PRINT(String(U"Hinterland：存下的账读不回来（") + String::utf8(error.c_str()) + String(U"），这一处重新开账"));
        }
        saved.erase(found);
    }
    ledgers[size_t(p_index)] = std::make_unique<WorldLedger>(std::move(p_ledger));
    opened++;
}

void Hinterland::save_states(std::map<int, std::string> &r_states, double &r_at) {
    collect_openings(true); // 后台在开的：等它开完一并存下
    for (size_t k = 0; k < ledgers.size(); k++) {
        if (ledgers[k]) {
            r_states[int(k)] = save_village_state(*ledgers[k]->state);
        }
    }
    // 上回存下、这回还没来得及开账的：原样留着
    for (const auto &entry : saved) {
        r_states.emplace(entry.first, entry.second);
    }
    const WorldClock *clock = WorldClock::get_singleton();
    r_at = clock ? clock->get_seconds() : 0.0;
}

void Hinterland::set_saved(const std::map<int, std::string> &p_states, double p_at) {
    saved = p_states;
    saved_at = p_at;
}

void Hinterland::collect_openings(bool p_install) {
    for (size_t k = 0; k < in_flight.size();) {
        if (p_install && in_flight[k].wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            k++;
            continue;
        }
        WorldLedger ledger = in_flight[k].get();
        if (p_install) {
            install(in_flight_index[k], std::move(ledger));
        }
        in_flight.erase(in_flight.begin() + long(k));
        in_flight_index.erase(in_flight_index.begin() + long(k));
    }
}

void Hinterland::request(int p_index) {
    if (p_index < 0 || is_open(p_index) || std::find(urgent.begin(), urgent.end(), p_index) != urgent.end() ||
            std::find(in_flight_index.begin(), in_flight_index.end(), p_index) != in_flight_index.end()) {
        return;
    }
    urgent.push_back(p_index);
}

void Hinterland::launch_openings() {
    // 近处要招村民的先开；再由近及远：离焦点（人或船）最近的还没开账的先开
    const std::vector<SiteSettlement> &sites = world->get_sites().settlements;
    const Vector3 focus = stream->get_focus();
    while (int(in_flight.size()) < max_openings) {
        int best = -1;
        double best_d = 1e300;
        while (!urgent.empty() && best < 0) {
            const int k = urgent.front();
            urgent.erase(urgent.begin());
            if (k >= 0 && k < int(sites.size()) && !ledgers[size_t(k)] && std::find(in_flight_index.begin(), in_flight_index.end(), k) == in_flight_index.end()) {
                best = k;
            }
        }
        const bool urgent_found = best >= 0;
        for (int k = 0; !urgent_found && k < int(sites.size()); k++) {
            if (ledgers[size_t(k)] || std::find(in_flight_index.begin(), in_flight_index.end(), k) != in_flight_index.end()) {
                continue;
            }
            const double d = std::hypot(sites[size_t(k)].x - focus.x, sites[size_t(k)].z - focus.z);
            if (d < best_d) {
                best_d = d;
                best = k;
            }
        }
        if (best < 0) {
            return;
        }
        const WorldTerrain *terrain = &world->get_terrain();
        const SimRules sim_rules = rules;
        in_flight.push_back(std::async(std::launch::async, [terrain, spec = spec_of(best), sim_rules]() { return open_world_ledger(*terrain, spec, sim_rules); }));
        in_flight_index.push_back(best);
    }
}

void Hinterland::settle_slice(WorldLedger &r_ledger, double p_until) {
    const double from = r_ledger.state->clock;
    const uint64_t t0 = ticks_us();
    // 这处的时令（作息随日出日落、坏天气歇工、冷天不播）：与近处的 Villagers 同一个村心、同一个海拔；每片现造（开发者改了天气也跟得上）
    const WorldClock *clock = WorldClock::get_singleton();
    const WeatherModel *wx = world->host_weather();
    const LocalSeason season(wx, clock->get_rules(), double(clock->get_calendar_offset()), r_ledger.x, r_ledger.z, world_ledger_height(r_ledger));
    settle_world_ledger(r_ledger, world->host_farm()->get_model(), taken.get(), clock->get_rules(), rules, p_until, wx ? &season : nullptr);
    const double us = double(ticks_us() - t0);
    slice_us_total += us;
    slice_max_us = std::max(slice_max_us, us);
    settled_hours += game_hours(p_until - from, WorldClock::get_singleton()->get_rules());
    slices++;
}

void Hinterland::_physics_process(double /*p_delta*/) {
    if (!ready_to_work()) {
        return;
    }
    StreamTimer timer(world, StreamChannel::HINTERLAND);
    const WorldClock *clock = WorldClock::get_singleton();
    const double now = clock->get_seconds();
    const uint64_t t0 = ticks_us();
    const double budget_us = budget_ms * 1000.0;
    collect_openings(true);
    launch_openings();

    // 轮转：落后满 settle_hours 的轮到它，一片一片结到此刻；每结完一片看一眼表，花到预算就停，下一帧从这一处接着结
    const double due = game_seconds(settle_hours, clock->get_rules());
    const double slice = game_seconds(slice_hours, clock->get_rules());
    const size_t count = ledgers.size();
    size_t visited = 0;
    while (double(ticks_us() - t0) < budget_us) {
        if (current >= 0) {
            WorldLedger *ledger = ledgers[size_t(current)].get();
            if (!ledger || ledger->state->deployed || ledger->state->clock >= now) {
                current = -1;
                continue;
            }
            settle_slice(*ledger, std::min(now, ledger->state->clock + slice));
            continue;
        }
        if (visited >= count) {
            break; // 这一帧没有该结的了
        }
        const size_t k = ring;
        ring = (ring + 1) % count;
        visited++;
        const WorldLedger *ledger = ledgers[k].get();
        if (ledger && !ledger->state->deployed && now - ledger->state->clock >= due) {
            current = int(k);
        }
    }
    frame_us = double(ticks_us() - t0);
    frame_max_us = std::max(frame_max_us, frame_us);
    frames++;
    frame_avg_us += (frame_us - frame_avg_us) / double(std::min<int64_t>(frames, 600)); // 近 600 帧的滑动平均
}

std::shared_ptr<VillageState> Hinterland::checkout(int p_index, const SettlementLayout &p_layout) {
    ERR_FAIL_COND_V(!ready_to_work(), nullptr);
    ERR_FAIL_INDEX_V(p_index, int(ledgers.size()), nullptr);
    if (!ledgers[size_t(p_index)]) {
        // 还没开账：后台在开就等它，没开就当场开
        for (size_t k = 0; k < in_flight_index.size(); k++) {
            if (in_flight_index[k] == p_index) {
                install(p_index, in_flight[k].get());
                in_flight.erase(in_flight.begin() + long(k));
                in_flight_index.erase(in_flight_index.begin() + long(k));
                break;
            }
        }
        if (!ledgers[size_t(p_index)]) {
            install(p_index, open_world_ledger(world->get_terrain(), spec_of(p_index), rules));
        }
    }
    if (current == p_index) {
        current = -1;
    }
    set_layout(p_index, p_layout);
    return ledgers[size_t(p_index)]->state;
}

void Hinterland::set_layout(int p_index, const SettlementLayout &p_layout) {
    ERR_FAIL_INDEX(p_index, int(ledgers.size()));
    WorldLedger *ledger = ledgers[size_t(p_index)].get();
    if (!ledger || layout_signature(ledger->layout) == layout_signature(p_layout)) {
        return;
    }
    ledger->layout = p_layout;
    ledger->water = village_water(*ledger->ground, ledger->layout, rules.work);
}

const WorldLedger *Hinterland::get_ledger(int p_index) const {
    return p_index >= 0 && p_index < int(ledgers.size()) ? ledgers[size_t(p_index)].get() : nullptr;
}

VillageState *Hinterland::get_state(int p_index) {
    return p_index >= 0 && p_index < int(ledgers.size()) && ledgers[size_t(p_index)] ? ledgers[size_t(p_index)]->state.get() : nullptr;
}

int Hinterland::find_site(int p_site_id, bool p_hamlet) const {
    if (!world) {
        return -1;
    }
    const std::vector<SiteSettlement> &sites = world->get_sites().settlements;
    for (int k = 0; k < int(sites.size()); k++) {
        if (sites[size_t(k)].id == p_site_id && sites[size_t(k)].hamlet == p_hamlet) {
            return k;
        }
    }
    return -1;
}

Dictionary Hinterland::get_ledger_summary(int p_site_id, bool p_hamlet) const {
    Dictionary d;
    const WorldLedger *ledger = get_ledger(find_site(p_site_id, p_hamlet));
    d["opened"] = ledger != nullptr;
    if (!ledger) {
        return d;
    }
    const VillageState &s = *ledger->state;
    int working = 0, outside = 0;
    for (const PersonState &p : s.people) {
        working += p.phase == FolkPhase::WORKING ? 1 : 0;
        outside += p.phase != FolkPhase::INDOORS ? 1 : 0;
    }
    d["deployed"] = s.deployed;
    d["clock"] = s.clock;
    d["grain"] = village_grain(s);
    d["water"] = s.water;
    d["water_cap"] = s.water_cap;
    d["harvested"] = s.tally.harvested;
    d["sown"] = s.tally.sown;
    d["eaten"] = s.tally.eaten;
    d["water_in"] = s.tally.water_in;
    int done = 0;
    for (int n : s.tally.done) {
        done += n;
    }
    d["done"] = done; // 干完了几件活（各样加起来）
    d["people"] = int(s.people.size());
    d["working"] = working;
    d["outside"] = outside;
    d["plots"] = int(s.deployed ? s.claims.size() : s.held.size());
    Dictionary store; // 仓里的：物品 id → 份数（粮种、土产）
    for (const ItemStack &stack : s.store.get_stacks()) {
        store[String::utf8(stack.item.c_str())] = stack.count;
    }
    d["store"] = store;
    return d;
}

Dictionary Hinterland::get_stats() const {
    Dictionary d;
    d["sites"] = int(ledgers.size());
    d["opened"] = opened;
    d["opening"] = int(in_flight.size());
    d["frame_us"] = frame_us;
    d["frame_avg_us"] = frame_avg_us;
    d["frame_max_us"] = frame_max_us;
    d["frames"] = frames;
    d["slices"] = slices;
    d["slice_avg_us"] = slices > 0 ? slice_us_total / double(slices) : 0.0;
    d["slice_max_us"] = slice_max_us;
    d["us_per_village_hour"] = settled_hours > 0.0 ? slice_us_total / settled_hours : 0.0;
    d["restored"] = restored; // 从上回存下的文本补回了几本（模拟分级）
    d["waiting_restore"] = int(saved.size()); // 存下了、还没开账补回的
    d["skipped_hours"] = skipped_hours;
    d["claims"] = taken ? int(taken->size()) : 0;
    int people = 0, deployed = 0;
    double stalest = 0.0;
    const double now = WorldClock::get_singleton() ? WorldClock::get_singleton()->get_seconds() : 0.0;
    for (const auto &ledger : ledgers) {
        if (ledger) {
            people += int(ledger->state->people.size());
            deployed += ledger->state->deployed ? 1 : 0;
            if (!ledger->state->deployed) {
                stalest = std::max(stalest, now - ledger->state->clock);
            }
        }
    }
    d["people"] = people;
    d["deployed"] = deployed;
    d["stalest_s"] = stalest; // 远处最久没结的一处落后几秒（钟的秒）
    return d;
}

String Hinterland::get_ledger_text(int p_site_id, bool p_hamlet) const {
    const WorldLedger *ledger = get_ledger(find_site(p_site_id, p_hamlet));
    return ledger ? String::utf8(save_village_state(*ledger->state).c_str()) : String();
}
