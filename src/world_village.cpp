#include "world_village.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/label3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "dock_builder.h"
#include "freight.h"
#include "land_berth.h"
#include "hinterland.h"
#include "template_library.h"

using namespace godot;

void convert_proxies(VillagePrepared &r_prepared) {
    r_prepared.proxies.clear();
    for (VillageCell &cell : r_prepared.prep.cells) {
        VillagePrepared::Proxy p;
        const MeshPart &m = cell.proxy;
        const int64_t n = int64_t(m.positions.size() / 3);
        p.vertices.resize(n);
        p.normals.resize(n);
        p.colors.resize(n);
        Vector3 *v = p.vertices.ptrw();
        Vector3 *nn = p.normals.ptrw();
        Color *c = p.colors.ptrw();
        for (int64_t k = 0; k < n; k++) {
            v[k] = Vector3(m.positions[size_t(k) * 3], m.positions[size_t(k) * 3 + 1], m.positions[size_t(k) * 3 + 2]);
            nn[k] = Vector3(m.normals[size_t(k) * 3], m.normals[size_t(k) * 3 + 1], m.normals[size_t(k) * 3 + 2]);
            c[k] = Color(m.colors[size_t(k) * 4], m.colors[size_t(k) * 4 + 1], m.colors[size_t(k) * 4 + 2], m.colors[size_t(k) * 4 + 3]);
        }
        p.indices.resize(int64_t(m.indices.size()));
        std::copy(m.indices.begin(), m.indices.end(), p.indices.ptrw());
        r_prepared.proxies.push_back(std::move(p));
        cell.proxy = MeshPart();
    }
}

void WorldVillage::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_site_id"), &WorldVillage::get_site_id);
    ClassDB::bind_method(D_METHOD("is_hamlet"), &WorldVillage::is_hamlet);
    ClassDB::bind_method(D_METHOD("get_site_name"), &WorldVillage::get_site_name);
    ClassDB::bind_method(D_METHOD("get_radius"), &WorldVillage::get_radius);
    ClassDB::bind_method(D_METHOD("get_missing"), &WorldVillage::get_missing);
    ClassDB::bind_method(D_METHOD("has_folk"), &WorldVillage::has_folk);
    ClassDB::bind_method(D_METHOD("get_settlement"), &WorldVillage::get_settlement);
    ClassDB::bind_method(D_METHOD("get_villagers"), &WorldVillage::get_villagers);
    ClassDB::bind_method(D_METHOD("is_edited"), &WorldVillage::is_edited);
    ClassDB::bind_method(D_METHOD("get_cell_count"), &WorldVillage::get_cell_count);
    ClassDB::bind_method(D_METHOD("get_detailed_cell_count"), &WorldVillage::get_detailed_cell_count);
    ClassDB::bind_method(D_METHOD("on_layout_changed"), &WorldVillage::on_layout_changed);
    ADD_SIGNAL(MethodInfo("rebuilt"));
}

void WorldVillage::setup(World *p_world, const SiteSettlement &p_site, uint64_t p_seed, const String &p_layout_path, Hinterland *p_hinterland, int p_index,
        std::shared_ptr<VillagePrepared> p_prepared) {
    world = p_world;
    hinterland = p_hinterland;
    site_index = p_index;
    site = p_site;
    seed = p_seed;
    layout_path = p_layout_path;
    prepared = std::move(p_prepared);
    set_name(vformat("%s%d", site.hamlet ? "Hamlet" : "Village", site.id));
    set_position(Vector3(real_t(site.x), 0, real_t(site.z)));
}

void WorldVillage::_ready() {
    ERR_FAIL_NULL_MSG(world, "WorldVillage 要先 setup 再进树");
    if (!prepared) {
        // 没有后台备好的料：当场备（与后台同一份，布置模式的改动由 Settlement 自己读）
        prepared = std::make_shared<VillagePrepared>();
        prepared->site_index = site_index;
        VillagePrepRules rules;
        rules.cell_m = CELL_M;
        prepared->prep = prepare_world_village(world->get_terrain(), site, seed, {}, rules);
        convert_proxies(*prepared);
    }
    plan = prepared->prep.plan;
    planned = true;
    settlement = memnew(Settlement);
    settlement->set_name("Settlement");
    settlement->set_deferred(true); // 细模按格子由 SiteStream 挂
    settlement->set_prepared_frames(prepared->prep.layout, prepared->prep.frames);
    settlement->set_layout_path(layout_path);
    settlement->set_base(plan.layout);
    add_child(settlement); // 宿主已经好了：Settlement 进树就叠上改动、量好地坪（对得上就用备好的）
    proxy_root = memnew(Node3D);
    proxy_root->set_name("Proxy");
    add_child(proxy_root);
    if (settlement->get_model() == prepared->prep.layout) {
        cells_version = settlement->get_layout_version();
        build_cells(prepared->prep.cells, &prepared->proxies);
    } else {
        on_layout_changed(); // 改动文件与后台读的时候不一样了：按聚落此刻的清单重分
    }
    prepared.reset(); // 简模挂上了，料不留
    settlement->connect("changed", Callable(this, "on_layout_changed"));
    if (world->find_freight_port(site_index) >= 0) {
        build_freight_post(); // 村（不是散户）：船台跳板脚边就是村仓
    }
}

