#include "template_library.h"

#include <cstdint>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "interior_rules.h"

using namespace godot;

TemplateLibrary *TemplateLibrary::singleton = nullptr;

namespace {

std::string to_std(const String &p_string) {
    return std::string(p_string.utf8().get_data());
}

String to_godot(const std::string &p_string) {
    return String::utf8(p_string.c_str());
}

uint64_t fnv1a(const std::string &p_text) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : p_text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

} // namespace

TemplateLibrary::TemplateLibrary() {
    if (!singleton) {
        singleton = this;
    }
}

TemplateLibrary::~TemplateLibrary() {
    if (singleton == this) {
        singleton = nullptr;
    }
}

void TemplateLibrary::_bind_methods() {
    ClassDB::bind_method(D_METHOD("reload"), &TemplateLibrary::reload);
    ClassDB::bind_method(D_METHOD("save", "id"), &TemplateLibrary::save);
    ClassDB::bind_method(D_METHOD("path_of", "id"), &TemplateLibrary::path_of);
    ClassDB::bind_method(D_METHOD("get_ids", "shell"), &TemplateLibrary::get_ids);
    ClassDB::bind_method(D_METHOD("get_lines", "id"), &TemplateLibrary::get_lines);
    ClassDB::bind_method(D_METHOD("get_name_of", "id"), &TemplateLibrary::get_name_of);
    ClassDB::bind_method(D_METHOD("get_problems", "id"), &TemplateLibrary::get_problems);
    ClassDB::bind_method(D_METHOD("set_dir", "value"), &TemplateLibrary::set_dir);
    ClassDB::bind_method(D_METHOD("get_dir"), &TemplateLibrary::get_dir);
    ClassDB::bind_method(D_METHOD("set_write_dir", "value"), &TemplateLibrary::set_write_dir);
    ClassDB::bind_method(D_METHOD("get_write_dir"), &TemplateLibrary::get_write_dir);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "dir", PROPERTY_HINT_DIR), "set_dir", "get_dir");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "write_dir"), "set_write_dir", "get_write_dir");
    ADD_SIGNAL(MethodInfo("changed", PropertyInfo(Variant::STRING, "id")));
}

void TemplateLibrary::ensure_loaded() const {
    if (!loaded) {
        const_cast<TemplateLibrary *>(this)->reload();
    }
}

void TemplateLibrary::load_dir(const String &p_dir) {
    for (int k = 0; k < buildings::COUNT; k++) {
        const String shell_dir = p_dir.path_join(buildings::at(k).id);
        if (!DirAccess::dir_exists_absolute(shell_dir)) {
            continue;
        }
        const PackedStringArray files = DirAccess::get_files_at(shell_dir);
        for (int64_t f = 0; f < files.size(); f++) {
            if (!files[f].ends_with(".txt")) {
                continue;
            }
            const String path = shell_dir.path_join(files[f]);
            const PackedStringArray lines = FileAccess::get_file_as_string(path).split("\n");
            std::vector<std::string> text;
            for (int64_t n = 0; n < lines.size(); n++) {
                text.push_back(to_std(lines[n]));
            }
            HouseTemplate t;
            std::vector<std::string> errors;
            if (!parse_template(text, t, &errors)) {
                WARN_PRINT(String(U"房屋模板读不成：") + path + String(" ") + (errors.empty() ? String() : to_godot(errors.back())));
                continue;
            }
            for (const std::string &e : errors) {
                WARN_PRINT(path + String(U"：") + to_godot(e));
            }
            if (t.shell() != buildings::at(k).id || to_godot(t.style()) + String(".txt") != files[f]) {
                WARN_PRINT(String(U"房屋模板的 id 与文件名对不上，照 id 收：") + path + String(" → ") + to_godot(t.id));
            }
            templates[t.id] = t;
        }
    }
}

void TemplateLibrary::reload() {
    templates.clear();
    dirty.clear();
    load_dir(dir);
    if (!write_dir.is_empty() && write_dir != dir) {
        load_dir(write_dir);
    }
    loaded = true;
}

const HouseTemplate *TemplateLibrary::find(const std::string &p_id) const {
    ensure_loaded();
    const auto it = templates.find(p_id);
    return it == templates.end() ? nullptr : &it->second;
}

std::vector<std::string> TemplateLibrary::ids_of(const std::string &p_shell) const {
    ensure_loaded();
    std::vector<std::string> ids;
    for (const auto &entry : templates) {
        if (entry.second.shell() == p_shell) {
            ids.push_back(entry.first); // map 本就按 id 排好
        }
    }
    return ids;
}

