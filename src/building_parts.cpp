#include "building_parts.h"

#include "land_berth.h"

#include <algorithm>
#include <cmath>

#include "interior_rules.h"

using namespace house_dims;

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double FRAME_W = 0.12; // 门框、窗框的宽
constexpr double RUNG_STEP = 0.35;
constexpr double LADDER_BOARD_T = 0.08;
constexpr double LADDER_ZONE_OVER = 1.2; // 爬梯区域超出上一层楼面多高：人到这儿才松开，往前一挪正好落在楼板上
constexpr double LADDER_ZONE_DEPTH = 1.0;

BoxPart box(double p_sx, double p_sy, double p_sz, double p_x, double p_y, double p_z, PartMaterial p_mat, bool p_visual, bool p_solid, int p_group) {
    BoxPart b;
    b.sx = p_sx, b.sy = p_sy, b.sz = p_sz;
    b.x = p_x, b.y = p_y, b.z = p_z;
    b.mat = p_mat;
    b.visual = p_visual;
    b.solid = p_solid;
    b.group = p_group;
    return b;
}

// 墙面上的一段（沿墙 [a, b]、高 [y0, y1]）：沿墙的轴是 x 还是 z 决定哪边是厚度
void wall_piece(std::vector<BoxPart> &r_boxes, bool p_along_x, double p_a, double p_b, double p_c_x, double p_c_z, double p_y0, double p_y1, double p_thick,
        PartMaterial p_mat, bool p_solid, int p_group) {
    const double len = p_b - p_a, h = p_y1 - p_y0;
    if (len < 0.005 || h < 0.005) {
        return;
    }
    const double mid = 0.5 * (p_a + p_b);
    if (p_along_x) {
        r_boxes.push_back(box(len, h, p_thick, mid, 0.5 * (p_y0 + p_y1), p_c_z, p_mat, true, p_solid, p_group));
    } else {
        r_boxes.push_back(box(p_thick, h, len, p_c_x, 0.5 * (p_y0 + p_y1), mid, p_mat, true, p_solid, p_group));
    }
}

// 大矩形扣掉几个洞 → 几块矩形（按洞的边切成格，洞外的格按行并起来）
std::vector<InteriorRect> subtract(const InteriorRect &p_outer, const std::vector<InteriorRect> &p_holes) {
    std::vector<double> xs = { p_outer.x0, p_outer.x1 }, zs = { p_outer.z0, p_outer.z1 };
    for (const InteriorRect &h : p_holes) {
        for (double x : { h.x0, h.x1 }) {
            if (x > p_outer.x0 && x < p_outer.x1) {
                xs.push_back(x);
            }
        }
        for (double z : { h.z0, h.z1 }) {
            if (z > p_outer.z0 && z < p_outer.z1) {
                zs.push_back(z);
            }
        }
    }
    std::sort(xs.begin(), xs.end());
    std::sort(zs.begin(), zs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    zs.erase(std::unique(zs.begin(), zs.end()), zs.end());
    std::vector<InteriorRect> out;
    for (size_t j = 0; j + 1 < zs.size(); j++) {
        double run_from = 0.0;
        bool running = false;
        for (size_t i = 0; i + 1 < xs.size(); i++) {
            const double cx = 0.5 * (xs[i] + xs[i + 1]), cz = 0.5 * (zs[j] + zs[j + 1]);
            bool hole = false;
            for (const InteriorRect &h : p_holes) {
                hole = hole || h.contains(cx, cz);
            }
            if (!hole && !running) {
                run_from = xs[i];
                running = true;
            }
            if (hole && running) {
                out.push_back(InteriorRect{ run_from, zs[j], xs[i], zs[j + 1] });
                running = false;
            }
        }
        if (running) {
            out.push_back(InteriorRect{ run_from, zs[j], xs.back(), zs[j + 1] });
        }
    }
    return out;
}

double ramp_run(double p_drop) {
    double run = std::max(0.6, p_drop / std::tan(RAMP_SLOPE_DEG * PI / 180.0));
    if (run > RAMP_MAX_RUN) {
        run = std::max(RAMP_MAX_RUN, p_drop / std::tan(RAMP_STEEP_DEG * PI / 180.0));
    }
    return run;
}

double side_yaw(WallSide p_side) {
    switch (p_side) {
        case WallSide::FRONT:
            return 0.0;
        case WallSide::RIGHT:
            return 0.5 * PI;
        case WallSide::BACK:
            return PI;
        case WallSide::LEFT:
            return -0.5 * PI;
    }
    return 0.0;
}

} // namespace

