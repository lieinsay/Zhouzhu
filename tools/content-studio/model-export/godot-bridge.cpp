#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include "placed_props.h"
#include "scatter_meshes.h"
#include "crops.h"

using namespace godot;

// Tool-only GDExtension: compile the unchanged game's mesh builders into a
// separate DLL. No class or asset in the game project is modified or replaced.
class ContentStudioModels : public Node3D {
    GDCLASS(ContentStudioModels, Node3D)
    ScatterMeshes meshes;

    void scatter(Array &rows, ScatterLook look, uint8_t sub, const String &suffix = String()) {
        const ScatterKind &kind = deposits::scatter(look);
        Node3D *root = memnew(Node3D);
        const String key = String(kind.key) + suffix;
        root->set_name("scatter_" + key);
        add_child(root);
        MeshInstance3D *instance = memnew(MeshInstance3D);
        instance->set_mesh(meshes.mesh(look));
        instance->set_meta("export_instance_color", scatter_meshes::instance_color(look, sub, 0));
        root->add_child(instance);
        Dictionary row;
        row["id"] = "scatter:" + key;
        row["name"] = String::utf8(deposits::scatter_name(look, sub).c_str());
        row["node"] = root;
        row["usage"] = "gather-source";
        row["item"] = String::utf8(deposits::scatter_product(look, sub).c_str());
        row["source"] = "src/scatter_meshes.cpp";
        row["subtype"] = int(sub);
        rows.push_back(row);
    }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("generate"), &ContentStudioModels::generate);
    }

public:
    Array generate() {
        Array rows;
        for (int k = 0; k < props::PROP_COUNT; k++) {
            const PropDef &def = props::PROPS[k];
            Node3D *root = memnew(Node3D);
            root->set_name("prop_" + String(def.item));
            add_child(root);
            build_prop_model(root, def, 0.0, Ref<Material>());
            Dictionary row;
            row["id"] = "prop:" + String(def.item);
            row["name"] = String::utf8(catalog::item_name(def.item).c_str());
            row["node"] = root;
            row["usage"] = "placed";
            row["item"] = String(def.item);
            row["source"] = "src/placed_props.cpp";
            rows.push_back(row);
        }
        for (int k = 0; k < deposits::SCATTER_COUNT; k++) {
            const ScatterLook look = deposits::SCATTER[k].look;
            scatter(rows, look, look == ScatterLook::REEDS ? deposits::SUB_REED : 0);
        }
        scatter(rows, ScatterLook::REEDS, 0, "_peat");
        scatter(rows, ScatterLook::CRUST, deposits::SUB_GUANO, "_guano");
        for (uint8_t sub = 1; sub < 8; sub++) {
            scatter(rows, ScatterLook::ORE_ROCK, sub, "_" + String::utf8(deposits::scatter_product(ScatterLook::ORE_ROCK, sub).c_str()));
        }
        return rows;
    }
};

void initialize_content_studio(ModuleInitializationLevel level) {
    if (level == MODULE_INITIALIZATION_LEVEL_SCENE) GDREGISTER_CLASS(ContentStudioModels);
}
void uninitialize_content_studio(ModuleInitializationLevel) {}

extern "C" {
GDExtensionBool GDE_EXPORT content_studio_library_init(GDExtensionInterfaceGetProcAddress get_proc,
        GDExtensionClassLibraryPtr library, GDExtensionInitialization *initialization) {
    GDExtensionBinding::InitObject init(get_proc, library, initialization);
    init.register_initializer(initialize_content_studio);
    init.register_terminator(uninitialize_content_studio);
    init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}
}
