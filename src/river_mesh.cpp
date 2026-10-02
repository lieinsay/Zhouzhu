#include "river_mesh.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace {

RiverChunkKey chunk_of(double p_x, double p_z, double p_chunk) {
    return { int(std::floor(p_x / p_chunk)), int(std::floor(p_z / p_chunk)) };
}

// 该点的横向单位向量（顺水看的左手，x 东 z 南：左 = (tz, −tx)），取前后两段切向的平均
void lateral(const RiverStem &p_stem, size_t p_i, double &r_lx, double &r_lz) {
    const size_t a = p_i > 0 ? p_i - 1 : p_i, b = p_i + 1 < p_stem.v.size() ? p_i + 1 : p_i;
    double tx = p_stem.v[b].x - p_stem.v[a].x, tz = p_stem.v[b].z - p_stem.v[a].z;
    const double len = std::hypot(tx, tz);
    if (len < 1e-9) {
        tx = 1.0, tz = 0.0;
    } else {
        tx /= len, tz /= len;
    }
    r_lx = tz;
    r_lz = -tx;
}

bool blocked(const WorldTerrain &p_terrain, double p_x, double p_z, double p_margin) {
    for (const YardPlan &yard : p_terrain.get_yards()) {
        if (yard.contains(p_x, p_z, p_margin)) {
            return true;
        }
    }
    return false;
}

struct Emit {
    RiverMesh &mesh;

    int vertex(double x, double y, double z, double u, double v, const RiverVertex &p_at, double p_drop, double p_speed, double tx, double tz, double grade, bool curtain = false) {
        mesh.positions.insert(mesh.positions.end(), { float(x - mesh.origin_x), float(y), float(z - mesh.origin_z) });
        const double inv = 1.0 / std::sqrt(1.0 + grade * grade);
        mesh.normals.insert(mesh.normals.end(), curtain ? std::initializer_list<float>{float(-tx),0.0f,float(-tz)}
                : std::initializer_list<float>{float(tx*grade*inv),float(inv),float(tz*grade*inv)});
        mesh.uvs.insert(mesh.uvs.end(), { float(u), float(v) });
        mesh.custom0.insert(mesh.custom0.end(), { float(p_at.bed()), float(p_at.depth), float(p_at.source_data() ? -std::max(1e-8,p_at.bankfull_flow) / std::max(1e-8, p_at.mean_flow) : p_at.dry), float(p_at.half()) });
        mesh.custom1.insert(mesh.custom1.end(), { float(p_at.floodplain_at(v)), float(p_drop), float(p_speed), float(p_at.basin + 1) });
        mesh.custom2.insert(mesh.custom2.end(), {float(p_at.mean_flow),float(p_at.ssc_mg_l),float(p_at.d50_mm),float(p_at.slope)});
        mesh.custom3.insert(mesh.custom3.end(), {float(p_at.floodplain_at(1)),float(p_at.floodplain_at(-1)),float(tx),float(tz)});
        if (mesh.positions.size() == 3) {
            mesh.min_y = y - p_drop;
            mesh.max_y = p_at.surface;
        } else {
            mesh.min_y = std::min(mesh.min_y, y - p_drop);
            mesh.max_y = std::max(mesh.max_y, p_at.surface);
        }
        return int(mesh.positions.size() / 3) - 1;
    }
    // a b 在上游一排、c d 在下游一排（同一侧的顺序一致）：两个三角形
    void quad(int a, int b, int c, int d) {
        mesh.indices.insert(mesh.indices.end(), { a, b, d, a, d, c });
    }
};

double flow_speed(const RiverVertex &a, const RiverVertex &b) {
    if (a.source_data()) return 0.0; // 实时流速由 shader 按 Q/(w*d) 计算
    const double len = std::max(1e-3, b.s - a.s);
    const double slope = std::max(0.0, (a.surface - b.surface) / len);
    return std::clamp(0.3 + 40.0 * slope, 0.3, 2.5);
}

} // namespace

