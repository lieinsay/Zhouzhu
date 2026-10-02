#include "gen_export.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <tuple>

#include <zlib.h>

#include "skyisle/grid.hpp"
#include "skyisle/island/resources.hpp"
#include "skyisle/island/landforms.hpp"

#include "py_format.h"
#include "scipy_edt.h"

using skyisle::Json;
using skyisle::npround;
using skyisle::pyround;
namespace isl = skyisle::island;

namespace gen_export {

namespace {

// ---------------------------------------------------------------- 生成器前端的中文表（skyisle_gen/island/decode.py、output.py、resources.py、s03_islands.py、weather.py）

const char *const LANDCOVER_CLASSES[12] = { "虚空", "崖缘", "裸岩", "高山草甸", "林地", "灌丛", "草坡", "可耕地", "梯田", "湿地", "河道", "湖" };
const int LANDCOVER_PALETTE[12][3] = { { 20, 24, 40 }, { 90, 80, 75 }, { 150, 150, 150 }, { 170, 200, 120 }, { 40, 110, 50 }, { 120, 150, 70 },
    { 190, 200, 110 }, { 230, 200, 90 }, { 210, 170, 60 }, { 90, 160, 150 }, { 40, 90, 200 }, { 30, 60, 170 } };
const char *const CLASS_ZH[4] = { "密接群岛", "中疏诸岛", "稀疏岛链", "孤悬散岛" };
const char *const WEATHER_TYPES[9] = { "晴", "多云", "小雨", "大雨", "云海漫顶", "风暴", "小雪", "大雪", "暴风雪" };
const char *const FIELD_KINDS[7] = { "ore", "sulfur", "placer", "clay", "gravel", "stone", "salt" };

std::string age_zh(isl::AgeKind p_kind) {
    return p_kind == isl::YOUNG ? "新岛" : (p_kind == isl::OLD ? "老岛" : "中年");
}

std::string grade_zh(const std::string &p_code) {
    if (p_code == "hi") {
        return "上";
    }
    if (p_code == "mid") {
        return "中";
    }
    if (p_code == "lo") {
        return "下";
    }
    throw std::runtime_error("不认识的品位代码 " + p_code);
}

const std::map<std::string, std::string> &subtype_table() {
    static const std::map<std::string, std::string> t = {
        { "conifer", "针叶林" },
        { "mixed", "针阔混交林" },
        { "broadleaf", "阔叶林" },
        { "peat", "泥炭" },
        { "reed", "芦苇荡" },
        { "cliff_face", "崖面露头" },
        { "gorge", "峡谷露头" },
        { "skeleton_void", "骨架空洞" },
        { "gorge_cave", "峡谷岩洞" },
        { "fracture_cave", "裂隙洞" },
        { "karst_cave", "溶洞" },
        { "sinkhole", "落水洞" },
        { "underground_river", "地下河" },
        { "cliff_cave", "崖洞" },
        { "underside_cave", "底面洞" },
        { "waterfall_cave", "瀑布后洞" },
        { "goldsilver", "金银" },
        { "iron", "铁" },
        { "leadzinc", "铅锌" },
        { "copper", "铜" },
        { "tin", "锡" },
        { "chromium", "铬" },
        { "nickel", "镍" },
        { "manganese", "锰" },
        { "hydrothermal", "热泉硫磺" },
        { "marine_limestone", "海相石灰岩" },
        { "gabbro", "辉长岩" },
        { "serpentinite", "蛇纹岩" },
        { "salt_dome", "盐丘" },
        { "karst_spring", "岩溶泉" },
        { "contact_spring", "接触泉" },
        { "fissure_spring", "裂隙泉" },
        { "seep_spring", "渗水泉" },
        { "core_hotspring", "核山温泉" },
        { "lake_clay", "河湖黏土" },
        { "stream_clay", "溪边黏土" },
        { "river_gravel", "河滩砂砾" },
        { "placer_gold", "砂金" },
    };
    return t;
}

// decode._sub：代码 → 中文；空（None）→ 空
std::string subtype_zh(const std::string &p_code) {
    if (p_code.empty()) {
        return std::string();
    }
    const auto &t = subtype_table();
    const auto it = t.find(p_code);
    if (it == t.end()) {
        throw std::runtime_error("不认识的亚类代码 " + p_code);
    }
    return it->second;
}

std::string special_zh(const std::string &p_code) {
    static const std::map<std::string, std::string> t = {
        { "ore_town", "矿镇" },
        { "ore_village", "矿村" },
        { "floatstone_village", "浮石采石村" },
        { "kiln_village", "窑村" },
        { "charcoal_camp", "烧炭营" },
        { "hotspring", "温泉地" },
        { "salt_village", "盐井村" },
    };
    const auto it = t.find(p_code);
    if (it == t.end()) {
        throw std::runtime_error("不认识的专业聚落代码 " + p_code);
    }
    return it->second;
}

std::string season_zh(const std::string &p_code) {
    static const std::map<std::string, std::string> t = [] {
        std::map<std::string, std::string> m = { { "hot", "热季" }, { "cold", "冷季" }, { "warm", "暖季" }, { "cool", "凉季" }, { "wet", "雨季" },
            { "dry", "旱季" }, { "turn", "转季" }, { "stormy", "风暴季" }, { "calm", "平静季" }, { "transition", "过渡季" }, { "season", "季" } };
        const char *const num[6] = { "一", "二", "三", "四", "五", "六" };
        for (int i = 0; i < 4; i++) {
            m["warm" + std::to_string(i + 1)] = std::string("暖季") + num[i];
            m["cold" + std::to_string(i + 1)] = std::string("冷季") + num[i];
        }
        for (int i = 0; i < 6; i++) {
            m["s" + std::to_string(i + 1)] = std::string("第") + num[i] + "季";
        }
        return m;
    }();
    const auto it = t.find(p_code);
    if (it == t.end()) {
        throw std::runtime_error("不认识的季名代码 " + p_code);
    }
    return it->second;
}

std::string pad_int(int64_t p_v, int p_width) {
    std::string s = std::to_string(p_v);
    while (int(s.size()) < p_width) {
        s = "0" + s;
    }
    return s;
}

// decode._name：村 / 散户 / 专业聚落的名字代码 → 中文（village:12 → 村012）
std::string name_zh(const std::string &p_code) {
    const size_t a = p_code.find(':');
    const std::string kind = p_code.substr(0, a);
    const std::string rest = a == std::string::npos ? std::string() : p_code.substr(a + 1);
    if (kind == "village") {
        return "村" + pad_int(std::stoll(rest), 3);
    }
    if (kind == "hamlet") {
        return "散户" + pad_int(std::stoll(rest), 3);
    }
    if (kind == "special") {
        const size_t b = rest.find(':');
        return special_zh(rest.substr(0, b)) + pad_int(std::stoll(rest.substr(b + 1)), 2);
    }
    if (p_code == "seat" || kind == "town") return p_code == "seat" ? "邑治" : "镇" + pad_int(std::stoll(rest), 2);
    return p_code;
}

// decode.settlements 的镇名
std::string town_zh(const std::string &p_code) {
    if (p_code == "seat") {
        return "邑治";
    }
    return "镇" + pad_int(std::stoll(p_code.substr(p_code.find(':') + 1)), 2);
}

const char *res_form_name(int p_kind) {
    switch (isl::res_form(p_kind)) {
        case isl::FORM_POINT:
            return "point";
        case isl::FORM_PATCH:
            return "patch";
        default:
            return "field";
    }
}


Json landscape_record(const Json &value) {
    if (value.type() == Json::OBJ) {
        Json out = Json::obj();
        for (const auto &kv : value.fields()) {
            if (kv.first != "note" && kv.first != "name" && kv.first != "summary") out.set(kv.first, landscape_record(kv.second));
        }
        std::sort(out.fields().begin(),out.fields().end(),[](const auto &a,const auto &b){return a.first<b.first;});
        return out;
    }
    if (value.type() == Json::ARR) {
        Json out = Json::arr();
        for (const Json &v : value.items()) out.push(landscape_record(v));
        return out;
    }
    return value;
}

// ---------------------------------------------------------------- 写二进制

void put_bytes(std::string &r_out, const void *p_data, size_t p_n) {
    r_out.append(reinterpret_cast<const char *>(p_data), p_n);
}

template <class T>
void put(std::string &r_out, T p_v) {
    put_bytes(r_out, &p_v, sizeof(T)); // 小端（x86 / ARM 都是）
}

// export_skyisle.to_game：skyisle 的 (x 东, y 北) 公里 → 游戏的 [x, z] 米（Python 浮点的 round）
Json to_game(double p_kx, double p_ky) {
    Json a = Json::arr();
    a.push(pyround(p_kx * 1000.0, 1));
    a.push(pyround(-p_ky * 1000.0, 1));
    return a;
}

std::string join_tab(const std::vector<std::string> &p_cols) {
    std::string s;
    for (size_t k = 0; k < p_cols.size(); k++) {
        if (k) {
            s += '\t';
        }
        s += p_cols[k];
    }
    return s;
}

// Python 的 str(v)：Json 里的数（整数 / 浮点）、字符串、布尔、None
std::string py_str(const Json &p_v) {
    switch (p_v.type()) {
        case Json::NUL:
            return "None";
        case Json::BOOL:
            return p_v.as_bool() ? "True" : "False";
        case Json::INT:
            return std::to_string(p_v.as_int());
        case Json::NUM:
            return pyfmt::repr(p_v.as_num());
        case Json::STR:
            return p_v.as_str();
        default:
            throw std::runtime_error("py_str：不支持数组 / 对象");
    }
}

bool has_key(const Json &p_obj, const char *p_key) {
    return p_obj.type() == Json::OBJ && p_obj.has(p_key);
}

const Json &field(const Json &p_obj, const char *p_key) {
    return p_obj.at(p_key);
}

} // namespace

// ================================================================ 流向（export_skyisle.flow_directions_from_acc）

namespace {
constexpr int FLOW_OFFSETS[8][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 } };
constexpr uint8_t FLOW_TO_VOID = 0x10;
} // namespace

