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
#include "world_clock.h"

using namespace godot;

namespace {

const char *GENERATED_META = "villagers_generated";
// 钟一下跳过这么多秒（拨钟、卡了一大下）：不逐段找路了，先估着走过去再展开
constexpr double CATCH_UP_S = 10.0;

MeshInstance3D *add_mesh(Node3D *p_parent, const String &p_name, const Ref<Mesh> &p_mesh, const Vector3 &p_position) {
    MeshInstance3D *instance = memnew(MeshInstance3D);
    instance->set_name(p_name);
    instance->set_mesh(p_mesh);
    instance->set_position(p_position);
    p_parent->add_child(instance);
    return instance;
}

// 小人的几件：同形同材质的共用一份网格（键里带材质的实例号：材质本身也是共用的）
std::string mesh_key(const char *p_kind, std::initializer_list<real_t> p_dims, const Ref<Material> &p_material) {
    std::string key = std::string("villager_") + p_kind;
    for (real_t d : p_dims) {
        key += "_" + std::to_string(int(std::lround(d * 1000.0f)));
    }
    return key + "_" + std::to_string(uint64_t(p_material->get_instance_id()));
}

Ref<Mesh> box(SharedLooks &p_looks, const Vector3 &p_size, const Ref<Material> &p_material) {
    return p_looks.mesh(mesh_key("box", { p_size.x, p_size.y, p_size.z }, p_material), [&]() {
        Ref<BoxMesh> mesh;
        mesh.instantiate();
        mesh->set_size(p_size);
        mesh->set_material(p_material);
        return Ref<Mesh>(mesh);
    });
}

Ref<Mesh> cylinder(SharedLooks &p_looks, real_t p_top, real_t p_bottom, real_t p_height, const Ref<Material> &p_material) {
    return p_looks.mesh(mesh_key("cylinder", { p_top, p_bottom, p_height }, p_material), [&]() {
        Ref<CylinderMesh> mesh;
        mesh.instantiate();
        mesh->set_top_radius(p_top);
        mesh->set_bottom_radius(p_bottom);
        mesh->set_height(p_height);
        mesh->set_radial_segments(12);
        mesh->set_rings(1);
        mesh->set_material(p_material);
        return Ref<Mesh>(mesh);
    });
}

Ref<Mesh> capsule(SharedLooks &p_looks, real_t p_radius, real_t p_height, const Ref<Material> &p_material) {
    return p_looks.mesh(mesh_key("capsule", { p_radius, p_height }, p_material), [&]() {
        Ref<CapsuleMesh> mesh;
        mesh.instantiate();
        mesh->set_radius(p_radius);
        mesh->set_height(p_height);
        mesh->set_material(p_material);
        return Ref<Mesh>(mesh);
    });
}

Ref<Mesh> sphere(SharedLooks &p_looks, real_t p_radius, real_t p_height, const Ref<Material> &p_material) {
    return p_looks.mesh(mesh_key("sphere", { p_radius, p_height }, p_material), [&]() {
        Ref<SphereMesh> mesh;
        mesh.instantiate();
        mesh->set_radius(p_radius);
        mesh->set_height(p_height);
        mesh->set_material(p_material);
        return Ref<Mesh>(mesh);
    });
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

// 近处的田：地图的农田节点。动地走 Farm::perform，好让地块重画
class FarmFields : public FieldBook {
    Farm *farm;

public:
    explicit FarmFields(Farm *p_farm) :
            farm(p_farm) {}
    Plot plot_at(int p_i, int p_j) const override { return farm->get_model().plot_at(p_i, p_j); }
    FarmAction available(int p_i, int p_j, bool p_has_seed) const override { return farm->get_model().available_action(p_i, p_j, p_has_seed); }
    bool till(int p_i, int p_j) override {
        return available(p_i, p_j, false) == FarmAction::TILL && (farm->perform(Vector2i(p_i, p_j), Farm::ACTION_TILL, String()), true);
    }
    bool sow(int p_i, int p_j, const CropDef *p_crop) override {
        return p_crop && available(p_i, p_j, true) == FarmAction::SOW && (farm->perform(Vector2i(p_i, p_j), Farm::ACTION_SOW, String(p_crop->id)), true);
    }
    Harvest harvest(int p_i, int p_j) override {
        const Plot plot = farm->get_model().plot_at(p_i, p_j);
        if (plot.state != PlotState::RIPE || !plot.crop) {
            return Harvest();
        }
        return { plot.crop->product, farm->perform(Vector2i(p_i, p_j), Farm::ACTION_HARVEST, String()) };
    }
};

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
    ClassDB::bind_method(D_METHOD("get_villager_door", "index"), &Villagers::get_villager_door);
    ClassDB::bind_method(D_METHOD("get_villager_carrying", "index"), &Villagers::get_villager_carrying);
    ClassDB::bind_method(D_METHOD("get_villager_home", "index"), &Villagers::get_villager_home);
    ClassDB::bind_method(D_METHOD("is_villager_visible", "index"), &Villagers::is_villager_visible);
    ClassDB::bind_method(D_METHOD("get_claimed_cells"), &Villagers::get_claimed_cells);
    ClassDB::bind_method(D_METHOD("get_store_count", "item"), &Villagers::get_store_count);
    ClassDB::bind_method(D_METHOD("get_grain"), &Villagers::get_grain);
    ClassDB::bind_method(D_METHOD("get_water"), &Villagers::get_water);
    ClassDB::bind_method(D_METHOD("get_water_cap"), &Villagers::get_water_cap);
    ClassDB::bind_method(D_METHOD("get_tally", "task"), &Villagers::get_tally);
    ClassDB::bind_method(D_METHOD("get_ledger_summary"), &Villagers::get_ledger_summary);
    ClassDB::bind_method(D_METHOD("get_path_failures"), &Villagers::get_path_failures);
    ClassDB::bind_method(D_METHOD("get_hour"), &Villagers::get_hour);
    ClassDB::bind_method(D_METHOD("get_day"), &Villagers::get_day);
    ClassDB::bind_method(D_METHOD("get_clock_text"), &Villagers::get_clock_text);
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
    ClassDB::bind_method(D_METHOD("set_walk_speed", "value"), &Villagers::set_walk_speed);
    ClassDB::bind_method(D_METHOD("get_walk_speed"), &Villagers::get_walk_speed);
    ClassDB::bind_method(D_METHOD("set_seed", "value"), &Villagers::set_seed);
    ClassDB::bind_method(D_METHOD("get_seed"), &Villagers::get_seed);
    ClassDB::bind_method(D_METHOD("set_starting_store", "value"), &Villagers::set_starting_store);
    ClassDB::bind_method(D_METHOD("get_starting_store"), &Villagers::get_starting_store);

    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "settlement_path"), "set_settlement_path", "get_settlement_path");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "per_house", PROPERTY_HINT_RANGE, "0,6,1"), "set_per_house", "get_per_house");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "max_folk", PROPERTY_HINT_RANGE, "0,500,1"), "set_max_folk", "get_max_folk");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "walk_speed", PROPERTY_HINT_RANGE, "0.3,6,0.05,suffix:m/s"), "set_walk_speed", "get_walk_speed");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "starting_store"), "set_starting_store", "get_starting_store");

    BIND_ENUM_CONSTANT(INDOORS);
    BIND_ENUM_CONSTANT(WALKING);
    BIND_ENUM_CONSTANT(WORKING);
}

