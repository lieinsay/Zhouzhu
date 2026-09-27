#include "planet_globe.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/light3d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "atmosphere.h"
#include "far_groups.h"
#include "job_pool.h"
#include "planet_jobs.h"
#include "planet_sky.h"
#include "planet_surface.h"
#include "planet_view.h"
#include "weather.h"
#include "world.h"
#include "world_clock.h"

using namespace godot;

namespace {

constexpr const char *OVERLAY_SHADER = "res://shaders/planet_overlay.gdshader";
constexpr const char *DECK_FADE_SHADER = "res://shaders/cloud_deck_fade.gdshader";
constexpr double PI = 3.14159265358979323846;
constexpr double FACE_PRIORITY = 1.0e6; // 排在 600 km 内的远处岛群之后
constexpr double MASK_INSET_M = 40000.0; // 贴图岛群在远处岛群的半径往里这么多开始淡掉
constexpr double HIDE_HYSTERESIS_M = 20000.0;
// 生成器 ④ 的带界随太阳摆动（island/climate.py：Δφ = k_shift × 倾角 × A_sea × cos(相位 − 滞后)，海洋混合层 τ = 110 天）
constexpr double BAND_K_SHIFT = 0.35;
constexpr double BAND_TAU_DAYS = 110.0;

double smooth01(double p_e0, double p_e1, double p_x) {
    const double t = std::clamp((p_x - p_e0) / (p_e1 - p_e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

Vector3 v3(const PlanetVec &p_v) {
    return Vector3(real_t(p_v.x), real_t(p_v.y), real_t(p_v.z));
}

struct FaceResult : JobResult {
    int face = 0;
    Ref<Image> color, extra;
    SurfaceBakeStats stats;
};

struct LutResult : JobResult {
    SkyLut trans, ms;
    double ms_time = 0.0;
};

// 行星天气场的贴图（行星计划 P5）：各格的群与权（静的，分几段备）/ 某一刻的四个通道
struct WeatherGridResult : JobResult {
    double ms = 0.0;
};

struct WeatherBakeResult : JobResult {
    std::vector<float> rgba;
    double day = 0.0;
    uint64_t rev = 0;
    double ms = 0.0;
};

constexpr uint64_t WX_GRID_ID = 200; // 200 + 段号（分 WX_GRID_BANDS 段）
constexpr int WX_GRID_BANDS = 8;
constexpr uint64_t WX_BAKE_ID = 300;
// 排在近处的地面块、600 km 内远处的岛群之后（生存世界刚进来时，远处的替身先画出来）
constexpr double WX_PRIORITY = FACE_PRIORITY - 1000.0;

uint64_t face_key(int p_face) {
    return JobPool::key(JobKind::PLANET, uint64_t(p_face));
}

uint64_t lut_key() {
    return JobPool::key(JobKind::PLANET, 100);
}

double ms_since(const std::chrono::steady_clock::time_point &p_t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p_t0).count();
}

// 一面做成带 mipmap 的纹理图，压成 BC3（DXT5，四比一）：2048² 的两张六面不压要 270 MB 显存（Windows 上还一样多地记进进程的提交内存），
// 压了约 70 MB。自己压（planet_surface 的 bc3_encode_image）：Godot 运行时的 Image::compress 走 GPU 压缩器，要另开一个渲染设备、常驻几百兆。
// 建纹理时进程的提交内存还会多出约「不压的大小」那么多（引擎 / 驱动那边的，与压不压无关），所以缓变的那张再减到半边长
Ref<Image> image_bc3(int p_size, const std::vector<uint8_t> &p_bytes, bool p_half) {
    PackedByteArray bytes;
    if (p_half) {
        // 缓变的那张（高、灯、林、枯黄）：2 × 2 取平均减到半边长（预乘过份额，平均是对的）
        const int h = p_size / 2;
        bytes.resize(int64_t(h) * h * 4);
        uint8_t *o = bytes.ptrw();
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < h; x++) {
                for (int c = 0; c < 4; c++) {
                    auto at = [&](int yy, int xx) { return int(p_bytes[(size_t(yy) * size_t(p_size) + size_t(xx)) * 4 + size_t(c)]); };
                    o[(size_t(y) * size_t(h) + size_t(x)) * 4 + size_t(c)] = uint8_t((at(2 * y, 2 * x) + at(2 * y, 2 * x + 1) + at(2 * y + 1, 2 * x) + at(2 * y + 1, 2 * x + 1) + 2) / 4);
                }
            }
        }
        p_size = h;
    } else {
        bytes.resize(int64_t(p_bytes.size()));
        std::memcpy(bytes.ptrw(), p_bytes.data(), p_bytes.size());
    }
    Ref<Image> src = Image::create_from_data(p_size, p_size, false, Image::FORMAT_RGBA8, bytes);
    src->generate_mipmaps();
    Ref<Image> dst = Image::create_empty(p_size, p_size, true, Image::FORMAT_DXT5);
    PackedByteArray out = dst->get_data();
    const PackedByteArray in = src->get_data();
    const int levels = std::min(dst->get_mipmap_count(), src->get_mipmap_count());
    for (int k = 0; k <= levels; k++) {
        const int w = std::max(1, p_size >> k);
        const int64_t so = k == 0 ? 0 : src->get_mipmap_offset(k);
        const int64_t dof = k == 0 ? 0 : dst->get_mipmap_offset(k);
        bc3_encode_image(in.ptr() + so, w, w, out.ptrw() + dof);
    }
    dst->set_data(p_size, p_size, true, Image::FORMAT_DXT5, out);
    return dst;
}

Ref<ImageTexture> float_texture(int p_w, int p_h, const std::vector<float> &p_rgba, bool p_half) {
    PackedByteArray bytes;
    bytes.resize(int64_t(p_rgba.size() * sizeof(float)));
    std::memcpy(bytes.ptrw(), p_rgba.data(), p_rgba.size() * sizeof(float));
    Ref<Image> image = Image::create_from_data(p_w, p_h, false, Image::FORMAT_RGBAF, bytes);
    if (p_half) {
        image->convert(Image::FORMAT_RGBAH);
    }
    return ImageTexture::create_from_image(image);
}

Ref<ImageTexture> lut_texture(const SkyLut &p_lut) {
    std::vector<float> rgba(size_t(p_lut.width) * size_t(p_lut.height) * 4, 1.0f);
    for (size_t k = 0; k < size_t(p_lut.width) * size_t(p_lut.height); k++) {
        for (int c = 0; c < 3; c++) {
            rgba[k * 4 + size_t(c)] = p_lut.rgb[k * 3 + size_t(c)];
        }
    }
    return float_texture(p_lut.width, p_lut.height, rgba, false);
}

// 行星贴图的一面（后台）：烘好、做成两张带 mipmap 的 RGBA8
void submit_face(JobPool &p_pool, std::shared_ptr<const PlanetData> p_data, int p_size, int p_face) {
    p_pool.submit(face_key(p_face), FACE_PRIORITY + p_face, [p_data, p_size, p_face]() -> std::unique_ptr<JobResult> {
        std::unique_ptr<FaceResult> out = std::make_unique<FaceResult>();
        out->face = p_face;
        SurfaceBakeRules rules;
        rules.size = p_size;
        SurfaceFace face;
        const auto t0 = std::chrono::steady_clock::now();
        bake_surface_face(*p_data, p_face, rules, face, &out->stats);
        out->color = image_bc3(p_size, face.color, false);
        out->extra = image_bc3(p_size, face.extra, true);
        out->stats.ms = ms_since(t0); // 含做 mipmap
        return out;
    });
}

} // namespace