std::vector<uint8_t> flow_directions_from_acc(const std::vector<double> &p_height, const std::vector<int32_t> &p_island, const std::vector<double> &p_acc,
        int p_h, int p_w, double p_res_m) {
    const size_t n = size_t(p_h) * size_t(p_w);
    const double cell_km2 = skyisle::c_pow(p_res_m / 1000.0, 2.0); // (res / 1000.0) ** 2：Python 浮点的幂 = C 的 pow
    const double eps = 0.25 * cell_km2;
    std::vector<double> room(n, 0.0);
    std::vector<uint8_t> fl(n, 0);
    std::vector<int64_t> order;
    order.reserve(n);
    for (size_t q = 0; q < n; q++) {
        if (p_island[q] >= 0) {
            room[q] = p_acc[q] - cell_km2;
            order.push_back(int64_t(q));
        }
    }
    // idx[np.argsort(-A[idx], kind="stable")]：汇流大的在前，一样大的按下标
    std::stable_sort(order.begin(), order.end(), [&](int64_t a, int64_t b) { return -p_acc[size_t(a)] < -p_acc[size_t(b)]; });
    struct Cand {
        double room_left, h;
        int code;
        int64_t r;
    };
    std::vector<Cand> cands;
    for (int64_t q : order) {
        const int i = int(q / p_w), j = int(q % p_w);
        int void_n = 0;
        cands.clear();
        bool has_spill = false;
        double spill_room = 0.0;
        int spill_code = 0;
        int64_t spill_r = 0;
        const double Aq = p_acc[size_t(q)];
        for (int nn = 0; nn < 8; nn++) {
            const int a = i + FLOW_OFFSETS[nn][0], b = j + FLOW_OFFSETS[nn][1];
            if (a < 0 || b < 0 || a >= p_h || b >= p_w || p_island[size_t(a) * size_t(p_w) + size_t(b)] < 0) {
                if (!void_n) {
                    void_n = nn + 1;
                }
                continue;
            }
            const int64_t r = int64_t(a) * p_w + b;
            if (p_island[size_t(r)] != p_island[size_t(q)]) {
                if (!void_n) {
                    void_n = nn + 1;
                }
                continue;
            }
            const double Ar = p_acc[size_t(r)];
            if (Ar > Aq) {
                if (room[size_t(r)] >= Aq - eps) {
                    cands.push_back(Cand{ room[size_t(r)] - Aq, p_height[size_t(r)], nn + 1, r });
                } else if (!has_spill || room[size_t(r)] > spill_room) {
                    has_spill = true;
                    spill_room = room[size_t(r)];
                    spill_code = nn + 1;
                    spill_r = r;
                }
            }
        }
        if (void_n) {
            fl[size_t(q)] = uint8_t(void_n | FLOW_TO_VOID);
            continue;
        }
        if (cands.empty()) {
            if (has_spill) {
                fl[size_t(q)] = uint8_t(spill_code);
                room[size_t(spill_r)] -= Aq;
            }
            continue;
        }
        // cands.sort()：元组按字典序
        const Cand *best = &cands[0];
        for (size_t k = 1; k < cands.size(); k++) {
            const Cand &c = cands[k];
            if (std::tie(c.room_left, c.h, c.code, c.r) < std::tie(best->room_left, best->h, best->code, best->r)) {
                best = &c;
            }
        }
        fl[size_t(q)] = uint8_t(best->code);
        room[size_t(best->r)] -= Aq;
    }
    return fl;
}

// ================================================================ 赋存区的长度与走向（resources._shape）

namespace {

// LAPACK 的 DLAEV2：2 × 2 对称阵 [[a, b], [b, c]] 的特征值（rt1 绝对值大）与 rt1 的单位特征向量 (cs1, sn1)
void dlaev2(double a, double b, double c, double &rt1, double &rt2, double &cs1, double &sn1) {
    const double sm = a + c;
    const double df = a - c;
    const double adf = std::fabs(df);
    const double tb = b + b;
    const double ab = std::fabs(tb);
    double acmx, acmn;
    if (std::fabs(a) > std::fabs(c)) {
        acmx = a;
        acmn = c;
    } else {
        acmx = c;
        acmn = a;
    }
    double rt;
    if (adf > ab) {
        const double q = ab / adf;
        rt = adf * std::sqrt(1.0 + q * q);
    } else if (adf < ab) {
        const double q = adf / ab;
        rt = ab * std::sqrt(1.0 + q * q);
    } else {
        rt = ab * std::sqrt(2.0);
    }
    int sgn1, sgn2;
    if (sm < 0.0) {
        rt1 = 0.5 * (sm - rt);
        sgn1 = -1;
        rt2 = (acmx / rt1) * acmn - (b / rt1) * b;
    } else if (sm > 0.0) {
        rt1 = 0.5 * (sm + rt);
        sgn1 = 1;
        rt2 = (acmx / rt1) * acmn - (b / rt1) * b;
    } else {
        rt1 = 0.5 * rt;
        rt2 = -0.5 * rt;
        sgn1 = 1;
    }
    double cs;
    if (df >= 0.0) {
        cs = df + rt;
        sgn2 = 1;
    } else {
        cs = df - rt;
        sgn2 = -1;
    }
    const double acs = std::fabs(cs);
    if (acs > ab) {
        const double ct = -tb / cs;
        sn1 = 1.0 / std::sqrt(1.0 + ct * ct);
        cs1 = ct * sn1;
    } else {
        if (ab == 0.0) {
            cs1 = 1.0;
            sn1 = 0.0;
        } else {
            const double tn = -cs / tb;
            cs1 = 1.0 / std::sqrt(1.0 + tn * tn);
            sn1 = tn * cs1;
        }
    }
    if (sgn1 == sgn2) {
        const double tn = cs1;
        cs1 = -sn1;
        sn1 = tn;
    }
}

// np.linalg.eigh（LAPACK DSYEVD，UPLO = 'L'）在 2 × 2 上：DSYTD2 什么都不做（反射向量长 1，tau = 0）→ DSTEDC → DSTEQR(COMPZ = 'I')。
// 返回升序的特征值 w 与列向量 V（V[行][列]）
void eigh2(double a, double b, double c, double w[2], double V[2][2]) {
    const double eps = std::ldexp(1.0, -53); // DLAMCH('E')
    const double eps2 = eps * eps;
    const double safmin = std::numeric_limits<double>::min(); // DLAMCH('S')
    double d[2] = { a, c };
    double e = b;
    double Z[2][2] = { { 1.0, 0.0 }, { 0.0, 1.0 } };
    // 分块：对角占优的小次对角元直接置 0
    bool split = false;
    {
        const double tst = std::fabs(e);
        if (tst == 0.0) {
            split = true;
        } else if (tst <= (std::sqrt(std::fabs(d[0])) * std::sqrt(std::fabs(d[1]))) * eps) {
            e = 0.0;
            split = true;
        }
    }
    if (!split) {
        const double anorm = std::max(std::max(std::fabs(d[0]), std::fabs(d[1])), std::fabs(e)); // DLANST('M')
        if (anorm != 0.0) {
            // QL（|d(lend)| ≥ |d(l)|）或 QR：两条路在 2 × 2 上只差小次对角元的判据里乘的次序
            bool small;
            if (std::fabs(d[1]) < std::fabs(d[0])) {
                small = std::fabs(e) * std::fabs(e) <= (eps2 * std::fabs(d[1])) * std::fabs(d[0]) + safmin;
            } else {
                small = std::fabs(e) * std::fabs(e) <= (eps2 * std::fabs(d[0])) * std::fabs(d[1]) + safmin;
            }
            if (!small) {
                double rt1, rt2, cs, sn;
                dlaev2(d[0], e, d[1], rt1, rt2, cs, sn);
                // DLASR('R', 'V', 'B' / 'F', 2, 2, c, s, Z)：一次平面旋转作用在第 1、2 列
                if (cs != 1.0 || sn != 0.0) {
                    for (int i = 0; i < 2; i++) {
                        const double temp = Z[i][1];
                        Z[i][1] = cs * temp - sn * Z[i][0];
                        Z[i][0] = sn * temp + cs * Z[i][0];
                    }
                }
                d[0] = rt1;
                d[1] = rt2;
            }
        }
    }
    // 升序（选择排序，换列）
    if (d[1] < d[0]) {
        std::swap(d[0], d[1]);
        std::swap(Z[0][0], Z[0][1]);
        std::swap(Z[1][0], Z[1][1]);
    }
    w[0] = d[0];
    w[1] = d[1];
    V[0][0] = Z[0][0];
    V[0][1] = Z[0][1];
    V[1][0] = Z[1][0];
    V[1][1] = Z[1][1];
}

} // namespace

