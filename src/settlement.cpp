#include "settlement.h"

#include <cmath>
#include <limits>

#include <godot_cpp/classes/area3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "land_berth.h"
#include "template_library.h"
#include "village_prep.h"

using namespace godot;

namespace {

const char *GENERATED_META = "settlement_generated";
constexpr double SITE_STEP = 0.5; // find_site 的搜索格
constexpr uint32_t LADDER_LAYER = 8; // Player 的 Sensor 只看这一层（与船上的梯子同）

std::string to_std(const String &p_string) {
    return std::string(p_string.utf8().get_data());
}

PackedVector3Array vec3s(const std::vector<float> &p_flat) {
    PackedVector3Array out;
    out.resize(int64_t(p_flat.size() / 3));
    Vector3 *w = out.ptrw();
    for (int64_t k = 0; k < out.size(); k++) {
        w[k] = Vector3(p_flat[size_t(k) * 3], p_flat[size_t(k) * 3 + 1], p_flat[size_t(k) * 3 + 2]);
    }
    return out;
}

} // namespace

BuildingArrays to_building_arrays(BuildingMesh &&p_mesh) {
    BuildingArrays out;
    for (int g = 0; g < int(p_mesh.groups.size()); g++) {
        for (int m = 0; m < int(PartMaterial::COUNT); m++) {
            MeshPart &part = p_mesh.groups[size_t(g)][size_t(m)];
            if (part.empty()) {
                continue;
            }
            BuildingArrays::Surface s;
            s.group = g;
            s.material = m;
            s.vertices = vec3s(part.positions);
            s.normals = vec3s(part.normals);
            s.indices.resize(int64_t(part.indices.size()));
            std::copy(part.indices.begin(), part.indices.end(), s.indices.ptrw());
            out.surfaces.push_back(std::move(s));
            part = MeshPart();
        }
    }
    out.collision = vec3s(p_mesh.collision);
    p_mesh.collision.clear();
    out.mesh = std::move(p_mesh);
    return out;
}

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
    ClassDB::bind_method(D_METHOD("place", "kind", "global_position", "yaw", "style"), &Settlement::place, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("get_building_style", "index"), &Settlement::get_building_style);
    ClassDB::bind_method(D_METHOD("set_building_style", "index", "style"), &Settlement::set_building_style);
    ClassDB::bind_method(D_METHOD("get_building_template", "index"), &Settlement::get_building_template);
    ClassDB::bind_method(D_METHOD("get_building_entry", "index"), &Settlement::get_building_entry);
    ClassDB::bind_method(D_METHOD("get_berth_contact", "index"), &Settlement::get_berth_contact);
    ClassDB::bind_method(D_METHOD("get_berth_indices"), &Settlement::get_berth_indices);
    ClassDB::bind_method(D_METHOD("get_building_ladders", "index"), &Settlement::get_building_ladders);
    ClassDB::bind_method(D_METHOD("set_cutaway", "index", "level"), &Settlement::set_cutaway);
    ClassDB::bind_method(D_METHOD("on_templates_changed", "id"), &Settlement::on_templates_changed);
    ClassDB::bind_method(D_METHOD("remove", "index"), &Settlement::remove);
    ClassDB::bind_method(D_METHOD("find_at", "global_position"), &Settlement::find_at);
    ClassDB::bind_method(D_METHOD("find_site", "kind", "global_position", "yaw", "radius"), &Settlement::find_site);
    ClassDB::bind_method(D_METHOD("load"), &Settlement::load);
    ClassDB::bind_method(D_METHOD("save"), &Settlement::save);
    ClassDB::bind_method(D_METHOD("is_dirty"), &Settlement::is_dirty);
    ClassDB::bind_method(D_METHOD("get_base_count"), &Settlement::get_base_count);
    ClassDB::bind_method(D_METHOD("get_ground_height", "global_position"), &Settlement::get_ground_height);
    ClassDB::bind_method(D_METHOD("on_host_rebuilt"), &Settlement::on_host_rebuilt);
    ClassDB::bind_method(D_METHOD("is_mounted", "index"), &Settlement::is_mounted);
    ClassDB::bind_method(D_METHOD("get_mounted_count"), &Settlement::get_mounted_count);
    ClassDB::bind_method(D_METHOD("is_deferred"), &Settlement::is_deferred);

    ClassDB::bind_method(D_METHOD("set_layout_path", "path"), &Settlement::set_layout_path);
    ClassDB::bind_method(D_METHOD("get_layout_path"), &Settlement::get_layout_path);
    ClassDB::bind_method(D_METHOD("set_max_rise", "value"), &Settlement::set_max_rise);
    ClassDB::bind_method(D_METHOD("get_max_rise"), &Settlement::get_max_rise);

    ADD_PROPERTY(PropertyInfo(Variant::STRING, "layout_path", PROPERTY_HINT_FILE, "*.txt"), "set_layout_path", "get_layout_path");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_rise", PROPERTY_HINT_RANGE, "0.2,6,0.1,suffix:m"), "set_max_rise", "get_max_rise");

    ADD_SIGNAL(MethodInfo("changed"));
}