void PlanetGlobe::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_surface_ready"), &PlanetGlobe::is_surface_ready);
    ClassDB::bind_method(D_METHOD("is_ready"), &PlanetGlobe::is_ready);
    ClassDB::bind_method(D_METHOD("get_state"), &PlanetGlobe::get_state);
    ClassDB::bind_method(D_METHOD("weather_texel", "lat", "lon"), &PlanetGlobe::weather_texel);
    ClassDB::bind_method(D_METHOD("set_planet_path", "value"), &PlanetGlobe::set_planet_path);
    ClassDB::bind_method(D_METHOD("get_planet_path"), &PlanetGlobe::get_planet_path);
    ClassDB::bind_method(D_METHOD("set_atmosphere_path", "value"), &PlanetGlobe::set_atmosphere_path);
    ClassDB::bind_method(D_METHOD("get_atmosphere_path"), &PlanetGlobe::get_atmosphere_path);
    ClassDB::bind_method(D_METHOD("set_map_path", "value"), &PlanetGlobe::set_map_path);
    ClassDB::bind_method(D_METHOD("get_map_path"), &PlanetGlobe::get_map_path);
    ClassDB::bind_method(D_METHOD("set_far_groups_path", "value"), &PlanetGlobe::set_far_groups_path);
    ClassDB::bind_method(D_METHOD("get_far_groups_path"), &PlanetGlobe::get_far_groups_path);
    ClassDB::bind_method(D_METHOD("set_surface_size", "value"), &PlanetGlobe::set_surface_size);
    ClassDB::bind_method(D_METHOD("get_surface_size"), &PlanetGlobe::get_surface_size);
    ClassDB::bind_method(D_METHOD("set_sun_power", "value"), &PlanetGlobe::set_sun_power);
    ClassDB::bind_method(D_METHOD("get_sun_power"), &PlanetGlobe::get_sun_power);
    ClassDB::bind_method(D_METHOD("set_sky_power", "value"), &PlanetGlobe::set_sky_power);
    ClassDB::bind_method(D_METHOD("get_sky_power"), &PlanetGlobe::get_sky_power);
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "planet_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "PlanetView"), "set_planet_path", "get_planet_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "atmosphere_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Atmosphere"), "set_atmosphere_path", "get_atmosphere_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "map_path"), "set_map_path", "get_map_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "far_groups_path"), "set_far_groups_path", "get_far_groups_path");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "surface_size", PROPERTY_HINT_RANGE, "256,4096,256"), "set_surface_size", "get_surface_size");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sun_power", PROPERTY_HINT_RANGE, "0,10,0.01"), "set_sun_power", "get_sun_power");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sky_power", PROPERTY_HINT_RANGE, "0,50,0.1"), "set_sky_power", "get_sky_power");
}

