#include "settlement.h"

#include <cmath>
#include <limits>

#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/prism_mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace {

const char *GENERATED_META = "settlement_generated";
constexpr real_t PLINTH_MARGIN = 0.6f; // 台基比墙脚往外多这么多
constexpr real_t ROOF_OVERHANG = 0.9f; // 出檐（檐深是中式房子的样子）
constexpr real_t DOOR_W = 1.3f, DOOR_H = 2.5f, DOOR_T = 0.12f;
constexpr double SITE_STEP = 0.5; // find_site 的搜索格

Ref<StandardMaterial3D> make_material(const Color &p_albedo, float p_roughness) {
    Ref<StandardMaterial3D> material;
    material.instantiate();
    material->set_albedo(p_albedo);
    material->set_roughness(p_roughness);
    return material;
}

std::string to_std(const String &p_string) {
    return std::string(p_string.utf8().get_data());
}

} // namespace

void Settlement::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_layout"), &Settlement::get_layout);
    ClassDB::bind_method(D_METHOD("set_layout", "ids"), &Settlement::set_layout);
    ClassDB::bind_method(D_METHOD("get_count"), &Settlement::get_count);
    ClassDB::bind_method(D_METHOD("get_building_kind", "index"), &Settlement::get_building_kind);
    ClassDB::bind_method(D_METHOD("get_building_position", "index"), &Settlement::get_building_position);
    ClassDB::bind_method(D_METHOD("get_building_yaw_deg", "index"), &Settlement::get_building_yaw_deg);
    ClassDB::bind_method(D_METHOD("get_building_transform", "index"), &Settlement::get_building_transform);
    ClassDB::bind_method(D_METHOD("survey", "kind", "global_position", "yaw"), &Settlement::survey);
    ClassDB::bind_method(D_METHOD("survey_building", "index"), &Settlement::survey_building);
    ClassDB::bind_method(D_METHOD("place", "kind", "global_position", "yaw"), &Settlement::place);
    ClassDB::bind_method(D_METHOD("remove", "index"), &Settlement::remove);
    ClassDB::bind_method(D_METHOD("find_at", "global_position"), &Settlement::find_at);
    ClassDB::bind_method(D_METHOD("find_site", "kind", "global_position", "yaw", "radius"), &Settlement::find_site);
    ClassDB::bind_method(D_METHOD("load"), &Settlement::load);
    ClassDB::bind_method(D_METHOD("save"), &Settlement::save);
    ClassDB::bind_method(D_METHOD("is_dirty"), &Settlement::is_dirty);
    ClassDB::bind_method(D_METHOD("on_host_rebuilt"), &Settlement::on_host_rebuilt);

    ClassDB::bind_method(D_METHOD("set_layout_path", "path"), &Settlement::set_layout_path);
    ClassDB::bind_method(D_METHOD("get_layout_path"), &Settlement::get_layout_path);
    ClassDB::bind_method(D_METHOD("set_max_rise", "value"), &Settlement::set_max_rise);
    ClassDB::bind_method(D_METHOD("get_max_rise"), &Settlement::get_max_rise);

    ADD_PROPERTY(PropertyInfo(Variant::STRING, "layout_path", PROPERTY_HINT_FILE, "*.txt"), "set_layout_path", "get_layout_path");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_rise", PROPERTY_HINT_RANGE, "0.2,6,0.1,suffix:m"), "set_max_rise", "get_max_rise");

    ADD_SIGNAL(MethodInfo("changed"));
}

void Settlement::_ready() {
    host = SettlementHost::of(get_parent());
    ERR_FAIL_NULL_MSG(host, "Settlement 要挂在聚落宿主（Island / WorldVillage）下面");
    mat_stone = make_material(Color(0.52f, 0.50f, 0.46f), 1.0f);
    mat_wall = make_material(Color(0.76f, 0.64f, 0.46f), 1.0f); // 夯土
    mat_roof = make_material(Color(0.30f, 0.30f, 0.34f), 0.9f); // 灰瓦
    mat_door = make_material(Color(0.30f, 0.20f, 0.12f), 0.95f);
    if (!layout_path.is_empty()) {
        load(); // 没有文件的（岛群里的村）清单由宿主用 set_model 给
    }
    // 子节点先于父节点 _ready：宿主此刻可能还没生成地面，等它的 rebuilt；已经生成过就直接盖
    host->host_node()->connect("rebuilt", Callable(this, "on_host_rebuilt"));
    if (host->host_ready()) {
        on_host_rebuilt();
    }
}