void Settlement::_ready() {
    add_to_group(GROUP); // 编排据此找各处的船台（落地靠港）
    host = GroundHost::of(get_parent());
    ERR_FAIL_NULL_MSG(host, "Settlement 要挂在地面宿主（Island / WorldVillage / FlatMap）下面");
    looks = host->host_looks();
    if (!looks) {
        own_looks = std::make_unique<SharedLooks>();
        looks = own_looks.get();
    }
    if (TemplateLibrary *library = TemplateLibrary::get_singleton()) {
        library->connect("changed", Callable(this, "on_templates_changed"));
    }
    if (!layout_path.is_empty()) {
        load(); // 改动叠到宿主给的底清单上（岛上底是空的）
    }
    // 子节点先于父节点 _ready：宿主此刻可能还没生成地面，等它的 rebuilt；已经生成过就直接盖
    host->host_node()->connect("rebuilt", Callable(this, "on_host_rebuilt"));
    if (host->host_ready()) {
        on_host_rebuilt();
    }
}

void Settlement::on_host_rebuilt() {
    refresh_ground();
    const std::set<std::string> keep = mounted_ids();
    clear_generated();
    remount = keep;
    build_all();
}

void Settlement::refresh_ground() {
    ground.reset();
    map = host->host_ground();
    ground = std::make_unique<MapSiteGround>(*map, rules);
}