void PlanetGlobe::_notification(int p_what) {
    if (p_what == NOTIFICATION_EXIT_TREE) {
        if (pool) {
            for (int f = 0; f < 6; f++) {
                pool->cancel(face_key(f));
            }
            pool->cancel(lut_key());
            for (int b = 0; b < WX_GRID_BANDS; b++) {
                pool->cancel(JobPool::key(JobKind::PLANET, WX_GRID_ID + uint64_t(b)));
            }
            pool->cancel(JobPool::key(JobKind::PLANET, WX_BAKE_ID));
        }
    }
}

void PlanetGlobe::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        return;
    }
    set_process_priority(120); // PlanetView（100）、FarGroups（110）、Atmosphere 之后
}

bool PlanetGlobe::setup() {
    view = Object::cast_to<PlanetView>(get_node_or_null(planet_path));
    atmosphere = Object::cast_to<Atmosphere>(get_node_or_null(atmosphere_path));
    if (PlanetJobs *jobs = PlanetJobs::find(this)) {
        pool = jobs->share_pool();
    } else if (GroupWorld *w = Object::cast_to<GroupWorld>(get_node_or_null(map_path))) {
        pool = w->share_pool();
    }
    if (FarGroups *fg = Object::cast_to<FarGroups>(get_node_or_null(far_groups_path))) {
        far_groups_id = fg->get_instance_id();
    }
    if (!view || !view->is_active() || !atmosphere || !pool) {
        return false;
    }
    sky_mat = atmosphere->get_sky_material();
    build_fields();
    build_overlay();
    start_jobs();
    return true;
}

void PlanetGlobe::build_fields() {
    const PlanetData *pd = view->get_planet();
    // 1° 场：第 0 行在南、格心对齐，与纹理的第 0 行一致（着色器按经纬度取，经度绕回）
    auto grid = [&](const char *p_name) { return pd->grid(p_name); };
    const PlanetGrid *g[8] = { grid("s04.climate_grid.precip"), grid("s04.climate_grid.storm"), grid("s04.climate_grid.q"), grid("s04.climate_grid.uplift"),
        grid("s04.wind_local.u"), grid("s04.wind_local.v"), grid("s04.climate_grid.temp"), grid("s04.climate_grid.season_range") };
    const float fallback[8] = { 0.35f, 0.25f, 0.55f, 0.05f, 0.0f, 0.0f, 12.0f, 20.0f };
    const int W = 360, H = 180;
    std::vector<float> a(size_t(W) * H * 4), b(size_t(W) * H * 4);
    for (int r = 0; r < H; r++) {
        for (int c = 0; c < W; c++) {
            const size_t k = (size_t(r) * W + size_t(c)) * 4;
            for (int j = 0; j < 8; j++) {
                const PlanetGrid *pg = g[j];
                const float v = pg && pg->rows == H && pg->cols == W ? pg->at(r, c) : fallback[j];
                (j < 4 ? a : b)[k + size_t(j % 4)] = std::isfinite(v) ? v : fallback[j];
            }
        }
    }
    fields_tex = float_texture(W, H, a, true);
    fields2_tex = float_texture(W, H, b, true);
    // 带界：两行，第 0 行北半球四条（赤道永暴、信风、无风、西风的顶）、第 1 行南半球四条（取绝对值）
    const PlanetGrid *edges = grid("s04.band_local.edges");
    std::vector<float> e(size_t(W) * 2 * 4);
    for (int c = 0; c < W; c++) {
        for (int j = 0; j < 8; j++) {
            float v = float(pd->band_tops_deg[j % 4]);
            if (edges && edges->rows == 8 && edges->cols == W) {
                v = std::abs(edges->at(j, c));
            }
            e[(size_t(j / 4) * W + size_t(c)) * 4 + size_t(j % 4)] = v;
        }
    }
    bands_tex = float_texture(W, 2, e, true);
}

