#include "villagers.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/capsule_mesh.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

#include "crops.h"

using namespace godot;

namespace {

const char *GENERATED_META = "villagers_generated";

Ref<StandardMaterial3D> make_material(const Color &p_albedo) {
    Ref<StandardMaterial3D> material;
    material.instantiate();
    material->set_albedo(p_albedo);
    material->set_roughness(1.0);
    return material;
}

MeshInstance3D *add_mesh(Node3D *p_parent, const String &p_name, const Ref<Mesh> &p_mesh, const Vector3 &p_position) {
    MeshInstance3D *instance = memnew(MeshInstance3D);
    instance->set_name(p_name);
    instance->set_mesh(p_mesh);
    instance->set_position(p_position);
    p_parent->add_child(instance);
    return instance;
}

Ref<BoxMesh> box(const Vector3 &p_size, const Ref<StandardMaterial3D> &p_material) {
    Ref<BoxMesh> mesh;
    mesh.instantiate();
    mesh->set_size(p_size);
    mesh->set_material(p_material);
    return mesh;
}

Ref<CylinderMesh> cylinder(real_t p_top, real_t p_bottom, real_t p_height, const Ref<StandardMaterial3D> &p_material) {
    Ref<CylinderMesh> mesh;
    mesh.instantiate();
    mesh->set_top_radius(p_top);
    mesh->set_bottom_radius(p_bottom);
    mesh->set_height(p_height);
    mesh->set_radial_segments(12);
    mesh->set_rings(1);
    mesh->set_material(p_material);
    return mesh;
}

std::string to_std(const String &p_string) {
    return std::string(p_string.utf8().get_data());
}

String utf8(const char *p_text) {
    return String::utf8(p_text);
}

double facing(double p_dx, double p_dz, double p_fallback) {
    return (std::abs(p_dx) + std::abs(p_dz)) < 1e-6 ? p_fallback : std::atan2(p_dx, p_dz);
}

} // namespace

