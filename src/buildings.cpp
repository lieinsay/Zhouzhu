#include "buildings.h"

namespace buildings {

const BuildingDef DEFS[] = {
    { "house", "民居", 6.0, 5.0, 0.4, 2.6, 1.6 },
    { "hall", "堂屋", 10.0, 7.0, 0.7, 3.4, 2.4 }, // 村里最大的一座：祠、公所，或大户的正房
    { "granary", "仓", 4.5, 4.5, 1.2, 2.8, 1.8 }, // 台基高，防潮
    { "tower", "望楼", 4.0, 4.0, 0.6, 7.5, 1.4 },
};
const int COUNT = int(sizeof(DEFS) / sizeof(DEFS[0]));

const BuildingDef *by_id(const std::string &p_id) {
    const int k = index_of(p_id);
    return k < 0 ? nullptr : &DEFS[k];
}

int index_of(const std::string &p_id) {
    for (int k = 0; k < COUNT; k++) {
        if (p_id == DEFS[k].id) {
            return k;
        }
    }
    return -1;
}

} // namespace buildings