void PlanetGlobe::build_overlay() {
    const Ref<Shader> shader = ResourceLoader::get_singleton()->load(OVERLAY_SHADER);
    if (shader.is_valid()) {
        overlay_mat.instantiate();
        overlay_mat->set_shader(shader);
        overlay_mat->set_render_priority(Material::RENDER_PRIORITY_MAX); // 透明的里面最后画
        Ref<QuadMesh> quad;
        quad.instantiate();
        quad->set_size(Vector2(1, 1));
        quad->set_material(overlay_mat);
        overlay = memnew(MeshInstance3D);
        overlay->set_name("CloudOverlay");
        overlay->set_mesh(quad);
        overlay->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        overlay->set_custom_aabb(AABB(Vector3(-1e5f, -1e5f, -1e5f), Vector3(2e5f, 2e5f, 2e5f))); // 顶点着色器直接写裁剪坐标：包围盒只要永远在视锥里
        overlay->set_as_top_level(true);
        overlay->set_visible(false);
        add_child(overlay);
    } else {
        ERR_PRINT(String(U"PlanetGlobe：读不了云层叠画的着色器 ") + OVERLAY_SHADER + String(U"，飞到云层以上看不见云"));
    }
    const Ref<Shader> deck_shader = ResourceLoader::get_singleton()->load(DECK_FADE_SHADER);
    if (deck_shader.is_valid()) {
        deck_fade_mat.instantiate();
        deck_fade_mat->set_shader(deck_shader);
    }
}

void PlanetGlobe::start_jobs() {
    if (!pool || bake.started) {
        return;
    }
    bake.started = true;
    bake.start_usec = Time::get_singleton()->get_ticks_usec();
    bake.color.resize(6);
    bake.extra.resize(6);
    bake.lod_gen = view->get_planet()->lod_generation();
    // 大气的两张表（几百毫秒，只算一次；重烘贴图时不再算）
    if (!luts_ready && !bake.rebaking) {
        pool->submit(lut_key(), 0.0, []() -> std::unique_ptr<JobResult> {
            std::unique_ptr<LutResult> out = std::make_unique<LutResult>();
            const auto t0 = std::chrono::steady_clock::now();
            const AtmosphereModel atmo;
            out->trans = atmo_transmittance_lut(atmo);
            out->ms = atmo_multiscatter_lut(atmo, out->trans);
            out->ms_time = ms_since(t0);
            return out;
        });
    }
    // 行星贴图：一次只交一面（一面要几百毫秒到一秒多，别把四条线程都占了，近处的地面块照样先做）；做完一面再交下一面（collect_jobs）
    submit_face(*pool, view->share_planet(), surface_size, 0);
}

