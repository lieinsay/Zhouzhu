#include "planet_host.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "farm.h"
#include "group_stream.h"
#include "planet_frame.h"
#include "world.h"
#include "world_map.h"

using namespace godot;

namespace {

constexpr int NODE_CODE = 1000; // 别的群的码头号、岛号 = 群号 × 1000 + 它自己的号

} // namespace

void PlanetHost::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_anchor_node"), &PlanetHost::get_anchor_node);
    ClassDB::bind_method(D_METHOD("find_dock_id", "node", "local_id"), &PlanetHost::find_dock_id);
    ClassDB::bind_method(D_METHOD("get_dock_berth_by_id", "id"), &PlanetHost::get_dock_berth_by_id);
    ClassDB::bind_method(D_METHOD("get_dock_spawn_by_id", "id"), &PlanetHost::get_dock_spawn_by_id);
    ClassDB::bind_method(D_METHOD("get_location_text", "global"), &PlanetHost::get_location_text);
    ClassDB::bind_method(D_METHOD("set_stream_path", "value"), &PlanetHost::set_stream_path);
    ClassDB::bind_method(D_METHOD("get_stream_path"), &PlanetHost::get_stream_path);
    ClassDB::bind_method(D_METHOD("set_home_path", "value"), &PlanetHost::set_home_path);
    ClassDB::bind_method(D_METHOD("get_home_path"), &PlanetHost::get_home_path);
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "stream_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "GroupStream"), "set_stream_path", "get_stream_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "home_path"), "set_home_path", "get_home_path");
}

void PlanetHost::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        return;
    }
    stream = Object::cast_to<GroupStream>(get_node_or_null(stream_path));
    GroupWorld *home = Object::cast_to<GroupWorld>(get_node_or_null(home_path));
    ERR_FAIL_NULL_MSG(home, "PlanetHost 找不到场景里的岛群（home_path）");
    home_id = home->get_instance_id();
    home_node = home->get_node_id();
    build_docks();
}

void PlanetHost::_process(double /* p_delta */) {
    // GroupStream 就绪（PlanetView 锚定之后）才知道别的群：码头表补上；按需生成的群加进来了再补（群只往后加，已有的下标不变）
    if (stream && stream->is_ready() && (!docks_full || stream->get_group_count() != docks_groups)) {
        build_docks();
    }
}

GroupWorld *PlanetHost::home_world() const {
    if (stream && stream->is_ready()) {
        const GroupStream::Group *g = stream->group_of(home_node);
        return g ? g->world : nullptr;
    }
    return Object::cast_to<GroupWorld>(ObjectDB::get_instance(home_id));
}

GroupWorld *PlanetHost::anchor_world() const {
    if (stream && stream->is_ready()) {
        if (const GroupStream::Group *a = stream->anchor_group()) {
            if (a->world) {
                return a->world;
            }
        }
    }
    return home_world();
}

int PlanetHost::get_anchor_node() const {
    const GroupWorld *w = anchor_world();
    return w ? w->get_node_id() : -1;
}

void PlanetHost::build_docks() {
    docks.clear();
    // 场景里那一群的码头在前：下标、号与它自己的一样（World 的码头下标就是 sites.tsv 的行序）。
    // GroupStream 就绪以后按它记的 sites.tsv 排（行星计划 P4：按需生成的群加进来要重排码头表，这时场景里那一群可能已经拆了，照样排在前面，下标不变）
    const GroupStream::Group *home_group = stream && stream->is_ready() ? stream->group_of(home_node) : nullptr;
    const GroupWorld *home = Object::cast_to<GroupWorld>(ObjectDB::get_instance(home_id));
    if (home_group) {
        for (int k = 0; k < int(home_group->sites.docks.size()); k++) {
            docks.push_back(DockRef{ home_node, k, home_group->sites.docks[size_t(k)].id, true });
        }
    } else if (home && home->is_loaded()) {
        for (int k = 0; k < home->get_dock_count(); k++) {
            docks.push_back(DockRef{ home_node, k, home->get_dock_id(k), true });
        }
    }
    if (!stream || !stream->is_ready()) {
        return;
    }
    for (int g = 0; g < stream->get_group_count(); g++) {
        const GroupStream::Group *grp = stream->group_at(g);
        if (grp->home) {
            continue;
        }
        for (int k = 0; k < int(grp->sites.docks.size()); k++) {
            docks.push_back(DockRef{ grp->node, k, grp->node * NODE_CODE + grp->sites.docks[size_t(k)].id, false });
        }
    }
    docks_full = true;
    docks_groups = stream->get_group_count();
}