double Settlement::get_ground_height(const Vector3 &p_global) const {
    double y = 0.0;
    if (!ready()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const Vector3 local = to_island(p_global);
    if (!map->site_height(local.x, local.z, y)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return host_transform().xform(Vector3(local.x, real_t(y), local.z)).y;
}

std::set<std::string> Settlement::mounted_ids() const {
    std::set<std::string> ids;
    for (int k = 0; k < int(built_list.size()) && k < layout.size(); k++) {
        if (built_list[size_t(k)].root) {
            ids.insert(building_id(layout.at(k)));
        }
    }
    return ids;
}

void Settlement::clear_generated() {
    built_list.clear();
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
    // 地坪：进树前后台量好的对得上就用，否则逐座量
    if (has_prepared && prepared_layout == layout && prepared_frames.size() == size_t(layout.size())) {
        frames = prepared_frames;
    } else {
        frames = layout_frames(*map, layout, rules);
    }
    has_prepared = false;
    prepared_frames.clear();
    built_list.assign(size_t(layout.size()), Built());
    layout_version++;
    for (int k = 0; k < layout.size(); k++) {
        if (!deferred || remount.count(building_id(layout.at(k)))) {
            build_one(k);
        }
    }
    remount.clear();
}

Vector3 Settlement::to_island(const Vector3 &p_global) const {
    return host_transform().affine_inverse().xform(p_global);
}

Transform3D Settlement::building_transform_global(const BuildingDef &, const Vector3 &p_global, double p_yaw, double p_floor_y) const {
    const Vector3 local = to_island(p_global);
    const Transform3D in_island(Basis(Vector3(0, 1, 0), real_t(p_yaw)), Vector3(local.x, real_t(p_floor_y), local.z));
    return host_transform() * in_island;
}

Transform3D Settlement::local_frame(int p_index) const {
    const Building &b = layout.at(p_index);
    double floor_y = 0.0;
    if (size_t(p_index) < frames.size()) {
        floor_y = frames[size_t(p_index)].floor_y;
    } else if (const BuildingDef *def = buildings::by_id(b.kind); def && ground) {
        floor_y = building_frame(*ground, *def, b, rules).floor_y;
    }
    return Transform3D(Basis(Vector3(0, 1, 0), real_t(b.yaw)), Vector3(real_t(b.x), real_t(floor_y), real_t(b.z)));
}

void Settlement::set_prepared_frames(const SettlementLayout &p_layout, const std::vector<BuildingFrame> &p_frames) {
    prepared_layout = p_layout;
    prepared_frames = p_frames;
    has_prepared = true;
}

BuildingMesh Settlement::make_mesh(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, layout.size(), BuildingMesh());
    const Building &b = layout.at(p_index);
    const BuildingDef *def = buildings::by_id(b.kind);
    ERR_FAIL_NULL_V(def, BuildingMesh());
    const TemplateLibrary *library = TemplateLibrary::get_singleton();
    const HouseTemplate tmpl = !def->house ? HouseTemplate() : (library ? library->resolve(b) : bare_template(*def));
    const BuildingFrame frame = size_t(p_index) < frames.size() ? frames[size_t(p_index)] : building_frame(*ground, *def, b, rules);
    return build_building_mesh(*map, frame, b, tmpl, p_index);
}

void Settlement::build_one(int p_index) {
    mount(to_building_arrays(make_mesh(p_index)));
}

// 一座建筑的节点：每组一个节点（组里一个合并网格，按材质分面）、每层一盏灯、梯子区域；整座一个凹多边形碰撞（聚落这个静态体的直接子节点）
void Settlement::mount(const BuildingArrays &p_arrays) {
    const BuildingMesh &m = p_arrays.mesh;
    const int k = m.index;
    if (k < 0 || k >= layout.size() || size_t(k) >= frames.size() || std::abs(frames[size_t(k)].floor_y - m.frame.floor_y) > 1e-6) {
        return; // 清单换过：这份细模是照旧清单备的
    }
    const Building &b = layout.at(k);
    const BuildingDef *def = buildings::by_id(b.kind);
    if (!def) {
        return;
    }
    if (is_mounted(k)) {
        unmount(k);
    }
    const Transform3D frame = local_frame(k);
    Node3D *root = memnew(Node3D);
    root->set_name(vformat("B%d_%s", k, String(def->id)));
    root->set_meta(GENERATED_META, true);
    root->set_meta("template", String::utf8(m.template_id.c_str()));
    root->set_transform(frame);

    const int roof_group = m.roof_group();
    std::vector<Node3D *> group_nodes;
    std::vector<Ref<ArrayMesh>> meshes(size_t(m.group_count));
    for (int g = 0; g < m.group_count; g++) {
        Node3D *group = memnew(Node3D);
        group->set_name(g == 0 ? String("Ground") : (g == roof_group ? String("Roof") : vformat("Level%d", g - 1)));
        root->add_child(group);
        group_nodes.push_back(group);
    }
    for (const BuildingArrays::Surface &s : p_arrays.surfaces) {
        Ref<ArrayMesh> &mesh = meshes[size_t(s.group)];
        if (mesh.is_null()) {
            mesh.instantiate();
        }
        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = s.vertices;
        arrays[Mesh::ARRAY_NORMAL] = s.normals;
        arrays[Mesh::ARRAY_INDEX] = s.indices;
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
        mesh->surface_set_material(mesh->get_surface_count() - 1, looks->part_material(PartMaterial(s.material)));
    }
    for (int g = 0; g < m.group_count; g++) {
        if (meshes[size_t(g)].is_valid()) {
            MeshInstance3D *instance = memnew(MeshInstance3D);
            instance->set_name("Mesh");
            instance->set_mesh(meshes[size_t(g)]);
            group_nodes[size_t(g)]->add_child(instance);
        }
    }
    // 屋里：每层一盏不投影的暖光，只照屋里那一圈；远了就淡掉（村子里几十座，远处的不点）
    for (const BuildingLamp &lamp : m.lamps) {
        OmniLight3D *light = memnew(OmniLight3D);
        light->set_name("Lamp");
        light->set_color(Color(1.0f, 0.86f, 0.66f));
        light->set_param(Light3D::PARAM_ENERGY, 0.7f);
        light->set_param(Light3D::PARAM_RANGE, real_t(lamp.range));
        light->set_shadow(false);
        light->set_enable_distance_fade(true);
        light->set_distance_fade_begin(35.0f);
        light->set_distance_fade_length(10.0f);
        light->set_position(Vector3(real_t(lamp.x), real_t(lamp.y), real_t(lamp.z)));
        group_nodes[size_t(lamp.group)]->add_child(light);
    }
    // 梯子：与船上的同一套（Player 的 Sensor 看第 8 层、认 "ladder" 元数据；区域的 +Z 指向人站的那侧）
    for (size_t z = 0; z < m.ladders.size(); z++) {
        const LadderZone &l = m.ladders[z];
        Area3D *zone = memnew(Area3D);
        zone->set_name(vformat("Ladder%d", int(z)));
        zone->set_collision_layer(LADDER_LAYER);
        zone->set_collision_mask(0);
        zone->set_monitoring(false);
        zone->set_meta("ladder", true);
        Ref<BoxShape3D> box;
        box.instantiate();
        box->set_size(Vector3(real_t(l.sx), real_t(l.sy), real_t(l.sz)));
        CollisionShape3D *zone_shape = memnew(CollisionShape3D);
        zone_shape->set_shape(box);
        zone->add_child(zone_shape);
        zone->set_transform(Transform3D(Basis(Vector3(0, 1, 0), real_t(l.yaw)), Vector3(real_t(l.x), real_t(l.y), real_t(l.z))));
        group_nodes[size_t(l.group)]->add_child(zone);
    }
    add_child(root);

    // 碰撞：整座一个凹多边形（两面都挡：墙薄，人与相机从哪边碰都算）；碰撞形状得是 StaticBody3D 的直接子节点
    CollisionShape3D *collider = nullptr;
    if (!p_arrays.collision.is_empty()) {
        Ref<ConcavePolygonShape3D> shape;
        shape.instantiate();
        shape->set_backface_collision_enabled(true);
        shape->set_faces(p_arrays.collision);
        collider = memnew(CollisionShape3D);
        collider->set_name(vformat("B%d_Shape", k));
        collider->set_shape(shape);
        collider->set_transform(frame);
        collider->set_meta(GENERATED_META, true);
        add_child(collider);
    }

    Built built;
    built.root = root;
    built.collider = collider;
    built.groups = group_nodes;
    built.template_id = m.template_id;
    built.levels = m.levels;
    built.doors = m.doors;
    built.ladders = m.ladders;
    if (size_t(k) >= built_list.size()) {
        built_list.resize(size_t(k) + 1);
    }
    built_list[size_t(k)] = built;
    apply_cutaway(k);
}

void Settlement::unmount(int p_index) {
    if (!is_mounted(p_index)) {
        return;
    }
    Built &b = built_list[size_t(p_index)];
    for (Node *node : { static_cast<Node *>(b.root), b.collider }) {
        if (node) {
            remove_child(node);
            node->queue_free();
        }
    }
    b = Built();
    if (cutaway_index == p_index) {
        cutaway_index = -1;
    }
}

int Settlement::get_mounted_count() const {
    int n = 0;
    for (const Built &b : built_list) {
        n += b.root != nullptr;
    }
    return n;
}

const Settlement::Built &Settlement::parts_of(int p_index, Built &r_scratch) const {
    if (is_mounted(p_index)) {
        return built_list[size_t(p_index)];
    }
    const BuildingMesh m = make_mesh(p_index);
    r_scratch.template_id = m.template_id;
    r_scratch.levels = m.levels;
    r_scratch.doors = m.doors;
    r_scratch.ladders = m.ladders;
    return r_scratch;
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
    const std::set<std::string> keep = mounted_ids();
    layout = settlement_from_ids(ids, &ok);
    if (!ok) {
        WARN_PRINT("Settlement: 清单里有认不出的条目，跳过了");
    }
    dirty = true;
    clear_generated();
    remount = keep;
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
    return host_transform() * local_frame(p_index);
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

String Settlement::place(const String &p_kind, const Vector3 &p_global, double p_yaw, const String &p_style) {
    const Vector3 local = to_island(p_global);
    const std::string reason = settlement_place(layout, *ground, to_std(p_kind), local.x, local.z, p_yaw, rules, to_std(p_style));
    if (!reason.empty()) {
        return String::utf8(reason.c_str());
    }
    dirty = true;
    const int k = layout.size() - 1;
    const BuildingDef *def = buildings::by_id(layout.at(k).kind);
    frames.push_back(def ? building_frame(*ground, *def, layout.at(k), rules) : BuildingFrame());
    built_list.resize(size_t(layout.size()));
    layout_version++;
    build_one(k); // 放下的这座当场挂上（人就在它跟前）
    emit_signal("changed");
    return String();
}

bool Settlement::remove(int p_index) {
    if (p_index < 0 || p_index >= layout.size()) {
        return false;
    }
    // deferred：挂着的照挂（拆掉的那座除外）
    std::set<std::string> keep = mounted_ids();
    keep.erase(building_id(layout.at(p_index)));
    layout.erase(p_index);
    dirty = true;
    clear_generated(); // 节点按序号命名，整份重画最省事
    remount = keep;
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
    LayoutEdits edits;
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
            edits = parse_layout_edits(ids, &ok);
            if (!ok) {
                WARN_PRINT(String("Settlement: ") + layout_path + String(U" 里有认不出的行，跳过了"));
            }
            read = true;
        }
    }
    int unmatched = 0;
    const std::set<std::string> keep = mounted_ids();
    layout = apply_layout_edits(base, edits, &unmatched);
    if (unmatched > 0) {
        WARN_PRINT(vformat(String(U"Settlement: %s 里要拆的 %d 座在底清单里找不到了（铺村的规矩改过？），跳过"), layout_path, unmatched));
    }
    if (ready()) {
        clear_generated();
        remount = keep;
        build_all();
    }
    emit_signal("changed");
    return read;
}

