#include "template_set.h"

#include <cstdint>

namespace {

uint64_t fnv1a(const std::string &p_text) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : p_text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

} // namespace

std::vector<std::string> template_ids_of(const TemplateMap &p_templates, const std::string &p_shell) {
    std::vector<std::string> ids;
    for (const auto &entry : p_templates) {
        if (entry.second.shell() == p_shell) {
            ids.push_back(entry.first); // map 本就按 id 排好
        }
    }
    return ids;
}

std::vector<std::string> template_auto_ids_of(const TemplateMap &p_templates, const std::string &p_shell) {
    std::vector<std::string> ids;
    for (const std::string &id : template_ids_of(p_templates, p_shell)) {
        if (p_templates.at(id).auto_pick) {
            ids.push_back(id);
        }
    }
    return ids;
}

std::string resolve_template_id(const TemplateMap &p_templates, const Building &p_building) {
    if (!p_building.style.empty() && p_templates.count(p_building.kind + "/" + p_building.style)) {
        return p_building.kind + "/" + p_building.style;
    }
    const std::vector<std::string> ids = template_auto_ids_of(p_templates, p_building.kind);
    if (ids.empty()) {
        return std::string();
    }
    Building plain = p_building;
    plain.style.clear();
    return ids[size_t(fnv1a(building_id(plain)) % ids.size())];
}

HouseTemplate resolve_template(const TemplateMap &p_templates, const Building &p_building) {
    const std::string id = resolve_template_id(p_templates, p_building);
    const auto found = id.empty() ? p_templates.end() : p_templates.find(id);
    if (found != p_templates.end()) {
        return found->second;
    }
    const BuildingDef *def = buildings::by_id(p_building.kind);
    return def ? bare_template(*def) : HouseTemplate();
}
