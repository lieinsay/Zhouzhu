#include "model_art.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/os.hpp>
#include "art_surface.h"
#include "art_mesh.h"
#include "curved_material.h"
using namespace godot;
Dictionary model_art::binding(const String &id){
    String source="res://art/model-bindings.json";
    const PackedStringArray args=OS::get_singleton()->get_cmdline_user_args();for(int i=0;i<args.size();++i)if(args[i].begins_with("--model-bindings=")){const String candidate=args[i].trim_prefix("--model-bindings=");if(candidate.begins_with("res://art/")&&candidate.ends_with(".json")&&!candidate.contains("..")&&!candidate.contains("\\"))source=candidate;}
    const Ref<FileAccess> file=FileAccess::open(source,FileAccess::READ);if(file.is_null())return {};
    const Variant parsed=JSON::parse_string(file->get_as_text());if(parsed.get_type()!=Variant::DICTIONARY)return {};
    const Dictionary root=parsed;if(int(root.get("schemaVersion",0))!=1)return {};
    const Variant values=root.get("bindings",Variant());if(values.get_type()!=Variant::DICTIONARY)return {};
    const Variant value=Dictionary(values).get(id,Variant());if(value.get_type()!=Variant::DICTIONARY)return {};
    const Dictionary entry=value;const String path=entry.get("path",String());
    if(!path.begins_with("res://art/models/")||!path.ends_with(".glb")||path.contains("..")||path.contains("\\"))return {};
    return entry;
}
Node3D *model_art::instantiate(const String &id){
    const Dictionary entry=binding(id);if(entry.is_empty())return nullptr;
    const String path=entry.get("path",String());if(!ResourceLoader::get_singleton()->exists(path,"PackedScene"))return nullptr;
    const Ref<PackedScene> scene=ResourceLoader::get_singleton()->load(path,"PackedScene");if(scene.is_null())return nullptr;
    Node *node=scene->instantiate();if(!node)return nullptr;Node3D *model=Object::cast_to<Node3D>(node);if(!model){memdelete(node);return nullptr;}
    model->set_meta("art_model_id",id);model->set_meta("art_model_version",entry.get("version",String()));model->set_meta("art_model_path",path);
    return model;
}
namespace {
Color color_value(const Variant &raw,const Color &fallback){if(raw.get_type()!=Variant::ARRAY)return fallback;const Array a=raw;if(a.size()!=3)return fallback;return Color(a[0],a[1],a[2]);}
void style(Node *node,SharedLooks &looks,const String &id,const Dictionary &appearance){
    if(auto *instance=Object::cast_to<MeshInstance3D>(node)){
        instance->set_visibility_range_end(90);const Ref<Mesh> mesh=instance->get_mesh();
        if(mesh.is_valid())for(int surface=0;surface<mesh->get_surface_count();++surface){const Ref<BaseMaterial3D> original=instance->get_active_material(surface);if(original.is_null())continue;
            const String name=original->get_name();const Dictionary named=appearance.get("projectMaterials",Dictionary());const Variant raw=named.get(name,Variant());
            const std::string cache=std::string(id.utf8().get_data())+"_"+std::to_string(uint64_t(original->get_instance_id()));
            const Ref<Material> material=looks.imported_material(cache,[&](){
                if(raw.get_type()==Variant::DICTIONARY){const Dictionary info=raw;const String asset=info.get("asset",String());return curved_material::surface(color_value(info.get("color",Variant()),original->get_albedo()),float(double(info.get("roughness",.9))),asset,float(double(info.get("metallic",0.0))));}
                Ref<ShaderMaterial> m;m.instantiate();m->set_shader(ResourceLoader::get_singleton()->load("res://shaders/model_surface.gdshader"));
                const Ref<Texture2D> texture=original->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);m->set_shader_parameter("model_has_texture",texture.is_valid());if(texture.is_valid())m->set_shader_parameter("model_tex",texture);
                const String project=appearance.get("projectMaterial",String());m->set_shader_parameter("model_role",project.is_empty()?5:2);
                m->set_shader_parameter("model_tint",color_value(appearance.get("color",Variant()),Color(.5,.36,.22)));
                if(!project.is_empty())ArtSurface::configure(m,"model_detail",project);
                const Dictionary means=appearance.get("textureMeans",Dictionary());m->set_shader_parameter("model_source_mean",Vector3(color_value(means.get(name,Variant()),Color(.2,.2,.2)).r,color_value(means.get(name,Variant()),Color(.2,.2,.2)).g,color_value(means.get(name,Variant()),Color(.2,.2,.2)).b));
                const Dictionary scales=appearance.get("uvMetresPerUnit",Dictionary());const double metres=double(scales.get(name,1.0));const double tile=double(appearance.get("tileMetres",.5));m->set_shader_parameter("model_uv_repeat",metres/std::max(.01,tile));
                const Ref<Texture2D> normal=original->get_texture(BaseMaterial3D::TEXTURE_NORMAL);m->set_shader_parameter("model_has_normal",normal.is_valid());if(normal.is_valid())m->set_shader_parameter("model_normal",normal);m->set_shader_parameter("model_normal_depth",appearance.get("normalStrength",.18));
                const Ref<Texture2D> orm=original->get_texture(BaseMaterial3D::TEXTURE_ROUGHNESS);m->set_shader_parameter("model_has_orm",orm.is_valid());if(orm.is_valid())m->set_shader_parameter("model_orm",orm);
                m->set_shader_parameter("model_metallic",original->get_metallic());m->set_shader_parameter("model_roughness",appearance.get("roughness",.88));m->set_meta("art_model_id",id);return Ref<Material>(m);
            });instance->set_surface_override_material(surface,material);
        }
    }
    const auto children=node->get_children();for(int i=0;i<children.size();++i)style(Object::cast_to<Node>(children[i]),looks,id,appearance);
}
}
Node3D *model_art::instantiate_styled(const String &id,SharedLooks &looks){Node3D *node=instantiate(id);if(!node)return nullptr;const Dictionary entry=binding(id);style(node,looks,id,entry.get("appearance",Dictionary()));return node;}
Node3D *model_art::furniture(const std::string &kind,SharedLooks &looks){
    const String id=String("furniture.")+String::utf8(kind.c_str());if(auto *node=instantiate_styled(id,looks))return node;
    if(!::furniture::by_id(kind))return nullptr;auto *root=memnew(Node3D);root->set_meta("art_model_id",id);root->set_meta("art_model_fallback",true);
    Furnishing item;item.kind=kind;std::vector<BoxPart> parts;furniture_parts(item,0,0,parts);
    for(const auto &part:parts)if(part.visual){auto *node=memnew(MeshInstance3D);node->set_mesh(art_mesh::shape(part,looks.part_material(part.mat)));root->add_child(node);}
    return root;
}