std::vector<RiverChunkKey> river_chunks(const WorldTerrain &p_terrain, const RiverMeshRules &p_rules) {
    std::set<std::pair<int, int>> seen;
    std::vector<RiverChunkKey> out;
    if (!p_terrain.has_rivers()) {
        return out;
    }
    for (const RiverStem &stem : p_terrain.get_rivers().get_stems()) {
        for (size_t i = 0; i + 1 < stem.v.size(); i++) {
            const RiverChunkKey key = chunk_of(0.5 * (stem.v[i].x + stem.v[i + 1].x), 0.5 * (stem.v[i].z + stem.v[i + 1].z), p_rules.chunk_m);
            if (seen.insert({ key.ix, key.iz }).second) {
                out.push_back(key);
            }
        }
    }
    return out;
}

RiverMesh build_river_mesh(const WorldTerrain &p_terrain, const RiverChunkKey &p_key, const RiverMeshRules &p_rules) {
    RiverMesh mesh;
    mesh.key = p_key;
    mesh.origin_x = p_key.ix * p_rules.chunk_m;
    mesh.origin_z = p_key.iz * p_rules.chunk_m;
    if (!p_terrain.has_rivers()) {
        return mesh;
    }
    const WorldRivers &rivers = p_terrain.get_rivers();
    Emit emit{ mesh };
    const double x0 = mesh.origin_x, z0 = mesh.origin_z, x1 = x0 + p_rules.chunk_m, z1 = z0 + p_rules.chunk_m;
    rivers.segments_in(x0, z0, x1, z1, [&](int p_stem, int p_seg) {
        const RiverStem &stem = rivers.get_stems()[size_t(p_stem)];
        const size_t i = size_t(p_seg);
        const RiverVertex &a = stem.v[i], &b = stem.v[i + 1];
        const double mx = 0.5 * (a.x + b.x), mz = 0.5 * (a.z + b.z);
        if (mx < x0 || mx >= x1 || mz < z0 || mz >= z1) {
            return; // 归别的块
        }
        if (blocked(p_terrain, mx, mz, std::max(a.core(), b.core()) + p_rules.yard_margin_m)) {
            return;
        }
        const double speed = flow_speed(a, b);
        const double grade = (a.bed()-b.bed()) / std::max(1e-3,b.s-a.s);
        int row[2][5];
        for (int e = 0; e < 2; e++) {
            const RiverVertex &v = e == 0 ? a : b;
            double lx, lz;
            lateral(stem, i + size_t(e), lx, lz);
            // 近岸处带子收窄到离岸线的距离：河口一带岸线斜着切过河，整幅带子会伸到云海上空
            const double room = std::max(0.5, p_terrain.sample_raw(v.x, v.z).shore - p_rules.coast_margin_m);
            const double half = std::min(v.half(), room);
            const double left=std::min(v.core_at(1),std::max(half,room)), right=std::min(v.core_at(-1),std::max(half,room));
            const double offs[5] = { -right, -half, 0.0, half, left };
            for (int k = 0; k < 5; k++) {
                row[e][k] = emit.vertex(v.x + lx * offs[k], v.bed(), v.z + lz * offs[k], v.s, offs[k], v, 0.0, speed, -lz, lx, grade);
            }
        }
        for (int k = 0; k < 4; k++) {
            emit.quad(row[0][k], row[0][k + 1], row[1][k], row[1][k + 1]);
        }
        // 河口的跌水帘
        const double drop = a.source_data() ? stem.outlet_drop : p_rules.fall_m;
        if (stem.mouth && drop > 0 && i + 2 == stem.v.size()) {
            double lx, lz;
            lateral(stem, i + 1, lx, lz);
            const double tx = -lz, tz = lx; // 顺水方向
            const double offs[3] = { -b.half(), 0.0, b.half() };
            int top[3], low[3];
            for (int k = 0; k < 3; k++) {
                const double px = b.x + lx * offs[k], pz = b.z + lz * offs[k];
                top[k] = emit.vertex(px, b.bed(), pz, b.s, offs[k], b, 0.0, speed, tx, tz, grade, true);
                // 新数据没有水平抛距，只画给出的垂直落差，不补一律 14 m 的抛物线。
                const double reach = a.source_data() ? 0.0 : p_rules.fall_throw_m;
                low[k] = emit.vertex(px + tx * reach, b.bed(), pz + tz * reach, b.s + drop, offs[k], b, drop, speed, tx, tz, grade, true);
            }
            for (int k = 0; k < 2; k++) {
                emit.quad(top[k], top[k + 1], low[k], low[k + 1]);
            }
        }
    });
    return mesh;
}
