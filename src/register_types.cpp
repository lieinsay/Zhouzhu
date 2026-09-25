#include "register_types.h"

#include <gdextension_interface.h>
#include <godot_cpp/godot.hpp>

#include "build_overlay.h"
#include "build_panel.h"
#include "dev_tools.h"
#include "expedition.h"
#include "farm.h"
#include "flat_map.h"
#include "hinterland.h"
#include "hotbar_bar.h"
#include "inventory_panel.h"
#include "island.h"
#include "island_map.h"
#include "placed_props.h"
#include "player.h"
#include "refit_overlay.h"
#include "refit_panel.h"
#include "settlement.h"
#include "site_stream.h"
#include "ship.h"
#include "shipyard.h"
#include "villagers.h"
#include "world.h"
#include "world_map.h"
#include "world_resource.h"
#include "world_village.h"
#include "interior_overlay.h"
#include "interior_panel.h"
#include "template_library.h"
#include "world_clock.h"

using namespace godot;

void initialize_zhouzhu_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(WorldClock);
    GDREGISTER_CLASS(TemplateLibrary);
    GDREGISTER_CLASS(Island);
    GDREGISTER_CLASS(Farm);
    GDREGISTER_CLASS(Shipyard);
    GDREGISTER_CLASS(Settlement);
    GDREGISTER_CLASS(Villagers);
    GDREGISTER_CLASS(Player);
    GDREGISTER_CLASS(Ship);
    GDREGISTER_CLASS(IslandMap);
    GDREGISTER_CLASS(FlatMap);
    GDREGISTER_CLASS(InventoryPanel);
    GDREGISTER_CLASS(HotbarBar);
    GDREGISTER_CLASS(PlacedProps);
    GDREGISTER_CLASS(RefitPanel);
    GDREGISTER_CLASS(RefitOverlay);
    GDREGISTER_CLASS(BuildPanel);
    GDREGISTER_CLASS(BuildOverlay);
    GDREGISTER_CLASS(InteriorPanel);
    GDREGISTER_CLASS(InteriorOverlay);
    GDREGISTER_CLASS(World);
    GDREGISTER_CLASS(WorldMap);
    GDREGISTER_CLASS(WorldVillage);
    GDREGISTER_CLASS(WorldDock);
    GDREGISTER_CLASS(WorldResource);
    GDREGISTER_CLASS(SiteStream);
    GDREGISTER_CLASS(Hinterland);
    GDREGISTER_CLASS(Expedition);
    GDREGISTER_CLASS(DevTools);
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