void occurrence_shape(const std::vector<int32_t> &p_cells, int p_w, double p_res_km, double &r_length, double &r_axis, bool &r_has_axis) {
    const size_t n = p_cells.size();
    if (n < 3) {
        r_length = pyround(std::max(1.0, std::sqrt(double(n))) * p_res_km, 2);
        r_axis = 0.0;
        r_has_axis = false;
        return;
    }
    // np.cov(np.vstack([jj · res, −ii · res]))
    std::vector<double> x0(n), x1(n);
    for (size_t k = 0; k < n; k++) {
        const int64_t ii = p_cells[k] / p_w, jj = p_cells[k] % p_w;
        x0[k] = double(jj) * p_res_km;
        x1[k] = double(-ii) * p_res_km;
    }
    const double m0 = skyisle::np_sum(x0.data(), n) / double(n);
    const double m1 = skyisle::np_sum(x1.data(), n) / double(n);
    for (size_t k = 0; k < n; k++) {
        x0[k] -= m0;
        x1[k] -= m1;
    }
    // dot(X, X.T)：numpy 认出 A·Aᵀ 走 BLAS dsyrk；本机 OpenBLAS 在 2 × n（n ≤ 512 实测逐位）上是按 k 顺序 FMA
    double c00 = 0.0, c01 = 0.0, c11 = 0.0;
    for (size_t k = 0; k < n; k++) {
        c00 = std::fma(x0[k], x0[k], c00);
        c01 = std::fma(x0[k], x1[k], c01);
        c11 = std::fma(x1[k], x1[k], c11);
    }
    const double f = 1.0 / double(n - 1); // c *= np.true_divide(1, fact)
    c00 *= f;
    c01 *= f;
    c11 *= f;
    double w[2], V[2][2];
    eigh2(c00, c01, c11, w, V);
    const double length = std::max(4.0 * std::sqrt(std::max(w[1], 0.0)), p_res_km);
    r_length = pyround(length, 2);
    const double rad_to_deg = 180.0 / 3.141592653589793; // CPython 的 math.degrees = radToDeg · x
    r_axis = pyround(skyisle::pymod(rad_to_deg * std::atan2(V[1][1], V[0][1]), 180.0), 0);
    r_has_axis = true;
}

// ================================================================ 岛群包（export_skyisle.export）

namespace {

struct Raster {
    int rows = 0, cols = 0;
    double res = 0.0; // res_m（float）
    double ox_km = 0.0, oy_km = 0.0; // origin_km（round 3）
    double res_km() const { return res / 1000.0; }
    // 格心的 km（Python 浮点）
    double kx(int j) const { return ox_km + (double(j) + 0.5) * res_km(); }
    double ky(int i) const { return oy_km - (double(i) + 0.5) * res_km(); }
};

std::string terrain_bin(const isl::Group &g, const Raster &R, const std::vector<int32_t> &island, std::vector<double> &r_inside) {
    const int H = g.H, W = g.W;
    const size_t n = size_t(H) * size_t(W);
    std::vector<uint8_t> land(n), notland(n);
    for (size_t q = 0; q < n; q++) {
        land[q] = island[q] >= 0 ? 1 : 0;
        notland[q] = land[q] ? 0 : 1;
    }
    // terrain.npz 里 height / flowacc_km2 / river_width_m / river_depth_m 存 float32；读出来 astype(float64)
    std::vector<double> height(n), ref(n);
    for (size_t q = 0; q < n; q++) {
        height[q] = double(float(g.height.v[q]));
        ref[q] = double(float(g.acc_km2.v[q]));
    }
    const std::vector<uint8_t> flow = flow_directions_from_acc(height, island, ref, H, W, R.res);
    // 虚空格取最近陆地格：高程与岛号都外推
    std::vector<int32_t> ri, ci;
    scipy_nd::feature_transform(notland.data(), H, W, ri, ci);
    // 有符号岸距
    const std::vector<double> in_d = scipy_nd::distance_edt(land.data(), H, W);
    const std::vector<double> out_d = scipy_nd::distance_edt(notland.data(), H, W);
    r_inside.resize(n);
    for (size_t q = 0; q < n; q++) {
        r_inside[q] = in_d[q] * R.res;
    }
    std::string out;
    out.reserve(n * 30 + 64);
    out += "ZZWG";
    put<uint32_t>(out, 6u);
    put<uint32_t>(out, uint32_t(R.rows));
    put<uint32_t>(out, uint32_t(R.cols));
    put<double>(out, R.res);
    put<double>(out, R.ox_km * 1000.0 + 0.5 * R.res);
    put<double>(out, -R.oy_km * 1000.0 + 0.5 * R.res);
    for (size_t q = 0; q < n; q++) {
        const size_t src = size_t(ri[q]) * size_t(W) + size_t(ci[q]);
        put<float>(out, float(height[src]));
    }
    for (size_t q = 0; q < n; q++) {
        put<float>(out, float(g.coast_dist.v[q]));
    }
    for (size_t q = 0; q < n; q++) {
        const size_t src = size_t(ri[q]) * size_t(W) + size_t(ci[q]);
        put<uint8_t>(out, uint8_t(island[src]));
    }
    for (size_t q = 0; q < n; q++) {
        put<uint8_t>(out, g.landcover.v[q]);
    }
    for (size_t q = 0; q < n; q++) {
        put<uint8_t>(out, g.stream.v[q] > 0 ? 1 : 0);
    }
    put_bytes(out, flow.data(), n);
    for (size_t q = 0; q < n; q++) {
        put<float>(out, land[q] ? float(ref[q]) : 0.0f);
    }
    // np.where(land, f32, 0) * 10.0：float32 的乘法与 rint（NEP 50）
    for (size_t q = 0; q < n; q++) {
        const float w = land[q] ? float(g.width_m.v[q]) : 0.0f;
        float v = std::nearbyint(w * 10.0f);
        v = v < 0.0f ? 0.0f : (v > 65535.0f ? 65535.0f : v);
        put<uint16_t>(out, uint16_t(v));
    }
    for (size_t q = 0; q < n; q++) {
        const float d = land[q] ? float(g.depth_m.v[q]) : 0.0f;
        float v = std::nearbyint(d * 100.0f);
        v = v < 0.0f ? 0.0f : (v > 65535.0f ? 65535.0f : v);
        put<uint16_t>(out, uint16_t(v));
    }
    for (size_t q = 0; q < n; q++) {
        put<uint8_t>(out, land[q] ? g.zone.v[q] : uint8_t(0));
    }
    for (int f = 0; f < isl::FK_COUNT; f++) {
        for (size_t q = 0; q < n; q++) {
            put<uint8_t>(out, land[q] ? g.res_field[f].v[q] : uint8_t(0));
        }
    }
    for (size_t q = 0; q < n; q++) {
        const int32_t p = g.patch_id.v[q];
        put<uint16_t>(out, uint16_t(land[q] && p >= 0 ? p + 1 : 0));
    }
    for (const auto *v : { &g.cultivable, &g.cultivated, &g.fallow_years, &g.landcover_natural, &g.landuse, &g.lith, &g.cloud_forest, &g.rockwall_dir }) {
        for (size_t q = 0; q < n; q++) put<uint8_t>(out, v->v.empty() ? 0 : v->v[q]);
    }
    for (const auto *v : { &g.rockwall_m, &g.rain, &g.runoff, &g.bed_m, &g.wt, &g.condense }) {
        for (size_t q = 0; q < n; q++) put<float>(out, v->v.empty() ? 0.0f : float(v->v[q]));
    }
    for (size_t q = 0; q < n; q++) put<int32_t>(out, g.polder_id.v.empty() ? 0 : g.polder_id.v[q]);
    const auto &net = g.rnet;
    put<uint32_t>(out, uint32_t(net.segs.size()));
    for (const auto &s : net.segs) {
        for (int v : { s.island, s.down, s.join, int(s.exit), s.basin }) put<int32_t>(out, v);
        put<uint32_t>(out, uint32_t(s.n));
        for (int i = s.start; i < s.start + s.n; i++) {
            const double vals[20] = { double(net.cell[i] / W), double(net.cell[i] % W), double(net.level[i]), npround(net.acc[i], 2),
                npround(net.q_mean[i], 4), npround(net.q_bf[i], 4), npround(net.w[i], 2), npround(net.d[i], 2),
                npround(net.w_mean[i], 2), npround(net.d_mean[i], 2), npround(net.surf[i], 1), npround(net.bed[i], 1),
                npround(net.slope[i], 5), npround(net.d50_mm[i], 3), double(net.d50c[i]), double(net.planform[i]),
                npround(net.ssc[i], 0), npround(net.fp_l[i], 0), npround(net.fp_r[i], 0), double(net.confine[i]) };
            for (double v : vals) put<float>(out, float(v));
        }
    }
    put<uint32_t>(out, uint32_t(net.basins.size()));
    for (const auto &b : net.basins) {
        put<uint32_t>(out, uint32_t(b.index.size()));
        for (double v : b.index) put<float>(out, float(npround(v, 3)));
    }
    return out;
}

std::string weather_tsv(const isl::Group &g, const GroupMeta &m, const Raster &) {
    const isl::Climate &cl = g.clim;
    const double ref = cl.a_ref_h;
    const double rim = g.islands[0].rim_j;
    const double fog_rise = 120.0 + 0.25 * std::max(0.0, m.fog_surface_max_m - ref);
    std::vector<std::string> lines;
    lines.push_back("# 由 tools/export_skyisle.py 从 weather_y0.csv / climate.json 导出，别手改。year_days " + std::to_string(int64_t(cl.cal.year_days)) +
            "  precip_mm " + pyfmt::fmt_g(pyround(g.P_mm, 0)));
    lines.push_back("# calendar seasons " + std::to_string(cl.cal.seasons) + "  months_per_season " + std::to_string(cl.cal.months_per_season) +
            "  days_per_month " + std::to_string(int64_t(cl.cal.days_per_month)) + "  solstice_n " + pyfmt::fmt_g(cl.cal.offset));
    lines.push_back("# sun lat " + pyfmt::fmt_f(g.inp.lat, 4) + "  lon " + pyfmt::fmt_f(g.inp.lon, 4) + "  tilt " + pyfmt::fmt_g(m.tilt_deg));
    lines.push_back("# climate ref_m " + pyfmt::fmt_g(ref) + "  lapse " + pyfmt::fmt_g(m.lapse_c_per_km) + "  rim_m " + pyfmt::fmt_g(rim) + "  fog_rise_m " +
            pyfmt::fmt_f(fog_rise, 1) + "  snow_temp " + pyfmt::fmt_g(m.snow_temp_c));
    std::string names;
    for (size_t k = 0; k < cl.names.size(); k++) {
        names += (k ? "," : "") + season_zh(cl.names[k]);
    }
    lines.push_back("# season_names " + names);
    const isl::WeatherYear &y = g.weather;
    for (size_t d = 0; d < y.day.size(); d++) {
        // climate.set_weather 的逐日行（round(float, 1)、int(round(风向))），csv 里是 str() 的写法
        lines.push_back(join_tab({ std::to_string(d), pyfmt::repr(pyround(y.precip_mm[d], 1)), pyfmt::repr(pyround(y.temp_c[d], 1)),
                WEATHER_TYPES[int(y.type[d])], std::to_string(int64_t(pyround(y.wind_from_deg[d], 0))), pyfmt::repr(pyround(y.wind_ms[d], 1)),
                y.sailable[d] ? "1" : "0", std::to_string(y.storm_event[d]) }));
    }
    std::string out;
    for (const std::string &l : lines) {
        out += l;
        out += '\n';
    }
    return out;
}

Json palette_json() {
    Json p = Json::arr();
    for (const auto &c : LANDCOVER_PALETTE) {
        Json rgb = Json::arr();
        rgb.push(c[0]);
        rgb.push(c[1]);
        rgb.push(c[2]);
        p.push(rgb);
    }
    return p;
}

// export_skyisle.places：{id, island, pos} + 列出的字段（有才抄）
Json place_row(const Json &p_item, const std::vector<const char *> &p_fields, bool p_decode_name) {
    Json row = Json::obj();
    row.set("id", field(p_item, "id"));
    row.set("island", field(p_item, "island"));
    const Json &km = field(p_item, "km");
    row.set("pos", to_game(km.items()[0].as_num(), km.items()[1].as_num()));
    for (const char *name : p_fields) {
        if (!has_key(p_item, name)) {
            continue;
        }
        const Json &v = field(p_item, name);
        if (p_decode_name && (std::strcmp(name, "name") == 0 || std::strcmp(name, "settlement") == 0) && v.type() == Json::STR) {
            row.set(name, name_zh(v.as_str()));
        } else {
            row.set(name, v);
        }
    }
    return row;
}

} // namespace

