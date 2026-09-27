#include "atmosphere.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/particle_process_material.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/sky.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include "map_host.h"
#include "planet_view.h"
#include "weather_noise.h"
#include "weather_view.h"
#include "world_clock.h"

using namespace godot;

namespace {

constexpr const char *SKY_SHADER = "res://shaders/sky.gdshader";
constexpr double JUMP_M = 400.0; // 相机一帧挪这么远（瞬移、换地图）：画面不平滑，直接跟上
constexpr double INDOOR_REACH_M = 30.0; // 头顶这么高以内有东西就算在屋里 / 舱里（不下雨雪）
constexpr double INDOOR_CHECK_S = 0.25;
constexpr double FOG_THIN_FROM_M = 2500.0; // 有行星数据时：相机高过这里，雾按 exp(−(高 − 2500) / 7000) 减淡（空气变稀）
constexpr double FOG_THIN_SCALE_M = 7000.0;
// 行星计划 P2：高空的气温。天气模型按直减率一路往上减（20 km 处 −90 °C 的量级）；对流层顶（零点以上 11 km）往上当平流层、气温不再降，
// 100 km 往上是太空（HUD 不报天气）
constexpr double TROPOPAUSE_M = 11000.0;
constexpr double SPACE_M = 100000.0;

double smooth01(double p_e0, double p_e1, double p_x) {
    const double t = std::clamp((p_x - p_e0) / (p_e1 - p_e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

Color mix_color(const Color &p_a, const Color &p_b, double p_t) {
    return p_a.lerp(p_b, float(std::clamp(p_t, 0.0, 1.0)));
}

// 一道雨丝：两片交叉的细长条（绕 y 轴互相垂直），哪个方向看都有；粒子按速度把 y 轴对过去
Ref<ArrayMesh> rain_streak_mesh(const Ref<Material> &p_material) {
    PackedVector3Array v, n;
    const float w = 0.012f, h = 0.45f;
    const Vector3 quads[2][4] = {
        { Vector3(-w, -h, 0), Vector3(w, -h, 0), Vector3(w, h, 0), Vector3(-w, h, 0) },
        { Vector3(0, -h, -w), Vector3(0, -h, w), Vector3(0, h, w), Vector3(0, h, -w) },
    };
    for (const auto &q : quads) {
        const int order[6] = { 0, 2, 1, 0, 3, 2 };
        for (int k : order) {
            v.push_back(q[k]);
            n.push_back(Vector3(0, 0, 1));
        }
    }
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = v;
    arrays[Mesh::ARRAY_NORMAL] = n;
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    mesh->surface_set_material(0, p_material);
    return mesh;
}

GPUParticles3D *make_particles(bool p_snow, const Ref<StandardMaterial3D> &p_material) {
    GPUParticles3D *p = memnew(GPUParticles3D);
    p->set_name(p_snow ? "Snow" : "Rain");
    Ref<ParticleProcessMaterial> m;
    m.instantiate();
    m->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_BOX);
    m->set_emission_box_extents(p_snow ? Vector3(26, 2, 26) : Vector3(20, 1, 20));
    m->set_direction(Vector3(0, -1, 0));
    m->set_spread(p_snow ? 20.0 : 2.0);
    m->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, p_snow ? 0.6 : 11.0);
    m->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, p_snow ? 1.4 : 14.0);
    m->set_gravity(Vector3(0, p_snow ? -0.6 : -6.0, 0));
    if (p_snow) {
        m->set_turbulence_enabled(true);
        m->set_turbulence_noise_strength(1.2);
        m->set_turbulence_noise_scale(4.0);
        m->set_param_min(ParticleProcessMaterial::PARAM_TURB_VEL_INFLUENCE, 0.04);
        m->set_param_max(ParticleProcessMaterial::PARAM_TURB_VEL_INFLUENCE, 0.09);
        m->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 0.6);
        m->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 1.3);
    } else {
        m->set_particle_flag(ParticleProcessMaterial::PARTICLE_FLAG_ALIGN_Y_TO_VELOCITY, true);
    }
    p->set_process_material(m);
    if (p_snow) {
        Ref<QuadMesh> quad;
        quad.instantiate();
        quad->set_size(Vector2(0.07f, 0.07f));
        quad->set_material(p_material);
        p->set_draw_pass_mesh(0, quad);
    } else {
        p->set_draw_pass_mesh(0, rain_streak_mesh(p_material));
    }
    p->set_amount(p_snow ? 3500 : 6000);
    p->set_lifetime(p_snow ? 9.0 : 1.9);
    p->set_use_local_coordinates(false);
    p->set_visibility_aabb(AABB(Vector3(-45, -70, -45), Vector3(90, 90, 90)));
    p->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    p->set_amount_ratio(0.0f);
    p->set_emitting(true);
    return p;
}

} // namespace