void Settlement::on_host_rebuilt() {
    ground = host->host_site_ground(rules);
    clear_generated();
    build_all();
}

void Settlement::clear_generated() {
    const TypedArray<Node> children = get_children();
    for (int64_t k = 0; k < children.size(); k++) {
        Node *child = Object::cast_to<Node>(children[k]);
        if (child && child->has_meta(GENERATED_META)) {
            remove_child(child);
            child->queue_free();
        }
    }
}

void Settlement::build_all() {
    if (!ready()) {
        return;
    }
    for (int k = 0; k < layout.size(); k++) {
        build_one(k);
    }
}

Vector3 Settlement::to_island(const Vector3 &p_global) const {
    return host_transform().affine_inverse().xform(p_global);
}

Transform3D Settlement::building_transform_global(const BuildingDef &, const Vector3 &p_global, double p_yaw, double p_floor_y) const {
    const Vector3 local = to_island(p_global);
    const Transform3D in_island(Basis(Vector3(0, 1, 0), real_t(p_yaw)), Vector3(local.x, real_t(p_floor_y), local.z));
    return host_transform() * in_island;
}

// 一座建筑的几何（岛的局部坐标）：台基埋到最低地面之下、露出 plinth_m；墙立在台基面上；悬山顶的脊沿面阔；门开在 +z 面
void Settlement::build_one(int p_index) {
    const Building &b = layout.at(p_index);
    const BuildingDef *def = buildings::by_id(b.kind);
    if (!def) {
        return;
    }
    SettlementLayout none;
    const SiteReport report = survey_site(*ground, *def, b.x, b.z, b.yaw, none, rules);
    double floor_y = report.floor_y, bottom = report.plinth_bottom;
    if (report.reason.find("没有地面") != std::string::npos) {
        // 地形换了、这座悬在岛外：照画在岸缘高度，开发者一眼能看见它该挪了
        floor_y = def->plinth_m;
        bottom = -rules.bury_m;
    }
    const Transform3D frame(Basis(Vector3(0, 1, 0), real_t(b.yaw)), Vector3(real_t(b.x), real_t(floor_y), real_t(b.z)));
    const real_t w = real_t(def->width_m), d = real_t(def->depth_m), wall = real_t(def->wall_m), roof = real_t(def->roof_m);
    const real_t plinth_h = real_t(floor_y - bottom);

    Node3D *root = memnew(Node3D);
    root->set_name(vformat("B%d_%s", p_index, String(def->id)));
    root->set_meta(GENERATED_META, true);
    root->set_transform(frame);
    add_child(root);

    auto add_box = [&](const String &p_name, const Vector3 &p_size, const Vector3 &p_center, const Ref<StandardMaterial3D> &p_material) {
        Ref<BoxMesh> mesh;
        mesh.instantiate();
        mesh->set_size(p_size);
        mesh->set_material(p_material);
        MeshInstance3D *instance = memnew(MeshInstance3D);
        instance->set_name(p_name);
        instance->set_mesh(mesh);
        instance->set_position(p_center);
        root->add_child(instance);
    };
    auto add_shape = [&](const String &p_name, const Vector3 &p_size, const Vector3 &p_center) {
        Ref<BoxShape3D> shape;
        shape.instantiate();
        shape->set_size(p_size);
        CollisionShape3D *collider = memnew(CollisionShape3D);
        collider->set_name(vformat("B%d_%s", p_index, p_name));
        collider->set_shape(shape);
        collider->set_transform(frame * Transform3D(Basis(), p_center));
        collider->set_meta(GENERATED_META, true);
        add_child(collider); // 碰撞形状得是 StaticBody3D 的直接子节点
    };

    add_box("Plinth", Vector3(w + 2 * PLINTH_MARGIN, plinth_h, d + 2 * PLINTH_MARGIN), Vector3(0, -0.5f * plinth_h, 0), mat_stone);
    add_shape("PlinthShape", Vector3(w + 2 * PLINTH_MARGIN, plinth_h, d + 2 * PLINTH_MARGIN), Vector3(0, -0.5f * plinth_h, 0));
    add_box("Walls", Vector3(w, wall, d), Vector3(0, 0.5f * wall, 0), mat_wall);
    add_shape("WallShape", Vector3(w, wall, d), Vector3(0, 0.5f * wall, 0));

    // 悬山顶：PrismMesh 的脊沿它自己的 z，转 90° 让脊沿面阔（x）
    Ref<PrismMesh> prism;
    prism.instantiate();
    prism->set_left_to_right(0.5f);
    prism->set_size(Vector3(d + 2 * ROOF_OVERHANG, roof, w + 2 * ROOF_OVERHANG));
    prism->set_material(mat_roof);
    MeshInstance3D *roof_instance = memnew(MeshInstance3D);
    roof_instance->set_name("Roof");
    roof_instance->set_mesh(prism);
    roof_instance->set_transform(Transform3D(Basis(Vector3(0, 1, 0), real_t(Math_PI / 2)), Vector3(0, wall + 0.5f * roof, 0)));
    root->add_child(roof_instance);
    add_box("Ridge", Vector3(w + 2 * ROOF_OVERHANG + 0.4f, 0.35f, 0.5f), Vector3(0, wall + roof, 0), mat_roof);

    add_box("Door", Vector3(DOOR_W, DOOR_H, DOOR_T), Vector3(0, 0.5f * DOOR_H, 0.5f * d + 0.02f), mat_door);
    if (std::string(def->id) == "tower") {
        // 望楼：顶层一圈栏杆的样子
        add_box("Rail", Vector3(w + 1.6f, 0.2f, d + 1.6f), Vector3(0, wall - 1.2f, 0), mat_door);
    }
}

