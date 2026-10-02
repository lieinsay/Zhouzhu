#include "site_stream.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "hinterland.h"
#include "dock_builder.h"
#include "template_library.h"

using namespace godot;

namespace {

// 后台备好的一村导航格
struct NavPrepared : JobResult {
    int site_index = -1;
    uint64_t layout_sig = 0;
    std::shared_ptr<VillageNav> nav;
};

std::string to_std(const String &p_string) {
    return std::string(p_string.utf8().get_data());
}

uint64_t now_usec() {
    return Time::get_singleton()->get_ticks_usec();
}

} // namespace

void SiteStream::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_focus", "focus"), &SiteStream::set_focus);
    ClassDB::bind_method(D_METHOD("ensure_near", "point"), &SiteStream::ensure_near);
    ClassDB::bind_method(D_METHOD("get_built_docks"), &SiteStream::get_built_docks);
    ClassDB::bind_method(D_METHOD("get_built_villages"), &SiteStream::get_built_villages);
    ClassDB::bind_method(D_METHOD("get_built_hamlet_count"), &SiteStream::get_built_hamlet_count);
    ClassDB::bind_method(D_METHOD("get_prepared_count"), &SiteStream::get_prepared_count);
    ClassDB::bind_method(D_METHOD("get_detailed_cell_count"), &SiteStream::get_detailed_cell_count);
    ClassDB::bind_method(D_METHOD("get_village", "village_id"), &SiteStream::get_village);
    ClassDB::bind_method(D_METHOD("get_hamlet", "hamlet_id"), &SiteStream::get_hamlet);
    ClassDB::bind_method(D_METHOD("get_dock", "dock_id"), &SiteStream::get_dock);
    ClassDB::bind_method(D_METHOD("get_built_resources"), &SiteStream::get_built_resources);
    ClassDB::bind_method(D_METHOD("get_resource", "working_id"), &SiteStream::get_resource);
    ClassDB::bind_method(D_METHOD("get_built_landmarks"), &SiteStream::get_built_landmarks);
    ClassDB::bind_method(D_METHOD("get_village_version"), &SiteStream::get_village_version);
    ClassDB::bind_method(D_METHOD("set_resource_radius", "value"), &SiteStream::set_resource_radius);
    ClassDB::bind_method(D_METHOD("get_resource_radius"), &SiteStream::get_resource_radius);
    ClassDB::bind_method(D_METHOD("get_focus"), &SiteStream::get_focus);
    ClassDB::bind_method(D_METHOD("get_hinterland"), &SiteStream::get_hinterland);
    ClassDB::bind_method(D_METHOD("set_hinterland_path", "value"), &SiteStream::set_hinterland_path);
    ClassDB::bind_method(D_METHOD("get_hinterland_path"), &SiteStream::get_hinterland_path);

    ClassDB::bind_method(D_METHOD("set_world_path", "value"), &SiteStream::set_world_path);
    ClassDB::bind_method(D_METHOD("get_world_path"), &SiteStream::get_world_path);
    ClassDB::bind_method(D_METHOD("set_dock_radius", "value"), &SiteStream::set_dock_radius);
    ClassDB::bind_method(D_METHOD("get_dock_radius"), &SiteStream::get_dock_radius);
    ClassDB::bind_method(D_METHOD("set_village_radius", "value"), &SiteStream::set_village_radius);
    ClassDB::bind_method(D_METHOD("get_village_radius"), &SiteStream::get_village_radius);
    ClassDB::bind_method(D_METHOD("set_detail_radius", "value"), &SiteStream::set_detail_radius);
    ClassDB::bind_method(D_METHOD("get_detail_radius"), &SiteStream::get_detail_radius);
    ClassDB::bind_method(D_METHOD("set_folk_radius", "value"), &SiteStream::set_folk_radius);
    ClassDB::bind_method(D_METHOD("get_folk_radius"), &SiteStream::get_folk_radius);
    ClassDB::bind_method(D_METHOD("set_per_house", "value"), &SiteStream::set_per_house);
    ClassDB::bind_method(D_METHOD("get_per_house"), &SiteStream::get_per_house);
    ClassDB::bind_method(D_METHOD("set_max_folk", "value"), &SiteStream::set_max_folk);
    ClassDB::bind_method(D_METHOD("get_max_folk"), &SiteStream::get_max_folk);
    ClassDB::bind_method(D_METHOD("set_seed", "value"), &SiteStream::set_seed);
    ClassDB::bind_method(D_METHOD("get_seed"), &SiteStream::get_seed);
    ClassDB::bind_method(D_METHOD("set_mount_budget_usec", "value"), &SiteStream::set_mount_budget_usec);
    ClassDB::bind_method(D_METHOD("get_mount_budget_usec"), &SiteStream::get_mount_budget_usec);

    ClassDB::bind_method(D_METHOD("set_layout_dir", "value"), &SiteStream::set_layout_dir);
    ClassDB::bind_method(D_METHOD("get_layout_dir"), &SiteStream::get_layout_dir);
    ClassDB::bind_method(D_METHOD("village_at", "point", "margin"), &SiteStream::village_at, DEFVAL(40.0));
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "world_path"), "set_world_path", "get_world_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "hinterland_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Hinterland"), "set_hinterland_path", "get_hinterland_path");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "layout_dir", PROPERTY_HINT_DIR), "set_layout_dir", "get_layout_dir");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dock_radius", PROPERTY_HINT_RANGE, "200,20000,10,suffix:m"), "set_dock_radius", "get_dock_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "village_radius", PROPERTY_HINT_RANGE, "200,20000,10,suffix:m"), "set_village_radius", "get_village_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "detail_radius", PROPERTY_HINT_RANGE, "50,2000,10,suffix:m"), "set_detail_radius", "get_detail_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "folk_radius", PROPERTY_HINT_RANGE, "0,5000,10,suffix:m"), "set_folk_radius", "get_folk_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "resource_radius", PROPERTY_HINT_RANGE, "100,10000,10,suffix:m"), "set_resource_radius", "get_resource_radius");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "per_house", PROPERTY_HINT_RANGE, "0,6,1"), "set_per_house", "get_per_house");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "max_folk", PROPERTY_HINT_RANGE, "0,500,1"), "set_max_folk", "get_max_folk");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "mount_budget_usec", PROPERTY_HINT_RANGE, "500,20000,100,suffix:µs"), "set_mount_budget_usec", "get_mount_budget_usec");
}