void Atmosphere::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_weather"), &Atmosphere::get_weather);
    ClassDB::bind_method(D_METHOD("weather_at", "global"), &Atmosphere::weather_at);
    ClassDB::bind_method(D_METHOD("hud_text", "global"), &Atmosphere::hud_text);
    ClassDB::bind_method(D_METHOD("get_look"), &Atmosphere::get_look);
    ClassDB::bind_method(D_METHOD("get_sun_altitude"), &Atmosphere::get_sun_altitude);
    ClassDB::bind_method(D_METHOD("is_night"), &Atmosphere::is_night);
    ClassDB::bind_method(D_METHOD("set_weather_override", "kind", "temp_offset"), &Atmosphere::set_weather_override);
    ClassDB::bind_method(D_METHOD("clear_weather_override"), &Atmosphere::clear_weather_override);
    ClassDB::bind_method(D_METHOD("get_override"), &Atmosphere::get_override);
    ClassDB::bind_method(D_METHOD("snap"), &Atmosphere::snap);

    ClassDB::bind_method(D_METHOD("set_sun_path", "value"), &Atmosphere::set_sun_path);
    ClassDB::bind_method(D_METHOD("get_sun_path"), &Atmosphere::get_sun_path);
    ClassDB::bind_method(D_METHOD("set_environment_path", "value"), &Atmosphere::set_environment_path);
    ClassDB::bind_method(D_METHOD("get_environment_path"), &Atmosphere::get_environment_path);
    ClassDB::bind_method(D_METHOD("set_map_path", "value"), &Atmosphere::set_map_path);
    ClassDB::bind_method(D_METHOD("get_map_path"), &Atmosphere::get_map_path);
    ClassDB::bind_method(D_METHOD("set_latitude", "value"), &Atmosphere::set_latitude);
    ClassDB::bind_method(D_METHOD("get_latitude"), &Atmosphere::get_latitude);
    ClassDB::bind_method(D_METHOD("set_smoothing", "value"), &Atmosphere::set_smoothing);
    ClassDB::bind_method(D_METHOD("get_smoothing"), &Atmosphere::get_smoothing);
    ClassDB::bind_method(D_METHOD("set_override_fade", "value"), &Atmosphere::set_override_fade);
    ClassDB::bind_method(D_METHOD("get_override_fade"), &Atmosphere::get_override_fade);
    ClassDB::bind_method(D_METHOD("set_cloud_height", "value"), &Atmosphere::set_cloud_height);
    ClassDB::bind_method(D_METHOD("get_cloud_height"), &Atmosphere::get_cloud_height);
    ClassDB::bind_method(D_METHOD("set_precipitation", "value"), &Atmosphere::set_precipitation);
    ClassDB::bind_method(D_METHOD("get_precipitation"), &Atmosphere::get_precipitation);
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sun_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DirectionalLight3D"), "set_sun_path", "get_sun_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "environment_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "WorldEnvironment"), "set_environment_path", "get_environment_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "map_path"), "set_map_path", "get_map_path");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "latitude", PROPERTY_HINT_RANGE, "-89,89,0.1,suffix:°"), "set_latitude", "get_latitude");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "smoothing", PROPERTY_HINT_RANGE, "0.05,20,0.05,suffix:s"), "set_smoothing", "get_smoothing");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "override_fade", PROPERTY_HINT_RANGE, "0.05,60,0.05,suffix:s"), "set_override_fade", "get_override_fade");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cloud_height", PROPERTY_HINT_RANGE, "300,8000,10,suffix:m"), "set_cloud_height", "get_cloud_height");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "precipitation"), "set_precipitation", "get_precipitation");
}

void Atmosphere::set_latitude(double p_value) {
    latitude = p_value;
    Climate c;
    c.calendar.latitude_deg = latitude;
    fallback = WeatherModel(c, 0.0, 2500.0);
}

WeatherModel *Atmosphere::model() {
    WeatherModel *wx = map ? map->map_weather() : nullptr;
    return wx ? wx : &fallback;
}

WeatherSample Atmosphere::sample_mix(double p_day, const Vector3 &p_global, double p_h) {
    if (!map) {
        return model()->sample(p_day, p_global.x, p_global.z, p_h);
    }
    map->map_weather_mix(p_global, mix_buf);
    if (mix_buf.size() <= 1) {
        WeatherModel *wx = mix_buf.empty() ? model() : mix_buf[0].model;
        const Vector3 at = mix_buf.empty() ? p_global : mix_buf[0].local;
        return wx->sample(p_day, at.x, at.z, p_h);
    }
    mix_samples.clear();
    mix_weights.clear();
    for (const WeatherMix &m : mix_buf) {
        mix_samples.push_back(m.model->sample(p_day, m.local.x, m.local.z, p_h));
        mix_weights.push_back(m.weight);
    }
    return blend_weather(mix_samples, mix_weights);
}