void PlanetGlobe::collect_jobs() {
    if (!pool) {
        return;
    }
    std::vector<JobPool::Done> done;
    pool->collect(JobKind::PLANET, done);
    for (JobPool::Done &d : done) {
        if (JobPool::id_of(d.key) >= WX_GRID_ID && JobPool::id_of(d.key) < WX_GRID_ID + WX_GRID_BANDS) {
            // 各格的群与权备好一段；段都齐了才换上（生存世界重备时旧的照用）
            if (--wx_grid_bands_left == 0 && wx_grid_building) {
                wx_grid = wx_grid_building;
                wx_grid_building.reset();
                wx_grid_ms = double(Time::get_singleton()->get_ticks_usec()) / 1000.0 - wx_grid_started;
                wx_baked_rev = ~uint64_t(0); // 权换了：下一回重烘
            }
            continue;
        }
        if (JobPool::id_of(d.key) == WX_BAKE_ID) {
            WeatherBakeResult *r = static_cast<WeatherBakeResult *>(d.result.get());
            wx_bake_inflight = false;
            wx_last = r->rgba;
            PackedByteArray bytes;
            bytes.resize(int64_t(r->rgba.size() * sizeof(float)));
            std::memcpy(bytes.ptrw(), r->rgba.data(), r->rgba.size() * sizeof(float));
            Ref<Image> image = Image::create_from_data(weather_width, weather_height, false, Image::FORMAT_RGBAF, bytes);
            image->convert(Image::FORMAT_RGBAH);
            if (weather_tex.is_valid() && weather_tex->get_width() == weather_width && weather_tex->get_height() == weather_height) {
                weather_tex->update(image);
            } else {
                weather_tex = ImageTexture::create_from_image(image);
                textures_pushed = false;
            }
            wx_baked_day = r->day;
            wx_baked_rev = r->rev;
            wx_bake_ms = r->ms;
            if (wx_bakes++ == 0) {
                print_line(vformat(String(U"PlanetGlobe：行星天气场的贴图 %d × %d 好了——各格的群与权 %.0f ms（一回）、烘一回 %.0f ms（后台）"), weather_width, weather_height, wx_grid_ms,
                        wx_bake_ms));
            }
            continue;
        }
        if (JobPool::id_of(d.key) == 100) {
            const LutResult *r = static_cast<const LutResult *>(d.result.get());
            trans_tex = lut_texture(r->trans);
            ms_tex = lut_texture(r->ms);
            bake.lut_ms = r->ms_time;
            luts_ready = true;
            textures_pushed = false;
            continue;
        }
        FaceResult *r = static_cast<FaceResult *>(d.result.get());
        bake.color[size_t(r->face)] = r->color;
        bake.extra[size_t(r->face)] = r->extra;
        bake.cpu_ms += r->stats.ms;
        bake.groups += r->stats.groups;
        bake.stand_ins += r->stats.stand_ins;
        bake.land_km2 += r->stats.land_km2;
        bake.faces_done++;
        if (r->face + 1 < 6) {
            submit_face(*pool, view->share_planet(), surface_size, r->face + 1);
        }
    }
    if (bake.faces_done == 6 && (!bake.uploaded || bake.rebaking)) {
        const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
        TypedArray<Image> colors, extras;
        for (int f = 0; f < 6; f++) {
            colors.push_back(bake.color[size_t(f)]);
            extras.push_back(bake.extra[size_t(f)]);
        }
        surface_color.instantiate();
        surface_color->create_from_images(colors);
        surface_extra.instantiate();
        surface_extra->create_from_images(extras);
        bake.color.clear();
        bake.extra.clear();
        bake.uploaded = true;
        bake.rebakes += bake.rebaking ? 1 : 0;
        bake.rebaking = false;
        bake.finished_at = double(Time::get_singleton()->get_ticks_usec()) * 1e-6;
        const uint64_t now = Time::get_singleton()->get_ticks_usec();
        bake.upload_ms = double(now - t0) / 1000.0;
        bake.wall_ms = double(now - bake.start_usec) / 1000.0;
        textures_pushed = false;
        print_line(vformat(String(U"PlanetGlobe：行星贴图 6 × %d² 烘好——%d 群次（替身 %d）、陆地 %s km²；后台 %.0f ms（一面一面烘，墙钟 %.0f ms），建纹理 %.0f ms；大气表 %.0f ms"),
                surface_size, bake.groups, bake.stand_ins, String::num(bake.land_km2, 0), bake.cpu_ms, bake.wall_ms, bake.upload_ms, bake.lut_ms));
    }
}

void PlanetGlobe::push_textures() {
    auto both = [&](const char *p_name, const Variant &p_value) {
        if (sky_mat.is_valid()) {
            sky_mat->set_shader_parameter(p_name, p_value);
        }
        if (overlay_mat.is_valid()) {
            overlay_mat->set_shader_parameter(p_name, p_value);
        }
    };
    both("planet_fields", fields_tex);
    both("planet_fields2", fields2_tex);
    both("planet_bands", bands_tex);
    if (luts_ready) {
        both("atmo_trans_lut", trans_tex);
        both("atmo_ms_lut", ms_tex);
    }
    if (bake.uploaded) {
        both("planet_surface_color", surface_color);
        both("planet_surface_extra", surface_extra);
    }
    both("planet_surface_ready", bake.uploaded ? 1.0 : 0.0);
    if (weather_tex.is_valid()) {
        both("planet_weather", weather_tex);
    }
    both("planet_weather_on", weather_tex.is_valid() ? 1.0 : 0.0);
    textures_pushed = true;
}