bool Settlement::save() {
    if (layout_path.is_empty()) {
        return false;
    }
    DirAccess::make_dir_recursive_absolute(layout_path.get_base_dir());
    Ref<FileAccess> file = FileAccess::open(layout_path, FileAccess::WRITE);
    if (file.is_null()) {
        ERR_PRINT(String("Settlement: 写不了 ") + layout_path);
        return false;
    }
    if (base.empty()) {
        file->store_line(String(U"# 聚落清单：一座一行 kind@x,z,yaw°（岛的局部坐标，米 / 度）。在游戏里按 B 进布置模式改，F5 存回这里。"));
    } else {
        file->store_line(String(U"# 村落改动：在自动铺村的结果上加（kind@x,z,yaw°）或拆（-kind@x,z,yaw°），村心为原点，米 / 度。在游戏里按 B 进布置模式改，F5 存回这里。"));
    }
    for (const std::string &line : layout_edit_lines(diff_layouts(base, layout))) {
        file->store_line(String::utf8(line.c_str()));
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

void Settlement::set_base(const SettlementLayout &p_base) {
    base = p_base;
    if (is_inside_tree() && host) {
        load(); // 改动重新叠一遍，重画，发 changed
    } else {
        layout = base;
    }
}

void Settlement::set_max_rise(double p_value) {
    rules.max_rise_m = p_value;
    if (ready()) {
        refresh_ground();
    }
}

// ---------------------------------------------------------------- 房屋模板与室内

String Settlement::get_building_style(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, layout.size(), String());
    return String::utf8(layout.at(p_index).style.c_str());
}

void Settlement::set_building_style(int p_index, const String &p_style) {
    ERR_FAIL_INDEX(p_index, layout.size());
    if (layout.at(p_index).style == to_std(p_style)) {
        return;
    }
    std::set<std::string> keep = mounted_ids();
    layout.set_style(p_index, to_std(p_style));
    keep.insert(building_id(layout.at(p_index))); // 换了款式的这座当场挂（在它跟前改的）
    dirty = true;
    clear_generated(); // 节点按序号命名，整份重画最省事
    remount = keep;
    build_all();
    emit_signal("changed");
}

String Settlement::get_building_template(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, layout.size(), String());
    if (is_mounted(p_index)) {
        return String::utf8(built_list[size_t(p_index)].template_id.c_str());
    }
    const TemplateLibrary *library = TemplateLibrary::get_singleton();
    return library ? String::utf8(library->resolve_id(layout.at(p_index)).c_str()) : String();
}