const WeatherModel *Atmosphere::get_model_const() {
    return model();
}

double Atmosphere::calendar_day() const {
    const WorldClock *clock = WorldClock::get_singleton();
    return clock ? clock->get_calendar_day() : 0.0;
}

double Atmosphere::random01() {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return double(rng >> 11) * (1.0 / 9007199254740992.0);
}

void Atmosphere::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        set_physics_process(false);
        return;
    }
    add_to_group(GROUP);
    sun = Object::cast_to<DirectionalLight3D>(get_node_or_null(sun_path));
    if (WorldEnvironment *we = Object::cast_to<WorldEnvironment>(get_node_or_null(environment_path))) {
        env = we->get_environment();
    }
    map = map_path.is_empty() ? nullptr : MapHost::of(get_node_or_null(map_path));
    set_latitude(latitude);
    if (env.is_valid()) {
        base_fog = env->get_fog_density();
        base_fog_color = env->get_fog_light_color();
        base_ambient = env->get_ambient_light_energy();
        base_fog_sky = env->get_fog_sky_affect();
    }
    if (sun) {
        base_sun = sun->get_param(Light3D::PARAM_ENERGY);
    }
    build();
    set_process(true);
    set_physics_process(true);
}

void Atmosphere::build() {
    // 天空：换成天空着色器（原来的程序化天空只是一张渐变）
    const Ref<Shader> shader = ResourceLoader::get_singleton()->load(SKY_SHADER);
    if (env.is_valid() && shader.is_valid()) {
        sky_material.instantiate();
        sky_material->set_shader(shader);
        sky_material->set_shader_parameter("cloud_height", cloud_height);
        sky_material->set_shader_parameter("cell_m", model()->get_rules().cell_m);
        sky_material->set_shader_parameter("cloud_edge", model()->get_rules().cloud_edge);
        Ref<Sky> sky = env->get_sky();
        if (sky.is_null()) {
            sky.instantiate();
            env->set_sky(sky);
            env->set_background(Environment::BG_SKY);
        }
        sky->set_material(sky_material);
        sky->set_process_mode(Sky::PROCESS_MODE_INCREMENTAL);
        sky->set_radiance_size(Sky::RADIANCE_SIZE_64);
        // 环境光按天色自己给（夜里不至于伸手不见五指，阴天压暗）；反射照旧取天空
        env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
    } else if (env.is_valid()) {
        ERR_PRINT(String(U"Atmosphere：读不了天空着色器 ") + SKY_SHADER + String(U"，天空照旧（没有云、没有星）"));
    }
    // 月亮
    moon = memnew(DirectionalLight3D);
    moon->set_name("Moon");
    moon->set_color(Color(0.62f, 0.7f, 0.95f));
    moon->set_param(Light3D::PARAM_ENERGY, 0.0f);
    moon->set_shadow(false);
    moon->set_sky_mode(DirectionalLight3D::SKY_MODE_LIGHT_ONLY);
    add_child(moon);
    // 雨雪
    rain_material.instantiate();
    rain_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    rain_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    rain_material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
    rain_material->set_albedo(Color(0.75f, 0.8f, 0.88f, 0.3f));
    // 贴着相机的雨丝别糊一脸：两米以内淡掉
    rain_material->set_distance_fade(BaseMaterial3D::DISTANCE_FADE_PIXEL_ALPHA);
    rain_material->set_distance_fade_min_distance(0.8f);
    rain_material->set_distance_fade_max_distance(2.5f);
    snow_material.instantiate();
    snow_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    snow_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    snow_material->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
    snow_material->set_albedo(Color(0.95f, 0.96f, 1.0f, 0.85f));
    rain = make_particles(false, rain_material);
    snow = make_particles(true, snow_material);
    add_child(rain);
    add_child(snow);
    // 海拔表（全局着色器参数 wx_elev）
    const WeatherModel *wx = model();
    const Ref<Image> image = Image::create_empty(wx->get_bins(), 2, false, Image::FORMAT_RGBAF);
    elev_texture = ImageTexture::create_from_image(image);
    RenderingServer::get_singleton()->global_shader_parameter_set("wx_elev", elev_texture);
}