int PlanetHost::group_index_of(int p_node) const {
    if (!stream) {
        return -1;
    }
    for (int g = 0; g < stream->get_group_count(); g++) {
        if (stream->group_at(g)->node == p_node) {
            return g;
        }
    }
    return -1;
}

int PlanetHost::code_island(int p_node, int p_island) const {
    if (p_island < 0 || p_node == home_node) {
        return p_island;
    }
    return p_node * NODE_CODE + p_island;
}

int PlanetHost::find_dock_id(int p_node, int p_local_id) const {
    for (const DockRef &d : docks) {
        if (d.node == p_node) {
            const GroupStream::Group *g = stream && stream->is_ready() ? stream->group_of(p_node) : nullptr;
            const int local_id = d.home ? d.id : (g ? g->sites.docks[size_t(d.local)].id : -1);
            if (local_id == p_local_id) {
                return d.id;
            }
        }
    }
    return -1;
}

Transform3D PlanetHost::get_dock_berth_by_id(int p_id) const {
    const int k = map_find_dock(p_id);
    return k >= 0 ? map_dock_berth(k) : Transform3D();
}

Transform3D PlanetHost::get_dock_spawn_by_id(int p_id) const {
    const int k = map_find_dock(p_id);
    return k >= 0 ? map_dock_spawn(k) : Transform3D();
}

// ---------------------------------------------------------------- 地面

Node3D *PlanetHost::map_node() {
    return anchor_world();
}

bool PlanetHost::map_ready() const {
    const GroupWorld *a = anchor_world();
    return a && a->is_loaded();
}

double PlanetHost::map_surface(const Vector3 &p_global) const {
    const GroupWorld *a = anchor_world();
    const double y = a ? a->get_surface_height(p_global.x, p_global.z) : NAN;
    if (!std::isnan(y) || !stream || !stream->is_ready()) {
        return y;
    }
    // 锚群脚下没有地：按真球面问载入了的邻群（瞬移找陆地、离地多高）
    for (int g = 0; g < stream->get_group_count(); g++) {
        const GroupStream::Group *grp = stream->group_at(g);
        if (!grp->world || grp->world == a || !grp->world->is_loaded()) {
            continue;
        }
        const PlanetVec q = stream->to_group(*grp, p_global);
        const double h = grp->world->get_surface_height(q.x, q.z);
        if (!std::isnan(h)) {
            return h;
        }
    }
    return NAN;
}

int PlanetHost::map_island_at(const Vector3 &p_global) const {
    const GroupWorld *a = anchor_world();
    return a ? code_island(a->get_node_id(), a->map_island_at(p_global)) : -1;
}

bool PlanetHost::map_footing(const Vector3 &p_global) const {
    const GroupWorld *a = anchor_world();
    return a && a->map_footing(p_global);
}

// ---------------------------------------------------------------- 码头

int PlanetHost::map_dock_count() const {
    return int(docks.size());
}

int PlanetHost::map_dock_id(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(docks.size()), -1);
    return docks[size_t(p_index)].id;
}

bool PlanetHost::map_dock_open(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(docks.size()), false);
    const DockRef &d = docks[size_t(p_index)];
    const GroupWorld *a = anchor_world();
    // 只有锚群的码头靠得上（栈桥由它的 SiteStream 铺、碰撞在它的平面里）
    return a && a->get_node_id() == d.node && a->map_dock_open(d.local);
}