Villagers::Villagers() {
    const Inventory store = village_starting_store();
    for (const ItemStack &s : store.get_stacks()) {
        starting_store[String(s.item.c_str())] = s.count;
    }
}

void Villagers::set_ledger(const std::shared_ptr<VillageState> &p_state, const std::shared_ptr<ClaimRegistry> &p_taken) {
    state = p_state;
    taken = p_taken;
    borrowed = state != nullptr;
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

    looks = host->host_looks();
    if (!looks) {
        own_looks = std::make_unique<SharedLooks>();
        looks = own_looks.get();
    }
    mat_skin = looks->plain_material(Color(0.86f, 0.68f, 0.52f));
    mat_hat = looks->plain_material(Color(0.78f, 0.68f, 0.42f)); // 斗笠
    mat_wood = looks->plain_material(Color(0.42f, 0.30f, 0.18f));
    mat_water = looks->plain_material(Color(0.35f, 0.55f, 0.75f));
    mat_sack = looks->plain_material(Color(0.82f, 0.74f, 0.55f));
    mat_job[int(Job::FARMER)] = looks->plain_material(Color(0.52f, 0.40f, 0.26f)); // 褐短褐
    mat_job[int(Job::CARRIER)] = looks->plain_material(Color(0.33f, 0.40f, 0.52f)); // 靛青
    mat_job[int(Job::WATCHMAN)] = looks->plain_material(Color(0.55f, 0.20f, 0.17f)); // 赭红
    mat_job[int(Job::IDLER)] = looks->plain_material(Color(0.36f, 0.50f, 0.44f)); // 青绿
    rules.walk_speed = walk_speed;

    // 聚落与农田都在宿主 rebuilt 时重建，它们排在前面、先连的信号，所以这里收到时它们已经好了
    host->host_node()->connect("rebuilt", Callable(this, "on_host_rebuilt"));
    settlement->connect("changed", Callable(this, "on_layout_changed"));
    if (host->host_ready()) {
        on_host_rebuilt();
    }
}