void Atmosphere::_process(double p_delta) {
    WeatherModel *wx = model();
    const double day = calendar_day();
    const Camera3D *camera = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
    Vector3 eye = camera ? camera->get_global_position() : get_global_position();
    if (!eye.is_finite()) {
        eye = last_eye; // 相机的位置坏了（NaN）：照上一帧的
    }
    update_override(p_delta);
    now = sample_mix(day, eye, std::min(double(eye.y), TROPOPAUSE_M)); // 行星上：几群按离各群陆地的距离过渡
    // 日月：有行星数据时按相机的经纬度（钟点是参考经度的地方时，D10），否则按这张图的纬度、钟点就是当地时
    CalendarRules rules = wx->calendar();
    const PlanetView *pv = PlanetView::active_view();
    on_planet = pv != nullptr;
    local_offset_h = pv ? pv->get_local_hour_offset() : 0.0;
    if (pv) {
        rules.latitude_deg = pv->get_camera_lat();
        rules.longitude_deg = pv->get_camera_lon();
    }
    sun_latitude = rules.latitude_deg;
    sun_longitude = rules.longitude_deg;
    sun_day = day + local_offset_h / 24.0;
    sun_state = sun_at(sun_day, rules);
    moon_state = moon_at(sun_day, rules);
    fog_thin = pv ? std::exp(-std::max(0.0, double(eye.y) - FOG_THIN_FROM_M) / FOG_THIN_SCALE_M) : 1.0;
    // 行星计划 P2：相机升过天气云层，头顶就没有云了——天不再阴、日光不被云遮、不下雨雪（云在脚下，由 PlanetGlobe 的云层叠画画出来）
    above_clouds = pv ? smooth01(cloud_height - 300.0, cloud_height + 300.0, double(eye.y)) : 0.0;
    update_look(eye, p_delta);
    update_lightning(p_delta);
    update_lights();
    update_environment();
    update_globals(day);
    update_particles(eye, p_delta);
    gust_phase += p_delta * (0.6 + 2.0 * look.storm);
    last_eye = eye;
}

void Atmosphere::_physics_process(double p_delta) {
    const Camera3D *camera = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
    if (camera) {
        check_indoor(camera->get_global_position(), p_delta);
    }
}

void Atmosphere::check_indoor(const Vector3 &p_eye, double p_delta) {
    indoor_timer -= p_delta;
    if (indoor_timer > 0.0 || !p_eye.is_finite()) {
        return;
    }
    indoor_timer = INDOOR_CHECK_S;
    const Ref<World3D> world = get_world_3d();
    PhysicsDirectSpaceState3D *space = world.is_valid() ? world->get_direct_space_state() : nullptr;
    if (!space) {
        indoor_hit = false;
        return;
    }
    const Ref<PhysicsRayQueryParameters3D> query = PhysicsRayQueryParameters3D::create(p_eye, p_eye + Vector3(0, real_t(INDOOR_REACH_M), 0));
    query->set_hit_back_faces(false);
    indoor_hit = !space->intersect_ray(query).is_empty();
}

void Atmosphere::update_override(double p_delta) {
    WeatherOverride &o = model()->edit_override();
    const double target = override_on ? 1.0 : 0.0;
    const double rate = p_delta / std::max(0.01, override_fade);
    o.blend = target > o.blend ? std::min(target, o.blend + rate) : std::max(target, o.blend - rate);
    if (override_on) {
        o.active = true;
        o.kind = override_kind;
        o.temp_offset_c = override_temp;
    } else if (o.blend <= 0.0) {
        o.active = false;
        o.kind = WeatherKind::COUNT;
        o.temp_offset_c = 0.0;
    }
    // 新雪：覆盖成下雪、这里正下着就一点点积起来（真实秒，几十秒盖白）；不下了、暖了就化
    const bool snowing = o.blend > 0.5 && o.kind != WeatherKind::COUNT && weather_kinds::at(o.kind).snowy;
    if (snowing) {
        o.fresh_snow_mm = std::min(80.0, o.fresh_snow_mm + p_delta * 2.0 * std::max(0.3, now.precip));
    } else if (o.fresh_snow_mm > 0.0 && now.temp_c > model()->get_climate().snow_temp) {
        o.fresh_snow_mm = std::max(0.0, o.fresh_snow_mm - p_delta * 1.5);
    }
    // 行星上几群各有一个模型：覆盖（渐入渐出、新雪）以锚群的为准，推完抄给别的群（行星计划 P3）
    if (map) {
        map->map_weather_models(models_buf);
        for (WeatherModel *m : models_buf) {
            if (&m->edit_override() != &o) {
                m->set_override(o);
            }
        }
    }
}