std::vector<std::string> TemplateLibrary::auto_ids_of(const std::string &p_shell) const {
    std::vector<std::string> ids;
    for (const std::string &id : ids_of(p_shell)) {
        if (templates.at(id).auto_pick) {
            ids.push_back(id);
        }
    }
    return ids;
}

std::string TemplateLibrary::resolve_id(const Building &p_building) const {
    if (!p_building.style.empty() && find(p_building.kind + "/" + p_building.style)) {
        return p_building.kind + "/" + p_building.style;
    }
    const std::vector<std::string> ids = auto_ids_of(p_building.kind);
    if (ids.empty()) {
        return std::string();
    }
    Building plain = p_building;
    plain.style.clear();
    return ids[size_t(fnv1a(building_id(plain)) % ids.size())];
}

HouseTemplate TemplateLibrary::resolve(const Building &p_building) const {
    const std::string id = resolve_id(p_building);
    const HouseTemplate *t = id.empty() ? nullptr : find(id);
    if (t) {
        return *t;
    }
    const BuildingDef *def = buildings::by_id(p_building.kind);
    return def ? bare_template(*def) : HouseTemplate();
}

void TemplateLibrary::put(const HouseTemplate &p_template) {
    ensure_loaded();
    templates[p_template.id] = p_template;
    dirty[p_template.id] = true;
    emit_signal("changed", to_godot(p_template.id));
}

bool TemplateLibrary::is_dirty(const std::string &p_id) const {
    const auto it = dirty.find(p_id);
    return it != dirty.end() && it->second;
}

std::string TemplateLibrary::unused_style(const std::string &p_shell, const std::string &p_base) const {
    for (int n = 2;; n++) {
        const std::string style = p_base + std::to_string(n);
        if (!find(p_shell + "/" + style)) {
            return style;
        }
    }
}

String TemplateLibrary::path_of(const String &p_id) const {
    const std::string id = to_std(p_id);
    const size_t slash = id.find('/');
    if (slash == std::string::npos) {
        return String();
    }
    const String base = write_dir.is_empty() ? dir : write_dir;
    return base.path_join(to_godot(id.substr(0, slash))).path_join(to_godot(id.substr(slash + 1)) + String(".txt"));
}

bool TemplateLibrary::save(const String &p_id) {
    const HouseTemplate *t = find(to_std(p_id));
    const String path = path_of(p_id);
    if (!t || path.is_empty()) {
        return false;
    }
    DirAccess::make_dir_recursive_absolute(path.get_base_dir());
    Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
    if (file.is_null()) {
        ERR_PRINT(String(U"房屋模板写不了：") + path);
        return false;
    }
    file->store_line(String(U"# 房屋模板：") + to_godot(t->name) + String(U"。格式见 src/house_template.h；在游戏里对着这种房子按 T（开发者）改，F5 存回这里。"));
    for (const std::string &line : template_lines(*t)) {
        file->store_line(to_godot(line));
    }
    file->close();
    dirty[t->id] = false;
    return true;
}

PackedStringArray TemplateLibrary::get_ids(const String &p_shell) const {
    PackedStringArray out;
    for (const std::string &id : ids_of(to_std(p_shell))) {
        out.push_back(to_godot(id));
    }
    return out;
}

PackedStringArray TemplateLibrary::get_lines(const String &p_id) const {
    PackedStringArray out;
    if (const HouseTemplate *t = find(to_std(p_id))) {
        for (const std::string &line : template_lines(*t)) {
            out.push_back(to_godot(line));
        }
    }
    return out;
}

String TemplateLibrary::get_name_of(const String &p_id) const {
    const HouseTemplate *t = find(to_std(p_id));
    return t ? to_godot(t->name) : String();
}

PackedStringArray TemplateLibrary::get_problems(const String &p_id) const {
    PackedStringArray out;
    const HouseTemplate *t = find(to_std(p_id));
    const BuildingDef *def = t ? buildings::by_id(t->shell()) : nullptr;
    if (def) {
        for (const std::string &p : interior_report(*t, *def).problems) {
            out.push_back(to_godot(p));
        }
    }
    return out;
}

void TemplateLibrary::set_dir(const String &p_value) {
    dir = p_value;
    loaded = false;
}

void TemplateLibrary::set_write_dir(const String &p_value) {
    write_dir = p_value;
    loaded = false;
}