GroupFiles group_files(const isl::Group &g, const GroupMeta &m) {
    GroupFiles F;
    Raster R;
    R.rows = g.H;
    R.cols = g.W;
    R.res = g.res_km * 1000.0; // _terrain_from 的 res_m_eff
    R.ox_km = pyround(g.x0, 3);
    R.oy_km = pyround(g.y0, 3);
    const size_t n = size_t(g.H) * size_t(g.W);
    std::vector<int32_t> island(n);
    for (size_t q = 0; q < n; q++) {
        island[q] = g.island_id.v[q];
    }

    // ---- terrain.bin
    std::vector<double> inside;
    F.terrain_bin = terrain_bin(g, R, island, inside);

    // ---- weather.tsv
    F.weather_tsv = weather_tsv(g, m, R);

    // ---- world.json
    int max_id = -1;
    for (int32_t v : island) {
        max_id = std::max(max_id, int(v));
    }
    std::vector<double> max_shore(size_t(max_id + 1), 0.0);
    std::vector<uint8_t> seen(size_t(max_id + 1), 0);
    for (size_t q = 0; q < n; q++) {
        const int k = island[q];
        if (k < 0) {
            continue;
        }
        if (!seen[size_t(k)] || inside[q] > max_shore[size_t(k)]) {
            max_shore[size_t(k)] = inside[q];
            seen[size_t(k)] = 1;
        }
    }
    Json world = Json::obj();
    {
        Json src = Json::obj();
        src.set("run", m.run);
        src.set("node", m.node);
        src.set("seed", int64_t(m.seed));
        src.set("generator", "skyisle_gen.island");
        src.set("cls_zh", CLASS_ZH[std::clamp(m.cls, 0, 3)]);
        src.set("lat", g.inp.lat);
        src.set("lon", g.inp.lon);
        world.set("source", src);
        Json grid = Json::obj();
        grid.set("rows", R.rows);
        grid.set("cols", R.cols);
        grid.set("res_m", R.res);
        grid.set("x0", R.ox_km * 1000.0 + 0.5 * R.res);
        grid.set("z0", -R.oy_km * 1000.0 + 0.5 * R.res);
        world.set("grid", grid);
        Json lc = Json::obj();
        Json classes = Json::arr();
        for (const char *c : LANDCOVER_CLASSES) {
            classes.push(c);
        }
        lc.set("classes", classes);
        lc.set("palette", palette_json());
        world.set("landcover", lc);
    }
    Json islands = Json::arr();
    for (size_t k = 0; k < g.islands.size(); k++) {
        const isl::IslandRec &r = g.islands[k];
        Json row = Json::obj();
        row.set("id", int64_t(k));
        row.set("center", to_game(r.cx_j, r.cy_j));
        row.set("area_km2", r.area_j);
        row.set("age", pyround(r.age, 3));
        row.set("age_zh", age_zh(r.kind));
        row.set("main", k == 0);
        row.set("rim_m", r.rim_j);
        row.set("peak_m", r.peak_j);
        row.set("surface_m", pyround(r.surface, 1));
        row.set("cliff_m", r.cliff_j);
        row.set("keel_m", r.keel_j);
        row.set("max_shore_m", pyround(k < max_shore.size() ? max_shore[k] : 0.0, 1));
        Json cores = Json::arr();
        for (const auto &c : r.cores) {
            Json core = Json::obj();
            core.set("pos", to_game(pyround(r.gcx+c.load_x,3), pyround(r.gcy+c.load_y,3)));
            core.set("area_km2", pyround(double(c.cells)*R.res_km()*R.res_km(),3));
            core.set("depth_m", pyround(m.core_root_ratio*c.mean_above,1));
            cores.push(core);
        }
        row.set("cores", cores);
        islands.push(row);
    }
    world.set("islands", islands);
    Json links = Json::arr();
    for (const isl::Link &l : g.links) {
        Json row = Json::obj();
        row.set("a", l.a);
        row.set("b", l.b);
        row.set("kind", "ferry");
        row.set("gap_km", pyround(l.gap, 3));
        links.push(row);
    }
    world.set("links", links);

    const Json &S = g.settle;
    // 村：生成器的村，镇的非农户住进它所在的村；专业聚落接着村号往下编
    Json villages = Json::arr();
    std::map<int64_t, size_t> by_id;
    std::map<std::string, int64_t> owner;
    for (const Json &v : field(S, "villages").items()) {
        Json row = place_row(v, { "name", "households", "seat", "elev_m", "landing" }, true);
        by_id[field(row, "id").as_int()] = villages.items().size();
        owner[field(row, "name").as_str()] = field(row, "id").as_int();
        villages.push(row);
    }
    for (const Json &t : field(S, "towns").items()) {
        Json &v = villages.items()[by_id.at(field(t, "village").as_int())];
        const int64_t market = has_key(t, "households_market") ? field(t, "households_market").as_int() : 0;
        v.set("households", field(v, "households").as_int() + market);
        const std::string tname = town_zh(field(t, "name").as_str());
        v.set("town", tname);
        owner[tname] = field(v, "id").as_int();
    }
    int64_t next_id = (by_id.empty() ? 0 : by_id.rbegin()->first) + 1;
    std::map<int64_t, int64_t> special_ids; // 生成器的专业聚落号 → 游戏的村号
    for (const Json &sp : field(S, "specials").items()) {
        Json row = Json::obj();
        row.set("id", next_id);
        row.set("island", field(sp, "island"));
        const Json &km = field(sp, "km");
        row.set("pos", to_game(km.items()[0].as_num(), km.items()[1].as_num()));
        const std::string nm = name_zh(field(sp, "name").as_str());
        row.set("name", nm);
        row.set("households", field(sp, "households"));
        row.set("special", special_zh(field(sp, "kind").as_str()));
        row.set("landing", has_key(sp, "landing") ? field(sp, "landing") : Json());
        row.set("resident", !has_key(sp,"occupancy") || field(sp,"occupancy").as_str()=="resident");
        row.set("home_village", has_key(sp,"home_village") ? field(sp,"home_village") : Json());
        villages.push(row);
        const bool resident=field(row,"resident").as_bool();
        const int64_t home=has_key(sp,"home_village") && field(sp,"home_village").type()!=Json::NUL ? field(sp,"home_village").as_int() : -1;
        owner[nm] = resident ? next_id : home;
        special_ids[field(sp, "id").as_int()] = resident ? next_id : home;
        next_id++;
    }
    // Permanent relay households join the same village stream and ledger.
    // Rotation-only beacons remain landmarks without an invented population.
    for (const Json &relay:field(S,"relays").items()) {
        if (field(relay,"occupancy").as_str()!="resident" || field(relay,"households").as_int()<=0) continue;
        Json row=place_row(relay,{"households","elev_m","landing"},false);
        row.set("id",next_id++);
        const int id=int(field(relay,"id").as_int());
        char suffix[32]; std::snprintf(suffix,sizeof(suffix),"%03d",id);
        row.set("name",std::string("驿站")+suffix);
        row.set("special","驿站");
        row.set("resident",true);
        row.set("relay",id);
        villages.push(row);
    }
    world.set("villages", villages);
    Json hamlets = Json::arr();
    for (const Json &v : field(S, "hamlets").items()) {
        hamlets.push(place_row(v, { "name", "households", "elev_m" }, true));
    }
    world.set("hamlets", hamlets);

    // 码头：从泊场推（docks_from_landings）
    Json docks = Json::arr();
    {
        const int H = g.H, W = g.W;
        std::vector<uint8_t> land(n);
        for (size_t q = 0; q < n; q++) {
            land[q] = island[q] >= 0 ? 1 : 0;
        }
        const std::vector<uint8_t> er = scipy_nd::binary_erosion_3x3(land.data(), H, W);
        const double res_km = R.res_km();
        struct Kept {
            int64_t island;
            double kx, ky;
            bool main;
            int64_t landing;
            std::vector<int64_t> serves;
        };
        std::vector<Kept> kept;
        std::vector<const Json *> lands;
        for (const Json &l : field(S, "landings").items()) {
            lands.push_back(&l);
        }
        auto is_main = [](const Json *l) { return has_key(*l, "main") && field(*l, "main").as_bool(); };
        std::stable_sort(lands.begin(), lands.end(), [&](const Json *a, const Json *b) {
            const bool ma = is_main(a), mb = is_main(b);
            if (ma != mb) {
                return ma; // (not main) 小的在前：主泊场在前
            }
            return field(*a, "id").as_int() < field(*b, "id").as_int();
        });
        for (const Json *lp : lands) {
            const Json &l = *lp;
            const int i = int(field(l, "cell").items()[0].as_int());
            const int j = int(field(l, "cell").items()[1].as_int());
            const bool main = is_main(lp);
            if (!main && inside[size_t(i) * size_t(W) + size_t(j)] - 0.5 * R.res > 1000.0) {
                continue;
            }
            const int64_t k = field(l, "island").as_int();
            // 同岛最近的岸缘格（np.where 的光栅次序、argmin 取第一个）
            int64_t best = -1, bi = 0, bj = 0;
            for (int a = 0; a < H; a++) {
                for (int b = 0; b < W; b++) {
                    const size_t q = size_t(a) * size_t(W) + size_t(b);
                    if (!(land[q] && !er[q] && island[q] == k)) {
                        continue;
                    }
                    const int64_t d2 = int64_t(a - i) * (a - i) + int64_t(b - j) * (b - j);
                    if (best < 0 || d2 < best) {
                        best = d2;
                        bi = a;
                        bj = b;
                    }
                }
            }
            const double kx = R.ox_km + (double(bj) + 0.5) * res_km;
            const double ky = R.oy_km - (double(bi) + 0.5) * res_km;
            int64_t own = -1;
            bool has_owner = false;
            if (has_key(l, "of") && field(l, "of").type() == Json::STR) {
                const auto it = owner.find(name_zh(field(l, "of").as_str()));
                if (it != owner.end()) {
                    own = it->second;
                    has_owner = true;
                }
            }
            Kept *near = nullptr;
            for (Kept &d : kept) {
                if (d.island == k && std::hypot(d.kx - kx, d.ky - ky) < 2.0) {
                    near = &d;
                    break;
                }
            }
            if (near) {
                if (has_owner && std::find(near->serves.begin(), near->serves.end(), own) == near->serves.end()) {
                    near->serves.push_back(own);
                }
                continue;
            }
            Kept d;
            d.island = k;
            d.kx = kx;
            d.ky = ky;
            d.main = main;
            d.landing = field(l, "id").as_int();
            if (has_owner) {
                d.serves.push_back(own);
            }
            kept.push_back(d);
        }
        for (size_t q = 0; q < kept.size(); q++) {
            const Kept &d = kept[q];
            Json row = Json::obj();
            row.set("id", int64_t(q) + 1);
            row.set("island", d.island);
            row.set("pos", to_game(d.kx, d.ky));
            row.set("main", d.main);
            row.set("landing", d.landing);
            Json serves = Json::arr();
            for (int64_t s : d.serves) {
                serves.push(s);
            }
            row.set("serves", serves);
            docks.push(row);
        }
    }
    world.set("docks", docks);
    Json outposts = Json::arr();
    for (const Json &o : field(S, "outposts").items()) {
        outposts.push(place_row(o, { "settlement", "households" }, true));
    }
    world.set("outposts", outposts);
    world.set("population", has_key(S, "population") ? field(S, "population") : Json());
    Json landscape = Json::obj();
    for (const char *key : {"towns", "boat_lines", "relays", "specials", "uses", "ruins", "land_tenure", "waterworks"})
        landscape.set(key, has_key(S,key) ? landscape_record(field(S,key)) : (std::string(key)=="waterworks" ? Json::obj() : Json::arr()));
    Json landforms = Json::arr();
    for (size_t i=0;i<g.landforms.size();i++) {
        landforms.push(isl::landform_json(g,g.landforms[i],int(i)));
    }
    landscape.set("landforms",landforms);
    Json springs=Json::arr();
    for (const auto &sl:g.springline) {
        Json v=Json::arr(), cell=Json::arr(); cell.push(sl.ci); cell.push(sl.cj);
        v.push(sl.island); v.push(cell); v.push(pyround(sl.q_ls,2)); v.push(pyround(sl.height_m,0));
        v.push(pyround(sl.length_km,2)); v.push(sl.fall); v.push(pyround(sl.aquifer_m,1)); v.push(sl.kind); springs.push(v);
    }
    landscape.set("springline",springs);
    Json falls=Json::arr();
    for (const auto &f:g.rnet.falls) {
        Json v=Json::obj(), cell=Json::arr(); cell.push(f.ci); cell.push(f.cj);
        v.set("seg",f.seg); v.set("idx",f.idx); v.set("cell",cell); v.set("kind",int(f.kind)); v.set("level",f.level);
        v.set("drop_m",pyround(f.drop_m,1)); v.set("length_m",pyround(f.length_m,0)); v.set("q_mean",pyround(f.q_mean,4)); falls.push(v);
    }
    landscape.set("falls",falls); world.set("landscape",landscape_record(landscape));
#ifdef _WIN32
    const bool crlf = true; // Python 的 Path.write_text 在 Windows 上把 \n 写成 \r\n
#else
    const bool crlf = false;
#endif
    F.world_json = pyfmt::json_dumps(world, 1, crlf);

    // ---- sites.tsv
    {
        std::string s = "# 由 tools/export_skyisle.py 从 world.json 导出，别手改。格式见那个脚本开头。\n";
        for (const Json &i : field(world, "islands").items()) {
            s += join_tab({ "island", py_str(field(i, "id")), py_str(field(i, "rim_m")), py_str(field(i, "cliff_m")), py_str(field(i, "keel_m")),
                         py_str(field(i, "max_shore_m")) }) +
                    "\n";
        }
        for (const Json &i : field(world,"islands").items()) for (const Json &c : field(i,"cores").items()) {
            const auto &pos=field(c,"pos").items();
            s+=join_tab({"core",py_str(field(i,"id")),py_str(pos[0]),py_str(pos[1]),py_str(field(c,"area_km2")),py_str(field(c,"depth_m"))})+"\n";
        }
        for (const Json &d : field(world, "docks").items()) {
            const Json &pos = field(d, "pos");
            s += join_tab({ "dock", py_str(field(d, "id")), py_str(field(d, "island")), py_str(pos.items()[0]), py_str(pos.items()[1]),
                         field(d, "main").as_bool() ? "1" : "0" }) +
                    "\n";
        }
        const std::pair<const char *, const char *> kinds[2] = { { "village", "villages" }, { "hamlet", "hamlets" } };
        for (const auto &kk : kinds) {
            for (const Json &v : field(world, kk.second).items()) {
                if (has_key(v,"resident") && !field(v,"resident").as_bool()) continue;
                const Json &pos = field(v, "pos");
                const bool seat = has_key(v, "seat") && field(v, "seat").type() == Json::BOOL && field(v, "seat").as_bool();
                s += join_tab({ kk.first, py_str(field(v, "id")), py_str(field(v, "island")), py_str(pos.items()[0]), py_str(pos.items()[1]),
                             py_str(field(v, "households")), seat ? "1" : "0", has_key(v, "name") ? py_str(field(v, "name")) : std::string() }) +
                        "\n";
            }
        }
        F.sites_tsv = s;
    }

    // ---- resources.tsv
    {
        const double res_km = R.res_km();
        auto cell_game = [&](int i, int j) { return to_game(R.ox_km + (double(j) + 0.5) * res_km, R.oy_km - (double(i) + 0.5) * res_km); };
        std::string s = "# 由 tools/export_skyisle.py 从 resources.json 导出，别手改。格式见那个脚本开头。\n";
        for (const isl::Deposit &d : g.res.deposits) {
            const Json xz = cell_game(d.ci, d.cj);
            s += join_tab({ "deposit", std::to_string(d.id), isl::res_key(d.kind), std::to_string(d.island), py_str(xz.items()[0]), py_str(xz.items()[1]),
                         pyfmt::repr(d.area_km2), grade_zh(d.grade), subtype_zh(d.subtype), res_form_name(d.kind) }) +
                    "\n";
        }
        std::map<int, std::string> occ_subtype;
        for (const isl::Occurrence &o : g.res.occ) {
            const Json xz = cell_game(o.ci, o.cj);
            double length = 0.0, axis = 0.0;
            bool has_axis = false;
            occurrence_shape(o.cells, g.W, g.res_km, length, axis, has_axis);
            occ_subtype[o.id] = subtype_zh(o.subtype);
            s += join_tab({ "occurrence", std::to_string(o.id), isl::res_key(o.kind), std::to_string(o.island), py_str(xz.items()[0]),
                         py_str(xz.items()[1]), pyfmt::repr(o.area_km2), grade_zh(o.grade), subtype_zh(o.subtype), pyfmt::repr(length),
                         has_axis ? pyfmt::repr(axis) : std::string("None") }) +
                    "\n";
        }
        for (const isl::Working &w : g.res.works) {
            const Json xz = cell_game(w.ci, w.cj);
            std::string users;
            for (int v : w.villages) {
                users += (users.empty() ? "" : ",") + std::to_string(v);
            }
            if (w.special >= 0 && special_ids.at(w.special)>=0 && std::find(w.villages.begin(),w.villages.end(),special_ids.at(w.special))==w.villages.end()) {
                users += (users.empty() ? "" : ",") + std::to_string(special_ids.at(w.special));
            }
            const auto it = occ_subtype.find(w.occurrence);
            s += join_tab({ "working", std::to_string(w.id), isl::res_key(w.kind), std::to_string(w.island), py_str(xz.items()[0]), py_str(xz.items()[1]),
                         std::to_string(w.occurrence), pyfmt::repr(w.grade), it == occ_subtype.end() ? std::string() : it->second,
                         users.empty() ? std::string("-") : users }) +
                    "\n";
        }
        F.resources_tsv = s;
    }
    return F;
}

