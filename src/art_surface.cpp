#include "art_surface.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <cmath>
#include "curved_material.h"
#include "shared_looks.h"
#include "scatter_meshes.h"
#include "model_art.h"
using namespace godot;
void ArtSurface::_bind_methods() {
    ClassDB::bind_static_method("ArtSurface", D_METHOD("create", "asset", "color", "roughness", "metallic"), &ArtSurface::create, DEFVAL(0.0));
    ClassDB::bind_static_method("ArtSurface", D_METHOD("configure", "material", "prefix", "asset"), &ArtSurface::configure);
    ClassDB::bind_static_method("ArtSurface",D_METHOD("part_material","index"),&ArtSurface::part_material);
    ClassDB::bind_static_method("ArtSurface",D_METHOD("scatter_mesh","look","lod"),&ArtSurface::scatter_mesh,DEFVAL(0));
    ClassDB::bind_static_method("ArtSurface",D_METHOD("model_instance","id"),&ArtSurface::model_instance);
}

bool ArtSurface::configure(const Ref<ShaderMaterial> &p_material, const String &p_prefix, const String &p_asset) {
    if (p_material.is_null()) return false;
    p_material->set_shader_parameter(p_prefix + String("_enabled"), false);
    const Ref<FileAccess> file = FileAccess::open("res://art/bindings.json", FileAccess::READ);
    if (file.is_null()) return false;
    const Variant data = JSON::parse_string(file->get_as_text());
    if (data.get_type() != Variant::DICTIONARY) return false;
    const Dictionary root = data;
    const Variant raw_map = root.get("bindings", Variant());
    if (int(root.get("schemaVersion", 0)) != 1 || raw_map.get_type() != Variant::DICTIONARY) return false;
    const Dictionary bindings = raw_map;
    const Variant raw_entry = bindings.get(p_asset, Variant());
    if (raw_entry.get_type() != Variant::DICTIONARY) return false;
    const Dictionary entry = raw_entry;
    const Variant raw_surface = entry.get("surface", Variant());
    if (raw_surface.get_type() != Variant::DICTIONARY) return false;
    const Dictionary surface = raw_surface;
    const Variant raw_mean = surface.get("meanLinear", Variant());
    if (raw_mean.get_type() != Variant::ARRAY) return false;
    const Array mean = raw_mean;
    const double metres = surface.get("tileMetres", 0.0);
    if (mean.size() != 3 || !std::isfinite(metres) || metres <= 0.0 || metres > 16.0) return false;
    for (int i=0;i<3;++i) if ((mean[i].get_type()!=Variant::FLOAT && mean[i].get_type()!=Variant::INT) || !std::isfinite(double(mean[i])) || double(mean[i])<=0 || double(mean[i])>1) return false;
    const String path = entry.get("path", String());
    if (!path.begins_with("res://art/materials/") || !path.ends_with(".png") || path.contains("..") || path.contains("\\") ||
            !ResourceLoader::get_singleton()->exists(path,"Texture2D")) return false;
    const Ref<Texture2D> image = ResourceLoader::get_singleton()->load(path,"Texture2D");
    if (image.is_null()) return false;
    p_material->set_shader_parameter(p_prefix+String("_tex"),image);
    p_material->set_shader_parameter(p_prefix+String("_mean"),Vector3(real_t(double(mean[0])),real_t(double(mean[1])),real_t(double(mean[2]))));
    p_material->set_shader_parameter(p_prefix+String("_tile_metres"),metres);
    p_material->set_shader_parameter(p_prefix+String("_enabled"),true);
    return true;
}
Ref<Material> ArtSurface::create(const String &p_asset, const Color &p_color, double p_roughness, double p_metallic) {
    return curved_material::surface(p_color, float(p_roughness), p_asset, float(p_metallic));
}
Ref<Material> ArtSurface::part_material(int p_index){if(p_index<0||p_index>=int(PartMaterial::COUNT))return {};SharedLooks looks;return looks.part_material(PartMaterial(p_index));}
Ref<Mesh> ArtSurface::scatter_mesh(int p_look,int p_lod){if(p_look<0||p_look>int(ScatterLook::CRUST))return {};ScatterMeshes meshes;return p_lod==1?meshes.low_mesh(ScatterLook(p_look)):p_lod==2?meshes.felled_mesh(ScatterLook(p_look)):meshes.mesh(ScatterLook(p_look));}
Node3D *ArtSurface::model_instance(const String &id){SharedLooks looks;return model_art::instantiate_styled(id,looks);}