void SiteStream::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        return;
    }
    world = Object::cast_to<GroupWorld>(get_node_or_null(world_path));
    ERR_FAIL_NULL_MSG(world, "SiteStream 找不到 World（world_path）");
    world_id = world->get_instance_id();
    hinterland = Object::cast_to<Hinterland>(get_node_or_null(hinterland_path));
}

void SiteStream::_notification(int p_what) {
    if (p_what != NOTIFICATION_EXIT_TREE) {
        return;
    }
    // 拆了（GroupStream 把这一群降到近景、换场景）：自己在共用池里排着的活撤掉、做完没取的丢掉（岛群还在，别人还用这个池）
    GroupWorld *w = Object::cast_to<GroupWorld>(ObjectDB::get_instance(world_id));
    if (!w || !w->share_pool()) {
        return;
    }
    JobPool &pool = w->get_pool();
    for (JobKind kind : { JobKind::VILLAGE, JobKind::CELL, JobKind::NAV }) {
        pool.cancel_kind(kind, w->get_lane());
        std::vector<JobPool::Done> done;
        pool.collect(kind, done, w->get_lane());
    }
}

void SiteStream::set_focus(const Vector3 &p_focus) {
    focus = p_focus;
    has_focus = true;
}

void SiteStream::_process(double p_delta) {
    since_check += p_delta;
    if (!world || !world->is_loaded() || !has_focus) {
        return;
    }
    if (since_check >= check_interval) {
        since_check = 0.0;
        refresh(1);
    }
    collect_jobs();
    step(now_usec() + uint64_t(mount_budget_usec));
}

double SiteStream::edge_distance(int p_index) const {
    const SiteSettlement &s = world->get_sites().settlements[size_t(p_index)];
    const auto built = villages.find(p_index);
    const double radius = built != villages.end() ? built->second->get_radius() : settlement_guess_radius(s);
    return std::max(0.0, std::hypot(s.x - focus.x, s.z - focus.z) - radius);
}