// ================================================================ 粗版（export_planet.encode_lod / write_lods）

int lod_factor(double p_native_res_m, double p_lod_res_m) {
    const int f = int(pyround(p_lod_res_m / p_native_res_m, 0));
    return f < 1 ? 1 : f;
}

LodEntry lod_entry(int64_t p_node, const isl::Group &g, const isl::LodBlock &b) {
    LodEntry e;
    e.node = p_node;
    const double native = g.res_km * 1000.0;
    e.res_m = native * double(b.f);
    e.origin_x = pyround(g.x0, 3);
    e.origin_y = pyround(g.y0, 3);
    e.rows = b.H;
    e.cols = b.W;
    e.n_islands = int(g.islands.size());
    std::string &out = e.blob;
    for (const isl::IslandRec &r : g.islands) {
        put<float>(out, float(r.cx_j));
        put<float>(out, float(r.cy_j));
        put<float>(out, float(r.rim_j));
        put<float>(out, float(r.keel_j));
        put<float>(out, float(r.peak_j));
        put<float>(out, float(pyround(r.surface, 1)));
    }
    const size_t n = size_t(b.H) * size_t(b.W);
    std::vector<uint16_t> hgt(n);
    std::vector<uint8_t> packed(n), ii(n), is_land(n);
    for (size_t q = 0; q < n; q++) {
        float pk = b.peak[q];
        if (std::isnan(pk)) {
            pk = 0.0f; // np.nan_to_num(peak, nan=0.0)
        }
        float h = std::nearbyint(pk); // float32 的 np.round
        h = h < 0.0f ? 0.0f : (h > 65535.0f ? 65535.0f : h);
        hgt[q] = uint16_t(h);
        const uint8_t lc = b.landcover[q];
        const int16_t isl_id = b.island[q];
        ii[q] = uint8_t(isl_id < 0 ? 0 : (isl_id > 255 ? 255 : isl_id));
        is_land[q] = b.land[q] > 0 ? 1 : 0;
        uint8_t land4 = 0;
        if (is_land[q]) {
            double v = std::nearbyint(double(b.land[q]) / 17.0);
            v = v < 1.0 ? 1.0 : (v > 15.0 ? 15.0 : v);
            land4 = uint8_t(v);
        }
        packed[q] = uint8_t((std::min<uint8_t>(lc, 15) << 4) | land4);
    }
    size_t k = 0;
    while (k < n) {
        const bool v = is_land[k] != 0;
        size_t j = k;
        while (j < n && (is_land[j] != 0) == v && j - k < 32767) {
            j++;
        }
        const size_t cnt = j - k;
        put<uint16_t>(out, uint16_t((v ? 0x8000u : 0u) | unsigned(cnt)));
        if (v) {
            for (size_t q = k; q < j; q++) {
                put<uint16_t>(out, hgt[q]);
                put<uint8_t>(out, packed[q]);
                put<uint8_t>(out, ii[q]);
            }
        }
        k = j;
    }
    return e;
}