void Villagers::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_villager_count"), &Villagers::get_villager_count);
    ClassDB::bind_method(D_METHOD("get_villager_name", "index"), &Villagers::get_villager_name);
    ClassDB::bind_method(D_METHOD("get_villager_job", "index"), &Villagers::get_villager_job);
    ClassDB::bind_method(D_METHOD("get_villager_task", "index"), &Villagers::get_villager_task);
    ClassDB::bind_method(D_METHOD("get_villager_activity", "index"), &Villagers::get_villager_activity);
    ClassDB::bind_method(D_METHOD("get_villager_phase", "index"), &Villagers::get_villager_phase);
    ClassDB::bind_method(D_METHOD("get_villager_position", "index"), &Villagers::get_villager_position);
    ClassDB::bind_method(D_METHOD("get_villager_target", "index"), &Villagers::get_villager_target);
    ClassDB::bind_method(D_METHOD("get_villager_carrying", "index"), &Villagers::get_villager_carrying);
    ClassDB::bind_method(D_METHOD("get_villager_home", "index"), &Villagers::get_villager_home);
    ClassDB::bind_method(D_METHOD("is_villager_visible", "index"), &Villagers::is_villager_visible);
    ClassDB::bind_method(D_METHOD("get_claimed_cells"), &Villagers::get_claimed_cells);
    ClassDB::bind_method(D_METHOD("get_store_count", "item"), &Villagers::get_store_count);
    ClassDB::bind_method(D_METHOD("get_tally", "task"), &Villagers::get_tally);
    ClassDB::bind_method(D_METHOD("get_path_failures"), &Villagers::get_path_failures);
    ClassDB::bind_method(D_METHOD("get_hour"), &Villagers::get_hour);
    ClassDB::bind_method(D_METHOD("get_day"), &Villagers::get_day);
    ClassDB::bind_method(D_METHOD("get_clock_text"), &Villagers::get_clock_text);
    ClassDB::bind_method(D_METHOD("set_hour", "hour"), &Villagers::set_hour);
    ClassDB::bind_method(D_METHOD("get_nav_height", "global_position"), &Villagers::get_nav_height);
    ClassDB::bind_method(D_METHOD("on_host_rebuilt"), &Villagers::on_host_rebuilt);
    ClassDB::bind_method(D_METHOD("on_layout_changed"), &Villagers::on_layout_changed);

    ClassDB::bind_method(D_METHOD("set_settlement_path", "path"), &Villagers::set_settlement_path);
    ClassDB::bind_method(D_METHOD("get_settlement_path"), &Villagers::get_settlement_path);
    ClassDB::bind_method(D_METHOD("get_farm"), &Villagers::get_farm);
    ClassDB::bind_method(D_METHOD("has_water"), &Villagers::has_water);
    ClassDB::bind_method(D_METHOD("set_max_folk", "value"), &Villagers::set_max_folk);
    ClassDB::bind_method(D_METHOD("get_max_folk"), &Villagers::get_max_folk);
    ClassDB::bind_method(D_METHOD("set_per_house", "value"), &Villagers::set_per_house);
    ClassDB::bind_method(D_METHOD("get_per_house"), &Villagers::get_per_house);
    ClassDB::bind_method(D_METHOD("set_day_seconds", "value"), &Villagers::set_day_seconds);
    ClassDB::bind_method(D_METHOD("get_day_seconds"), &Villagers::get_day_seconds);
    ClassDB::bind_method(D_METHOD("set_start_hour", "value"), &Villagers::set_start_hour);
    ClassDB::bind_method(D_METHOD("get_start_hour"), &Villagers::get_start_hour);
    ClassDB::bind_method(D_METHOD("set_walk_speed", "value"), &Villagers::set_walk_speed);
    ClassDB::bind_method(D_METHOD("get_walk_speed"), &Villagers::get_walk_speed);
    ClassDB::bind_method(D_METHOD("set_seed", "value"), &Villagers::set_seed);
    ClassDB::bind_method(D_METHOD("get_seed"), &Villagers::get_seed);
    ClassDB::bind_method(D_METHOD("set_starting_store", "value"), &Villagers::set_starting_store);
    ClassDB::bind_method(D_METHOD("get_starting_store"), &Villagers::get_starting_store);

    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "settlement_path"), "set_settlement_path", "get_settlement_path");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "per_house", PROPERTY_HINT_RANGE, "0,6,1"), "set_per_house", "get_per_house");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "max_folk", PROPERTY_HINT_RANGE, "0,500,1"), "set_max_folk", "get_max_folk");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "day_seconds", PROPERTY_HINT_RANGE, "10,7200,1,suffix:s"), "set_day_seconds", "get_day_seconds");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "start_hour", PROPERTY_HINT_RANGE, "0,23.99,0.25"), "set_start_hour", "get_start_hour");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "walk_speed", PROPERTY_HINT_RANGE, "0.3,6,0.05,suffix:m/s"), "set_walk_speed", "get_walk_speed");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "starting_store"), "set_starting_store", "get_starting_store");

    BIND_ENUM_CONSTANT(INDOORS);
    BIND_ENUM_CONSTANT(WALKING);
    BIND_ENUM_CONSTANT(WORKING);
}

Villagers::Villagers() {
    starting_store["millet"] = 8;
    starting_store["bean"] = 4;
}

void Villagers::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_physics_process(false);
        return;
    }
    host = GroundHost::of(get_parent());
    ERR_FAIL_NULL_MSG(host, "Villagers 要挂在地面宿主（Island / WorldVillage / FlatMap）下面");
    settlement = Object::cast_to<Settlement>(get_node_or_null(settlement_path));
    ERR_FAIL_NULL_MSG(settlement, "Villagers 找不到 Settlement（settlement_path）");

    mat_skin = make_material(Color(0.86f, 0.68f, 0.52f));
    mat_hat = make_material(Color(0.78f, 0.68f, 0.42f)); // 斗笠
    mat_wood = make_material(Color(0.42f, 0.30f, 0.18f));
    mat_water = make_material(Color(0.35f, 0.55f, 0.75f));
    mat_sack = make_material(Color(0.82f, 0.74f, 0.55f));
    mat_job[int(Job::FARMER)] = make_material(Color(0.52f, 0.40f, 0.26f)); // 褐短褐
    mat_job[int(Job::CARRIER)] = make_material(Color(0.33f, 0.40f, 0.52f)); // 靛青
    mat_job[int(Job::WATCHMAN)] = make_material(Color(0.55f, 0.20f, 0.17f)); // 赭红
    mat_job[int(Job::IDLER)] = make_material(Color(0.36f, 0.50f, 0.44f)); // 青绿

    clock = day.day_seconds * start_hour / 24.0;
    // 聚落与农田都在岛 rebuilt 时重建，它们排在前面、先连的信号，所以这里收到时它们已经好了
    host->host_node()->connect("rebuilt", Callable(this, "on_host_rebuilt"));
    settlement->connect("changed", Callable(this, "on_layout_changed"));
    if (host->host_ready()) {
        on_host_rebuilt();
    }
}

