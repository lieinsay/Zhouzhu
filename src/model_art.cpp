#include "model_art.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
using namespace godot;
Dictionary model_art::binding(const String &id){
    const Ref<FileAccess> file=FileAccess::open("res://art/model-bindings.json",FileAccess::READ);if(file.is_null())return {};
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
