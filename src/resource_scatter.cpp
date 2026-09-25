#include "resource_scatter.h"

#include <algorithm>
#include <cmath>

#include "rng.h"
#include "world_ground.h"

namespace {

constexpr double TAU = 6.283185307179586;
constexpr double SLOPE_ARM_M = 1.5; // 量坡：往东、往南各这么远取高差

// 地表类（skyisle-gen landcover.classes 的下标）
enum Landcover : int {
    LC_VOID = 0,
    LC_CLIFF = 1, // 崖缘
    LC_ROCK = 2, // 裸岩
    LC_MEADOW = 3, // 高山草甸
    LC_FOREST = 4, // 林地
    LC_SCRUB = 5, // 灌丛
    LC_GRASS = 6, // 草坡
    LC_FIELD = 7, // 可耕地
    LC_TERRACE = 8, // 梯田
    LC_WETLAND = 9, // 湿地
    LC_RIVER = 10,
    LC_LAKE = 11,
};

// 片的种类
enum PatchKind : uint8_t {
    PATCH_NONE = 0,
    PATCH_TIMBER,
    PATCH_PEAT,
    PATCH_FLOATSTONE,
    PATCH_GUANO,
};

// 各层（每层一张抖动网格）
enum Layer : int {
    LAYER_TREES = 0,
    LAYER_SHRUBS,
    LAYER_REEDS,
    LAYER_ROCKS,
    LAYER_BANKS,
    LAYER_COUNT,
};

// 各类赋存的门槛（同生成器的 occ_thr：品位够才算赋存区）
constexpr double ORE_MIN = 0.15, SULFUR_MIN = 0.15, PLACER_MIN = 0.15, CLAY_MIN = 0.3, GRAVEL_MIN = 0.3, STONE_MIN = 0.4;
constexpr double HIGH_CONIFER_M = 1100.0; // 高过这么多（云带顶以上）针叶树多起来

uint64_t mix(uint64_t p_a, uint64_t p_b) {
    uint64_t z = p_a ^ (p_b + 0x9E3779B97F4A7C15ull + (p_a << 6) + (p_a >> 2));
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double step_of(const ScatterRules &p_rules, int p_layer) {
    switch (p_layer) {
        case LAYER_TREES:
            return p_rules.tree_step_m;
        case LAYER_SHRUBS:
            return p_rules.shrub_step_m;
        case LAYER_REEDS:
            return p_rules.reed_step_m;
        case LAYER_ROCKS:
            return p_rules.rock_step_m;
        default:
            return p_rules.bank_step_m;
    }
}

// 这一层在这种地表上长的机会
double tree_chance(int p_lc) {
    switch (p_lc) {
        case LC_FOREST:
            return 0.85;
        case LC_SCRUB:
            return 0.05;
        case LC_GRASS:
            return 0.02;
        case LC_CLIFF:
            return 0.06;
        case LC_MEADOW:
            return 0.03;
        case LC_WETLAND:
            return 0.02;
        default:
            return 0.0;
    }
}

double shrub_chance(int p_lc) {
    switch (p_lc) {
        case LC_SCRUB:
            return 0.55;
        case LC_FOREST:
            return 0.05;
        case LC_GRASS:
            return 0.06;
        case LC_CLIFF:
            return 0.12;
        case LC_MEADOW:
            return 0.05;
        case LC_ROCK:
            return 0.03;
        case LC_WETLAND:
            return 0.1;
        default:
            return 0.0;
    }
}

// 露头不上林（林下看不见）；河道格里没水的滩地可以（水里的另按河槽让开）
bool wooded_or_wet(int p_lc) {
    return p_lc == LC_FOREST || p_lc == LC_LAKE || p_lc == LC_VOID;
}

// 田里：砂砾、砂金是河边滩上冲下来的（生成器的砂砾赋存七成压在河边的田下），照样露；黏土坡、结壳不上田
bool farmed(int p_lc) {
    return p_lc == LC_FIELD || p_lc == LC_TERRACE;
}

} // namespace

// ---------------------------------------------------------------- 键

ScatterChunkKey ScatterChunkKey::unpack(uint64_t p_packed) {
    ScatterChunkKey key;
    // 24 位有符号
    key.ix = int32_t(uint32_t(p_packed >> 24 & 0xFFFFFF) << 8) >> 8;
    key.iz = int32_t(uint32_t(p_packed & 0xFFFFFF) << 8) >> 8;
    return key;
}

uint64_t scatter_key(const ScatterChunkKey &p_chunk, int p_index) {
    return (p_chunk.packed() << 16) | uint64_t(uint16_t(p_index));
}

ScatterChunkKey scatter_key_chunk(uint64_t p_key) {
    return ScatterChunkKey::unpack(p_key >> 16);
}

int scatter_key_index(uint64_t p_key) {
    return int(p_key & 0xFFFF);
}

// ---------------------------------------------------------------- 圆

void DiskIndex::add(double p_x, double p_z, double p_r) {
    disks.push_back({ p_x, p_z, p_r });
    max_r = std::max(max_r, p_r);
}

void DiskIndex::finish() {
    std::sort(disks.begin(), disks.end(), [](const Disk &a, const Disk &b) { return a.x < b.x; });
}

bool DiskIndex::hit(double p_x, double p_z) const {
    auto it = std::lower_bound(disks.begin(), disks.end(), p_x - max_r, [](const Disk &d, double v) { return d.x < v; });
    for (; it != disks.end() && it->x <= p_x + max_r; ++it) {
        const double dx = p_x - it->x, dz = p_z - it->z;
        if (dx * dx + dz * dz <= it->r * it->r) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- 生成

ResourceScatter::ResourceScatter(const WorldTerrain &p_terrain, const WorldSites &p_sites, const ResourceData &p_data, const ResourcePlan &p_plan, uint64_t p_seed,
        const ScatterRules &p_rules) :
        terrain(&p_terrain), rules(p_rules), seed(mix(p_seed, 0x5CA77E4ull)) {
    for (const SiteSettlement &s : p_sites.settlements) {
        villages.add(s.x, s.z, settlement_guess_radius(s) + rules.village_margin_m);
    }
    villages.finish();
    for (const ResourceSite &s : p_plan.sites) {
        works.add(s.face_x, s.face_z, rules.site_clear_m);
        works.add(s.pile_x, s.pile_z, rules.site_clear_m);
        if (s.reach_m >= 0.0) {
            works.add(s.land_x, s.land_z, rules.landing_clear_m);
        }
    }
    for (const ResourceLandmark &m : p_plan.landmarks) {
        works.add(m.x, m.z, rules.landmark_clear_m);
    }
    works.finish();

    int max_id = -1;
    for (const ResourceDeposit &d : p_data.deposits) {
        max_id = std::max(max_id, d.id);
    }
    patches.assign(size_t(max_id + 1), Patch());
    for (const ResourceDeposit &d : p_data.deposits) {
        if (d.form != DepositForm::PATCH) {
            continue;
        }
        Patch &p = patches[size_t(d.id)];
        if (d.kind == "timber") {
            p.kind = PATCH_TIMBER;
            p.conifer = d.subtype.find("针阔") != std::string::npos ? 0.5f : d.subtype.find("针叶") != std::string::npos ? 0.9f : 0.12f;
        } else if (d.kind == "peat") {
            p.kind = PATCH_PEAT;
            p.sub = d.subtype.find("芦苇") != std::string::npos ? deposits::SUB_REED : deposits::SUB_PEAT;
        } else if (d.kind == "floatstone") {
            p.kind = PATCH_FLOATSTONE;
        } else if (d.kind == "guano") {
            p.kind = PATCH_GUANO;
        }
    }
    for (const ResourceOccurrence &o : p_data.occurrences) {
        if (o.kind == "ore") {
            ores.push_back({ o.island, o.x, o.z, uint8_t(deposits::ore_index(o.subtype)) });
        }
    }
}

uint8_t ResourceScatter::ore_sub(int p_island, double p_x, double p_z) const {
    double best = 1e300;
    uint8_t sub = 0;
    for (const Ore &o : ores) {
        const double d = std::hypot(o.x - p_x, o.z - p_z) + (o.island == p_island ? 0.0 : 1e7);
        if (d < best) {
            best = d;
            sub = o.sub;
        }
    }
    return sub;
}

bool ResourceScatter::blocked(double p_x, double p_z) const {
    return villages.hit(p_x, p_z) || works.hit(p_x, p_z) || terrain->near_pad(p_x, p_z, rules.pad_margin_m);
}

ScatterChunkKey ResourceScatter::chunk_at(double p_x, double p_z) const {
    return ScatterChunkKey{ int(std::floor(p_x / rules.chunk_m)), int(std::floor(p_z / rules.chunk_m)) };
}

void ResourceScatter::chunk_corner(const ScatterChunkKey &p_key, double &r_x, double &r_z) const {
    r_x = p_key.ix * rules.chunk_m;
    r_z = p_key.iz * rules.chunk_m;
}

std::vector<ScatterItem> ResourceScatter::chunk(const ScatterChunkKey &p_key) const {
    std::vector<ScatterItem> out;
    if (!valid()) {
        return out;
    }
    const WorldGrid &grid = terrain->get_grid();
    double x0 = 0.0, z0 = 0.0;
    chunk_corner(p_key, x0, z0);
    const double x1 = x0 + rules.chunk_m, z1 = z0 + rules.chunk_m;
    // 块（外扩扭曲的幅度）里一格陆地都没有就不用看了
    {
        const double pad = rules.edge_warp_m + grid.res;
        const int c0 = std::max(0, int(std::floor(grid.col_of(x0 - pad)))), c1 = std::min(grid.cols - 1, int(std::ceil(grid.col_of(x1 + pad))));
        const int r0 = std::max(0, int(std::floor(grid.row_of(z0 - pad)))), r1 = std::min(grid.rows - 1, int(std::ceil(grid.row_of(z1 + pad))));
        bool land = false;
        for (int r = r0; r <= r1 && !land; r++) {
            for (int c = c0; c <= c1 && !land; c++) {
                land = grid.landcover[grid.index(r, c)] != LC_VOID;
            }
        }
        if (!land) {
            return out;
        }
    }
    const WorldGround ground(*terrain, 0.0, 0.0);

    for (int layer = 0; layer < LAYER_COUNT; layer++) {
        const double step = step_of(rules, layer);
        // 格点抖动后落在哪块就归哪块（块边上的格点要看两边）：每一株都在自己的块里，按人周围几米找块不会漏
        const int gi0 = int(std::floor(x0 / step)), gi1 = int(std::ceil(x1 / step));
        const int gj0 = int(std::floor(z0 / step)), gj1 = int(std::ceil(z1 / step));
        for (int gj = gj0; gj < gj1; gj++) {
            for (int gi = gi0; gi < gi1; gi++) {
                Rng rng(mix(mix(seed, uint64_t(layer) + 1), (uint64_t(uint32_t(gi)) << 32) | uint32_t(gj)));
                const double x = (gi + 0.1 + 0.8 * rng.uniform()) * step, z = (gj + 0.1 + 0.8 * rng.uniform()) * step;
                if (x < x0 || x >= x1 || z < z0 || z >= z1) {
                    continue;
                }
                const double roll = rng.uniform(), size_u = rng.uniform(), yaw = TAU * rng.uniform(), kind_u = rng.uniform();
                // 地表与片号：把点扭一扭再取最近的格
                const double wx = x + rules.edge_warp_m * hash_fbm(seed ^ 0xA1ull, x, z, rules.edge_feature_m, 2);
                const double wz = z + rules.edge_warp_m * hash_fbm(seed ^ 0xB2ull, x, z, rules.edge_feature_m, 2);
                const size_t cell = grid.nearest_cell(wx, wz);
                const int lc = grid.landcover[cell];
                if (lc == LC_VOID) {
                    continue;
                }
                const int patch_id = grid.patch_cell(cell);
                const Patch patch = patch_id >= 0 && size_t(patch_id) < patches.size() ? patches[size_t(patch_id)] : Patch();

                ScatterItem item;
                item.yaw = float(yaw);
                double sink = 0.1;
                bool tree_like = false, bank_like = false;
                if (layer == LAYER_TREES) {
                    const double density = std::clamp(0.75 + 0.35 * hash_fbm(seed ^ 0xC3ull, x, z, 45.0, 2), 0.3, 1.0);
                    if (roll >= tree_chance(lc) * (lc == LC_FOREST ? density : 1.0)) {
                        continue;
                    }
                    double conifer = patch.kind == PATCH_TIMBER ? patch.conifer : 0.15;
                    if (grid.height[cell] > HIGH_CONIFER_M || lc == LC_MEADOW) {
                        conifer = std::max(conifer, 0.7);
                    }
                    item.look = uint8_t(kind_u < conifer ? ScatterLook::CONIFER : ScatterLook::BROADLEAF);
                    item.scale = float(0.75 + 0.55 * size_u);
                    sink = 0.2;
                    tree_like = true;
                } else if (layer == LAYER_SHRUBS) {
                    if (roll >= shrub_chance(lc)) {
                        continue;
                    }
                    item.look = uint8_t(ScatterLook::SHRUB);
                    item.scale = float(0.7 + 0.6 * size_u);
                    tree_like = true;
                } else if (layer == LAYER_REEDS) {
                    const bool peat = patch.kind == PATCH_PEAT;
                    if (roll >= (peat ? 0.55 : lc == LC_WETLAND ? 0.35 : 0.0)) {
                        continue;
                    }
                    item.look = uint8_t(ScatterLook::REEDS);
                    item.sub = peat ? patch.sub : deposits::SUB_REED;
                    item.scale = float(0.8 + 0.4 * size_u);
                    bank_like = true;
                } else if (layer == LAYER_ROCKS) {
                    if (lc == LC_FIELD || lc == LC_TERRACE) {
                        continue; // 田里的石头早捡干净了（生成器的岩类赋存也不上田）
                    }
                    const double ore = grid.field_at(WorldGrid::FIELD_ORE, x, z), stone = grid.field_at(WorldGrid::FIELD_STONE, x, z);
                    const double p_ore = ore >= ORE_MIN ? 0.5 * ore : 0.0;
                    const double p_float = patch.kind == PATCH_FLOATSTONE ? 0.3 : 0.0;
                    double p_stone = stone >= STONE_MIN ? 0.08 + 0.5 * (stone - STONE_MIN) : (lc == LC_ROCK ? 0.1 : 0.0);
                    if (lc == LC_FOREST) {
                        p_stone *= 0.5;
                    }
                    if (roll < p_ore) {
                        item.look = uint8_t(ScatterLook::ORE_ROCK);
                        item.sub = ore_sub(grid.island[cell], x, z);
                        item.scale = float(0.7 + 0.5 * size_u);
                    } else if (roll < p_ore + p_float) {
                        item.look = uint8_t(ScatterLook::FLOATSTONE);
                        item.scale = float(0.7 + 0.7 * size_u);
                    } else if (roll < p_ore + p_float + p_stone) {
                        item.look = uint8_t(ScatterLook::BOULDER);
                        item.scale = float(0.6 + 0.5 * size_u + 0.5 * std::max(stone, 0.3));
                    } else {
                        continue;
                    }
                    sink = 0.25 * item.scale;
                } else {
                    if (wooded_or_wet(lc)) {
                        continue;
                    }
                    const bool field = farmed(lc);
                    const double placer = grid.field_at(WorldGrid::FIELD_PLACER, x, z), sulfur = grid.field_at(WorldGrid::FIELD_SULFUR, x, z);
                    const double gravel = grid.field_at(WorldGrid::FIELD_GRAVEL, x, z), clay = grid.field_at(WorldGrid::FIELD_CLAY, x, z);
                    const double p[5] = {
                        placer >= PLACER_MIN ? 0.35 * placer : 0.0,
                        sulfur >= SULFUR_MIN && !field ? 0.4 * sulfur : 0.0,
                        patch.kind == PATCH_GUANO && !field ? 0.35 : 0.0,
                        gravel >= GRAVEL_MIN ? 0.22 * gravel : 0.0,
                        clay >= CLAY_MIN && !field ? 0.12 * clay : 0.0,
                    };
                    double acc = 0.0;
                    int pick = -1;
                    for (int k = 0; k < 5 && pick < 0; k++) {
                        acc += p[k];
                        pick = roll < acc ? k : -1;
                    }
                    if (pick < 0) {
                        continue;
                    }
                    static const ScatterLook LOOKS[5] = { ScatterLook::PLACER, ScatterLook::CRUST, ScatterLook::CRUST, ScatterLook::GRAVEL_HEAP, ScatterLook::CLAY_BANK };
                    item.look = uint8_t(LOOKS[pick]);
                    item.sub = pick == 2 ? deposits::SUB_GUANO : deposits::SUB_SULFUR;
                    item.scale = float(0.8 + 0.5 * size_u);
                    sink = 0.05;
                    bank_like = true;
                }

                // 中了：查让不让、量高
                if (blocked(x, z)) {
                    continue;
                }
                const TerrainSample s = terrain->sample(x, z);
                if (!s.land || s.shore < rules.min_shore_m) {
                    continue;
                }
                if (ground.water_at(x, z) != WaterKind::NONE || ground.on_yard(x, z, rules.pad_margin_m)) {
                    continue;
                }
                if (!bank_like && ground.on_floodplain(x, z)) {
                    continue; // 河漫滩上不长树、不摆石头（水一涨就淹）
                }
                if (tree_like || bank_like) {
                    double east = 0.0, south = 0.0;
                    if (!terrain->surface(x + SLOPE_ARM_M, z, east) || !terrain->surface(x, z + SLOPE_ARM_M, south)) {
                        continue;
                    }
                    const double grade = std::hypot(east - s.top, south - s.top) / SLOPE_ARM_M;
                    if (grade > (tree_like ? rules.max_tree_grade : rules.max_bank_grade)) {
                        continue;
                    }
                }
                item.x = float(x);
                item.z = float(z);
                item.y = float(s.top - sink);
                item.index = uint16_t(out.size());
                out.push_back(item);
            }
        }
    }
    return out;
}

void ResourceScatter::solids_near(double p_x, double p_z, double p_radius, std::vector<ScatterItem> &r_items) const {
    const ScatterChunkKey lo = chunk_at(p_x - p_radius, p_z - p_radius), hi = chunk_at(p_x + p_radius, p_z + p_radius);
    for (int iz = lo.iz; iz <= hi.iz; iz++) {
        for (int ix = lo.ix; ix <= hi.ix; ix++) {
            for (const ScatterItem &item : chunk(ScatterChunkKey{ ix, iz })) {
                if (deposits::scatter(item.kind()).solid && std::hypot(item.x - p_x, item.z - p_z) <= p_radius + scatter_solid_radius(item)) {
                    r_items.push_back(item);
                }
            }
        }
    }
}

double scatter_solid_radius(const ScatterItem &p_item) {
    return deposits::scatter(p_item.kind()).radius * p_item.scale;
}

double scatter_reach(const ScatterItem &p_item, const ScatterRules &p_rules) {
    return deposits::scatter(p_item.kind()).radius * p_item.scale + p_rules.reach_m;
}

// ---------------------------------------------------------------- 存量账

bool ScatterLedger::standing(uint64_t p_key, int p_cap, double p_refill_hours, double p_now_hours) const {
    const auto found = entries.find(p_key);
    if (found == entries.end() || !found->second.felled) {
        return true;
    }
    return stock_amount(found->second.stock, p_cap, p_refill_hours, p_now_hours) + 1e-9 >= double(p_cap);
}

ResourceStock *ScatterLedger::stock(uint64_t p_key) {
    return &entries[p_key].stock;
}

const ResourceStock *ScatterLedger::find(uint64_t p_key) const {
    const auto found = entries.find(p_key);
    return found == entries.end() ? nullptr : &found->second.stock;
}

bool ScatterLedger::settle(uint64_t p_key, int p_cap, double p_refill_hours, double p_now_hours) {
    Entry &e = entries[p_key];
    if (e.felled && stock_amount(e.stock, p_cap, p_refill_hours, p_now_hours) + 1e-9 >= double(p_cap)) {
        e.felled = false; // 长回来了
    }
    if (!e.felled && stock_amount(e.stock, p_cap, p_refill_hours, p_now_hours) + 1e-9 < 1.0) {
        e.felled = true;
    }
    return e.felled;
}

bool ScatterLedger::take(uint64_t p_key, int p_cap, double p_refill_hours, double p_now_hours, int p_count) {
    if (!standing(p_key, p_cap, p_refill_hours, p_now_hours)) {
        return false;
    }
    Entry &e = entries[p_key];
    e.felled = false;
    if (!stock_take(e.stock, p_cap, p_refill_hours, p_now_hours, p_count)) {
        return false;
    }
    settle(p_key, p_cap, p_refill_hours, p_now_hours);
    return true;
}

bool ScatterLedger::any_felled_in(const ScatterChunkKey &p_chunk) const {
    const uint64_t packed = p_chunk.packed();
    for (const auto &[key, e] : entries) {
        if (e.felled && (key >> 16) == packed) {
            return true;
        }
    }
    return false;
}