String SiteStream::layout_path_of(const SiteSettlement &p_site) const {
    const String file = vformat("%s_%d.txt", p_site.hamlet ? "hamlet" : "village", p_site.id);
    return layout_dir.is_empty() ? String() : layout_dir.path_join(file);
}

std::vector<std::string> SiteStream::read_edits(const SiteSettlement &p_site) const {
    // 布置模式存下的改动：与 WorldVillage 的 Settlement 读的是同一个文件（后台不碰文件系统，这里先读好）
    std::vector<std::string> lines;
    const String path = layout_path_of(p_site);
    if (!path.is_empty() && FileAccess::file_exists(path)) {
        const PackedStringArray text = FileAccess::get_file_as_string(path).split("\n");
        for (int64_t k = 0; k < text.size(); k++) {
            lines.push_back(to_std(text[k]));
        }
    }
    return lines;
}

JobPool::Work SiteStream::village_job(int p_index) const {
    const WorldTerrain *terrain = &world->get_terrain();
    const SiteSettlement site = world->get_sites().settlements[size_t(p_index)];
    const std::vector<std::string> edits = read_edits(site);
    const uint64_t stream_seed = uint64_t(seed);
    return [terrain, site, edits, stream_seed, p_index]() -> std::unique_ptr<JobResult> {
        std::unique_ptr<VillagePrepared> out = std::make_unique<VillagePrepared>();
        out->site_index = p_index;
        VillagePrepRules rules;
        rules.cell_m = WorldVillage::CELL_M;
        out->prep = prepare_world_village(*terrain, site, stream_seed, edits, rules);
        convert_proxies(*out);
        return out;
    };
}

std::shared_ptr<VillagePrepared> SiteStream::prepare_now(int p_index) {
    const auto found = prepared.find(p_index);
    if (found != prepared.end()) {
        std::shared_ptr<VillagePrepared> out = found->second;
        prepared.erase(found);
        return out;
    }
    std::unique_ptr<JobResult> done = world->get_pool().run_now(world->job_key(JobKind::VILLAGE, uint64_t(p_index)));
    if (!done) {
        done = village_job(p_index)();
    }
    return std::shared_ptr<VillagePrepared>(static_cast<VillagePrepared *>(done.release()));
}

JobPool::Work SiteStream::cell_job(WorldVillage *p_village, int p_cell) const {
    const WorldTerrain *terrain = &world->get_terrain();
    const SiteSettlement site = p_village->get_site();
    const Settlement *settlement = p_village->get_settlement();
    const VillageCell &cell = p_village->get_cell(p_cell);
    struct Item {
        int index;
        Building building;
        BuildingFrame frame;
    };
    std::vector<Item> items;
    for (int k : cell.buildings) {
        if (k < settlement->get_count() && size_t(k) < settlement->get_frames().size()) {
            items.push_back({ k, settlement->get_model().at(k), settlement->get_frames()[size_t(k)] });
        }
    }
    const TemplateLibrary *library = TemplateLibrary::get_singleton();
    const std::shared_ptr<const TemplateSet> templates = library ? library->snapshot() : nullptr;
    const int64_t version = p_village->get_cells_version();
    const int site_index = p_village->get_site_index();
    return [terrain, site, items, templates, version, site_index, p_cell]() -> std::unique_ptr<JobResult> {
        std::unique_ptr<CellPrepared> out = std::make_unique<CellPrepared>();
        out->site_index = site_index;
        out->cell = p_cell;
        out->layout_version = version;
        const WorldGround ground(*terrain, site.x, site.z);
        for (const Item &item : items) {
            const BuildingDef *def = buildings::by_id(item.building.kind);
            if (!def) {
                continue;
            }
            const HouseTemplate tmpl = !def->house ? HouseTemplate() : (templates ? templates->resolve(item.building) : bare_template(*def));
            out->buildings.push_back(to_building_arrays(build_building_mesh(ground, item.frame, item.building, tmpl, item.index)));
        }
        return out;
    };
}