void Atmosphere::update_look(const Vector3 &p_eye, double p_delta) {
    bool jump = !have_look || p_eye.distance_to(last_eye) > JUMP_M;
    if (const PlanetView *pv = PlanetView::active_view()) {
        const PlanetVec c = pv->camera_planet();
        const double dx = c.x - last_true[0], dy = c.y - last_true[1], dz = c.z - last_true[2];
        jump = !have_look || !have_true || std::sqrt(dx * dx + dy * dy + dz * dz) > JUMP_M;
        last_true[0] = c.x;
        last_true[1] = c.y;
        last_true[2] = c.z;
        have_true = true;
    } else {
        have_true = false;
    }
    const double k = jump ? 1.0 : 1.0 - std::exp(-p_delta / std::max(0.01, smoothing));
    have_look = true;
    // 云量往外的梯度：东、南各 5 km 再取一次（锋面过来时远处的天先阴）
    const WeatherModel *wx = model();
    const double day = calendar_day();
    const double east = wx->sample(day, p_eye.x + 5000.0, p_eye.z, p_eye.y).cloud_cover;
    const double south = wx->sample(day, p_eye.x, p_eye.z + 5000.0, p_eye.y).cloud_cover;
    const double overcast = std::clamp(now.cloud * std::max(0.0, now.cloud_cover - 0.55) * 1.8 + now.precip * 0.7 + now.storm * 0.25, 0.0, 0.92);
    const double targets[] = {
        now.cloud, now.cloud_cover, now.precip, now.snow_frac, now.fog, now.storm, now.wet, now.wind_x, now.wind_z, overcast,
        std::clamp(now.precip * 1.3, 0.0, 1.0), now.sea_fog, (east - now.cloud_cover) / 5.0, (south - now.cloud_cover) / 5.0,
    };
    double *values[] = {
        &look.cloud, &look.cover, &look.precip, &look.snow, &look.fog, &look.storm, &look.wet, &look.wind_x, &look.wind_z, &look.overcast,
        &look.haze, &look.fog_height, &look.grad_x, &look.grad_z,
    };
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
        *values[i] += (targets[i] - *values[i]) * k;
    }
    const double indoor_target = indoor_hit ? 1.0 : 0.0;
    indoor += (indoor_target - indoor) * (jump ? 1.0 : std::min(1.0, p_delta * 4.0));
}

void Atmosphere::update_lightning(double p_delta) {
    if (look.storm > 0.3 && look.precip > 0.2) {
        flash_timer -= p_delta;
        if (flash_timer <= 0.0) {
            flash = 1.0;
            flicker = 0.09 + 0.1 * random01();
            flash_timer = (3.0 + 10.0 * random01()) / look.storm;
        }
    }
    flash = std::max(0.0, flash - p_delta * 6.0);
    if (flicker > 0.0) {
        flicker -= p_delta;
        if (flicker <= 0.0 && random01() < 0.6) {
            flash = std::max(flash, 0.75); // 一闪之后常再闪一下
        }
    }
}

void Atmosphere::update_lights() {
    const double s = sun_state.dir_y;
    const double day = smooth01(-0.12, 0.2, s);
    const double cloud_dim = std::clamp(1.0 - (0.7 * look.cloud * (0.4 + 0.6 * look.cover) + 0.25 * look.overcast) * (1.0 - above_clouds), 0.12, 1.0);
    const PlanetView *pv = PlanetView::active_view();
    if (sun) {
        // 有行星数据时：历法给的是相机处的地理方向，换到画面的世界轴（锚群平面轴的像，与地理的东南差一个收敛角）
        const Vector3 enu(real_t(sun_state.dir_x), real_t(sun_state.dir_y), real_t(sun_state.dir_z));
        const Vector3 d = pv ? pv->enu_to_visual(enu) : enu;
        const Vector3 up = std::abs(d.y) > 0.99f ? Vector3(0, 0, 1) : Vector3(0, 1, 0);
        sun->set_global_basis(Basis::looking_at(-d, up));
        const double energy = base_sun * smooth01(-0.04, 0.18, s) * cloud_dim;
        sun->set_param(Light3D::PARAM_ENERGY, real_t(energy));
        sun->set_color(mix_color(Color(1.0f, 0.52f, 0.28f), Color(1.0f, 0.97f, 0.92f), smooth01(0.0, 0.4, s)));
        sun->set_shadow(energy > 0.05);
    }
    if (moon) {
        const Vector3 enu(real_t(moon_state.dir_x), real_t(moon_state.dir_y), real_t(moon_state.dir_z));
        const Vector3 d = pv ? pv->enu_to_visual(enu) : enu;
        const Vector3 up = std::abs(d.y) > 0.99f ? Vector3(0, 0, 1) : Vector3(0, 1, 0);
        moon->set_global_basis(Basis::looking_at(-d, up));
        const double energy = 0.2 * (0.25 + 0.75 * moon_state.illuminated) * smooth01(-0.02, 0.15, moon_state.dir_y) * (1.0 - day) * (1.0 - 0.8 * look.cloud);
        moon->set_param(Light3D::PARAM_ENERGY, real_t(energy));
    }
    (void)day;
}

