#pragma once

#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/variant/color.hpp>

// 弯曲标准材质（行星计划 P1）：game/shaders/curved_standard*.gdshader 的 ShaderMaterial，代替原来的 StandardMaterial3D——
// 同样的反照率（source_color）、糙度、镜面 0.5、漫反射 Burley，顶点按真球面弯（没有行星数据的地图不弯，与原来一样）。
// 读不了着色器就退回同参数的 StandardMaterial3D（不弯）。只在主线程上用。
namespace curved_material {

// 素色；p_two_sided 是双面（帆）
godot::Ref<godot::Material> solid(const godot::Color &p_albedo, float p_roughness = 1.0f, bool p_two_sided = false);
// 正式材质小样：按独立美术绑定读取基础色，局部米制三平面映射；缺图仍为素色。
godot::Ref<godot::Material> surface(const godot::Color &p_albedo, float p_roughness, const godot::String &p_asset, float p_metallic = 0.0f, bool p_two_sided = false);
// 顶点色（按 sRGB 调的）当反照率
godot::Ref<godot::Material> vertex_colored(float p_roughness = 1.0f);
// 素色加自发光（浮石、舱窗的光）
godot::Ref<godot::Material> glowing(const godot::Color &p_albedo, float p_roughness, const godot::Color &p_emission, float p_energy);

} // namespace curved_material