Transform3D PlanetHost::map_dock_berth(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(docks.size()), Transform3D());
    const DockRef &d = docks[size_t(p_index)];
    const GroupStream::Group *g = stream && stream->is_ready() ? stream->group_of(d.node) : nullptr;
    if (!g) {
        const GroupWorld *home = home_world();
        return home ? home->get_dock_berth(d.local) : Transform3D();
    }
    if (g->world && g->world->is_loaded()) {
        return stream->transform_from_group(*g, g->world->get_dock_berth(d.local));
    }
    // 还没载入：按 sites.tsv 的点（泊位没算）
    const SiteDock &sd = g->sites.docks[size_t(d.local)];
    return Transform3D(Basis(), stream->from_group(*g, PlanetVec(sd.x, 0.0, sd.z)));
}

Transform3D PlanetHost::map_dock_spawn(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(docks.size()), Transform3D());
    const DockRef &d = docks[size_t(p_index)];
    const GroupStream::Group *g = stream && stream->is_ready() ? stream->group_of(d.node) : nullptr;
    if (!g) {
        const GroupWorld *home = home_world();
        return home ? home->get_dock_spawn(d.local) : Transform3D();
    }
    if (g->world && g->world->is_loaded()) {
        return stream->transform_from_group(*g, g->world->get_dock_spawn(d.local));
    }
    const SiteDock &sd = g->sites.docks[size_t(d.local)];
    return Transform3D(Basis(), stream->from_group(*g, PlanetVec(sd.x, 0.0, sd.z)));
}

Vector3 PlanetHost::map_dock_position(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(docks.size()), Vector3());
    const DockRef &d = docks[size_t(p_index)];
    const GroupStream::Group *g = stream && stream->is_ready() ? stream->group_of(d.node) : nullptr;
    if (!g) {
        const GroupWorld *home = home_world();
        return home ? home->get_dock_position(d.local) : Vector3();
    }
    if (g->world && g->world->is_loaded()) {
        const Vector3 p = g->world->get_dock_position(d.local);
        return stream->from_group(*g, PlanetVec(p.x, p.y, p.z));
    }
    const SiteDock &sd = g->sites.docks[size_t(d.local)];
    return stream->from_group(*g, PlanetVec(sd.x, 0.0, sd.z));
}

int PlanetHost::map_dock_island(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(docks.size()), -1);
    const DockRef &d = docks[size_t(p_index)];
    const GroupStream::Group *g = stream && stream->is_ready() ? stream->group_of(d.node) : nullptr;
    if (!g) {
        GroupWorld *home = home_world();
        return home ? home->map_dock_island(d.local) : -1;
    }
    return code_island(d.node, g->sites.docks[size_t(d.local)].island);
}

String PlanetHost::map_dock_label(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(docks.size()), String());
    const DockRef &d = docks[size_t(p_index)];
    const GroupStream::Group *g = stream && stream->is_ready() ? stream->group_of(d.node) : nullptr;
    if (!g) {
        GroupWorld *home = home_world();
        return home ? home->map_dock_label(d.local) : String();
    }
    const SiteDock &sd = g->sites.docks[size_t(d.local)];
    // 场景里那一群照旧「岛 #2 码头 #26」，别的群带群号
    return d.home ? vformat(String(U"岛 #%d 码头 #%d"), sd.island, sd.id) : vformat(String(U"群 #%d 岛 #%d 码头 #%d"), d.node, sd.island, sd.id);
}

// ---------------------------------------------------------------- 玩法的落脚处、流式

std::vector<Shipyard *> PlanetHost::map_shipyards() const {
    GroupWorld *a = anchor_world();
    return a ? a->map_shipyards() : std::vector<Shipyard *>();
}

Farm *PlanetHost::map_farm_at(const Vector3 &p_global) const {
    GroupWorld *a = anchor_world();
    return a ? a->host_farm() : nullptr;
}

GroundHost *PlanetHost::map_settlement_at(const Vector3 &p_global) const {
    GroupWorld *a = anchor_world();
    return a ? a->map_settlement_at(p_global) : nullptr;
}

void PlanetHost::map_focus(const Vector3 &p_global) {
    if (stream) {
        stream->set_focus(p_global);
    }
    if (GroupWorld *a = anchor_world()) {
        a->map_focus(p_global);
    }
}

void PlanetHost::map_prepare(const Vector3 &p_global) {
    if (GroupWorld *a = anchor_world()) {
        a->map_prepare(p_global);
    }
}

int PlanetHost::map_pending() const {
    const GroupWorld *a = anchor_world();
    return a ? a->map_pending() : 0;
}