std::string lods_bin(const std::vector<LodEntry> &p_entries) {
    std::string out = "ZZLD";
    put<uint32_t>(out, 1u);
    put<uint32_t>(out, uint32_t(p_entries.size()));
    uint64_t off = 12 + uint64_t(p_entries.size()) * (4 + 4 * 3 + 2 * 4 + 8 + 4);
    for (const LodEntry &e : p_entries) {
        put<uint32_t>(out, uint32_t(e.node));
        put<float>(out, float(e.res_m));
        put<float>(out, float(e.origin_x));
        put<float>(out, float(e.origin_y));
        put<uint16_t>(out, uint16_t(e.rows));
        put<uint16_t>(out, uint16_t(e.cols));
        put<uint16_t>(out, uint16_t(e.n_islands));
        put<uint16_t>(out, 0);
        put<uint64_t>(out, off);
        put<uint32_t>(out, uint32_t(e.blob.size()));
        off += e.blob.size();
    }
    for (const LodEntry &e : p_entries) {
        out += e.blob;
    }
    return out;
}

// ================================================================ 逐日天气（export_planet.write_weather）

WeatherEntry weather_entry(int64_t p_node, const isl::Group &g, const GroupMeta &m) {
    WeatherEntry e;
    e.node = p_node;
    const isl::Climate &cl = g.clim;
    for (const std::string &c : cl.names) {
        e.season_names.push_back(season_zh(c));
    }
    e.seasons = cl.cal.seasons;
    e.months_per_season = cl.cal.months_per_season;
    e.days_per_month = cl.cal.days_per_month;
    e.day_offset_solstice_n = cl.cal.offset;
    e.year = g.year;
    e.ref_m = cl.a_ref_h;
    e.lapse = g.inp.lapse_c_per_km;
    e.rim_m = g.islands[0].rim_j;
    e.fog_rise_m = 120.0 + 0.25 * std::max(0.0, m.fog_surface_max_m - e.ref_m);
    e.snow_temp_c = m.snow_temp_c;
    e.precip_mm = pyround(g.P_mm, 0);
    e.precip_climate_mm = cl.a_precip_mm;
    const isl::WeatherYear &y = g.weather;
    for (size_t d = 0; d < y.day.size(); d++) {
        e.type.push_back(int(y.type[d]));
        e.sailable.push_back(y.sailable[d] ? 1 : 0);
        e.storm_event.push_back(int(y.storm_event[d]));
        e.wind_from_deg.push_back(int(pyround(y.wind_from_deg[d], 0)));
        e.precip_day.push_back(pyround(y.precip_mm[d], 1));
        e.temp_c.push_back(pyround(y.temp_c[d], 1));
        e.wind_ms.push_back(pyround(y.wind_ms[d], 1));
    }
    return e;
}

namespace {

int64_t tenths(double p_v) {
    return int64_t(std::nearbyint(p_v * 10.0)); // np.round(a * 10.0).astype(int64)
}

int64_t zigzag(int64_t p_v) {
    return p_v >= 0 ? 2 * p_v : -2 * p_v - 1;
}

void u16_planes(std::string &r_out, const std::vector<int64_t> &p_v) {
    for (int64_t v : p_v) {
        if (v < 0 || v >= 65536) {
            throw std::runtime_error("逐日天气：超出 u16");
        }
    }
    for (int64_t v : p_v) {
        r_out.push_back(char(uint8_t(v & 0xFF)));
    }
    for (int64_t v : p_v) {
        r_out.push_back(char(uint8_t((v >> 8) & 0xFF)));
    }
}

} // namespace