void Villagers::on_host_rebuilt() {
    ground = host->host_ground();
    populate();
}

void Villagers::on_layout_changed() {
    if (!ground) {
        return; // 宿主还没好：等它的 rebuilt
    }
    populate();
}

void Villagers::clear_folk() {
    for (Folk &f : folk) {
        if (f.root) {
            f.root->queue_free();
        }
    }
    folk.clear();
}

void Villagers::populate() {
    clear_folk();
    claims.clear();
    store = Inventory();
    const Array keys = starting_store.keys();
    for (int64_t k = 0; k < keys.size(); k++) {
        store.add(to_std(String(keys[k])), int(starting_store[keys[k]]));
    }
    const SettlementLayout &layout = settlement->get_model();
    double x0 = 0.0, z0 = 0.0;
    int cols = 0, rows = 0;
    host->host_nav_window(layout, nav_rules, x0, z0, cols, rows);

    // 村子的范围（本宿主坐标）：房子的外接圆
    double cx = 0.0, cz = 0.0, reach = 0.0;
    for (const Building &b : layout.get_items()) {
        cx += b.x / layout.size();
        cz += b.z / layout.size();
    }
    for (const Building &b : layout.get_items()) {
        reach = std::max(reach, std::hypot(b.x - cx, b.z - cz));
    }

    // 农田：宿主指给的那一片，地块格换到本宿主的坐标；村子周围（离房子 field_radius_m 以内）有能垦的地才算有田
    farm = host->host_farm();
    bool has_farm = false;
    if (farm && farm->get_host() && !layout.empty()) {
        double fx = 0.0, fz = 0.0, vx = 0.0, vz = 0.0;
        farm->get_host()->host_origin(fx, fz);
        host->host_origin(vx, vz);
        plots = farm->get_model().get_lattice().shifted(vx - fx, vz - fz);
        has_farm = farm->get_model().any_arable_near(cx + vx - fx, cz + vz - fz, reach + work.field_radius_m);
    }

    // 水：村子 water_reach_m 以内的水中线；导航格够不着最近的那处水，就往那边扩
    water.clear();
    std::vector<WaterLine> lines;
    const double far = reach + work.water_reach_m;
    ground->water_lines(cx - far, cz - far, cx + far, cz + far, lines);
    double near_d = 1e18, near_x = 0.0, near_z = 0.0;
    for (WaterLine &line : lines) {
        bool close = false;
        for (const WaterPoint &p : line) {
            const double d = std::hypot(p.x - cx, p.z - cz);
            close = close || d <= far;
            if (d < near_d) {
                near_d = d;
                near_x = p.x;
                near_z = p.z;
            }
        }
        if (close) {
            water.push_back(std::move(line));
        }
    }
    const bool water_inside = near_x >= x0 && near_z >= z0 && near_x <= x0 + cols * nav_rules.cell_m && near_z <= z0 + rows * nav_rules.cell_m;
    if (!water.empty() && cols > 0 && !water_inside) {
        const double margin = 10.0;
        const double lo_x = std::min(x0, near_x - margin), lo_z = std::min(z0, near_z - margin);
        const double hi_x = std::max(x0 + cols * nav_rules.cell_m, near_x + margin), hi_z = std::max(z0 + rows * nav_rules.cell_m, near_z + margin);
        cols = int(std::ceil((hi_x - lo_x) / nav_rules.cell_m));
        rows = int(std::ceil((hi_z - lo_z) / nav_rules.cell_m));
        x0 = lo_x;
        z0 = lo_z;
    }
    nav = build_nav_grid(*ground, x0, z0, cols, rows, layout, nav_rules);
    std::vector<Resident> residents = assign_residents(layout, has_farm, !water.empty(), per_house, uint64_t(seed) ^ host->host_seed());
    if (max_folk > 0 && int(residents.size()) > max_folk) {
        residents.resize(size_t(max_folk)); // 封顶：其余的算在屋里
    }
    const Rng base(uint64_t(seed) * 0x9E3779B97F4A7C15ull + host->host_seed());
    folk.resize(residents.size());
    for (int k = 0; k < int(residents.size()); k++) {
        Folk &f = folk[size_t(k)];
        f.mind.index = k;
        f.mind.who = residents[size_t(k)];
        f.rng = base.fork(uint64_t(k));
        f.speed = walk_speed * f.rng.uniform(0.85, 1.15);
        NavPoint door = building_door(layout.at(f.mind.who.home));
        NavPoint snapped;
        if (nav_nearest_walkable(nav, door.x, door.z, 6.0, true, snapped)) {
            door = snapped;
        }
        f.mind.x = door.x;
        f.mind.z = door.z;
        f.task = Task();
        f.task.kind = TaskKind::GO_HOME;
        f.task.building = f.mind.who.home;
        build_body(f, k);
        enter_home(f);
        f.timer = f.rng.uniform(0.5, 12.0); // 出门错开些
        place_body(f);
    }
}

