#include "item_art.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

using namespace godot;

void ItemArt::load() {
    if (loaded) return;
    loaded = true;
    const Ref<FileAccess> file = FileAccess::open("res://art/bindings.json", FileAccess::READ);
    if (file.is_null()) return;
    const Variant value = JSON::parse_string(file->get_as_text());
    if (value.get_type() != Variant::DICTIONARY) return;
    const Dictionary root = value;
    if (int(root.get("schemaVersion", 0)) != 1) return;
    const Variant map = root.get("bindings", Variant());
    if (map.get_type() == Variant::DICTIONARY) bindings = map;
}

Ref<Texture2D> ItemArt::texture(const std::string &p_item) {
    load();
    const auto cached = textures.find(p_item);
    if (cached != textures.end()) return cached->second;
    Ref<Texture2D> result;
    const String key = "item." + String::utf8(p_item.c_str());
    const Variant entry = bindings.get(key, Variant());
    if (entry.get_type() == Variant::DICTIONARY) {
        const Dictionary binding = entry;
        const String path = binding.get("path", String());
        if (path.begins_with("res://art/items/") && path.ends_with(".png") && !path.contains("..") && !path.contains("\\") &&
                ResourceLoader::get_singleton()->exists(path, "Texture2D")) {
            result = ResourceLoader::get_singleton()->load(path, "Texture2D");
        }
    }
    textures.emplace(p_item, result);
    return result;
}

bool ItemArt::draw(Control *p_control, const Rect2 &p_rect, const std::string &p_item, float p_alpha) {
    const Ref<Texture2D> image = texture(p_item);
    if (image.is_null()) return false;
    p_control->draw_texture_rect(image, p_rect, false, Color(1, 1, 1, p_alpha));
    const String key = String::utf8(p_item.c_str());
    drawn[key] = int(drawn.get(key, 0)) + 1;
    return true;
}

Dictionary ItemArt::inspect(const String &p_item) {
    const Ref<Texture2D> image = texture(std::string(p_item.utf8().get_data()));
    Dictionary result;
    result["assetId"] = "item." + p_item;
    result["loaded"] = image.is_valid();
    result["draws"] = drawn.get(p_item, 0);
    if (image.is_valid()) {
        result["path"] = image->get_path();
        result["size"] = image->get_size();
        result["instanceId"] = image->get_instance_id();
    }
    return result;
}