void Atmosphere::update_environment() {
    const double s = sun_state.dir_y;
    const double day = smooth01(-0.12, 0.2, s);
    const double dusk = smooth01(-0.22, -0.02, s) * (1.0 - smooth01(0.02, 0.32, s));
    const double below = 1.0 - above_clouds;
    const double overcast = look.overcast * below, haze = look.haze * below;
    if (env.is_valid()) {
        // 环境光：白天偏天蓝、夜里是暗蓝（看得见路）、晨昏带暖；阴天压暗；闪电一亮
        Color ambient = mix_color(Color(0.2f, 0.25f, 0.4f), Color(0.62f, 0.7f, 0.84f), day);
        ambient = mix_color(ambient, Color(0.85f, 0.62f, 0.5f), dusk * 0.35);
        const double energy = base_ambient * (0.5 + 0.5 * day + 0.25 * dusk) * (1.0 - 0.3 * overcast) + flash * 1.2 * below;
        env->set_ambient_light_color(ambient);
        env->set_ambient_light_energy(real_t(energy));
        // 雾：晴天照场景原来的，雨雪天、雾里加浓；云海漫顶按雾顶起高度雾
        Color fog = mix_color(Color(0.05f, 0.07f, 0.11f), base_fog_color, day);
        fog = mix_color(fog, Color(0.9f, 0.62f, 0.45f), dusk * 0.4);
        fog = mix_color(fog, Color(0.56f, 0.6f, 0.65f) * float(0.2 + 0.8 * day), overcast * 0.7);
        env->set_fog_light_color(fog);
        // 引擎的雾按 fog_sky_affect 蒙在天空上（与雾的浓淡无关）：高空天上换成大气散射时跟着淡掉，太空是黑的
        env->set_fog_sky_affect(real_t(base_fog_sky * (1.0 - space_blend)));
        env->set_fog_density(real_t((base_fog + 0.0011 * haze + 0.012 * look.fog * below) * fog_thin));
        if (look.fog_height > 0.02 && now.fog_top > -1e8) {
            env->set_fog_height(real_t(now.fog_top));
            env->set_fog_height_density(real_t(0.02 * look.fog_height));
        } else {
            env->set_fog_height_density(0.0f);
        }
    }
    if (sky_material.is_valid()) {
        const WeatherModel *wx = model();
        double dx = 0.0, dz = 0.0;
        wx->cloud_drift(calendar_day(), dx, dz);
        const double cell = wx->get_rules().cell_m;
        const double px = std::fmod(dx / cell, double(WX_PERIOD)), pz = std::fmod(dz / cell, double(WX_PERIOD));
        const PlanetView *pv = PlanetView::active_view();
        const Vector3 sun_enu(real_t(sun_state.dir_x), real_t(sun_state.dir_y), real_t(sun_state.dir_z));
        const Vector3 moon_enu(real_t(moon_state.dir_x), real_t(moon_state.dir_y), real_t(moon_state.dir_z));
        sky_material->set_shader_parameter("sun_dir", pv ? pv->enu_to_visual(sun_enu) : sun_enu);
        sky_material->set_shader_parameter("moon_dir", pv ? pv->enu_to_visual(moon_enu) : moon_enu);
        sky_material->set_shader_parameter("moon_lit", moon_state.illuminated);
        sky_material->set_shader_parameter("cloud_cover", look.cover);
        sky_material->set_shader_parameter("cover_grad", Vector2(real_t(look.grad_x), real_t(look.grad_z)));
        sky_material->set_shader_parameter("overcast", overcast);
        sky_material->set_shader_parameter("rain_haze", haze);
        sky_material->set_shader_parameter("drift", Vector2(real_t(px), real_t(pz)));
        sky_material->set_shader_parameter("flash", flash * below);
    }
}