void Villagers::_notification(int p_what) {
    if (p_what == NOTIFICATION_EXIT_TREE) {
        collapse();
    }
}

double Villagers::now() const {
    const WorldClock *clock = WorldClock::get_singleton();
    return clock ? clock->get_seconds() : 0.0;
}

const DayRules &Villagers::day() const {
    static const DayRules fallback;
    const WorldClock *clock = WorldClock::get_singleton();
    return clock ? clock->get_rules() : fallback;
}

Inventory Villagers::starting_inventory() const {
    Inventory store;
    const Array keys = starting_store.keys();
    for (int64_t k = 0; k < keys.size(); k++) {
        store.add(to_std(String(keys[k])), int(starting_store[keys[k]]));
    }
    return store;
}

void Villagers::on_host_rebuilt() {
    ground = host->host_ground();
    prepare();
    bring_up(!borrowed); // 自己的账：地形换了，地块都没了，从头招人；借来的账：接着记
}

void Villagers::on_layout_changed() {
    if (!ground) {
        return; // 宿主还没好：等它的 rebuilt
    }
    prepare();
    bring_up(true);
}

void Villagers::prepare() {
    const SettlementLayout &layout = settlement->get_model();

    // 时令按村心（宿主原点）：远处账房也按这一点
    host->host_origin(season_x, season_z);
    if (!ground || !ground->site_height(0.0, 0.0, season_h) || std::isnan(season_h)) {
        season_h = 0.0;
    }

    // 农田：宿主指给的那一片，地块格换到本宿主的坐标
    farm = host->host_farm();
    fields.reset();
    if (farm && farm->get_host()) {
        double fx = 0.0, fz = 0.0, vx = 0.0, vz = 0.0;
        farm->get_host()->host_origin(fx, fz);
        host->host_origin(vx, vz);
        plots = farm->get_model().get_lattice().shifted(vx - fx, vz - fz);
        fields = std::make_unique<FarmFields>(farm);
    } else {
        farm = nullptr;
    }

    // 水与导航格：上层在后台照这张清单备好了就用，否则现算（窗口宿主给；附近的水够不着就往那边扩）
    if (prepared_nav && prepared_nav_sig == layout_signature(layout)) {
        nav = std::move(prepared_nav->nav);
        water = std::move(prepared_nav->water);
        prepared_nav.reset();
        return;
    }
    prepared_nav.reset();
    double x0 = 0.0, z0 = 0.0;
    int cols = 0, rows = 0;
    host->host_nav_window(layout, rules.nav, x0, z0, cols, rows);
    VillageNav fresh = prepare_village_nav(*ground, layout, rules, x0, z0, cols, rows);
    nav = std::move(fresh.nav);
    water = std::move(fresh.water);
}