void furniture_parts(const Furnishing &p_item, double p_base_y, int p_group, std::vector<BoxPart> &r_boxes) {
    const FurnitureDef *def = furniture::by_id(p_item.kind);
    if (!def) {
        return;
    }
    const int q = ((p_item.quarter % 4) + 4) % 4;
    BoxPart reference;reference.visual=false;reference.solid=false;reference.x=p_item.x;reference.y=p_base_y;reference.z=p_item.z;reference.yaw=q*.5*PI;reference.group=p_group;reference.model_id=std::string("furniture.")+def->id;r_boxes.push_back(reference);
    const double c = std::cos(q * 0.5 * PI), s = std::sin(q * 0.5 * PI);
    // 家具自己的坐标（原点在占地中心的地面，w 沿 x、正面 +z）→ 建筑坐标：+x → (cos, −sin)，+z → (sin, cos)
    auto add = [&](double sx,double sy,double sz,double lx,double ly,double lz,PartMaterial mat,VisualShape shape=VisualShape::BEVEL_BOX) {
        BoxPart part=box(sx,sy,sz,p_item.x+lx*c+lz*s,p_base_y+ly,p_item.z-lx*s+lz*c,mat,true,false,p_group);
        part.yaw=q*.5*PI;part.shape=shape;part.furniture_visual=true;r_boxes.push_back(part);
    };
    const std::string kind = def->id;
    if (kind == "bed") {
        for(double x:{-.86,.86})for(double z:{-.61,.61})add(.10,.34,.10,x,.17,z,PartMaterial::WOOD);
        for(double z:{-.69,.69})add(1.96,.18,.10,0,.34,z,PartMaterial::WOOD);
        for(double x:{-.94,.94})add(.10,.18,1.4,x,.34,0,PartMaterial::WOOD);
        for(int i=0;i<8;++i)add(.14,.04,1.3,-.84+i*.24,.43,0,PartMaterial::WOOD);
        add(1.85,.12,1.35,0,.49,0,PartMaterial::CLOTH);
        add(.35,.13,.62,-.67,.61,0,PartMaterial::CLOTH);
        add(.09,.65,1.46,-.95,.57,0,PartMaterial::DARK_WOOD);
    } else if (kind == "stove") {
        add(1.8, 0.85, 1.0, 0, 0.425, 0, PartMaterial::CLAY);
        add(.60,.08,.60,-.4,.87,.1,PartMaterial::CLAY,VisualShape::RING_Y);
        add(.45,.08,.45,.35,.87,.1,PartMaterial::CLAY,VisualShape::RING_Y);
        add(0.35, 1.9, 0.35, 0.7, 1.8, -0.3, PartMaterial::CLAY);
    } else if (kind == "table") {
        add(1.4, 0.06, 0.9, 0, 0.77, 0, PartMaterial::WOOD);
        for (int a = -1; a <= 1; a += 2) {
            for (int b = -1; b <= 1; b += 2) {
                add(0.08, 0.74, 0.08, a * 0.62, 0.37, b * 0.37, PartMaterial::WOOD);
            }
        }
    } else if (kind == "bench") {
        add(1.2, 0.06, 0.35, 0, 0.42, 0, PartMaterial::WOOD);
        add(0.08, 0.4, 0.3, -0.5, 0.2, 0, PartMaterial::WOOD);
        add(0.08, 0.4, 0.3, 0.5, 0.2, 0, PartMaterial::WOOD);
    } else if (kind == "chest") {
        add(1.18,.08,.58,0,.08,0,PartMaterial::DARK_WOOD);
        add(1.18,.08,.58,0,1.15,0,PartMaterial::WOOD);
        for(double x:{-.55,.55})add(.10,1.05,.58,x,.62,0,PartMaterial::WOOD);
        add(1.08,1.05,.06,0,.62,-.27,PartMaterial::WOOD);
        for(double x:{-.27,.27}){
            add(.52,.98,.045,x,.62,.29,PartMaterial::WOOD);
            add(.03,.13,.035,x+(x<0?.18:-.18),.65,.33,PartMaterial::METAL);
            for(double y:{.28,.95})add(.04,.09,.018,x+(x<0?-.22:.22),y,.322,PartMaterial::METAL);
        }
    } else if (kind == "vat") {
        add(.9,.8,.9,0,.4,0,PartMaterial::CLAY,VisualShape::VAT);
    } else if (kind == "bin") {
        add(1.4,1.3,1.4,0,.65,0,PartMaterial::STRAW,VisualShape::BIN);
        add(1.18,.35,1.18,0,1.34,0,PartMaterial::GRAIN,VisualShape::ELLIPSOID);
    } else if (kind == "altar") {
        add(1.7, 0.92, 0.5, 0, 0.46, 0, PartMaterial::WOOD);
        add(1.8, 0.08, 0.6, 0, 0.96, 0, PartMaterial::LACQUER);
        for (int k = -1; k <= 1; k++) {
            add(0.14, 0.45, 0.05, k * 0.35, 1.225, -0.15, PartMaterial::DARK_WOOD);
        }
        add(.2,.15,.2,0,1.075,.12,PartMaterial::CLAY,VisualShape::VAT);
    } else if (kind == "loom") {
        for (int a = -1; a <= 1; a += 2) {
            for (int b = -1; b <= 1; b += 2) {
                add(0.08, 1.5, 0.08, a * 0.76, 0.75, b * 0.6, PartMaterial::WOOD);
            }
            add(1.6, 0.08, 0.08, 0, 1.46, a * 0.6, PartMaterial::WOOD);
        }
        add(1.3,.02,.9,0,.85,-.1,PartMaterial::CLOTH);
        for(double z:{-.55,.35})add(1.4,.13,.13,0,.88,z,PartMaterial::WOOD,VisualShape::ROLLER_X);
        for(int i=0;i<18;++i)add(.012,.025,1.12,-.59+i*.069,.97,-.10,PartMaterial::CLOTH);
        add(1.32,.06,.04,0,1.02,.10,PartMaterial::DARK_WOOD);
        add(1.26,.04,.10,0,.30,.05,PartMaterial::WOOD);
        add(1.1, 0.06, 0.28, 0, 0.45, 0.48, PartMaterial::WOOD);
    } else if (kind == "shelf") {
        for (int a = -1; a <= 1; a += 2) {
            for (int b = -1; b <= 1; b += 2) {
                add(0.06, 1.9, 0.06, a * 0.77, 0.95, b * 0.22, PartMaterial::WOOD);
            }
        }
        for (double y : { 0.3, 0.95, 1.6 }) {
            add(1.6, 0.04, 0.5, 0, y, 0, PartMaterial::WOOD);
        }
        add(.25,.3,.25,-.4,1.12,0,PartMaterial::CLAY,VisualShape::VAT);
        add(.25,.3,.25,.3,1.12,0,PartMaterial::CLAY,VisualShape::VAT);
        add(.3,.32,.3,0,.48,0,PartMaterial::CLAY,VisualShape::VAT);
        add(.5,.25,.35,.2,1.745,0,PartMaterial::CLOTH,VisualShape::SACK);
    } else if (kind == "sacks") {
        for(double x:{-.29,.29}){
            add(.55,.58,.78,x,.29,0,PartMaterial::CLOTH,VisualShape::SACK);
            add(.10,.035,.10,x,.55,0,PartMaterial::CLOTH,VisualShape::RING_Y);
        }
        add(.50,.30,.59,0,.70,0,PartMaterial::CLOTH,VisualShape::SACK);
        add(.085,.028,.085,0,.835,0,PartMaterial::CLOTH,VisualShape::RING_Y);
    } else if (kind == "drum") {
        for(double x:{-.35,.35})add(.10,.35,.48,x,.175,0,PartMaterial::WOOD);
        add(.8,.8,.45,0,.75,0,PartMaterial::LACQUER,VisualShape::DRUM);
        for(double z:{-.235,.235}){
            add(.72,.72,.014,0,.75,z,PartMaterial::LEATHER,VisualShape::DISC_Z);
            add(.80,.80,.03,0,.75,z,PartMaterial::METAL,VisualShape::RING_Z);
        }
    } else if (kind == "mat") {
        add(2.0, 0.02, 1.4, 0, 0.012, 0, PartMaterial::STRAW);
    }
    if (def->solid) {
        double sx = 0.0, sz = 0.0;
        furniture_extent(*def, q, sx, sz);
        r_boxes.push_back(box(sx, def->h, sz, p_item.x, p_base_y + 0.5 * def->h, p_item.z, PartMaterial::WOOD, false, true, p_group));
    }
}

