#include "art_surface.h"
#include <godot_cpp/core/class_db.hpp>
#include "curved_material.h"
using namespace godot;
void ArtSurface::_bind_methods() {
    ClassDB::bind_static_method("ArtSurface", D_METHOD("create", "asset", "color", "roughness", "metallic"), &ArtSurface::create, DEFVAL(0.0));
}
Ref<Material> ArtSurface::create(const String &p_asset, const Color &p_color, double p_roughness, double p_metallic) {
    return curved_material::surface(p_color, float(p_roughness), p_asset, float(p_metallic));
}