void Atmosphere::update_globals(double p_day) {
    const WeatherModel *wx = model();
    wx->elevation_table(p_day, elev_values);
    const int bins = wx->get_bins(), fields = WeatherModel::table_fields();
    PackedByteArray bytes;
    bytes.resize(int64_t(bins) * 2 * 4 * int64_t(sizeof(float)));
    float *out = reinterpret_cast<float *>(bytes.ptrw());
    for (int b = 0; b < bins; b++) {
        const float *e = &elev_values[size_t(b) * size_t(fields)];
        float *r0 = out + size_t(b) * 4, *r1 = out + (size_t(bins) + size_t(b)) * 4;
        r0[0] = e[0]; // 积雪 mm
        r0[1] = e[1]; // 负积温
        r0[2] = e[2]; // 绿
        r0[3] = e[3]; // 叶
        r1[0] = e[4]; // 秋
        r1[1] = e[5]; // 春芽
        r1[2] = e[6]; // 作物
        r1[3] = e[7]; // 日均温
    }
    const Ref<Image> image = Image::create_from_data(bins, 2, false, Image::FORMAT_RGBAF, bytes);
    if (elev_texture.is_valid() && elev_texture->get_width() == bins && elev_texture->get_height() == 2) {
        elev_texture->update(image);
    } else {
        elev_texture = ImageTexture::create_from_image(image);
        RenderingServer::get_singleton()->global_shader_parameter_set("wx_elev", elev_texture);
    }
    RenderingServer *rs = RenderingServer::get_singleton();
    rs->global_shader_parameter_set("wx_elev_range", Vector2(real_t(wx->get_h_lo()), real_t(wx->get_h_hi())));
    rs->global_shader_parameter_set("wx_wet", look.wet);
    const double gust = 0.5 + 0.5 * std::sin(gust_phase) * std::sin(gust_phase * 0.37 + 1.3);
    rs->global_shader_parameter_set("wx_wind", Vector4(real_t(look.wind_x), real_t(look.wind_z), real_t(gust), real_t(look.storm)));
    const double night = 1.0 - smooth01(-0.12, 0.2, sun_state.dir_y);
    rs->global_shader_parameter_set("wx_misc", Vector4(wx->calendar().latitude_deg < 0.0 ? 1.0f : -1.0f, real_t(night), 0.0f, 0.0f));
    // 云团的公共平面（行星计划 P3）：锚群平面 → 钟的那一群的平面；天空、云层叠画、云带顶的云团花样按它取，换锚不跳
    const CloudFrame &cf = wx->get_cloud_frame();
    rs->global_shader_parameter_set("wx_cloud_frame", Vector4(real_t(cf.m00), real_t(cf.m02), real_t(cf.m20), real_t(cf.m22)));
    rs->global_shader_parameter_set("wx_cloud_origin", Vector2(real_t(cf.ox), real_t(cf.oz)));
}

void Atmosphere::update_particles(const Vector3 &p_eye, double p_delta) {
    const double shelter = (1.0 - indoor) * (1.0 - above_clouds); // 屋里、云层以上不下
    rain_ratio = precipitation ? std::clamp(look.precip * 1.5, 0.0, 1.0) * (1.0 - look.snow) * shelter : 0.0;
    snow_ratio = precipitation ? std::clamp(look.precip * 1.8, 0.0, 1.0) * look.snow * shelter : 0.0;
    const double bright = 0.15 + 0.85 * smooth01(-0.12, 0.2, sun_state.dir_y);
    if (rain) {
        rain->set_amount_ratio(real_t(rain_ratio));
        rain->set_global_position(p_eye + Vector3(real_t(-look.wind_x * 1.2), 16.0f, real_t(-look.wind_z * 1.2)));
        Ref<ParticleProcessMaterial> m = rain->get_process_material();
        if (m.is_valid()) {
            m->set_direction(Vector3(real_t(look.wind_x * 0.07), -1.0f, real_t(look.wind_z * 0.07)).normalized());
            m->set_gravity(Vector3(real_t(look.wind_x * 0.5), -6.0f, real_t(look.wind_z * 0.5)));
        }
        rain_material->set_albedo(Color(float(0.75 * bright), float(0.8 * bright), float(0.88 * bright), float(0.22 + 0.18 * look.precip)));
    }
    if (snow) {
        snow->set_amount_ratio(real_t(snow_ratio));
        snow->set_global_position(p_eye + Vector3(real_t(-look.wind_x * 3.0), 8.0f, real_t(-look.wind_z * 3.0)));
        Ref<ParticleProcessMaterial> m = snow->get_process_material();
        if (m.is_valid()) {
            m->set_gravity(Vector3(real_t(look.wind_x * 0.35), -0.6f, real_t(look.wind_z * 0.35)));
        }
        snow_material->set_albedo(Color(float(0.95 * bright), float(0.96 * bright), float(bright), 0.85f));
    }
    (void)p_delta;
}

Dictionary Atmosphere::get_weather() const {
    Dictionary d = weather_view::to_dictionary(now);
    Atmosphere *self = const_cast<Atmosphere *>(this);
    const WeatherModel *wx = self->model();
    const double day = calendar_day();
    const CalendarDate date = calendar_date(day, wx->calendar());
    d["sun_altitude"] = sun_state.altitude_deg;
    d["sun_azimuth"] = sun_state.azimuth_deg;
    // 日出日落按钟点报（相机处的地方时减经度差）
    d["sunrise"] = sun_state.sunrise_hour - local_offset_h;
    d["sunset"] = sun_state.sunset_hour - local_offset_h;
    d["planet"] = on_planet;
    d["sun_latitude"] = sun_latitude; // 算日月用的纬度、经度（有行星数据时是相机那一点）
    d["sun_longitude"] = sun_longitude;
    d["local_offset_h"] = local_offset_h; // 相机处的地方时比钟点早几个钟头
    d["local_hour"] = std::fmod(std::fmod(sun_day, 1.0) + 1.0, 1.0) * 24.0;
    d["day_length"] = sun_state.day_length_h;
    d["moon_phase"] = moon_state.phase;
    d["moon_lit"] = moon_state.illuminated;
    d["snow_line"] = wx->snow_line(day);
    d["ice_line"] = wx->ice_line(day, 20.0);
    d["season"] = date.season;
    d["season_name"] = String::utf8(wx->season_name(date.season).c_str());
    d["date"] = String::utf8(month_day_text(date).c_str());
    d["night"] = is_night();
    d["has_data"] = wx->has_data();
    d["latitude"] = wx->calendar().latitude_deg;
    d["override"] = get_override();
    // 相机在哪一层：0 对流层、1 高空（对流层顶以上，气温照对流层顶）、2 太空（100 km 以上）；云层以上多少（0..1）
    d["altitude_band"] = last_eye.y >= SPACE_M ? 2 : (last_eye.y >= TROPOPAUSE_M ? 1 : 0);
    d["above_clouds"] = above_clouds;
    return d;
}