VillageWorld Villagers::world(bool p_near) {
    VillageWorld w;
    w.layout = &settlement->get_model();
    w.plots = farm ? &plots : nullptr;
    w.water = &water;
    w.nav = p_near ? &nav : nullptr;
    w.ground = ground.get();
    w.fields = fields.get();
    w.taken = taken.get();
    w.day = &day();
    w.rules = &rules;
    // 有天气的地图（岛群）：按时令过日子。与远处账房同一处（村心、村心的地面高），远近照旧同一套
    const WeatherModel *wx = host ? host->host_weather() : nullptr;
    if (wx) {
        const WorldClock *clock = WorldClock::get_singleton();
        season = LocalSeason(wx, day(), clock ? double(clock->get_calendar_offset()) : 0.0, season_x, season_z, season_h, SeasonRules(),
                host ? host->host_local_offset_h() : 0.0);
        w.season = &season;
    }
    return w;
}

void Villagers::bring_up(bool p_refound) {
    clear_bodies();
    if (!state) {
        state = std::make_shared<VillageState>();
        borrowed = false;
    }
    const SettlementLayout &layout = settlement->get_model();
    VillageState &s = *state;
    if (p_refound || !s.founded || s.layout_sig != layout_signature(layout)) {
        // 开账（或按新清单重招）：仓与水缸留着
        const bool had = s.founded;
        const Inventory store = had ? s.store : starting_inventory();
        const double water_before = s.water;
        double fx = 0.0, fz = 0.0, vx = 0.0, vz = 0.0;
        if (farm) {
            farm->get_host()->host_origin(fx, fz);
            host->host_origin(vx, vz);
        }
        const bool has_farm = farm && village_has_farm(farm->get_model(), vx - fx, vz - fz, layout, rules.work);
        found_village(s, world(true), has_farm, !water.empty(), per_house, max_folk, uint64_t(seed) ^ host->host_seed(), store, now());
        if (had) {
            s.water = std::min(water_before, s.water_cap);
        }
    } else if (!s.deployed) {
        // 借来的账本还在远处：先在远处结到此刻，再展开（地块放回农田、人摆到各自此刻该在的地方）
        {
            HeldFields held(s.held, farm ? &farm->get_model() : nullptr, &s.arable);
            VillageWorld w = world(false);
            w.fields = farm ? &held : nullptr;
            advance_village(s, w, now());
        }
        std::vector<Vector2i> cells;
        for (const HeldPlot &h : s.held) {
            cells.push_back(Vector2i(h.i, h.j));
        }
        deploy_village(s, world(true), farm ? &farm->edit_model() : nullptr, !progressive);
        if (farm) {
            farm->refresh_cells(cells);
        }
    }
    bodies.resize(s.people.size());
    for (int k = 0; k < int(s.people.size()) && (!progressive || k < BODIES_PER_TICK); k++) {
        build_body(bodies[size_t(k)], s.people[size_t(k)], k);
        place_body(bodies[size_t(k)], s.people[size_t(k)], 0.0);
    }
    catching_up = progressive;
}

void Villagers::collapse() {
    if (!state || !borrowed || !state->deployed) {
        return;
    }
    // 还回远处账房：认领的地连同长势从农田里拿出来记进账本
    FarmModel *model = nullptr;
    std::vector<Vector2i> cells;
    if (farm && farm->is_inside_tree()) {
        model = &farm->edit_model();
        for (const Claim &c : state->claims) {
            if (c.owner >= 0) {
                cells.push_back(Vector2i(c.i, c.j));
            }
        }
    }
    collapse_village(*state, model);
    if (model) {
        farm->refresh_cells(cells);
    }
}

void Villagers::clear_bodies() {
    for (Body &b : bodies) {
        if (b.root) {
            b.root->queue_free();
        }
    }
    bodies.clear();
}