BuildingParts building_parts(const BuildingDef &p_def, const HouseTemplate &p_house_template, double p_plinth_h, const GroundDrop &p_ground_drop) {
    BuildingParts parts;
    const int levels = std::max(1, p_def.levels);
    parts.group_count = p_def.house ? levels + 2 : 2;
    // 船台不用房屋模板：前沿当中一道宽坡道，别的都在 land_berth 里
    HouseTemplate berth_template;
    if (!p_def.house) {
        berth_template.openings.push_back(Opening{ false, WallSide::FRONT, 0.0, land_berth::RAMP_W - 0.6, 0 });
    }
    const HouseTemplate &p_template = p_def.house ? p_house_template : berth_template;
    std::vector<BoxPart> &out = parts.boxes;
    const double w = p_def.width_m, d = p_def.depth_m, hx = interior_half_x(p_def), hz = interior_half_z(p_def);
    const double plinth_h = std::max(0.05, p_plinth_h);

    // 台基
    out.push_back(box(w + 2 * PLINTH_MARGIN, plinth_h, d + 2 * PLINTH_MARGIN, 0, -0.5 * plinth_h, 0, PartMaterial::STONE, true, true, 0));

    // 外门：坡道 + 台阶；门里门外各记一点
    for (const Opening &o : p_template.openings) {
        if (o.window || o.level != 0) {
            continue;
        }
        double ax = 0.0, az = 0.0, ox = 0.0, oz = 0.0;
        side_axes(o.side, ax, az, ox, oz);
        const double half_out = ox != 0.0 ? 0.5 * w : 0.5 * d;
        const double s = half_out + PLINTH_MARGIN; // 中心 → 台基边
        auto point = [&](double p_t, double &r_x, double &r_z) {
            r_x = ax * o.at + ox * p_t;
            r_z = az * o.at + oz * p_t;
        };
        auto drop_at = [&](double p_t) {
            double x = 0.0, z = 0.0;
            point(p_t, x, z);
            const double drop = p_ground_drop ? p_ground_drop(x, z) : std::nan("");
            return std::isnan(drop) ? std::nan("") : std::max(0.0, drop);
        };
        double drop = drop_at(s + 0.5);
        if (std::isnan(drop)) {
            drop = plinth_h;
        }
        double run = ramp_run(drop);
        for (int pass = 0; pass < 2; pass++) {
            const double foot = drop_at(s + run);
            if (!std::isnan(foot)) {
                drop = foot;
                run = ramp_run(drop);
            }
        }
        DoorEntry entry;
        entry.side = o.side;
        opening_point(p_def, o, 0.6, entry.in_x, entry.in_z);
        entry.in_y = 0.0;
        if (drop < 0.1) {
            point(s + 0.8, entry.out_x, entry.out_z);
            entry.out_y = -drop;
            parts.doors.push_back(entry);
            continue;
        }
        const double width = o.width + 0.6;
        const double yaw = side_yaw(o.side);
        // 碰撞：一块斜板，上沿在台基边、地坪高，下沿在地面
        const double len = std::hypot(run, drop), pitch = std::atan2(drop, run);
        const double nx = drop / len, ny = run / len; // 斜面朝上那侧的法线（沿外法线的分量, y 分量）
        const double thick = 0.3;
        const double mid_t = s + 0.5 * run - nx * 0.5 * thick, mid_y = -0.5 * drop - ny * 0.5 * thick;
        BoxPart ramp = box(width, thick, len, 0, mid_y, 0, PartMaterial::STONE, false, true, 0);
        point(mid_t, ramp.x, ramp.z);
        ramp.yaw = yaw;
        ramp.pitch = pitch;
        out.push_back(ramp);
        // 看上去：几级台阶，每级的面落在坡面的中间高
        const int steps = std::max(1, int(std::ceil(drop / STEP_RISE)));
        const double tread = run / steps;
        for (int k = 0; k < steps; k++) {
            const double top = -drop * (k + 0.5) / steps, bottom = -drop - 0.3;
            BoxPart step = box(width, top - bottom, tread, 0, 0.5 * (top + bottom), 0, PartMaterial::STONE, true, false, 0);
            point(s + (k + 0.5) * tread, step.x, step.z);
            step.yaw = yaw;
            out.push_back(step);
        }
        point(s + run + 0.6, entry.out_x, entry.out_z);
        entry.out_y = -drop;
        parts.doors.push_back(entry);
    }

    if (!p_def.house) {
        land_berth::add_parts(p_def, out);
        if(p_def.house){
        const double roof_w=w+2*house_dims::ROOF_OVERHANG;
        const double half_d=.5*d+house_dims::ROOF_OVERHANG;
        const int columns=std::max(1,int(std::floor(roof_w/.20)));
        const double pitch=std::atan2(p_def.roof_m,half_d);
        for(int col=0;col<columns;++col)for(int side:{-1,1}){
            BoxPart lip=box(.19,.055,.22,-.5*roof_w+(col+.5)*roof_w/columns,p_def.wall_m+.055,side*(half_d-.10),PartMaterial::ROOF,true,false,parts.roof_group());
            lip.pitch=side*pitch;lip.shape=VisualShape::TILE_EDGE;out.push_back(lip);
        }
        // 露明梁与门边接合，只增加视觉构件，不加占地和碰撞。
        for(int side:{-1,1}){
            out.push_back(box(w,.14,.14,0,p_def.wall_m-.08,side*(.5*d+.02),PartMaterial::DARK_WOOD,true,false,parts.roof_group()));
        }
    }
    return parts;
    }

    // 底层木地板（只画，碰撞是台基面）
    out.push_back(box(2 * hx, 0.06, 2 * hz, 0, -0.02, 0, PartMaterial::FLOOR, true, false, 1));

    for (int l = 0; l < levels; l++) {
        const int group = 1 + l;
        const double y0 = level_floor(p_def, l), y1 = l == levels - 1 ? p_def.wall_m : level_floor(p_def, l + 1), h = y1 - y0;
        // 外墙：四面，门窗处开洞
        for (int side = 0; side < 4; side++) {
            const WallSide ws = WallSide(side);
            double ax = 0.0, az = 0.0, ox = 0.0, oz = 0.0;
            side_axes(ws, ax, az, ox, oz);
            const bool along_x = ax != 0.0;
            const double half_out = along_x ? 0.5 * d : 0.5 * w;
            const double c = half_out - 0.5 * WALL_T;
            const double cx = ox * c, cz = oz * c;
            const double lo = along_x ? -0.5 * w : -0.5 * d + WALL_T, hi = -lo;
            std::vector<const Opening *> here;
            for (const Opening &o : p_template.openings) {
                if (o.side == ws && o.level == l) {
                    here.push_back(&o);
                }
            }
            std::sort(here.begin(), here.end(), [](const Opening *a, const Opening *b) { return a->at < b->at; });
            double cursor = lo;
            for (const Opening *o : here) {
                const double a = std::max(lo, o->at - 0.5 * o->width), b = std::min(hi, o->at + 0.5 * o->width);
                double oy0 = 0.0, oy1 = 0.0;
                opening_span(p_def, *o, oy0, oy1);
                oy1 = std::min(oy1, h);
                wall_piece(out, along_x, cursor, a, cx, cz, y0, y1, WALL_T, PartMaterial::WALL, true, group);
                wall_piece(out, along_x, a, b, cx, cz, y0, y0 + oy0, WALL_T, PartMaterial::WALL, true, group);
                wall_piece(out, along_x, a, b, cx, cz, y0 + oy1, y1, WALL_T, PartMaterial::WALL, true, group);
                // 框：两根立框、一根过梁，比墙厚一点，只画
                wall_piece(out, along_x, a - FRAME_W, a, cx, cz, y0 + oy0, y0 + oy1, WALL_T + 0.04, PartMaterial::DARK_WOOD, false, group);
                wall_piece(out, along_x, b, b + FRAME_W, cx, cz, y0 + oy0, y0 + oy1, WALL_T + 0.04, PartMaterial::DARK_WOOD, false, group);
                wall_piece(out, along_x, a - FRAME_W, b + FRAME_W, cx, cz, y0 + oy1, y0 + oy1 + 0.14, WALL_T + 0.04, PartMaterial::DARK_WOOD, false, group);
                // 过梁与立框的木销，沿既有接合位置；两面都能看到，且不侵入门窗净空。
                for(double edge:{a-FRAME_W*.5,b+FRAME_W*.5})for(double sign:{-1.0,1.0}){
                    const double face=(WALL_T+.04)*.5+.002;
                    BoxPart peg=box(.026,.026,.008,along_x?edge:cx+sign*face,y0+oy1-.06,along_x?cz+sign*face:edge,PartMaterial::WOOD,true,false,group);
                    if(!along_x)std::swap(peg.sx,peg.sz);peg.shape=VisualShape::BEVEL_BOX;out.push_back(peg);
                }
                if (o->window) {
                    wall_piece(out, along_x, a - FRAME_W, b + FRAME_W, cx, cz, y0 + oy0 - 0.1, y0 + oy0, WALL_T + 0.04, PartMaterial::DARK_WOOD, false, group);
                    const int bars = std::max(2, int(std::round(o->width / 0.3)));
                    for (int k = 1; k < bars; k++) {
                        const double at = a + (b - a) * k / bars;
                        wall_piece(out, along_x, at - 0.025, at + 0.025, cx, cz, y0 + oy0, y0 + oy1, 0.05, PartMaterial::DARK_WOOD, false, group);
                    }
                }
                cursor = std::max(cursor, b);
            }
            wall_piece(out, along_x, cursor, hi, cx, cz, y0, y1, WALL_T, PartMaterial::WALL, true, group);
        }
        // 楼板：扣掉下层梯子的梯口
        if (l > 0) {
            std::vector<InteriorRect> holes;
            for (const LadderSpot &ladder : p_template.ladders) {
                if (ladder.level + 1 == l) {
                    holes.push_back(ladder_hatch_rect(ladder));
                }
            }
            for (const InteriorRect &r : subtract(InteriorRect{ -hx, -hz, hx, hz }, holes)) {
                out.push_back(box(r.x1 - r.x0, SLAB_T, r.z1 - r.z0, 0.5 * (r.x0 + r.x1), y0 - 0.5 * SLAB_T, 0.5 * (r.z0 + r.z1), PartMaterial::FLOOR, true, true, group));
            }
        }
        // 隔墙：门洞处断开，上面留过梁
        for (size_t k = 0; k < p_template.partitions.size(); k++) {
            if (p_template.partitions[k].level != l) {
                continue;
            }
            const double top = l == levels - 1 ? y1 - CEILING_T : y1 - (l + 1 < levels ? SLAB_T : 0.0);
            for (const InteriorRect &r : partition_pieces(p_template, int(k))) {
                out.push_back(box(r.x1 - r.x0, top - y0, r.z1 - r.z0, 0.5 * (r.x0 + r.x1), 0.5 * (y0 + top), 0.5 * (r.z0 + r.z1), PartMaterial::WALL, true, true, group));
            }
            const Partition &p = p_template.partitions[k];
            const bool along_x = std::abs(p.z1 - p.z0) < 1e-6;
            for (const Doorway &dw : p_template.doorways) {
                if (dw.level != l) {
                    continue;
                }
                const bool on_it = along_x ? std::abs(dw.z - p.z0) < 0.15 && dw.x >= std::min(p.x0, p.x1) - 1e-6 && dw.x <= std::max(p.x0, p.x1) + 1e-6
                                           : std::abs(dw.x - p.x0) < 0.15 && dw.z >= std::min(p.z0, p.z1) - 1e-6 && dw.z <= std::max(p.z0, p.z1) + 1e-6;
                if (!on_it || top - y0 <= DOORWAY_H) {
                    continue;
                }
                const double cy = y0 + 0.5 * (DOORWAY_H + (top - y0));
                if (along_x) {
                    out.push_back(box(DOORWAY_W, top - y0 - DOORWAY_H, PARTITION_T, dw.x, cy, p.z0, PartMaterial::WALL, true, true, group));
                } else {
                    out.push_back(box(PARTITION_T, top - y0 - DOORWAY_H, DOORWAY_W, p.x0, cy, dw.z, PartMaterial::WALL, true, true, group));
                }
            }
        }
        // 家具
        for (const Furnishing &f : p_template.items) {
            if (f.level == l) {
                furniture_parts(f, y0, group, out);
            }
        }
        // 梯子：从这层到上一层的楼面
        for (const LadderSpot &ladder : p_template.ladders) {
            if (ladder.level != l || l + 1 >= levels) {
                continue;
            }
            double ox = 0.0, oz = 0.0;
            ladder_outward(ladder.quarter, ox, oz);
            const double yaw = ladder.quarter * 0.5 * PI;
            const double rise = level_floor(p_def, l + 1) - y0;
            auto at = [&](double p_along, double p_out, double &r_x, double &r_z) {
                // 梯子自己的坐标：x 沿梯板（区域的 +x），z 朝人站的那侧
                r_x = ladder.x + p_along * oz + p_out * ox;
                r_z = ladder.z - p_along * ox + p_out * oz;
            };
            BoxPart board = box(LADDER_W, rise, LADDER_BOARD_T, ladder.x, y0 + 0.5 * rise, ladder.z, PartMaterial::WOOD, true, true, group);
            board.yaw = yaw;
            out.push_back(board);
            for (int k = 0; k * RUNG_STEP + 0.3 < rise; k++) {
                BoxPart rung = box(LADDER_W + 0.1, 0.05, 0.12, 0, y0 + 0.3 + k * RUNG_STEP, 0, PartMaterial::DARK_WOOD, true, false, group);
                at(0.0, 0.1, rung.x, rung.z);
                rung.yaw = yaw;
                out.push_back(rung);
            }
            LadderZone zone;
            zone.sx = LADDER_W + 0.6;
            zone.sy = rise + LADDER_ZONE_OVER;
            zone.sz = LADDER_ZONE_DEPTH;
            at(0.0, 0.5 * LADDER_ZONE_DEPTH, zone.x, zone.z);
            zone.y = y0 + 0.5 * zone.sy;
            zone.yaw = yaw;
            zone.group = group;
            at(0.0, 0.6, zone.foot_x, zone.foot_z);
            zone.foot_y = y0;
            at(0.0, -0.6, zone.top_x, zone.top_z);
            zone.top_y = y0 + rise;
            parts.ladders.push_back(zone);
        }
    }

    // 天花板：带碰撞，第三人称相机在屋里顶到它就停住
    out.push_back(box(w, CEILING_T, d, 0, p_def.wall_m - 0.5 * CEILING_T, 0, PartMaterial::DARK_WOOD, true, true, parts.roof_group()));
    return parts;
}

bool part_contains(const BoxPart &p_part, double p_x, double p_y, double p_z, double p_margin) {
    const double dx = p_x - p_part.x, dy = p_y - p_part.y, dz = p_z - p_part.z;
    const double cy = std::cos(p_part.yaw), sy = std::sin(p_part.yaw);
    // 先反转 yaw（Basis(Y, yaw) 的转置），再反转 pitch
    const double lx = cy * dx - sy * dz, lz0 = sy * dx + cy * dz;
    const double cp = std::cos(p_part.pitch), sp = std::sin(p_part.pitch);
    const double ly = cp * dy + sp * lz0, lz = -sp * dy + cp * lz0;
    return std::abs(lx) <= 0.5 * p_part.sx + p_margin && std::abs(ly) <= 0.5 * p_part.sy + p_margin && std::abs(lz) <= 0.5 * p_part.sz + p_margin;
}