PackedStringArray Settlement::get_layout() const {
    PackedStringArray ids;
    for (const std::string &id : settlement_ids(layout)) {
        ids.push_back(String::utf8(id.c_str()));
    }
    return ids;
}

void Settlement::set_layout(const PackedStringArray &p_ids) {
    std::vector<std::string> ids;
    for (int k = 0; k < p_ids.size(); k++) {
        ids.push_back(to_std(p_ids[k]));
    }
    bool ok = true;
    layout = settlement_from_ids(ids, &ok);
    if (!ok) {
        WARN_PRINT("Settlement: 清单里有认不出的条目，跳过了");
    }
    dirty = true;
    clear_generated();
    build_all();
    emit_signal("changed");
}

String Settlement::get_building_kind(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, layout.size(), String());
    return String::utf8(layout.at(p_index).kind.c_str());
}

Vector3 Settlement::get_building_position(int p_index) const {
    return get_building_transform(p_index).origin;
}

double Settlement::get_building_yaw_deg(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, layout.size(), 0.0);
    return Math::rad_to_deg(layout.at(p_index).yaw);
}

Transform3D Settlement::get_building_transform(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, layout.size(), Transform3D());
    const Building &b = layout.at(p_index);
    const BuildingDef *def = buildings::by_id(b.kind);
    SettlementLayout none;
    const SiteReport report = def ? survey_site(*ground, *def, b.x, b.z, b.yaw, none, rules) : SiteReport();
    const Transform3D in_island(Basis(Vector3(0, 1, 0), real_t(b.yaw)), Vector3(real_t(b.x), real_t(report.floor_y), real_t(b.z)));
    return host_transform() * in_island;
}

SiteReport Settlement::survey_site_global(const BuildingDef &p_def, const Vector3 &p_global, double p_yaw, Transform3D &r_transform, int p_ignore) const {
    const Vector3 local = to_island(p_global);
    const SiteReport report = survey_site(*ground, p_def, local.x, local.z, p_yaw, layout, rules, p_ignore);
    const bool grounded = report.reason.find("没有地面") == std::string::npos;
    r_transform = building_transform_global(p_def, p_global, p_yaw, grounded ? report.floor_y : local.y + p_def.plinth_m);
    return report;
}

String Settlement::survey(const String &p_kind, const Vector3 &p_global, double p_yaw) const {
    const BuildingDef *def = buildings::by_id(to_std(p_kind));
    if (!def) {
        return String(U"没有这种建筑：") + p_kind;
    }
    Transform3D unused;
    return String::utf8(survey_site_global(*def, p_global, p_yaw, unused).reason.c_str());
}

String Settlement::survey_building(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, layout.size(), String(U"没有这一座"));
    const Building &b = layout.at(p_index);
    const BuildingDef *def = buildings::by_id(b.kind);
    if (!def) {
        return String(U"没有这种建筑：") + String::utf8(b.kind.c_str());
    }
    return String::utf8(survey_site(*ground, *def, b.x, b.z, b.yaw, layout, rules, p_index).reason.c_str());
}