std::vector<int> Settlement::find_berths() const {
    std::vector<int> out;
    for (int k = 0; k < layout.size() && size_t(k) < frames.size(); k++) {
        if (layout.at(k).kind == "berth") {
            out.push_back(k);
        }
    }
    return out;
}

Transform3D Settlement::get_berth_contact(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(frames.size()), Transform3D());
    // 船台的局部：X 指向跳板（+z），原点在跳板顶那条甲板边缘线上
    const Basis toward_gangway(Vector3(0, 0, 1), Vector3(0, 1, 0), Vector3(-1, 0, 0));
    return host_transform() * local_frame(p_index) * Transform3D(toward_gangway, Vector3(0, real_t(land_berth::LIFT_M), real_t(land_berth::CONTACT_Z)));
}

Vector3 Settlement::get_berth_point(int p_index, double p_x, double p_z) const {
    ERR_FAIL_INDEX_V(p_index, int(frames.size()), Vector3());
    return (host_transform() * local_frame(p_index)).xform(Vector3(real_t(p_x), 0, real_t(p_z)));
}

PackedVector3Array Settlement::get_building_entry(int p_index) const {
    PackedVector3Array out;
    ERR_FAIL_INDEX_V(p_index, layout.size(), out);
    Built scratch;
    const Built &b = parts_of(p_index, scratch);
    const Transform3D xf = host_transform() * local_frame(p_index);
    for (int pass = 0; pass < 2 && out.is_empty(); pass++) {
        for (const DoorEntry &door : b.doors) {
            if (pass == 1 || door.side == WallSide::FRONT) {
                out.push_back(xf.xform(Vector3(real_t(door.out_x), real_t(door.out_y), real_t(door.out_z))));
                out.push_back(xf.xform(Vector3(real_t(door.in_x), real_t(door.in_y), real_t(door.in_z))));
                break;
            }
        }
    }
    return out;
}