String PlanetHost::map_location_text(const Vector3 &p_global) const {
    const GroupWorld *a = anchor_world();
    if (!a) {
        return String();
    }
    const String group = vformat(String(U"群 #%d"), a->get_node_id());
    if (!std::isnan(a->get_surface_height(p_global.x, p_global.z)) || !stream || !stream->is_ready()) {
        return group + String(U" · ") + a->map_location_text(p_global);
    }
    // 云海上空：离各群的陆地多远（由近及远，最多三群）
    struct Near {
        double d;
        int node;
    };
    std::vector<Near> near;
    for (int g = 0; g < stream->get_group_count(); g++) {
        const GroupStream::Group *grp = stream->group_at(g);
        if (grp->land_distance < 1e17) {
            near.push_back({ grp->land_distance, grp->node });
        }
    }
    std::sort(near.begin(), near.end(), [](const Near &x, const Near &y) { return x.d < y.d; });
    // 在哪一层（D13）：云带顶以上是云海上空；往下是云带里、云底下（报离海面多高）
    String layer = String(U"云海上空");
    if (p_global.y < VerticalDatum::CLOUD_BASE_Y) {
        layer = vformat(String(U"云底下 · 离海面 %d m"), int(std::lround(p_global.y - VerticalDatum::SEA_Y)));
    } else if (p_global.y < VerticalDatum::CLOUD_TOP_Y) {
        layer = String(U"云带里");
    }
    String text = group + String(U" · ") + layer;
    for (size_t k = 0; k < near.size() && k < 3; k++) {
        text += vformat(String(U"%s群 #%d 陆地 %s km"), k == 0 ? String(U" · 离") : String(U"、"), near[k].node, String::num(near[k].d / 1000.0, 1));
    }
    return text + vformat(String(U" · 高 %d m"), int(std::lround(p_global.y)));
}

WorldMap *PlanetHost::map_make_panel() {
    GroupWorld *a = anchor_world();
    if (!a) {
        return nullptr;
    }
    panel_node = a->get_node_id();
    return a->map_make_panel();
}

void PlanetHost::map_refresh_panel(WorldMap *p_panel) const {
    GroupWorld *a = anchor_world();
    if (!a) {
        return;
    }
    // 换了锚：面板改画新锚群的图
    if (a->get_node_id() != panel_node) {
        a->map_fill_panel(p_panel);
        const_cast<PlanetHost *>(this)->panel_node = a->get_node_id();
    }
    a->map_refresh_panel(p_panel);
}

// ---------------------------------------------------------------- 跑货、采集、野泊、天气：锚群

const std::vector<FreightPort> &PlanetHost::map_freight_ports() const {
    const GroupWorld *a = anchor_world();
    return a ? a->map_freight_ports() : MapHost::map_freight_ports();
}

FreightPost PlanetHost::map_freight_post_near(const Vector3 &p_global, double p_range) {
    GroupWorld *a = anchor_world();
    return a ? a->map_freight_post_near(p_global, p_range) : FreightPost();
}

bool PlanetHost::map_freight_target(int p_port, Transform3D &r_target) const {
    const GroupWorld *a = anchor_world();
    return a && a->map_freight_target(p_port, r_target);
}

ResourceSpot PlanetHost::map_resource_near(const Vector3 &p_global, double p_range) {
    GroupWorld *a = anchor_world();
    return a ? a->map_resource_near(p_global, p_range) : ResourceSpot();
}

ResourceSpot PlanetHost::map_gather_near(const Vector3 &p_global, double p_face_range, double p_facing_yaw) {
    GroupWorld *a = anchor_world();
    return a ? a->map_gather_near(p_global, p_face_range, p_facing_yaw) : ResourceSpot();
}

void PlanetHost::map_resource_taken(const ResourceSpot &p_spot) {
    if (GroupWorld *a = anchor_world()) {
        a->map_resource_taken(p_spot);
    }
}

bool PlanetHost::map_landing_height(double p_x, double p_z, double &r_y) const {
    const GroupWorld *a = anchor_world();
    return a && a->map_landing_height(p_x, p_z, r_y);
}