MeshInstance3D *WorldVillage::make_proxy(const VillagePrepared::Proxy &p_proxy, const VillageCell &p_cell) {
    if (p_proxy.indices.is_empty()) {
        return nullptr;
    }
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = p_proxy.vertices;
    arrays[Mesh::ARRAY_NORMAL] = p_proxy.normals;
    arrays[Mesh::ARRAY_COLOR] = p_proxy.colors;
    arrays[Mesh::ARRAY_INDEX] = p_proxy.indices;
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    mesh->surface_set_material(0, world->get_looks().proxy_material());
    MeshInstance3D *view = memnew(MeshInstance3D);
    view->set_name(vformat("Cell_%d_%d", p_cell.ix, p_cell.iz));
    view->set_mesh(mesh);
    view->set_position(Vector3(real_t(p_cell.x0), 0, real_t(p_cell.z0)));
    proxy_root->add_child(view);
    return view;
}

void WorldVillage::build_cells(const std::vector<VillageCell> &p_cells, const std::vector<VillagePrepared::Proxy> *p_proxies) {
    for (Cell &cell : cells) {
        if (cell.proxy) {
            cell.proxy->queue_free();
        }
    }
    cells.clear();
    for (size_t k = 0; k < p_cells.size(); k++) {
        Cell cell;
        cell.data = p_cells[k];
        cell.data.proxy = MeshPart();
        cell.layout_version = cells_version;
        if (p_proxies && k < p_proxies->size()) {
            cell.proxy = make_proxy((*p_proxies)[k], cell.data);
        }
        cells.push_back(std::move(cell));
    }
}

// 布置模式改了清单：按聚落此刻的清单与地坪重分格子、重画简模（挂着的细模 Settlement 已经照新清单重挂了）；
// 格子都回到只有简模，近处的由 SiteStream 重新备细模（挂上时替掉同一座）
void WorldVillage::on_layout_changed() {
    if (!settlement || settlement->get_layout_version() == cells_version) {
        return;
    }
    cells_version = settlement->get_layout_version();
    VillagePrepared fresh;
    fresh.prep.cells = village_cells(settlement->get_model(), settlement->get_frames(), CELL_M);
    convert_proxies(fresh);
    build_cells(fresh.prep.cells, &fresh.proxies);
}

double WorldVillage::cell_distance_to(int p_cell, const Vector3 &p_global) const {
    return cell_distance(cells[size_t(p_cell)].data, p_global.x - site.x, p_global.z - site.z);
}

void WorldVillage::cancel_preparing(int p_cell) {
    Cell &cell = cells[size_t(p_cell)];
    if (cell.state == CELL_PREPARING) {
        cell.state = CELL_PROXY;
    }
}

void WorldVillage::accept_cell(CellPrepared &&p_cell) {
    if (p_cell.cell < 0 || p_cell.cell >= int(cells.size()) || p_cell.layout_version != cells_version) {
        return; // 清单换过：这份是照旧清单备的
    }
    Cell &cell = cells[size_t(p_cell.cell)];
    if (cell.state != CELL_PREPARING) {
        return; // 备的时候走远了
    }
    cell.pending = std::move(p_cell.buildings);
    cell.state = CELL_MOUNTING;
}

void WorldVillage::release_cell(int p_cell) {
    Cell &cell = cells[size_t(p_cell)];
    if (cell.state == CELL_PROXY || cell.state == CELL_UNMOUNTING) {
        return;
    }
    cell.pending.clear();
    if (cell.proxy) {
        cell.proxy->set_visible(true);
    }
    cell.state = CELL_PREPARING == cell.state ? CELL_PROXY : CELL_UNMOUNTING;
}