std::string weather_bin(const std::vector<WeatherEntry> &p_entries, const std::array<double, 3> &p_precip_formula, const std::string &p_source) {
    std::vector<const WeatherEntry *> entries;
    for (const WeatherEntry &e : p_entries) {
        entries.push_back(&e);
    }
    std::stable_sort(entries.begin(), entries.end(), [](const WeatherEntry *a, const WeatherEntry *b) { return a->node < b->node; });
    const uint32_t D = entries.empty() ? 336u : uint32_t(entries[0]->type.size());
    std::vector<std::string> season_sets;
    std::string index;
    std::string raw_types;
    std::vector<int64_t> precips, temps, wdirs, wspeeds;
    std::string storms;
    for (const WeatherEntry *ep : entries) {
        const WeatherEntry &e = *ep;
        if (e.type.size() != D) {
            throw std::runtime_error("逐日天气的天数不一样");
        }
        std::string names;
        for (size_t k = 0; k < e.season_names.size(); k++) {
            names += (k ? "," : "") + e.season_names[k];
        }
        size_t set = std::find(season_sets.begin(), season_sets.end(), names) - season_sets.begin();
        if (set == season_sets.size()) {
            season_sets.push_back(names);
        }
        put<uint32_t>(index, uint32_t(e.node));
        put<uint8_t>(index, uint8_t(set));
        put<uint8_t>(index, 0);
        put<uint16_t>(index, 0);
        put<float>(index, float(e.ref_m));
        put<float>(index, float(e.lapse));
        put<float>(index, float(e.rim_m));
        put<float>(index, float(pyround(e.fog_rise_m, 1)));
        put<float>(index, float(e.snow_temp_c));
        put<float>(index, float(e.precip_mm));
        put<float>(index, float(e.precip_climate_mm));
        int64_t prev_t = 0, prev_d = 0, prev_s = 0;
        for (size_t d = 0; d < D; d++) {
            const int t = e.type[d];
            raw_types.push_back(char(uint8_t(t | (e.sailable[d] << 4))));
            const int64_t p = tenths(e.precip_day[d]);
            if (t >= 2) {
                precips.push_back(p);
            } else if (p != 0) {
                throw std::runtime_error("逐日天气：晴、多云的日子有雨量");
            }
            if (t == 5 || t == 8) {
                storms.push_back(char(uint8_t(e.storm_event[d])));
            }
            const int64_t tt = tenths(e.temp_c[d]), dd = e.wind_from_deg[d], ss = tenths(e.wind_ms[d]);
            temps.push_back(zigzag(tt - prev_t));
            wdirs.push_back(zigzag(dd - prev_d));
            wspeeds.push_back(zigzag(ss - prev_s));
            prev_t = tt;
            prev_d = dd;
            prev_s = ss;
        }
    }
    std::string raw = raw_types;
    u16_planes(raw, precips);
    raw += storms;
    u16_planes(raw, temps);
    u16_planes(raw, wdirs);
    u16_planes(raw, wspeeds);
    // zlib.compress(raw, 9)：同一版 zlib（1.3.1）的 compress2
    uLongf packed_len = compressBound(uLong(raw.size()));
    std::string packed(size_t(packed_len), '\0');
    if (compress2(reinterpret_cast<Bytef *>(packed.data()), &packed_len, reinterpret_cast<const Bytef *>(raw.data()), uLong(raw.size()), 9) != Z_OK) {
        throw std::runtime_error("逐日天气：zlib 压缩失败");
    }
    packed.resize(size_t(packed_len));
    // 头
    int seasons = 4, mps = 3;
    double dpm = 28.0, offset = 126.0;
    int year = 0;
    if (!entries.empty()) {
        seasons = entries[0]->seasons;
        mps = entries[0]->months_per_season;
        dpm = entries[0]->days_per_month;
        offset = entries[0]->day_offset_solstice_n;
        year = entries[0]->year;
    }
    std::string types = "types ";
    for (int k = 0; k < 9; k++) {
        types += (k ? "," : "") + std::string(WEATHER_TYPES[k]);
    }
    std::vector<std::string> head = { types,
        "calendar " + std::to_string(seasons) + " " + std::to_string(mps) + " " + std::to_string(int64_t(pyround(dpm, 0))) + " " + pyfmt::fmt_g(offset),
        "year " + std::to_string(year),
        "precip_formula " + pyfmt::fmt_g(p_precip_formula[0]) + " " + pyfmt::fmt_g(p_precip_formula[1]) + " " + pyfmt::fmt_g(p_precip_formula[2]) };
    for (size_t k = 0; k < season_sets.size(); k++) {
        head.push_back("season_names " + std::to_string(k) + " " + season_sets[k]);
    }
    if (!p_source.empty()) {
        head.push_back("source " + p_source);
    }
    std::string head_b;
    for (size_t k = 0; k < head.size(); k++) {
        head_b += head[k];
        head_b += '\n';
    }
    std::string out = "ZZWX";
    put<uint32_t>(out, 1u);
    put<uint32_t>(out, D);
    put<uint32_t>(out, uint32_t(entries.size()));
    put<uint32_t>(out, uint32_t(head_b.size()));
    put<uint32_t>(out, uint32_t(packed.size()));
    put<uint32_t>(out, uint32_t(raw.size()));
    out += head_b;
    out += index;
    out += packed;
    return out;
}

// ================================================================ 行星包（export_planet.export_planet）

namespace {

// export_planet.as_column 之后的一列（或一张场）：类型码 0 f32、1 i32、2 i16、3 u8
struct Column {
    std::string name;
    int type = 0;
    std::string bytes;
};

Column f32_column(const std::string &p_name, const std::vector<double> &p_v) {
    Column c{ p_name, 0, std::string() };
    for (double v : p_v) {
        put<float>(c.bytes, float(v));
    }
    return c;
}

// 整数列：itemsize 1 且 ≥ 0 → u8；itemsize ≤ 2 → i16；否则 i32
template <class T>
Column int_column(const std::string &p_name, const std::vector<T> &p_v) {
    Column c{ p_name, 0, std::string() };
    int64_t lo = 0, hi = 0;
    for (size_t k = 0; k < p_v.size(); k++) {
        lo = k ? std::min<int64_t>(lo, int64_t(p_v[k])) : int64_t(p_v[k]);
        hi = k ? std::max<int64_t>(hi, int64_t(p_v[k])) : int64_t(p_v[k]);
    }
    if (sizeof(T) == 1 && lo >= 0 && hi <= 255) {
        c.type = 3;
        for (T v : p_v) {
            put<uint8_t>(c.bytes, uint8_t(v));
        }
    } else if (lo >= -32768 && hi <= 32767 && sizeof(T) <= 2) {
        c.type = 2;
        for (T v : p_v) {
            put<int16_t>(c.bytes, int16_t(v));
        }
    } else {
        c.type = 1;
        for (T v : p_v) {
            put<int32_t>(c.bytes, int32_t(v));
        }
    }
    return c;
}

Column bool_column(const std::string &p_name, const std::vector<uint8_t> &p_v) {
    Column c{ p_name, 3, std::string() };
    for (uint8_t v : p_v) {
        put<uint8_t>(c.bytes, v ? 1 : 0);
    }
    return c;
}

void put_name(std::string &r_out, const std::string &p_name) {
    put<uint8_t>(r_out, uint8_t(p_name.size()));
    r_out += p_name;
}

} // namespace

