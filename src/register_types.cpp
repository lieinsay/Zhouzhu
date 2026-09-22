#include "register_types.h"

#include <gdextension_interface.h>
#include <godot_cpp/godot.hpp>

#include "backpack_panel.h"
#include "build_overlay.h"
#include "build_panel.h"
#include "farm.h"
#include "island.h"
#include "player.h"
#include "refit_overlay.h"
#include "refit_panel.h"
#include "settlement.h"
#include "ship.h"
#include "shipyard.h"
#include "voyage.h"

using namespace godot;

void initialize_zhouzhu_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(Island);
    GDREGISTER_CLASS(Farm);
    GDREGISTER_CLASS(Shipyard);
    GDREGISTER_CLASS(Settlement);
    GDREGISTER_CLASS(Player);
    GDREGISTER_CLASS(Ship);
    GDREGISTER_CLASS(Voyage);
    GDREGISTER_CLASS(BackpackPanel);
    GDREGISTER_CLASS(RefitPanel);
    GDREGISTER_CLASS(RefitOverlay);
    GDREGISTER_CLASS(BuildPanel);
    GDREGISTER_CLASS(BuildOverlay);
}

void uninitialize_zhouzhu_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
}

extern "C" {
GDExtensionBool GDE_EXPORT zhouzhu_library_init(
        GDExtensionInterfaceGetProcAddress p_get_proc_address,
        const GDExtensionClassLibraryPtr p_library,
        GDExtensionInitialization *r_initialization) {
    GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

    init_obj.register_initializer(initialize_zhouzhu_module);
    init_obj.register_terminator(uninitialize_zhouzhu_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

    return init_obj.init();
}
}