void PlanetGlobe::update_weather_bake(double p_day) {
    const std::shared_ptr<PlanetWeather> pw = view->get_planet_weather();
    if (!pw || !pool) {
        return;
    }
    // 各格的群与权：只跟位置（与「一群管多远」）有关，分 WX_GRID_BANDS 段交给任务池的几条线程（Debug 下合起来十秒上下）；
    // 生存世界的粗版来了、一群管多远变了（reach_revision）就重备一回
    if (!wx_grid_submitted || (!wx_grid_building && wx_grid && pw->reach_revision() != wx_grid_reach_rev)) {
        wx_grid_submitted = true;
        wx_grid_reach_rev = pw->reach_revision();
        const std::shared_ptr<const PlanetWeather> w = pw;
        wx_grid_building = w->new_bake_grid(weather_width, weather_height);
        wx_grid_bands_left = WX_GRID_BANDS;
        wx_grid_started = double(Time::get_singleton()->get_ticks_usec()) / 1000.0;
        const std::shared_ptr<PlanetWeather::BakeGrid> grid = wx_grid_building;
        const int H = weather_height;
        for (int b = 0; b < WX_GRID_BANDS; b++) {
            const int r0 = H * b / WX_GRID_BANDS, r1 = H * (b + 1) / WX_GRID_BANDS;
            pool->submit(JobPool::key(JobKind::PLANET, WX_GRID_ID + uint64_t(b)), WX_PRIORITY + b, [w, grid, r0, r1]() -> std::unique_ptr<JobResult> {
                std::unique_ptr<WeatherGridResult> out = std::make_unique<WeatherGridResult>();
                const auto t0 = std::chrono::steady_clock::now();
                w->prepare_bake_rows(*grid, r0, r1);
                out->ms = ms_since(t0);
                return out;
            });
        }
        return;
    }
    if (!wx_grid || wx_bake_inflight) {
        return;
    }
    // 重烘：游戏钟走了一刻钟、拨了钟，或逐日表 / 开发者覆盖变了
    const uint64_t rev = pw->revision();
    if (std::abs(p_day - wx_baked_day) * 24.0 < weather_every_h && rev == wx_baked_rev) {
        return;
    }
    wx_bake_inflight = true;
    const std::shared_ptr<const PlanetWeather> w = pw;
    const std::shared_ptr<const PlanetWeather::BakeGrid> grid = wx_grid;
    pool->submit(JobPool::key(JobKind::PLANET, WX_BAKE_ID), WX_PRIORITY, [w, grid, p_day, rev]() -> std::unique_ptr<JobResult> {
        std::unique_ptr<WeatherBakeResult> out = std::make_unique<WeatherBakeResult>();
        const auto t0 = std::chrono::steady_clock::now();
        w->bake(*grid, p_day, out->rgba);
        out->day = p_day;
        out->rev = rev;
        out->ms = ms_since(t0);
        return out;
    });
}