void Villagers::build_body(Body &r_body, const PersonState &p_person, int p_index) {
    const Job job = p_person.mind.who.job;
    Node3D *root = memnew(Node3D);
    root->set_name(vformat("Villager%d", p_index));
    root->set_meta(GENERATED_META, true);
    add_child(root);
    r_body.root = root;

    SharedLooks &l = *looks;
    add_mesh(root, "Body", capsule(l, 0.28f, 1.3f, mat_job[int(job)]), Vector3(0, 0.72f, 0));
    add_mesh(root, "Head", sphere(l, 0.17f, 0.34f, mat_skin), Vector3(0, 1.55f, 0.02f));
    if (job == Job::WATCHMAN) {
        add_mesh(root, "Cap", cylinder(l, 0.15f, 0.19f, 0.14f, mat_job[int(job)]), Vector3(0, 1.72f, 0));
    } else if (job != Job::IDLER) {
        add_mesh(root, "Hat", cylinder(l, 0.03f, 0.36f, 0.16f, mat_hat), Vector3(0, 1.74f, 0));
    }

    // 手里的家伙挂在右肩的支点上（+z 是脸朝的方向，右手在 −x）
    Node3D *tool = memnew(Node3D);
    tool->set_name("Tool");
    tool->set_position(Vector3(-0.3f, 1.2f, 0.05f));
    root->add_child(tool);
    r_body.tool = tool;
    Node3D *load = memnew(Node3D);
    load->set_name("Load");
    root->add_child(load);
    r_body.load = load;
    switch (job) {
        case Job::FARMER:
            add_mesh(tool, "Handle", box(l, Vector3(0.05f, 1.3f, 0.05f), mat_wood), Vector3(0, -0.35f, 0.2f));
            add_mesh(tool, "Blade", box(l, Vector3(0.22f, 0.05f, 0.3f), mat_skin), Vector3(0, -1.0f, 0.33f));
            add_mesh(load, "Sack", box(l, Vector3(0.45f, 0.5f, 0.3f), mat_sack), Vector3(0, 1.05f, -0.38f));
            break;
        case Job::CARRIER:
            // 扁担横在两肩上、两头挂桶；挑着水时桶里露出水面
            tool->set_position(Vector3(0, 1.32f, 0));
            add_mesh(tool, "Pole", box(l, Vector3(1.7f, 0.05f, 0.06f), mat_wood), Vector3());
            for (real_t side : { -0.8f, 0.8f }) {
                add_mesh(tool, side < 0 ? "RopeL" : "RopeR", box(l, Vector3(0.02f, 0.55f, 0.02f), mat_wood), Vector3(side, -0.28f, 0));
                add_mesh(tool, side < 0 ? "BucketL" : "BucketR", cylinder(l, 0.17f, 0.14f, 0.34f, mat_wood), Vector3(side, -0.7f, 0));
                add_mesh(load, side < 0 ? "WaterL" : "WaterR", cylinder(l, 0.155f, 0.155f, 0.03f, mat_water), Vector3(side, 1.32f - 0.54f, 0));
            }
            break;
        case Job::WATCHMAN:
            add_mesh(tool, "Spear", box(l, Vector3(0.05f, 2.3f, 0.05f), mat_wood), Vector3(0, 0.0f, 0.12f));
            add_mesh(tool, "Tip", box(l, Vector3(0.08f, 0.25f, 0.02f), mat_skin), Vector3(0, 1.25f, 0.12f));
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
    r_body.label = label;
    r_body.yaw = facing(p_person.task.face_x - p_person.mind.x, p_person.task.face_z - p_person.mind.z, 0.0);
}

void Villagers::place_body(Body &r_body, const PersonState &p_person, double p_delta) {
    if (!r_body.root) {
        return;
    }
    const VillagerMind &m = p_person.mind;
    r_body.anim += p_delta;
    r_body.root->set_visible(p_person.phase != FolkPhase::INDOORS);
    // 朝向慢慢转过去，不是一下子掉头：走着朝下一个路点，干活朝活计面朝的点
    double want = r_body.yaw;
    if (p_person.phase == FolkPhase::WALKING) {
        const bool on_path = !p_person.estimated && p_person.next < p_person.path.size();
        const double tx = on_path ? p_person.path[p_person.next].x : p_person.task.x;
        const double tz = on_path ? p_person.path[p_person.next].z : p_person.task.z;
        want = facing(tx - m.x, tz - m.z, r_body.yaw);
    } else if (p_person.phase == FolkPhase::WORKING) {
        want = facing(p_person.task.face_x - m.x, p_person.task.face_z - m.z, r_body.yaw);
    }
    r_body.yaw = p_delta > 0.0 ? Math::lerp_angle(r_body.yaw, want, std::min(1.0, 10.0 * p_delta)) : want;

    double bob = 0.0, lean = 0.0, sway = 0.0;
    Vector3 tool_rot;
    if (p_person.phase == FolkPhase::WALKING) {
        bob = 0.04 * std::abs(std::sin(r_body.anim * 7.0));
    } else if (p_person.phase == FolkPhase::WORKING) {
        switch (p_person.task.kind) {
            case TaskKind::TILL:
            case TaskKind::TEND:
                // 抡锄：举起来、砸下去
                tool_rot.x = real_t(-0.9 + 0.9 * std::sin(r_body.anim * 4.0));
                lean = 0.18;
                break;
            case TaskKind::HARVEST:
            case TaskKind::SOW:
                lean = 0.45 + 0.1 * std::sin(r_body.anim * 3.0); // 弯腰
                break;
            case TaskKind::FETCH_WATER:
                lean = 0.35;
                break;
            case TaskKind::CHAT:
                sway = 0.25 * std::sin(r_body.anim * 1.7);
                break;
            default:
                break;
        }
    }
    const double y = ground_y(m.x, m.z);
    r_body.root->set_position(Vector3(real_t(m.x), real_t(y + bob), real_t(m.z)));
    r_body.root->set_rotation(Vector3(real_t(lean), real_t(r_body.yaw + sway), 0));
    if (r_body.tool && m.who.job != Job::CARRIER) {
        r_body.tool->set_rotation(tool_rot);
    }
    if (r_body.load) {
        r_body.load->set_visible(!m.carrying.empty());
    }
    refresh_label(r_body, p_person);
}

void Villagers::refresh_label(Body &r_body, const PersonState &p_person) {
    if (!r_body.label) {
        return;
    }
    const std::string key = std::to_string(int(p_person.phase)) + "/" + std::to_string(int(p_person.task.kind)) + "/" + p_person.mind.carrying + "/" +
            std::to_string(p_person.mind.carry_count);
    if (key == r_body.label_key) {
        return;
    }
    r_body.label_key = key;
    r_body.label->set_text(utf8(p_person.mind.who.name.c_str()) + String(U" · ") + get_villager_activity(p_person.mind.index));
}

void Villagers::_physics_process(double p_delta) {
    if (!host || !settlement || !state || !state->founded || nav.cols == 0) {
        return;
    }
    const double t = now();
    if (t - state->clock > CATCH_UP_S) {
        // 钟一下跳远了（拨钟）：这一段估着走过去，再从各自此刻的位置重新找路
        advance_village(*state, world(false), t);
        deploy_village(*state, world(true), farm ? &farm->edit_model() : nullptr);
    } else {
        advance_village(*state, world(true), t);
    }
    if (catching_up) {
        // 展开分几帧做：给几个估着走的找路、搭几个小人
        int repathed = 0, built = 0;
        bool left = false;
        const VillageWorld w = world(true);
        for (int k = 0; k < int(state->people.size()); k++) {
            const PersonState &p = state->people[size_t(k)];
            if (p.phase == FolkPhase::WALKING && p.estimated) {
                if (repathed < REPATH_PER_TICK) {
                    repathed += repath_walker(*state, w, k) ? 1 : 0;
                } else {
                    left = true;
                }
            }
            if (size_t(k) < bodies.size() && !bodies[size_t(k)].root) {
                if (built < BODIES_PER_TICK) {
                    build_body(bodies[size_t(k)], state->people[size_t(k)], k);
                    built++;
                } else {
                    left = true;
                }
            }
        }
        catching_up = left;
    }
    for (size_t k = 0; k < bodies.size() && k < state->people.size(); k++) {
        place_body(bodies[k], state->people[k], p_delta);
    }
}

// ---------------------------------------------------------------- 查询

String Villagers::get_villager_name(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, get_villager_count(), String());
    return utf8(state->people[size_t(p_index)].mind.who.name.c_str());
}