JobPool::Work SiteStream::nav_job(WorldVillage *p_village) const {
    const WorldTerrain *terrain = &world->get_terrain();
    const SiteSettlement site = p_village->get_site();
    const SettlementLayout layout = p_village->get_settlement()->get_model();
    const int site_index = p_village->get_site_index();
    return [terrain, site, layout, site_index]() -> std::unique_ptr<JobResult> {
        std::unique_ptr<NavPrepared> out = std::make_unique<NavPrepared>();
        out->site_index = site_index;
        out->layout_sig = layout_signature(layout);
        const WorldGround ground(*terrain, site.x, site.z);
        const SimRules rules;
        double x0 = 0.0, z0 = 0.0;
        int cols = 0, rows = 0;
        village_nav_window(layout, rules.nav, WorldVillage::NAV_MARGIN_M, x0, z0, cols, rows);
        out->nav = std::make_shared<VillageNav>(prepare_village_nav(ground, layout, rules, x0, z0, cols, rows));
        return out;
    };
}

WorldVillage *SiteStream::build_village(int p_index, std::shared_ptr<VillagePrepared> p_prepared) {
    WorldVillage *village = memnew(WorldVillage);
    const SiteSettlement &site = world->get_sites().settlements[size_t(p_index)];
    village->setup(world, site, uint64_t(seed), layout_path_of(site), hinterland, p_index, std::move(p_prepared));
    add_child(village);
    villages[p_index] = village;
    village_version++;
    return village;
}

void SiteStream::collect_jobs() {
    JobPool &pool = world->get_pool();
    std::vector<JobPool::Done> done;
    pool.collect(JobKind::VILLAGE, done, world->get_lane());
    for (JobPool::Done &d : done) {
        VillagePrepared *p = static_cast<VillagePrepared *>(d.result.release());
        if (!villages.count(p->site_index)) {
            prepared[p->site_index] = std::shared_ptr<VillagePrepared>(p);
        } else {
            delete p;
        }
    }
    done.clear();
    pool.collect(JobKind::CELL, done, world->get_lane());
    for (JobPool::Done &d : done) {
        CellPrepared *c = static_cast<CellPrepared *>(d.result.get());
        const auto found = villages.find(c->site_index);
        if (found != villages.end()) {
            found->second->accept_cell(std::move(*c));
        }
    }
    done.clear();
    pool.collect(JobKind::NAV, done, world->get_lane());
    for (JobPool::Done &d : done) {
        NavPrepared *n = static_cast<NavPrepared *>(d.result.get());
        if (villages.count(n->site_index)) {
            navs[n->site_index] = NavReady{ n->nav, n->layout_sig };
        }
    }
}

void SiteStream::refresh_cells(WorldVillage *p_village) {
    JobPool &pool = world->get_pool();
    const int index = p_village->get_site_index();
    for (int c = 0; c < p_village->get_cell_count(); c++) {
        const double d = p_village->cell_distance_to(c, focus);
        const WorldVillage::CellState state = p_village->get_cell_state(c);
        const uint64_t key = world->job_key(JobKind::CELL, cell_id(index, c));
        if (state == WorldVillage::CELL_PROXY && d <= detail_radius) {
            if (pool.submit(key, d, cell_job(p_village, c))) {
                p_village->mark_preparing(c);
            }
        } else if (state == WorldVillage::CELL_PREPARING && d > detail_radius * hysteresis) {
            pool.cancel(key);
            p_village->cancel_preparing(c);
        } else if ((state == WorldVillage::CELL_MOUNTING || state == WorldVillage::CELL_DETAILED) && d > detail_radius * hysteresis) {
            p_village->release_cell(c);
        }
    }
}