void PlanetGlobe::_process(double /* p_delta */) {
    if (!view || !view->is_active() || !pool) {
        if (!setup()) {
            return;
        }
    }
    collect_jobs();
    // 行星计划 P4：行星包加了粗版（生存模式的新世界，替身陆续换成粗版）——隔 rebake_every 秒整张重烘一回，六面烘好再换（期间旧贴图照用）
    if (bake.uploaded && !bake.rebaking && view->get_planet()->lod_generation() != bake.lod_gen &&
            double(Time::get_singleton()->get_ticks_usec()) * 1e-6 - bake.finished_at > rebake_every) {
        bake.started = false;
        bake.faces_done = 0;
        bake.cpu_ms = 0.0;
        bake.groups = bake.stand_ins = 0;
        bake.land_km2 = 0.0;
        bake.rebaking = true;
        start_jobs();
    }
    if (!textures_pushed) {
        push_textures();
    }
    const PlanetVec cam = view->camera_flat();
    const double h = cam.y;
    const Vector2 ll = view->get_camera_latlon();
    const GroupFrame &anchor = view->get_anchor();
    lateral_m = great_circle_m(ll.x, ll.y, anchor.lat_deg, anchor.lon_deg, anchor.radius);
    blend_atmo = luts_ready ? smooth01(atmo_from, atmo_to, h) : 0.0;
    blend_planet = luts_ready ? smooth01(deck_fade_from, deck_fade_to, h) : 0.0;
    deck_alpha = 1.0 - blend_planet;
    atmosphere->set_space_blend(blend_atmo);

    // 日月：按全局钟在行星系里（钟点是参考经度的地方时，D10），与方向光同一套历法
    const WorldClock *clock = WorldClock::get_singleton();
    const double day = clock ? clock->get_calendar_day() : 0.0;
    const WeatherModel *wx = atmosphere->get_model_const();
    const CalendarRules rules = wx ? wx->calendar() : CalendarRules();
    double sun[3], moon[3];
    planet_sun_dir(day, view->get_clock_longitude(), rules, sun);
    planet_moon_dir(day, view->get_clock_longitude(), rules, moon);
    const double year = double(std::max(1, rules.year_days));
    const double doy = day_of_year(day, rules);
    const double w = 2.0 * PI / year;
    const double amp = BAND_K_SHIFT * rules.tilt_deg / std::sqrt(1.0 + (w * BAND_TAU_DAYS) * (w * BAND_TAU_DAYS));
    band_shift = amp * std::cos(2.0 * PI * (doy - rules.solstice_n) / year - std::atan(w * BAND_TAU_DAYS));

    const PlanetBasis &axes = view->camera_axes();
    const Basis basis(v3(axes.cols[0]), v3(axes.cols[1]), v3(axes.cols[2]));
    const PlanetData *pd = view->get_planet();
    const PlanetVec anchor_dir = planet_unit(anchor.lat_deg, anchor.lon_deg);
    const PlanetVec g_dir = planet_unit(pd->g_lat, pd->g_lon);
    double mask_cos = 2.0;
    if (const FarGroups *fg = Object::cast_to<FarGroups>(ObjectDB::get_instance(far_groups_id))) {
        mask_cos = std::cos(std::max(0.0, fg->get_radius() - MASK_INSET_M) / anchor.radius);
    }
    const Camera3D *camera = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
    const double vh = get_viewport() ? std::max(1.0f, get_viewport()->get_visible_rect().size.y) : 900.0;
    const double pixel = (camera ? camera->get_fov() : 60.0) * PI / 180.0 / vh;
    const double moon_lit = atmosphere->get_moon_lit();
    update_weather_bake(day);

    auto both = [&](const char *p_name, const Variant &p_value) {
        if (sky_mat.is_valid()) {
            sky_mat->set_shader_parameter(p_name, p_value);
        }
        if (overlay_mat.is_valid()) {
            overlay_mat->set_shader_parameter(p_name, p_value);
        }
    };
    both("planet_on", 1.0);
    both("planet_axes", basis);
    both("planet_cam_h", h);
    both("planet_datum_r", anchor.radius);
    both("planet_sun", Vector3(real_t(sun[0]), real_t(sun[1]), real_t(sun[2])));
    both("planet_moon", Vector3(real_t(moon[0]), real_t(moon[1]), real_t(moon[2])));
    both("planet_moon_lit", moon_lit);
    both("planet_time", Vector4(real_t(day - std::floor(day / 1000.0) * 1000.0), real_t(doy / year), real_t(band_shift), 0.0f));
    both("planet_anchor", v3(anchor_dir));
    both("planet_g", Vector4(real_t(g_dir.x), real_t(g_dir.y), real_t(g_dir.z), real_t(pd->g_radius_deg * PI / 180.0)));
    both("planet_mask_cos", mask_cos);
    both("planet_pixel", pixel);
    both("planet_sun_power", sun_power);
    both("planet_sky_power", sky_power);
    if (sky_mat.is_valid()) {
        sky_mat->set_shader_parameter("planet_blend", blend_planet);
        sky_mat->set_shader_parameter("atmo_blend", blend_atmo);
    }
    // 云层叠画：相机升过天气云层才有（往下看的云）；跟着相机摆，免得被剔
    const double cloud_h = atmosphere->get_cloud_height();
    overlay_on = overlay && h > cloud_h + 1.0;
    if (overlay) {
        if (overlay_on != overlay->is_visible()) {
            overlay->set_visible(overlay_on);
        }
        if (overlay_on && camera) {
            overlay->set_global_position(camera->get_global_position());
            if (sky_mat.is_valid()) {
                for (const char *name : { "sun_dir", "cloud_cover", "cover_grad", "drift", "cell_m", "cloud_height", "cloud_edge" }) {
                    overlay_mat->set_shader_parameter(name, sky_mat->get_shader_parameter(name));
                }
            }
            overlay_mat->set_shader_parameter("atmo_blend", blend_atmo);
            overlay_mat->set_shader_parameter("cloud_fade", smooth01(cloud_h, cloud_h + 150.0, h));
        }
    }
    update_deck();
    update_anchor_visibility();
}