std::string planet_bin(const skyisle::planet::World &w, const skyisle::planet::Society &s, uint64_t p_seed, const std::string &p_source) {
    const skyisle::planet::Islands &I = w.islands;
    const skyisle::planet::Climate &C = w.climate;
    const skyisle::planet::Polity &P = s.polity;
    const size_t N = I.n();
    std::vector<Column> cols;
    // s03 islands.npz（savez 的次序；xyz 是二维的，跳过）
    cols.push_back(f32_column("s03.lat", I.lat));
    cols.push_back(f32_column("s03.lon", I.lon));
    cols.push_back(f32_column("s03.area_km2", I.area));
    cols.push_back(f32_column("s03.height_m", I.height));
    cols.push_back(f32_column("s03.territory_km2", I.territory));
    cols.push_back(f32_column("s03.land_frac", I.land_frac));
    cols.push_back(f32_column("s03.arable_frac", I.arable_frac));
    cols.push_back(f32_column("s03.main_frac", I.main_frac));
    cols.push_back(f32_column("s03.main_area_km2", I.main_area));
    cols.push_back(f32_column("s03.wall_m", I.wall));
    cols.push_back(f32_column("s03.age", I.age));
    cols.push_back(int_column("s03.plate", I.plate));
    cols.push_back(int_column("s03.cls", I.cls));
    cols.push_back(bool_column("s03.layered", I.layered));
    cols.push_back(f32_column("s03.density_at", I.density_at));
    cols.push_back(f32_column("s03.mean_nn_days", I.mean_nn));
    // s04 climate_islands.npz
    cols.push_back(f32_column("s04.precip", C.i_precip));
    cols.push_back(f32_column("s04.temp", C.i_temp));
    cols.push_back(f32_column("s04.storm", C.i_storm));
    cols.push_back(f32_column("s04.stability", C.i_stability));
    cols.push_back(f32_column("s04.window", C.i_window));
    cols.push_back(f32_column("s04.catch", C.i_catch));
    cols.push_back(bool_column("s04.has_river", C.i_has_river));
    cols.push_back(f32_column("s04.river_size", C.i_river_size));
    cols.push_back(f32_column("s04.temp_sea", C.i_temp_sea));
    cols.push_back(f32_column("s04.season_range_sea", C.i_season_range_sea));
    cols.push_back(f32_column("s04.season_range", C.i_season_range));
    cols.push_back(f32_column("s04.temp_winter", C.i_temp_winter));
    cols.push_back(f32_column("s04.temp_summer", C.i_temp_summer));
    cols.push_back(f32_column("s04.precip_mm", C.i_precip_mm));
    cols.push_back(f32_column("s04.precip_eff_mm", C.i_precip_eff_mm));
    cols.push_back(f32_column("s04.arable_frac_eff", C.i_arable_frac_eff));
    // s09 polity.npz（pop state polity kind realm circle fief）
    cols.push_back(f32_column("s09.pop", P.pop));
    cols.push_back(int_column("s09.state", P.state));
    cols.push_back(int_column("s09.polity", P.polity));
    cols.push_back(int_column("s09.kind", P.kind));
    cols.push_back(int_column("s09.realm", P.realm));
    cols.push_back(int_column("s09.circle", P.circle));
    cols.push_back(int_column("s09.fief", P.fief));
    // 粗版量的（新建世界时还没有粗版）
    const std::vector<double> nan(N, std::numeric_limits<double>::quiet_NaN());
    cols.push_back(f32_column("lod.peak_m", nan));
    cols.push_back(f32_column("lod.rim_m", nan));
    cols.push_back(f32_column("lod.violation_km", nan));
    cols.push_back(bool_column("lod.constrained", std::vector<uint8_t>(N, 0)));
    cols.push_back(f32_column("lod.extent_km", nan));
    for (const Column &c : cols) {
        if (c.bytes.size() != N * (c.type == 0 || c.type == 1 ? 4 : (c.type == 2 ? 2 : 1))) {
            throw std::runtime_error("行星包：列 " + c.name + " 的长度不对");
        }
    }

    // 场：plates、climate_grid、wind_local、band_local（npz 里的次序）
    struct Grid {
        Column col;
        int rows, cols;
        double lat0, lon0, dlat, dlon;
    };
    std::vector<Grid> grids;
    auto geo = [](const skyisle::planet::Axes &a, Column &&c) {
        return Grid{ std::move(c), a.nlat, a.nlon, a.lats[0], a.lons[0], a.lats[1] - a.lats[0], a.lons[1] - a.lons[0] };
    };
    const skyisle::planet::Axes &pa = I.ax;
    grids.push_back(geo(pa, int_column("s03.plates.plate_id", I.plate_id)));
    grids.push_back(geo(pa, int_column("s03.plates.btype", I.btype)));
    grids.push_back(geo(pa, f32_column("s03.plates.boundary_kernel", I.boundary_kernel)));
    grids.push_back(geo(pa, f32_column("s03.plates.conv_kernel", I.conv_kernel)));
    grids.push_back(geo(pa, f32_column("s03.plates.age", I.plate_age)));
    grids.push_back(geo(pa, f32_column("s03.plates.factor", I.factor)));
    const skyisle::planet::Axes &ca = C.ax;
    grids.push_back(geo(ca, f32_column("s04.climate_grid.precip", C.precip)));
    grids.push_back(geo(ca, f32_column("s04.climate_grid.temp", C.temp)));
    grids.push_back(geo(ca, f32_column("s04.climate_grid.storm", C.storm)));
    grids.push_back(geo(ca, f32_column("s04.climate_grid.storm_no_g", C.storm_no_g)));
    grids.push_back(geo(ca, f32_column("s04.climate_grid.stability", C.stability)));
    grids.push_back(geo(ca, f32_column("s04.climate_grid.window", C.window)));
    grids.push_back(geo(ca, f32_column("s04.climate_grid.q", C.q)));
    grids.push_back(geo(ca, f32_column("s04.climate_grid.uplift", C.uplift)));
    grids.push_back(geo(ca, f32_column("s04.climate_grid.conv", C.conv)));
    grids.push_back(geo(ca, f32_column("s04.climate_grid.eps", C.eps)));
    grids.push_back(geo(ca, f32_column("s04.climate_grid.season_range", C.season_range)));
    grids.push_back(geo(ca, f32_column("s04.climate_grid.continentality", C.continentality)));
    grids.push_back(geo(ca, f32_column("s04.climate_grid.lift", C.lift)));
    grids.push_back(geo(ca, f32_column("s04.wind_local.u", C.u)));
    grids.push_back(geo(ca, f32_column("s04.wind_local.v", C.v)));
    grids.push_back(geo(ca, f32_column("s04.wind_local.u_bg", C.u_bg)));
    grids.push_back(geo(ca, f32_column("s04.wind_local.v_bg", C.v_bg)));
    grids.push_back(geo(ca, f32_column("s04.wind_local.v_local", C.v_local)));
    grids.push_back(geo(ca, f32_column("s04.wind_local.obstacle", C.obstacle)));
    grids.push_back(geo(ca, f32_column("s04.wind_local.land", C.land)));
    grids.push_back(geo(ca, f32_column("s04.wind_local.wake", C.wake)));
    grids.push_back(geo(ca, f32_column("s04.wind_local.lat_eff", C.lat_eff)));
    grids.push_back(geo(ca, int_column("s04.wind_local.band", C.band)));
    // 带界 edges / dphi [8, nlon]：行是第几条带界（lats 缺省按 np.arange(−89.5, 90, 1)，形状对不上走「按经度」那一支）
    grids.push_back(Grid{ f32_column("s04.band_local.edges", C.edges), 8, ca.nlon, 0.0, ca.lons[0], 1.0, ca.lons[1] - ca.lons[0] });
    grids.push_back(Grid{ f32_column("s04.band_local.dphi", C.dphi), 8, ca.nlon, 0.0, ca.lons[0], 1.0, ca.lons[1] - ca.lons[0] });

    // 头文本
    const skyisle::planet::Planet &pl = w.planet;
    const double dps = pl.cal.present ? pl.cal.days_per_season : 84.0;
    const int mps = pl.cal.present ? pl.cal.months_per_season : 3;
    const int seasons = pl.cal.present ? pl.cal.seasons : 4;
    const int64_t ydays = pl.cal.present ? int64_t(pl.cal.year_days_solar) : 336;
    std::vector<std::string> head = {
        "radius_km " + pyfmt::repr(pl.radius_km),
        "rotation_hr " + pyfmt::repr(pl.rotation_period_hr),
        "tilt_deg " + pyfmt::repr(pl.axial_tilt_deg),
        "calendar " + std::to_string(ydays) + " " + std::to_string(seasons) + " " + std::to_string(mps) + " " + std::to_string(int64_t(pyround(dps / mps, 0))) +
                " " + pyfmt::repr(1.5 * dps),
        "bands " + pyfmt::repr(pl.eq_storm_top) + " " + pyfmt::repr(pl.trades_top) + " " + pyfmt::repr(pl.calm_top) + " " + pyfmt::repr(pl.westerlies_top),
        "g_storm " + pyfmt::repr(w.winds.g.lat) + " " + pyfmt::repr(w.winds.g.lon) + " " + pyfmt::repr(w.winds.g.radius_deg),
        "source " + p_source,
    };
    if (pl.vert.present) {
        const auto &v = pl.vert;
        head.push_back("vertical " + pyfmt::repr(v.sea_level_m) + " " + pyfmt::repr(v.cloud_base_m) + " " + pyfmt::repr(v.cloud_top_m) + " " +
            pyfmt::repr(v.keel_gap_m) + " " + pyfmt::repr(v.datum_pressure_atm) + " " + pyfmt::repr(v.scale_height_km) + " " + pyfmt::repr(v.o2_fraction));
    }
    std::string head_b;
    for (size_t k = 0; k < head.size(); k++) {
        head_b += head[k];
        head_b += '\n';
    }
    std::string out = "ZZPL";
    put<uint32_t>(out, 1u);
    put<uint32_t>(out, uint32_t(p_seed));
    put<uint32_t>(out, uint32_t(N));
    put<uint32_t>(out, uint32_t(cols.size()));
    put<uint32_t>(out, uint32_t(grids.size()));
    put<uint32_t>(out, uint32_t(head_b.size()));
    out += head_b;
    for (const Column &c : cols) {
        put_name(out, c.name);
        put<uint8_t>(out, uint8_t(c.type));
        out += c.bytes;
    }
    for (const Grid &g : grids) {
        put_name(out, g.col.name);
        put<uint16_t>(out, uint16_t(g.rows));
        put<uint16_t>(out, uint16_t(g.cols));
        put<float>(out, float(g.lat0));
        put<float>(out, float(g.lon0));
        put<float>(out, float(g.dlat));
        put<float>(out, float(g.dlon));
        put<uint8_t>(out, uint8_t(g.col.type));
        out += g.col.bytes;
    }
    return out;
}

} // namespace gen_export