void SiteStream::refresh_folk(int p_index, WorldVillage *p_village, double p_distance) {
    if (p_village->has_folk()) {
        if (p_distance > folk_radius * hysteresis) {
            StreamTimer timer(world, StreamChannel::FOLK);
            p_village->set_folk(false, per_house, max_folk);
            navs.erase(p_index);
        }
        return;
    }
    if (p_distance > folk_radius) {
        navs.erase(p_index);
        return;
    }
    // 招村民前两样先备好：账本（远处账房在后台开）与导航格（任务池）
    if (hinterland && !hinterland->is_open(p_index)) {
        hinterland->request(p_index);
    }
    const auto nav = navs.find(p_index);
    if (nav == navs.end() || nav->second.layout_sig != layout_signature(p_village->get_settlement()->get_model())) {
        world->get_pool().submit(world->job_key(JobKind::NAV, uint64_t(p_index)), p_distance, nav_job(p_village));
    }
}

void SiteStream::refresh_villages() {
    JobPool &pool = world->get_pool();
    const int count = int(world->get_sites().settlements.size());
    for (int k = 0; k < count; k++) {
        const double d = edge_distance(k);
        const auto found = villages.find(k);
        if (found != villages.end()) {
            if (d > village_radius * hysteresis && !found->second->is_edited()) {
                StreamTimer timer(world, StreamChannel::VILLAGES);
                found->second->queue_free();
                villages.erase(found);
                navs.erase(k);
                village_version++;
                continue;
            }
            refresh_cells(found->second);
            refresh_folk(k, found->second, d);
        } else if (d <= village_radius) {
            if (!prepared.count(k)) {
                pool.submit(world->job_key(JobKind::VILLAGE, uint64_t(k)), d, village_job(k));
            }
        } else if (d > village_radius * hysteresis) {
            prepared.erase(k);
        }
    }
    // 排着的：走远了撤掉，近的往前排
    pool.reprioritize(
            JobKind::VILLAGE, [this](uint64_t p_key) {
                const double d = edge_distance(int(JobPool::id_of(p_key)));
                return d > village_radius * hysteresis ? -1.0 : d;
            },
            world->get_lane());
    pool.reprioritize(
            JobKind::CELL, [this](uint64_t p_key) {
                const int index = int(JobPool::id_of(p_key) / 65536ull), cell = int(JobPool::id_of(p_key) % 65536ull);
                const auto found = villages.find(index);
                return found == villages.end() || cell >= found->second->get_cell_count() ? -1.0 : found->second->cell_distance_to(cell, focus);
            },
            world->get_lane());
}