String Villagers::get_villager_job(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, get_villager_count(), String());
    return String(villagers::job(state->people[size_t(p_index)].mind.who.job).id);
}

String Villagers::get_villager_task(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, get_villager_count(), String());
    return String(villagers::task(state->people[size_t(p_index)].task.kind).id);
}

String Villagers::get_villager_activity(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, get_villager_count(), String());
    const PersonState &p = state->people[size_t(p_index)];
    String text;
    const String task_name = utf8(villagers::task(p.task.kind).name);
    switch (p.phase) {
        case FolkPhase::INDOORS:
            text = String(U"在屋里");
            break;
        case FolkPhase::WALKING:
            text = p.task.kind == TaskKind::GO_HOME ? task_name : String(U"去") + task_name;
            break;
        case FolkPhase::WORKING:
            text = task_name;
            break;
    }
    if (p.mind.carrying == "water") {
        text += String(U"（挑着水）");
    } else if (!p.mind.carrying.empty()) {
        text += vformat(String(U"（背着%s %d）"), utf8(catalog::item_name(p.mind.carrying).c_str()), p.mind.carry_count);
    }
    return text;
}

Villagers::Phase Villagers::get_villager_phase(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, get_villager_count(), INDOORS);
    return Phase(state->people[size_t(p_index)].phase);
}