void Villagers::build_body(Folk &r_folk, int p_index) {
    const Job job = r_folk.mind.who.job;
    Node3D *root = memnew(Node3D);
    root->set_name(vformat("Villager%d", p_index));
    root->set_meta(GENERATED_META, true);
    add_child(root);
    r_folk.root = root;

    Ref<CapsuleMesh> body;
    body.instantiate();
    body->set_radius(0.28f);
    body->set_height(1.3f);
    body->set_material(mat_job[int(job)]);
    add_mesh(root, "Body", body, Vector3(0, 0.72f, 0));
    Ref<SphereMesh> head;
    head.instantiate();
    head->set_radius(0.17f);
    head->set_height(0.34f);
    head->set_material(mat_skin);
    add_mesh(root, "Head", head, Vector3(0, 1.55f, 0.02f));
    if (job == Job::WATCHMAN) {
        add_mesh(root, "Cap", cylinder(0.15f, 0.19f, 0.14f, mat_job[int(job)]), Vector3(0, 1.72f, 0));
    } else if (job != Job::IDLER) {
        add_mesh(root, "Hat", cylinder(0.03f, 0.36f, 0.16f, mat_hat), Vector3(0, 1.74f, 0));
    }

    // 手里的家伙挂在右肩的支点上（+z 是脸朝的方向，右手在 −x）
    Node3D *tool = memnew(Node3D);
    tool->set_name("Tool");
    tool->set_position(Vector3(-0.3f, 1.2f, 0.05f));
    root->add_child(tool);
    r_folk.tool = tool;
    Node3D *load = memnew(Node3D);
    load->set_name("Load");
    root->add_child(load);
    r_folk.load = load;
    switch (job) {
        case Job::FARMER:
            add_mesh(tool, "Handle", box(Vector3(0.05f, 1.3f, 0.05f), mat_wood), Vector3(0, -0.35f, 0.2f));
            add_mesh(tool, "Blade", box(Vector3(0.22f, 0.05f, 0.3f), mat_skin), Vector3(0, -1.0f, 0.33f));
            add_mesh(load, "Sack", box(Vector3(0.45f, 0.5f, 0.3f), mat_sack), Vector3(0, 1.05f, -0.38f));
            break;
        case Job::CARRIER:
            // 扁担横在两肩上、两头挂桶；挑着水时桶里露出水面
            tool->set_position(Vector3(0, 1.32f, 0));
            add_mesh(tool, "Pole", box(Vector3(1.7f, 0.05f, 0.06f), mat_wood), Vector3());
            for (real_t side : { -0.8f, 0.8f }) {
                add_mesh(tool, side < 0 ? "RopeL" : "RopeR", box(Vector3(0.02f, 0.55f, 0.02f), mat_wood), Vector3(side, -0.28f, 0));
                add_mesh(tool, side < 0 ? "BucketL" : "BucketR", cylinder(0.17f, 0.14f, 0.34f, mat_wood), Vector3(side, -0.7f, 0));
                add_mesh(load, side < 0 ? "WaterL" : "WaterR", cylinder(0.155f, 0.155f, 0.03f, mat_water), Vector3(side, 1.32f - 0.54f, 0));
            }
            break;
        case Job::WATCHMAN:
            add_mesh(tool, "Spear", box(Vector3(0.05f, 2.3f, 0.05f), mat_wood), Vector3(0, 0.0f, 0.12f));
            add_mesh(tool, "Tip", box(Vector3(0.08f, 0.25f, 0.02f), mat_skin), Vector3(0, 1.25f, 0.12f));
            break;
        case Job::IDLER:
            break;
    }
    load->set_visible(false);

    Label3D *label = memnew(Label3D);
    label->set_name("Label");
    label->set_position(Vector3(0, 2.2f, 0));
    label->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
    label->set_font_size(40);
    label->set_outline_size(10);
    label->set_pixel_size(0.006f);
    label->set_visibility_range_end(40.0f);
    root->add_child(label);
    r_folk.label = label;
}