void SiteStream::step(uint64_t p_deadline_usec) {
    // 备好的村：近的先挂（一帧至少挂一处）
    {
        StreamTimer timer(world, StreamChannel::VILLAGES);
        std::vector<std::pair<double, int>> order;
        for (const auto &entry : prepared) {
            const double d = edge_distance(entry.first);
            if (d <= village_radius) {
                order.emplace_back(d, entry.first);
            }
        }
        std::sort(order.begin(), order.end());
        for (size_t k = 0; k < order.size() && (k == 0 || now_usec() < p_deadline_usec); k++) {
            const int index = order[k].second;
            std::shared_ptr<VillagePrepared> p = prepared[index];
            prepared.erase(index);
            build_village(index, std::move(p));
        }
        // 细模：近的村先挂 / 拆
        std::vector<std::pair<double, WorldVillage *>> near;
        for (const auto &[index, village] : villages) {
            near.emplace_back(edge_distance(index), village);
        }
        std::sort(near.begin(), near.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        for (const auto &[d, village] : near) {
            if (now_usec() >= p_deadline_usec) {
                break;
            }
            village->step_mount(p_deadline_usec, focus);
        }
    }
    // 村民：账本与导航格都齐了的，近的先招（一帧只招一处：展开与摆小人是一整件）
    if (now_usec() >= p_deadline_usec) {
        return;
    }
    StreamTimer timer(world, StreamChannel::FOLK);
    int best = -1;
    double best_d = folk_radius;
    for (const auto &[index, village] : villages) {
        const double d = edge_distance(index);
        const auto nav = navs.find(index);
        if (village->has_folk() || d > best_d || nav == navs.end() || nav->second.layout_sig != layout_signature(village->get_settlement()->get_model()) ||
                (hinterland && !hinterland->is_open(index))) {
            continue;
        }
        best = index;
        best_d = d;
    }
    if (best >= 0) {
        villages[best]->set_folk(true, per_house, max_folk, navs[best].nav, true);
        navs.erase(best);
    }
}

void SiteStream::ensure_near(const Vector3 &p_point) {
    focus = p_point;
    has_focus = true;
    if (!world || !world->is_loaded()) {
        return;
    }
    refresh(1 << 20);
    collect_jobs();
    // 落点跟前（ENSURE_VILLAGE_M 以内）的村：当场备好、挂上，ENSURE_DETAIL_M 以内的格子挂齐细模（人可能就落在房子边上）；
    // 更远的村、更远的格子、村民（账本与导航格）都照常在后台备、分帧挂——瞬移那一帧不再把一两公里内的村子连人一起铺完
    const int count = int(world->get_sites().settlements.size());
    for (int k = 0; k < count; k++) {
        if (edge_distance(k) > ENSURE_VILLAGE_M) {
            continue;
        }
        WorldVillage *village = villages.count(k) ? villages[k] : build_village(k, prepare_now(k));
        for (int c = 0; c < village->get_cell_count(); c++) {
            if (village->cell_distance_to(c, focus) <= ENSURE_DETAIL_M) {
                world->get_pool().cancel(world->job_key(JobKind::CELL, cell_id(k, c)));
                village->mount_cell_now(c);
            }
        }
    }
    refresh_villages(); // 刚挂上的村：远一点的格子、村民交给后台
}

void SiteStream::refresh(int p_build_budget) {
    // 码头
    {
        StreamTimer dock_timer(world, StreamChannel::DOCKS);
        const std::vector<DockPlan> &plans = world->get_docks();
        for (int k = 0; k < int(plans.size()); k++) {
            if (!plans[size_t(k)].ok) {
                continue;
            }
            const double d = std::hypot(plans[size_t(k)].x - focus.x, plans[size_t(k)].z - focus.z);
            const auto found = docks.find(k);
            if (found == docks.end() && d <= dock_radius) {
                WorldDock *dock = memnew(WorldDock);
                const auto &plan=plans[size_t(k)];
                const double back=dock_builder::PIER_INLAND+4.0;
                const auto approach=world->get_terrain().sample(plan.x-back*plan.out_x,plan.z-back*plan.out_z);
                dock->setup(plan, k, world->get_dock_id(k), approach.land ? approach.top : plan.y);
                add_child(dock);
                docks[k] = dock;
            } else if (found != docks.end() && d > dock_radius * hysteresis) {
                found->second->queue_free();
                docks.erase(found);
            }
        }
    }

    // 村与散户：该备的交给后台、远的拆，格子与村民按距离推（挂是 step 里分帧做的）
    refresh_villages();

    // 采场与地标：先拆远的，再由近及远摆（两样合用一份预算）
    StreamTimer site_timer(world, StreamChannel::SITES);
    const ResourcePlan &plan = world->get_resource_plan();
    std::vector<std::pair<double, int>> wanted_resources; // 下标 ≥ 0 是采场，< 0 是地标（−1 − 下标）
    for (int k = 0; k < int(plan.sites.size()); k++) {
        const double d = std::hypot(plan.sites[size_t(k)].face_x - focus.x, plan.sites[size_t(k)].face_z - focus.z);
        const auto found = resources.find(k);
        if (found != resources.end()) {
            if (d > resource_radius * hysteresis) {
                found->second->queue_free();
                resources.erase(found);
            }
        } else if (d <= resource_radius) {
            wanted_resources.emplace_back(d, k);
        }
    }
    for (int k = 0; k < int(plan.landmarks.size()); k++) {
        const double d = std::hypot(plan.landmarks[size_t(k)].x - focus.x, plan.landmarks[size_t(k)].z - focus.z);
        const auto found = landmarks.find(k);
        if (found != landmarks.end()) {
            if (d > resource_radius * hysteresis) {
                found->second->queue_free();
                landmarks.erase(found);
            }
        } else if (d <= resource_radius) {
            wanted_resources.emplace_back(d, -1 - k);
        }
    }
    std::sort(wanted_resources.begin(), wanted_resources.end());
    const int budget = p_build_budget > 1 ? p_build_budget : resource_budget;
    for (size_t k = 0; k < wanted_resources.size() && int(k) < budget; k++) {
        const int index = wanted_resources[k].second;
        WorldResource *node = memnew(WorldResource);
        if (index >= 0) {
            node->setup(plan.sites[size_t(index)], index);
            resources[index] = node;
        } else {
            node->setup_landmark(plan.landmarks[size_t(-1 - index)], -1 - index);
            landmarks[-1 - index] = node;
        }
        add_child(node);
    }
}

void SiteStream::retire_folk() {
    for (auto &[index, village] : villages) {
        if (village->has_folk()) {
            village->set_folk(false, per_house, max_folk);
        }
        navs.erase(index);
    }
}

int SiteStream::get_detailed_cell_count() const {
    int n = 0;
    for (const auto &entry : villages) {
        n += entry.second->get_detailed_cell_count();
    }
    return n;
}

PackedInt32Array SiteStream::get_built_landmarks() const {
    PackedInt32Array ids;
    for (const auto &entry : landmarks) {
        ids.push_back(entry.second->get_deposit_id());
    }
    return ids;
}

std::vector<WorldVillage *> SiteStream::built_villages() const {
    std::vector<WorldVillage *> out;
    out.reserve(villages.size());
    for (const auto &entry : villages) {
        out.push_back(entry.second);
    }
    return out;
}

PackedInt32Array SiteStream::get_built_resources() const {
    PackedInt32Array ids;
    for (const auto &entry : resources) {
        ids.push_back(entry.second->get_deposit_id());
    }
    return ids;
}

WorldResource *SiteStream::get_resource(int p_working_id) const {
    for (const auto &entry : resources) {
        if (entry.second->get_deposit_id() == p_working_id) {
            return entry.second;
        }
    }
    return nullptr;
}

WorldResource *SiteStream::resource_near(const Vector3 &p_point, double p_range) const {
    WorldResource *best = nullptr;
    double best_d = p_range;
    for (const auto &entry : resources) {
        const ResourceSite &s = entry.second->get_site();
        const double d = std::min(std::hypot(s.face_x - p_point.x, s.face_z - p_point.z), std::hypot(s.pile_x - p_point.x, s.pile_z - p_point.z));
        if (d <= best_d) {
            best_d = d;
            best = entry.second;
        }
    }
    return best;
}

PackedInt32Array SiteStream::get_built_docks() const {
    PackedInt32Array ids;
    for (const auto &entry : docks) {
        ids.push_back(entry.second->get_dock_id());
    }
    return ids;
}

PackedInt32Array SiteStream::get_built_villages() const {
    PackedInt32Array ids;
    for (const auto &entry : villages) {
        if (!entry.second->is_hamlet()) {
            ids.push_back(entry.second->get_site_id());
        }
    }
    return ids;
}

int SiteStream::get_built_hamlet_count() const {
    int n = 0;
    for (const auto &entry : villages) {
        n += entry.second->is_hamlet() ? 1 : 0;
    }
    return n;
}

WorldVillage *SiteStream::get_village(int p_village_id) const {
    for (const auto &entry : villages) {
        if (!entry.second->is_hamlet() && entry.second->get_site_id() == p_village_id) {
            return entry.second;
        }
    }
    return nullptr;
}

WorldVillage *SiteStream::get_hamlet(int p_hamlet_id) const {
    for (const auto &entry : villages) {
        if (entry.second->is_hamlet() && entry.second->get_site_id() == p_hamlet_id) {
            return entry.second;
        }
    }
    return nullptr;
}

WorldVillage *SiteStream::village_at(const Vector3 &p_point, double p_margin) const {
    WorldVillage *best = nullptr;
    double best_d = 1e18;
    for (const auto &entry : villages) {
        const SiteSettlement &s = entry.second->get_site();
        const double d = std::hypot(s.x - p_point.x, s.z - p_point.z);
        if (d <= entry.second->get_radius() + p_margin && d < best_d) {
            best_d = d;
            best = entry.second;
        }
    }
    return best;
}

WorldDock *SiteStream::get_dock(int p_dock_id) const {
    for (const auto &entry : docks) {
        if (entry.second->get_dock_id() == p_dock_id) {
            return entry.second;
        }
    }
    return nullptr;
}