bool WorldVillage::step_mount(uint64_t p_deadline_usec, const Vector3 &p_focus) {
    Time *time = Time::get_singleton();
    bool more = false;
    const double fx = p_focus.x - site.x, fz = p_focus.z - site.z;
    // 近的格子先
    std::vector<std::pair<double, int>> order;
    for (int k = 0; k < int(cells.size()); k++) {
        if (cells[size_t(k)].state == CELL_MOUNTING || cells[size_t(k)].state == CELL_UNMOUNTING) {
            order.emplace_back(cell_distance(cells[size_t(k)].data, fx, fz), k);
        }
    }
    std::sort(order.begin(), order.end());
    for (const auto &[d, k] : order) {
        Cell &cell = cells[size_t(k)];
        if (cell.state == CELL_MOUNTING) {
            // 格里近的那座先挂
            std::sort(cell.pending.begin(), cell.pending.end(), [&](const BuildingArrays &a, const BuildingArrays &b) {
                const Building &ba = settlement->get_model().at(a.mesh.index), &bb = settlement->get_model().at(b.mesh.index);
                return std::hypot(ba.x - fx, ba.z - fz) > std::hypot(bb.x - fx, bb.z - fz); // 从尾巴上取
            });
            while (!cell.pending.empty() && time->get_ticks_usec() < p_deadline_usec) {
                settlement->mount(cell.pending.back());
                cell.pending.pop_back();
            }
            if (cell.pending.empty()) {
                if (cell.proxy) {
                    cell.proxy->set_visible(false);
                }
                cell.state = CELL_DETAILED;
            } else {
                more = true;
            }
        } else {
            size_t done = 0;
            for (int b : cell.data.buildings) {
                if (time->get_ticks_usec() >= p_deadline_usec) {
                    break;
                }
                settlement->unmount(b);
                done++;
            }
            if (done == cell.data.buildings.size()) {
                cell.state = CELL_PROXY;
            } else {
                more = true;
            }
        }
        if (time->get_ticks_usec() >= p_deadline_usec) {
            return more || d >= 0.0;
        }
    }
    return more;
}

void WorldVillage::mount_cell_now(int p_cell) {
    Cell &cell = cells[size_t(p_cell)];
    if (cell.state == CELL_DETAILED) {
        return;
    }
    const std::set<int> have = [&]() {
        std::set<int> s;
        for (const BuildingArrays &a : cell.pending) {
            s.insert(a.mesh.index);
        }
        return s;
    }();
    for (const BuildingArrays &a : cell.pending) {
        settlement->mount(a);
    }
    for (int b : cell.data.buildings) {
        if (!have.count(b) && !settlement->is_mounted(b)) {
            settlement->mount(to_building_arrays(settlement->make_mesh(b)));
        }
    }
    cell.pending.clear();
    if (cell.proxy) {
        cell.proxy->set_visible(false);
    }
    cell.state = CELL_DETAILED;
}

int WorldVillage::get_detailed_cell_count() const {
    int n = 0;
    for (const Cell &cell : cells) {
        n += cell.state == CELL_DETAILED;
    }
    return n;
}

namespace {

// 共用的一块货箱 / 杆子：同尺寸同色的共用一份网格
Ref<Mesh> shared_box(SharedLooks &p_looks, const Vector3 &p_size, const Color &p_color) {
    const std::string key = std::string("freight_box_") + String::num(p_size.x, 3).utf8().get_data() + "_" + String::num(p_size.y, 3).utf8().get_data() + "_" +
            String::num(p_size.z, 3).utf8().get_data() + "_" + p_color.to_html(false).utf8().get_data();
    return p_looks.mesh(key, [&]() {
        Ref<BoxMesh> mesh;
        mesh.instantiate();
        mesh->set_size(p_size);
        mesh->set_material(p_looks.plain_material(p_color, 0.95f));
        return Ref<Mesh>(mesh);
    });
}

void add_block(Node3D *p_parent, const String &p_name, const Ref<Mesh> &p_mesh, const Vector3 &p_at) {
    MeshInstance3D *instance = memnew(MeshInstance3D);
    instance->set_name(p_name);
    instance->set_mesh(p_mesh);
    instance->set_position(p_at);
    p_parent->add_child(instance);
}

} // namespace