PackedVector3Array Settlement::get_building_ladders(int p_index) const {
    PackedVector3Array out;
    ERR_FAIL_INDEX_V(p_index, layout.size(), out);
    Built scratch;
    const Built &b = parts_of(p_index, scratch);
    const Transform3D xf = host_transform() * local_frame(p_index);
    for (const LadderZone &z : b.ladders) {
        out.push_back(xf.xform(Vector3(real_t(z.foot_x), real_t(z.foot_y), real_t(z.foot_z))));
        out.push_back(xf.xform(Vector3(real_t(z.top_x), real_t(z.top_y), real_t(z.top_z))));
    }
    return out;
}

void Settlement::set_cutaway(int p_index, int p_level) {
    const int old = cutaway_index;
    cutaway_index = p_index;
    cutaway_level = p_level;
    for (int k : { old, p_index }) {
        if (is_mounted(k)) {
            apply_cutaway(k);
        }
    }
}

// 剖开看：被编辑的那座藏起顶与编辑层以上各层（碰撞不动，只是看不见）
void Settlement::apply_cutaway(int p_index) {
    const Built &b = built_list[size_t(p_index)];
    const bool cut = p_index == cutaway_index;
    for (size_t g = 0; g < b.groups.size(); g++) {
        const int level = int(g) - 1; // 0 组是地面上的；最后一组是顶
        const bool roof = g + 1 == b.groups.size();
        b.groups[g]->set_visible(!cut || (!roof && level <= cutaway_level));
    }
}

// 模板库改了：用到这套的、或者自动挑的结果变了的，挂着的就地重画（一村几十座，改一下家具不必整村重来；没挂的等挂的时候照新模板拼）
void Settlement::on_templates_changed(const String &p_id) {
    const TemplateLibrary *library = TemplateLibrary::get_singleton();
    if (!ready() || !library) {
        return;
    }
    const std::string id = to_std(p_id);
    for (int k = 0; k < layout.size() && size_t(k) < built_list.size(); k++) {
        if (!is_mounted(k)) {
            continue;
        }
        const std::string used = built_list[size_t(k)].template_id;
        if (used == id || library->resolve_id(layout.at(k)) != used) {
            rebuild_one(k);
        }
    }
}

void Settlement::rebuild_one(int p_index) {
    unmount(p_index);
    build_one(p_index);
}

PackedInt32Array Settlement::get_berth_indices() const {
    PackedInt32Array out;
    for (int k : find_berths()) {
        out.push_back(k);
    }
    return out;
}
