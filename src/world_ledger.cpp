#include "world_ledger.h"

#include <algorithm>
#include <cmath>

#include "freight.h"

uint64_t world_village_seed(uint64_t p_stream_seed, const SiteSettlement &p_site) {
    const uint64_t host = p_stream_seed * 2654435761ull + uint64_t(p_site.id) * 2ull + (p_site.hamlet ? 1ull : 0ull);
    return (host & 0x7FFFFFFFull) ^ host;
}

WorldLedger open_world_ledger(const WorldTerrain &p_terrain, const WorldLedgerSpec &p_spec, const SimRules &p_rules) {
    WorldLedger out;
    const SiteSettlement &site = p_spec.site;
    const VillagePlan plan = plan_world_village(p_terrain, site, p_spec.seed, WorldVillageRules());
    out.layout = apply_layout_edits(plan.layout, parse_layout_edits(p_spec.edits));
    out.ground = std::make_unique<WorldGround>(p_terrain, site.x, site.z);
    out.x = site.x;
    out.z = site.z;
    out.plots = p_spec.lattice.shifted(site.x, site.z);
    out.water = village_water(*out.ground, out.layout, p_rules.work);
    // 农田的勘察另开一份（它有缓存，不能和别的线程共用）
    const WorldGround land_ground(p_terrain, 0.0, 0.0);
    const FarmModel land(&land_ground, p_spec.lattice, p_spec.farm_rules);
    out.has_farm = village_has_farm(land, site.x, site.z, out.layout, p_rules.work);
    out.state = std::make_shared<VillageState>();
    ArableMask arable;
    if (out.has_farm) {
        // 农夫只在家门口 field_radius_m 以内开荒：把村子周围这一圈能不能垦先勘好
        double cx = 0.0, cz = 0.0, reach = 0.0;
        village_extent(out.layout, cx, cz, reach);
        arable = survey_arable(land, site.x + cx, site.z + cz, reach + p_rules.work.field_radius_m + 10.0);
    }
    VillageWorld w;
    w.layout = &out.layout;
    w.water = &out.water;
    w.ground = out.ground.get();
    w.day = &p_spec.day;
    w.rules = &p_rules;
    // 仓 = 粮种 + 这村的土产（托运的货，见 freight.h；不是粮，吃饭播种都不动它）
    Inventory store = village_starting_store();
    store.add(freight::specialty_of(p_spec.seed, site.id, site.hamlet), freight::specialty_stock(site.households, FreightRules()));
    found_village(*out.state, w, out.has_farm, !out.water.empty(), p_spec.per_house, p_spec.max_folk, world_village_seed(p_spec.seed, site),
            store, p_spec.now);
    out.state->arable = std::move(arable);
    return out;
}

double resume_clock(double p_saved_clock, double p_now, double p_max_seconds) {
    return std::max(p_saved_clock, p_now - std::max(0.0, p_max_seconds));
}

double world_ledger_height(const WorldLedger &p_ledger) {
    double h = 0.0;
    if (!p_ledger.ground || !p_ledger.ground->site_height(0.0, 0.0, h) || std::isnan(h)) {
        h = 0.0;
    }
    return h;
}

void settle_world_ledger(WorldLedger &r_ledger, const FarmModel &p_land, ClaimRegistry *p_taken, const DayRules &p_day, const SimRules &p_rules, double p_until,
        const SeasonView *p_season) {
    VillageState &s = *r_ledger.state;
    HeldFields held(s.held, &p_land, &s.arable);
    VillageWorld w;
    w.layout = &r_ledger.layout;
    w.plots = &r_ledger.plots;
    w.water = &r_ledger.water;
    w.ground = r_ledger.ground.get();
    w.fields = &held;
    w.taken = p_taken;
    w.day = &p_day;
    w.rules = &p_rules;
    w.season = p_season;
    advance_village(s, w, p_until);
}