// 村仓：船台跳板脚边三只货箱（箱盖是这村土产的颜色）+ 一根木杆挂牌子。只是摆设，不挡人
void WorldVillage::build_freight_post() {
    const int k = freight_post_building(settlement->get_model());
    if (k < 0) {
        return;
    }
    freight_post = memnew(Node3D);
    freight_post->set_name("FreightPost");
    add_child(freight_post);
    freight_post->set_global_transform(settlement->get_building_transform(k) * Transform3D(Basis(), Vector3(real_t(land_berth::PILE_X), 0, real_t(land_berth::PILE_Z))));
    float r = 0, g = 0, b = 0;
    freight::crate_color(freight::specialty_of(seed, site.id, site.hamlet), r, g, b);
    SharedLooks &looks = world->get_looks();
    const Ref<Mesh> crate = shared_box(looks, Vector3(1.4f, 1.1f, 1.4f), Color(0.58f, 0.44f, 0.26f));
    const Ref<Mesh> lid = shared_box(looks, Vector3(1.2f, 0.05f, 1.2f), Color(r, g, b));
    const Vector3 crates[3] = { Vector3(-0.8f, 0.0f, 0.0f), Vector3(0.8f, 0.0f, 0.1f), Vector3(-0.1f, 1.1f, 0.05f) };
    for (int c = 0; c < 3; c++) {
        add_block(freight_post, vformat("Crate%d", c), crate, crates[c] + Vector3(0, 0.55f, 0));
        add_block(freight_post, vformat("Lid%d", c), lid, crates[c] + Vector3(0, 1.125f, 0));
    }
    add_block(freight_post, "Pole", shared_box(looks, Vector3(0.14f, 3.2f, 0.14f), Color(0.33f, 0.22f, 0.15f)), Vector3(1.9f, 1.6f, 0.0f));
    add_block(freight_post, "Board", shared_box(looks, Vector3(1.3f, 0.5f, 0.06f), Color(0.8f, 0.7f, 0.5f)), Vector3(1.9f, 2.7f, 0.1f));
    Label3D *label = memnew(Label3D);
    label->set_name("Sign");
    label->set_text(String(U"村仓 · 托运"));
    label->set_position(Vector3(1.9f, 3.45f, 0.0f));
    label->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
    label->set_font_size(48);
    label->set_outline_size(12);
    label->set_pixel_size(0.008f);
    label->set_visibility_range_end(80.0f);
    freight_post->add_child(label);
}

bool WorldVillage::get_freight_post(Vector3 &r_global) const {
    if (!freight_post || !settlement) {
        return false;
    }
    const int k = freight_post_building(settlement->get_model());
    if (k < 0) {
        return false;
    }
    r_global = settlement->get_berth_point(k, 0.0, land_berth::POST_Z);
    return true;
}

void WorldVillage::set_folk(bool p_on, int p_per_house, int p_max_folk, std::shared_ptr<VillageNav> p_nav, bool p_progressive) {
    if (p_on == has_folk() || !settlement) {
        return;
    }
    if (!p_on) {
        if (hinterland) {
            hinterland->set_layout(site_index, settlement->get_model()); // 布置模式改过：远处照新清单记
        }
        villagers->queue_free(); // 离树时把账收回去（地块从农田里拿出来记进账本）
        villagers = nullptr;
        return;
    }
    villagers = memnew(Villagers);
    villagers->set_name("Villagers");
    villagers->set_settlement_path(NodePath("../Settlement"));
    villagers->set_per_house(p_per_house);
    villagers->set_max_folk(p_max_folk);
    villagers->set_seed(int64_t(host_seed() & 0x7FFFFFFF));
    villagers->set_progressive(p_progressive);
    if (p_nav) {
        villagers->set_prepared_nav(p_nav, layout_signature(settlement->get_model()));
    }
    if (hinterland && site_index >= 0) {
        villagers->set_ledger(hinterland->checkout(site_index, settlement->get_model()), hinterland->get_registry());
    }
    add_child(villagers); // 排在 Settlement 后面
}

std::unique_ptr<MapGround> WorldVillage::host_ground() const {
    return std::make_unique<WorldGround>(world->get_terrain(), site.x, site.z);
}

void WorldVillage::host_nav_window(const SettlementLayout &p_layout, const NavRules &p_rules, double &r_x0, double &r_z0, int &r_cols, int &r_rows) const {
    village_nav_window(p_layout, p_rules, NAV_MARGIN_M, r_x0, r_z0, r_cols, r_rows);
}

void WorldDock::setup(const DockPlan &p_plan, int p_index, int p_id) {
    index = p_index;
    dock_id = p_id;
    set_name(vformat("Dock%d", p_id));
    set_position(Vector3(real_t(p_plan.x), 0, real_t(p_plan.z)));
    DockPlan local = p_plan; // 以岸缘点为原点
    local.x = 0.0;
    local.z = 0.0;
    dock_builder::build_pier(this, local, [](Node3D *p_parent, Node *p_node, const char *p_name) {
        p_node->set_name(p_name);
        p_parent->add_child(p_node);
    });
}