String Settlement::place(const String &p_kind, const Vector3 &p_global, double p_yaw) {
    const Vector3 local = to_island(p_global);
    const std::string reason = settlement_place(layout, *ground, to_std(p_kind), local.x, local.z, p_yaw, rules);
    if (!reason.empty()) {
        return String::utf8(reason.c_str());
    }
    dirty = true;
    build_one(layout.size() - 1);
    emit_signal("changed");
    return String();
}

bool Settlement::remove(int p_index) {
    if (p_index < 0 || p_index >= layout.size()) {
        return false;
    }
    layout.erase(p_index);
    dirty = true;
    clear_generated(); // 节点按序号命名，整份重画最省事
    build_all();
    emit_signal("changed");
    return true;
}

int Settlement::find_at(const Vector3 &p_global) const {
    const Vector3 local = to_island(p_global);
    return settlement_find(layout, local.x, local.z);
}

Vector3 Settlement::find_site(const String &p_kind, const Vector3 &p_global, double p_yaw, double p_radius) const {
    const Vector3 nan(0, real_t(std::numeric_limits<double>::quiet_NaN()), 0);
    const BuildingDef *def = buildings::by_id(to_std(p_kind));
    if (!def || !ready()) {
        return nan;
    }
    const Vector3 center = to_island(p_global);
    const double cx = std::round(center.x / SITE_STEP) * SITE_STEP, cz = std::round(center.z / SITE_STEP) * SITE_STEP;
    const int steps = int(std::ceil(p_radius / SITE_STEP));
    // 从近到远一圈圈找（按圈上的格子）
    for (int ring = 0; ring <= steps; ring++) {
        double best_d2 = 1e18;
        Vector3 best = nan;
        for (int a = -ring; a <= ring; a++) {
            for (int b = -ring; b <= ring; b++) {
                if (std::max(std::abs(a), std::abs(b)) != ring) {
                    continue;
                }
                const double x = cx + a * SITE_STEP, z = cz + b * SITE_STEP;
                const double d2 = (x - center.x) * (x - center.x) + (z - center.z) * (z - center.z);
                if (d2 > p_radius * p_radius || d2 >= best_d2) {
                    continue;
                }
                const SiteReport report = survey_site(*ground, *def, x, z, p_yaw, layout, rules);
                if (report.ok()) {
                    best_d2 = d2;
                    best = host_transform().xform(Vector3(real_t(x), real_t(report.floor_y - def->plinth_m), real_t(z)));
                }
            }
        }
        if (!std::isnan(best.y)) {
            return best;
        }
    }
    return nan;
}

bool Settlement::load() {
    layout = SettlementLayout();
    dirty = false;
    bool read = false;
    if (!layout_path.is_empty() && FileAccess::file_exists(layout_path)) {
        Ref<FileAccess> file = FileAccess::open(layout_path, FileAccess::READ);
        if (file.is_valid()) {
            const PackedStringArray lines = file->get_as_text().split("\n");
            std::vector<std::string> ids;
            for (int k = 0; k < lines.size(); k++) {
                ids.push_back(to_std(lines[k]));
            }
            bool ok = true;
            layout = settlement_from_ids(ids, &ok);
            if (!ok) {
                WARN_PRINT(String("Settlement: ") + layout_path + String(U" 里有认不出的行，跳过了"));
            }
            read = true;
        }
    }
    if (ready()) {
        clear_generated();
        build_all();
    }
    emit_signal("changed");
    return read;
}

bool Settlement::save() {
    if (layout_path.is_empty()) {
        return false;
    }
    Ref<FileAccess> file = FileAccess::open(layout_path, FileAccess::WRITE);
    if (file.is_null()) {
        ERR_PRINT(String("Settlement: 写不了 ") + layout_path);
        return false;
    }
    file->store_line(String(U"# 聚落清单：一座一行 kind@x,z,yaw°（岛的局部坐标，米 / 度）。在游戏里按 B 进布置模式改，F5 存回这里。"));
    for (const std::string &id : settlement_ids(layout)) {
        file->store_line(String::utf8(id.c_str()));
    }
    file->close();
    dirty = false;
    return true;
}

void Settlement::set_layout_path(const String &p_path) {
    layout_path = p_path;
    if (is_inside_tree() && host) {
        load();
    }
}

void Settlement::set_model(const SettlementLayout &p_layout) {
    layout = p_layout;
    dirty = false;
    if (ready()) {
        clear_generated();
        build_all();
    }
    emit_signal("changed");
}

void Settlement::set_max_rise(double p_value) {
    rules.max_rise_m = p_value;
    if (ready()) {
        ground = host->host_site_ground(rules);
    }
}