Vector3 Villagers::get_villager_position(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, get_villager_count(), Vector3());
    const VillagerMind &m = state->people[size_t(p_index)].mind;
    return host_transform().xform(Vector3(real_t(m.x), real_t(ground_y(m.x, m.z)), real_t(m.z)));
}

Vector3 Villagers::get_villager_target(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, get_villager_count(), Vector3());
    const Task &t = state->people[size_t(p_index)].task;
    return host_transform().xform(Vector3(real_t(t.x), real_t(ground_y(t.x, t.z)), real_t(t.z)));
}

Vector3 Villagers::get_villager_door(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, get_villager_count(), Vector3());
    const NavPoint door = building_door(settlement->get_model().at(state->people[size_t(p_index)].mind.who.home));
    return host_transform().xform(Vector3(real_t(door.x), real_t(ground_y(door.x, door.z)), real_t(door.z)));
}

String Villagers::get_villager_carrying(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, get_villager_count(), String());
    return String(state->people[size_t(p_index)].mind.carrying.c_str());
}

int Villagers::get_villager_home(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, get_villager_count(), -1);
    return state->people[size_t(p_index)].mind.who.home;
}

bool Villagers::is_villager_visible(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(bodies.size()), false);
    const Body &b = bodies[size_t(p_index)];
    return b.root && b.root->is_visible();
}

Array Villagers::get_claimed_cells() const {
    Array cells;
    if (state) {
        for (const Claim &c : state->claims) {
            if (c.owner >= 0) {
                cells.push_back(Vector2i(c.i, c.j));
            }
        }
    }
    return cells;
}

int Villagers::get_store_count(const String &p_item) const {
    return state ? state->store.count(to_std(p_item)) : 0;
}

int Villagers::get_grain() const {
    return state ? village_grain(*state) : 0;
}

double Villagers::get_water() const {
    return state ? state->water : 0.0;
}

double Villagers::get_water_cap() const {
    return state ? state->water_cap : 0.0;
}

int Villagers::get_tally(const String &p_task) const {
    if (!state) {
        return 0;
    }
    const std::string id = to_std(p_task);
    for (int k = 0; k <= int(TaskKind::WAIT); k++) {
        if (id == villagers::task(TaskKind(k)).id) {
            return state->tally.done[k];
        }
    }
    return 0;
}

Dictionary Villagers::get_ledger_summary() const {
    Dictionary d;
    if (!state) {
        return d;
    }
    const VillageTally &t = state->tally;
    d["harvested"] = t.harvested;
    d["sown"] = t.sown;
    d["eaten"] = t.eaten;
    d["hungry"] = t.hungry;
    d["water_in"] = t.water_in;
    d["water_used"] = t.water_used;
    d["clock"] = state->clock;
    d["grain"] = village_grain(*state);
    d["water"] = state->water;
    return d;
}

double Villagers::get_hour() const {
    return read_clock(now(), day()).hour;
}

int Villagers::get_day() const {
    return read_clock(now(), day()).day;
}

String Villagers::get_clock_text() const {
    return utf8(clock_text(now(), day()).c_str());
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