VillageView Villagers::view() const {
    VillageView v;
    v.plots = farm ? &plots : nullptr;
    v.water = &water;
    v.layout = &settlement->get_model();
    v.nav = &nav;
    v.farm = farm ? &farm->get_model() : nullptr;
    v.claims = &claims;
    v.store = &store;
    v.day = &day;
    v.work = &work;
    v.hour = hour();
    return v;
}

void Villagers::assign(int p_index) {
    Folk &f = folk[size_t(p_index)];
    f.task = decide_task(f.mind, view(), f.rng);
    if (f.task.kind == TaskKind::TILL) {
        claims.push_back({ f.task.cell_i, f.task.cell_j, p_index }); // 先占上，免得两个人挑中同一块
    }
    f.path = nav_find_path(nav, nav_rules, f.mind.x, f.mind.z, f.task.x, f.task.z);
    f.next = 0;
    f.root->set_visible(true);
    if (f.path.empty()) {
        path_failures++;
        if (f.task.kind == TaskKind::TILL) {
            claims.back().owner = -1; // 够不着：记下来，谁也别再挑它
        }
        if (f.task.kind == TaskKind::GO_HOME) {
            enter_home(f); // 回不了家也不能在外面过夜
            return;
        }
        f.task.kind = TaskKind::WAIT;
        f.task.x = f.mind.x;
        f.task.z = f.mind.z;
        f.task.face_x = f.mind.x + std::sin(f.yaw);
        f.task.face_z = f.mind.z + std::cos(f.yaw);
        f.task.seconds = work.wait_s;
        f.phase = WORKING;
        f.timer = f.task.seconds;
    } else {
        f.phase = WALKING;
    }
    refresh_label(f);
}

void Villagers::remove_claim(int p_i, int p_j) {
    for (size_t k = 0; k < claims.size(); k++) {
        if (claims[k].i == p_i && claims[k].j == p_j) {
            claims.erase(claims.begin() + long(k));
            return;
        }
    }
}

void Villagers::finish(int p_index) {
    Folk &f = folk[size_t(p_index)];
    const Vector2i cell(f.task.cell_i, f.task.cell_j);
    bool done = true;
    switch (f.task.kind) {
        case TaskKind::TILL:
            done = farm && farm->available_action(cell, false) == Farm::ACTION_TILL;
            if (done) {
                farm->perform(cell, Farm::ACTION_TILL, String());
            } else {
                remove_claim(cell.x, cell.y); // 让人抢先垦了（或地形换了）：这块不算他的
            }
            break;
        case TaskKind::SOW:
            done = farm && farm->available_action(cell, true) == Farm::ACTION_SOW && store.remove(f.task.seed, 1);
            if (done) {
                farm->perform(cell, Farm::ACTION_SOW, utf8(f.task.seed.c_str()));
            }
            break;
        case TaskKind::HARVEST: {
            done = farm && farm->available_action(cell, false) == Farm::ACTION_HARVEST;
            if (done) {
                const CropDef *crop = catalog::crop(to_std(farm->get_crop(cell)));
                const int got = farm->perform(cell, Farm::ACTION_HARVEST, String());
                if (crop && got > 0) {
                    f.mind.carrying = crop->product;
                    f.mind.carry_count = got;
                }
            }
        } break;
        case TaskKind::DELIVER:
            if (!f.mind.carrying.empty() && f.mind.carrying != "water") {
                store.add(f.mind.carrying, f.mind.carry_count);
            }
            f.mind.carrying.clear();
            f.mind.carry_count = 0;
            break;
        case TaskKind::FETCH_WATER:
            f.mind.carrying = "water";
            f.mind.carry_count = 1;
            break;
        default:
            break;
    }
    if (done) {
        tallies[int(f.task.kind)]++;
    }
}