void PlanetGlobe::update_deck() {
    MeshInstance3D *deck = view->get_deck();
    if (!deck) {
        return;
    }
    if (deck_alpha >= 0.999) {
        if (deck->get_material_override().is_valid()) {
            deck->set_material_override(Ref<Material>());
        }
        if (!deck->is_visible()) {
            deck->set_visible(true);
        }
    } else if (deck_alpha <= 0.002 || deck_fade_mat.is_null()) {
        if (deck->is_visible()) {
            deck->set_visible(false);
        }
    } else {
        if (deck->get_material_override() != deck_fade_mat) {
            deck->set_material_override(deck_fade_mat);
        }
        deck_fade_mat->set_shader_parameter("fade", deck_alpha);
        if (!deck->is_visible()) {
            deck->set_visible(true);
        }
    }
}

void PlanetGlobe::update_anchor_visibility() {
    const bool want = anchor_hidden ? lateral_m > geometry_radius - HIDE_HYSTERESIS_M : lateral_m > geometry_radius + HIDE_HYSTERESIS_M;
    if (want == anchor_hidden) {
        return;
    }
    anchor_hidden = want;
    if (want) {
        Node *root = get_parent();
        if (!root) {
            return;
        }
        const Camera3D *camera = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
        for (int64_t k = 0; k < root->get_child_count(); k++) {
            Node3D *n = Object::cast_to<Node3D>(root->get_child(k));
            if (!n || n == this || n == view || n == atmosphere || n == camera || Object::cast_to<Camera3D>(n) || Object::cast_to<Light3D>(n) ||
                    ObjectID(n->get_instance_id()) == far_groups_id || !n->is_visible()) {
                continue;
            }
            n->set_visible(false);
            hidden_nodes.push_back(ObjectID(n->get_instance_id()));
        }
    } else {
        for (const ObjectID &id : hidden_nodes) {
            if (Node3D *n = Object::cast_to<Node3D>(ObjectDB::get_instance(id))) {
                n->set_visible(true);
            }
        }
        hidden_nodes.clear();
    }
}

Color PlanetGlobe::weather_texel(double p_lat, double p_lon) const {
    if (wx_last.size() != size_t(weather_width) * size_t(weather_height) * 4) {
        return Color(-1, -1, -1, -1);
    }
    double lon = std::fmod(p_lon + 180.0, 360.0);
    lon += lon < 0.0 ? 360.0 : 0.0;
    const int r = std::clamp(int((p_lat + 90.0) / 180.0 * weather_height), 0, weather_height - 1);
    const int c = std::clamp(int(lon / 360.0 * weather_width), 0, weather_width - 1);
    const float *t = &wx_last[(size_t(r) * size_t(weather_width) + size_t(c)) * 4];
    return Color(t[0], t[1], t[2], t[3]);
}

Dictionary PlanetGlobe::get_state() const {
    Dictionary d;
    d["surface_ready"] = bake.uploaded;
    d["luts_ready"] = luts_ready;
    d["bake_wall_ms"] = bake.wall_ms;
    d["bake_cpu_ms"] = bake.cpu_ms;
    d["upload_ms"] = bake.upload_ms;
    d["lut_ms"] = bake.lut_ms;
    d["groups"] = bake.groups;
    d["stand_ins"] = bake.stand_ins;
    d["rebakes"] = bake.rebakes;
    d["rebaking"] = bake.rebaking;
    d["land_km2"] = bake.land_km2;
    d["blend_planet"] = blend_planet;
    d["blend_atmo"] = blend_atmo;
    d["deck_alpha"] = deck_alpha;
    d["overlay"] = overlay_on;
    d["anchor_hidden"] = anchor_hidden;
    d["lateral_km"] = lateral_m / 1000.0;
    d["band_shift"] = band_shift;
    d["weather_ready"] = weather_tex.is_valid();
    d["weather_bakes"] = wx_bakes;
    d["weather_grid_ms"] = wx_grid_ms;
    d["weather_bake_ms"] = wx_bake_ms;
    d["weather_day"] = wx_baked_day;
    return d;
}