Dictionary Atmosphere::weather_at(const Vector3 &p_global) {
    return weather_view::to_dictionary(sample_mix(calendar_day(), p_global, p_global.y));
}

String Atmosphere::hud_text(const Vector3 &p_global) {
    const WeatherModel *wx = model();
    const double day = calendar_day();
    const CalendarDate date = calendar_date(day, wx->calendar());
    const String season = String::utf8(wx->season_name(date.season).c_str());
    if (double(p_global.y) >= SPACE_M) {
        return season + String(U" · 太空，没有天气");
    }
    if (double(p_global.y) >= TROPOPAUSE_M) {
        // 对流层顶以上：气温照对流层顶的（平流层下部近乎不变），没有晴雨
        const WeatherSample s = sample_mix(day, p_global, TROPOPAUSE_M);
        return season + vformat(String(U" · 高空 %d°C"), int(std::lround(s.temp_c)));
    }
    const WeatherSample s = sample_mix(day, p_global, p_global.y);
    return season + String(U" · ") + weather_view::short_text(s);
}

Vector2 Atmosphere::wind_at(const Vector3 &p_global) {
    const WeatherSample s = sample_mix(calendar_day(), p_global, p_global.y);
    return Vector2(real_t(s.wind_x), real_t(s.wind_z));
}

String Atmosphere::sailing_text(const Vector3 &p_global) {
    const WeatherSample s = sample_mix(calendar_day(), p_global, p_global.y);
    String text;
    if (s.wind_ms >= 5.0) {
        text = vformat(String(U"%s %d m/s，船往下风漂"), String::utf8(wind_name(s.wind_x, s.wind_z).c_str()), int(std::lround(s.wind_ms)));
    }
    if (!s.sailable) {
        text = (s.storm > 0.5 ? String(U"风暴，禁航") : String(U"今天禁航（风大浪急）")) + (text.is_empty() ? String() : String(U" · ") + text);
    }
    return text;
}

Dictionary Atmosphere::get_look() const {
    Dictionary d;
    d["cloud"] = look.cloud;
    d["cover"] = look.cover;
    d["precip"] = look.precip;
    d["snow"] = look.snow;
    d["fog"] = look.fog;
    d["storm"] = look.storm;
    d["wet"] = look.wet;
    d["wind_x"] = look.wind_x;
    d["wind_z"] = look.wind_z;
    d["overcast"] = look.overcast;
    d["indoor"] = indoor;
    d["flash"] = flash;
    d["rain_ratio"] = rain_ratio;
    d["snow_ratio"] = snow_ratio;
    d["sun_energy"] = sun ? double(sun->get_param(Light3D::PARAM_ENERGY)) : 0.0;
    d["moon_energy"] = moon ? double(moon->get_param(Light3D::PARAM_ENERGY)) : 0.0;
    return d;
}

void Atmosphere::set_weather_override(const String &p_kind, double p_temp_offset) {
    const WeatherKindDef *def = p_kind.is_empty() ? nullptr : weather_kinds::by_id(std::string(p_kind.utf8().get_data()));
    override_on = true;
    override_kind = def ? def->kind : WeatherKind::COUNT;
    override_temp = p_temp_offset;
}

void Atmosphere::clear_weather_override() {
    override_on = false;
}

Dictionary Atmosphere::get_override() const {
    Atmosphere *self = const_cast<Atmosphere *>(this);
    const WeatherOverride &o = self->model()->get_override();
    Dictionary d;
    d["active"] = override_on;
    d["kind"] = override_on && override_kind != WeatherKind::COUNT ? String(weather_kinds::at(override_kind).id) : String();
    d["kind_name"] = override_on && override_kind != WeatherKind::COUNT ? weather_view::kind_name(override_kind) : String(U"自动");
    d["temp_offset"] = override_on ? override_temp : 0.0;
    d["blend"] = o.blend;
    d["fresh_snow_mm"] = o.fresh_snow_mm;
    return d;
}

void Atmosphere::snap() {
    have_look = false;
}