void Villagers::enter_home(Folk &r_folk) {
    // 背着的粮顺手放进仓里（天黑了不再跑一趟），水倒进自家缸里
    if (!r_folk.mind.carrying.empty() && r_folk.mind.carrying != "water") {
        store.add(r_folk.mind.carrying, r_folk.mind.carry_count);
    }
    r_folk.mind.carrying.clear();
    r_folk.mind.carry_count = 0;
    r_folk.phase = INDOORS;
    r_folk.path.clear();
    r_folk.timer = r_folk.rng.uniform(1.0, 25.0);
    if (r_folk.root) {
        r_folk.root->set_visible(false);
    }
    refresh_label(r_folk);
}

void Villagers::step_walk(Folk &r_folk, double p_delta) {
    double budget = r_folk.speed * p_delta;
    while (budget > 0.0 && r_folk.next < r_folk.path.size()) {
        const NavPoint &target = r_folk.path[r_folk.next];
        const double dx = target.x - r_folk.mind.x, dz = target.z - r_folk.mind.z;
        const double dist = std::hypot(dx, dz);
        if (dist > 1e-6) {
            // 朝向慢慢转过去，不是一下子掉头
            const double want = std::atan2(dx, dz);
            r_folk.yaw = Math::lerp_angle(r_folk.yaw, want, std::min(1.0, 10.0 * p_delta));
        }
        if (dist <= budget) {
            r_folk.mind.x = target.x;
            r_folk.mind.z = target.z;
            budget -= dist;
            r_folk.next++;
        } else {
            r_folk.mind.x += dx / dist * budget;
            r_folk.mind.z += dz / dist * budget;
            budget = 0.0;
        }
    }
}

void Villagers::place_body(Folk &r_folk) {
    if (!r_folk.root) {
        return;
    }
    const double y = ground_y(r_folk.mind.x, r_folk.mind.z);
    double bob = 0.0, lean = 0.0, sway = 0.0;
    Vector3 tool_rot;
    if (r_folk.phase == WALKING) {
        bob = 0.04 * std::abs(std::sin(r_folk.anim * 7.0));
    } else if (r_folk.phase == WORKING) {
        switch (r_folk.task.kind) {
            case TaskKind::TILL:
            case TaskKind::TEND:
                // 抡锄：举起来、砸下去
                tool_rot.x = real_t(-0.9 + 0.9 * std::sin(r_folk.anim * 4.0));
                lean = 0.18;
                break;
            case TaskKind::HARVEST:
            case TaskKind::SOW:
                lean = 0.45 + 0.1 * std::sin(r_folk.anim * 3.0); // 弯腰
                break;
            case TaskKind::FETCH_WATER:
                lean = 0.35;
                break;
            case TaskKind::CHAT:
                sway = 0.25 * std::sin(r_folk.anim * 1.7);
                break;
            default:
                break;
        }
    }
    r_folk.root->set_position(Vector3(real_t(r_folk.mind.x), real_t(y + bob), real_t(r_folk.mind.z)));
    r_folk.root->set_rotation(Vector3(real_t(lean), real_t(r_folk.yaw + sway), 0));
    if (r_folk.tool && r_folk.mind.who.job != Job::CARRIER) {
        r_folk.tool->set_rotation(tool_rot);
    }
    if (r_folk.load) {
        r_folk.load->set_visible(!r_folk.mind.carrying.empty());
    }
}

void Villagers::refresh_label(const Folk &r_folk) {
    if (r_folk.label) {
        r_folk.label->set_text(utf8(r_folk.mind.who.name.c_str()) + String(U" · ") + get_villager_activity(r_folk.mind.index));
    }
}

void Villagers::_physics_process(double p_delta) {
    if (!host || !settlement || nav.cols == 0) {
        return;
    }
    clock += p_delta;
    const bool home_time = is_home_hour(hour(), day);
    for (int k = 0; k < int(folk.size()); k++) {
        Folk &f = folk[size_t(k)];
        f.anim += p_delta;
        switch (f.phase) {
            case INDOORS:
                if (!home_time) {
                    f.timer -= p_delta;
                    if (f.timer <= 0.0) {
                        assign(k);
                    }
                }
                break;
            case WALKING:
                if (home_time && f.task.kind != TaskKind::GO_HOME) {
                    assign(k); // 收工 / 歇晌：半路掉头回家
                    break;
                }
                step_walk(f, p_delta);
                if (f.next >= f.path.size()) {
                    if (f.task.kind == TaskKind::GO_HOME) {
                        enter_home(f);
                    } else {
                        f.phase = WORKING;
                        f.timer = f.task.seconds;
                        f.yaw = facing(f.task.face_x - f.mind.x, f.task.face_z - f.mind.z, f.yaw);
                        refresh_label(f);
                    }
                }
                break;
            case WORKING:
                if (home_time) {
                    assign(k);
                    break;
                }
                f.timer -= p_delta;
                if (f.timer <= 0.0) {
                    finish(k);
                    assign(k);
                }
                break;
        }
        place_body(f);
    }
}