bool PlanetHost::map_water_at(double p_x, double p_z) const {
    const GroupWorld *a = anchor_world();
    return a && a->map_water_at(p_x, p_z);
}

// 营造（切片十一）：人与船都在锚群的平面里，营造的账、改地形都问锚群
WorksSite PlanetHost::map_works_at(const Vector3 &p_global) {
    GroupWorld *a = anchor_world();
    return a ? a->map_works_at(p_global) : WorksSite();
}

bool PlanetHost::map_earth_cell(const Vector3 &p_global, int &r_i, int &r_j) const {
    const GroupWorld *a = anchor_world();
    return a && a->map_earth_cell(p_global, r_i, r_j);
}

bool PlanetHost::map_earth_cell_center(int p_i, int p_j, Vector3 &r_global) const {
    const GroupWorld *a = anchor_world();
    return a && a->map_earth_cell_center(p_i, p_j, r_global);
}

bool PlanetHost::map_earth_facts(int p_i, int p_j, const Vector3 &p_player, EarthFacts &r_facts) {
    GroupWorld *a = anchor_world();
    return a && a->map_earth_facts(p_i, p_j, p_player, r_facts);
}

int PlanetHost::map_earth_step(int p_i, int p_j) const {
    const GroupWorld *a = anchor_world();
    return a ? a->map_earth_step(p_i, p_j) : 0;
}

bool PlanetHost::map_set_earth_step(int p_i, int p_j, int p_step) {
    GroupWorld *a = anchor_world();
    return a && a->map_set_earth_step(p_i, p_j, p_step);
}

int PlanetHost::map_set_earth_steps(const std::vector<EditCellStep> &p_cells) {
    GroupWorld *a = anchor_world();
    return a ? a->map_set_earth_steps(p_cells) : 0;
}

bool PlanetHost::map_shift_pending() const {
    return stream && stream->is_shift_pending();
}

bool PlanetHost::map_shift_begin(MapShift &r_shift) {
    GroupShift s;
    if (!stream || !stream->begin_shift(s)) {
        return false;
    }
    r_shift.point = [s](const Vector3 &p) { return s.point(p); };
    r_shift.rotation = [s](const Vector3 &p) { return s.rotation(p); };
    r_shift.yaw = [s](const Vector3 &p) { return s.yaw(p); };
    r_shift.regroup = s.from_node != s.to_node;
    return true;
}

void PlanetHost::map_shift_commit() {
    if (stream) {
        stream->commit_shift();
    }
}

bool PlanetHost::map_shift_toward(const Vector3 &p_global) {
    return stream && stream->is_ready() && stream->request_shift_toward(p_global);
}

bool PlanetHost::map_jump_begin(int p_node, String &r_why) {
    if (!stream) {
        r_why = String(U"没有 GroupStream");
        return false;
    }
    return stream->jump_begin(p_node, r_why);
}

int PlanetHost::map_jump_poll(double &r_progress, String &r_text) {
    return stream ? stream->jump_poll(r_progress, r_text) : -1;
}

bool PlanetHost::map_jump_commit() {
    return stream && stream->jump_commit();
}

bool PlanetHost::map_jump_landing(Transform3D &r_spawn, Transform3D &r_berth, bool &r_has_berth, String &r_where) {
    return stream && stream->jump_landing(r_spawn, r_berth, r_has_berth, r_where);
}

void PlanetHost::map_jump_end() {
    if (stream) {
        stream->jump_end();
    }
}

void PlanetHost::map_weather_models(std::vector<WeatherModel *> &r_out) {
    r_out.clear();
    if (!stream || !stream->is_ready()) {
        if (GroupWorld *a = anchor_world(); a && a->map_weather()) {
            r_out.push_back(a->map_weather());
        }
        return;
    }
    for (int g = 0; g < stream->get_group_count(); g++) {
        GroupStream::Group *grp = stream->group_at(g);
        if (grp->world && grp->world->is_loaded() && grp->world->map_weather()) {
            r_out.push_back(grp->world->map_weather());
        }
    }
}

WeatherModel *PlanetHost::map_weather() {
    GroupWorld *a = anchor_world();
    return a ? a->map_weather() : nullptr;
}
