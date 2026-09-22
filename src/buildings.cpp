#include "buildings.h"

namespace buildings {

const BuildingDef DEFS[] = {
    // 尺度按人（1.8 m）来定，且比真实略大一成：第三人称的相机抬得高、视野宽，真实尺度的房子看着像茅屋
    { "house", "民居", 11.0, 6.5, 0.5, 3.3, 2.6 }, // 三间：面阔三间约 11 m
    { "hall", "堂屋", 17.0, 9.5, 1.0, 4.6, 3.8 }, // 五间：村里最大的一座——祠、公所，或大户的正房
    { "granary", "仓", 6.0, 6.0, 1.8, 3.4, 2.6 }, // 台基高，防潮
    { "tower", "望楼", 6.0, 6.0, 0.8, 12.0, 2.2 },
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