// ---------------------------------------------------------------- 查询

String Villagers::get_villager_name(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(folk.size()), String());
    return utf8(folk[size_t(p_index)].mind.who.name.c_str());
}

String Villagers::get_villager_job(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(folk.size()), String());
    return String(villagers::job(folk[size_t(p_index)].mind.who.job).id);
}

String Villagers::get_villager_task(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(folk.size()), String());
    return String(villagers::task(folk[size_t(p_index)].task.kind).id);
}

String Villagers::get_villager_activity(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(folk.size()), String());
    const Folk &f = folk[size_t(p_index)];
    String text;
    const String task_name = utf8(villagers::task(f.task.kind).name);
    switch (f.phase) {
        case INDOORS:
            text = String(U"在屋里");
            break;
        case WALKING:
            text = f.task.kind == TaskKind::GO_HOME ? task_name : String(U"去") + task_name;
            break;
        case WORKING:
            text = task_name;
            break;
    }
    if (f.mind.carrying == "water") {
        text += String(U"（挑着水）");
    } else if (!f.mind.carrying.empty()) {
        text += vformat(String(U"（背着%s %d）"), utf8(catalog::item_name(f.mind.carrying).c_str()), f.mind.carry_count);
    }
    return text;
}

Villagers::Phase Villagers::get_villager_phase(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(folk.size()), INDOORS);
    return folk[size_t(p_index)].phase;
}

Vector3 Villagers::get_villager_position(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(folk.size()), Vector3());
    const Folk &f = folk[size_t(p_index)];
    return host_transform().xform(Vector3(real_t(f.mind.x), real_t(ground_y(f.mind.x, f.mind.z)), real_t(f.mind.z)));
}

Vector3 Villagers::get_villager_target(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(folk.size()), Vector3());
    const Task &t = folk[size_t(p_index)].task;
    return host_transform().xform(Vector3(real_t(t.x), real_t(ground_y(t.x, t.z)), real_t(t.z)));
}

String Villagers::get_villager_carrying(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(folk.size()), String());
    return String(folk[size_t(p_index)].mind.carrying.c_str());
}

int Villagers::get_villager_home(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(folk.size()), -1);
    return folk[size_t(p_index)].mind.who.home;
}

bool Villagers::is_villager_visible(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(folk.size()), false);
    const Folk &f = folk[size_t(p_index)];
    return f.root && f.root->is_visible();
}

Array Villagers::get_claimed_cells() const {
    Array cells;
    for (const Claim &c : claims) {
        if (c.owner >= 0) {
            cells.push_back(Vector2i(c.i, c.j));
        }
    }
    return cells;
}

int Villagers::get_store_count(const String &p_item) const {
    return store.count(to_std(p_item));
}

int Villagers::get_tally(const String &p_task) const {
    const std::string id = to_std(p_task);
    for (int k = 0; k <= int(TaskKind::WAIT); k++) {
        if (id == villagers::task(TaskKind(k)).id) {
            return tallies[k];
        }
    }
    return 0;
}

String Villagers::get_clock_text() const {
    return utf8(clock_text(clock, day).c_str());
}

void Villagers::set_hour(double p_hour) {
    const ClockReading now = read_clock(clock, day);
    int target_day = now.day;
    if (p_hour < now.hour) {
        target_day++;
    }
    clock = (target_day + Math::clamp(p_hour, 0.0, 23.999) / 24.0) * day.day_seconds;
}

double Villagers::get_nav_height(const Vector3 &p_global) const {
    if (!host || !ground) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const Vector3 local = host_transform().affine_inverse().xform(p_global);
    double y = 0.0;
    if (!ground->walk_height(local.x, local.z, y)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return host_transform().xform(Vector3(local.x, real_t(y), local.z)).y;
}

double Villagers::ground_y(double p_x, double p_z) const {
    double y = 0.0;
    if (!ground || !ground->walk_height(p_x, p_z, y)) {
        return 0.0;
    }
    return y;
}
