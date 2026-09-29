#include "piece_hand.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_shape_query_parameters3d.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/object.hpp>

#include "crops.h"
#include "ground_works.h"
#include "input_actions.h"
#include "piece_mesh.h"
#include "placed_props.h"
#include "scatter_stream.h"
#include "settlement.h"
#include "shared_looks.h"
#include "ship.h"
#include "works_view.h"

using namespace godot;

namespace {

constexpr const char *REPO_STYLES = "res://structures";
constexpr const char *USER_STYLES = "user://structures";

String utf8(const std::string &p_text) {
    return String::utf8(p_text.c_str());
}

String piece_name(const std::string &p_item) {
    return PackHand::name_of(p_item);
}

Transform3D structure_xform(const Structure &p_s) {
    return Transform3D(Basis(Vector3(0, 1, 0), real_t(p_s.yaw)), Vector3(real_t(p_s.x), real_t(p_s.y), real_t(p_s.z)));
}

const char *dir_name(int p_dir) {
    static const char *NAMES[4] = { "南", "东", "北", "西" };
    return NAMES[((p_dir % 4) + 4) % 4];
}

} // namespace

void PieceHand::setup(Node3D *p_ghost_parent) {
    ghost_parent = p_ghost_parent;
    for (Ref<StandardMaterial3D> *m : { &ghost_good, &ghost_bad }) {
        m->instantiate();
        (*m)->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        (*m)->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    }
    ghost_good->set_albedo(Color(0.45f, 1.0f, 0.5f, 0.38f));
    ghost_bad->set_albedo(Color(1.0f, 0.35f, 0.3f, 0.38f));
    load_styles();
}

void PieceHand::load_styles() {
    styles.clear();
    for (const char *dir : { REPO_STYLES, USER_STYLES }) {
        if (!DirAccess::dir_exists_absolute(dir)) {
            continue;
        }
        PackedStringArray files = DirAccess::get_files_at(dir);
        files.sort();
        for (int64_t k = 0; k < files.size(); k++) {
            if (!files[k].ends_with(".txt")) {
                continue;
            }
            const String path = String(dir).path_join(files[k]);
            const PackedByteArray bytes = FileAccess::get_file_as_bytes(path);
            StructureStyle style;
            style.id = files[k].get_basename().utf8().get_data();
            std::string error;
            if (!parse_structure_style(std::string(reinterpret_cast<const char *>(bytes.ptr()), size_t(bytes.size())), style, error)) {
                ERR_PRINT(vformat(String(U"PieceHand：样式 %s 读不了（%s）"), path, utf8(error)));
                continue;
            }
            styles.push_back(style);
        }
    }
}

bool PieceHand::holds(const std::string &p_held) const {
    return style_index >= 0 || pieces::is_piece(p_held);
}

Transform3D PieceHand::frame_xform() const {
    return site.frame ? site.frame->get_global_transform() : Transform3D();
}

void PieceHand::stand_down() {
    hide_ghost();
    ghost_on = false;
    look_structure = look_piece = -1;
    // 营造账、它的框架与 WorksView 只在看着的那一帧可信：站下来（上了船、开着面板）就不再拿着——人上船飞走、脚下原来那一群
    // 降档拆掉以后，下船那一帧 HUD 问「在不在自己盖的屋里」还会读这份账（用了已经释放的账会崩：开船去 #2051 下船时真遇上了）
    site = WorksSite();
    view = nullptr;
}

void PieceHand::hide_ghost() {
    if (ghost) {
        ghost->set_visible(false);
    }
    ghost_on = false;
}

void PieceHand::show_ghost(const Structure &p_frame, const std::vector<Piece> &p_pieces, bool p_ok) {
    if (!ghost_parent) {
        return;
    }
    // 签名没变就只挪位置
    uint64_t sig = 1469598103934665603ull;
    auto mix = [&sig](uint64_t v) { sig = (sig ^ v) * 1099511628211ull; };
    mix(p_ok ? 1 : 2);
    mix(uint64_t(p_frame.pieces.size()));
    for (const Piece &p : p_pieces) {
        for (char c : p.item) {
            mix(uint64_t(uint8_t(c)));
        }
        mix(uint64_t(int64_t(p.spot.x) * 73856093 ^ int64_t(p.spot.z) * 19349663));
        mix(uint64_t(p.spot.level * 64 + p.spot.tier * 8 + p.spot.dir + int(p.spot.slot) * 1024));
        mix(uint64_t(int64_t(std::llround(p.bottom * 100.0))));
    }
    if (!ghost || sig != ghost_signature) {
        if (ghost) {
            ghost_parent->remove_child(ghost);
            ghost->queue_free();
        }
        ghost = memnew(Node3D);
        ghost->set_name("PieceGhost");
        ghost->set_as_top_level(true);
        ghost_parent->add_child(ghost);
        ghost_signature = sig;
        Structure context = p_frame;
        StructureMesh mesh;
        for (const Piece &p : p_pieces) {
            context.pieces.push_back(p);
        }
        for (const Piece &p : p_pieces) {
            const StructureMesh one = build_piece_mesh(context, p);
            for (int m = 0; m < int(PartMaterial::COUNT); m++) {
                MeshPart &dst = mesh.parts[size_t(m)];
                const MeshPart &src = one.parts[size_t(m)];
                const int base = dst.vertex_count();
                dst.positions.insert(dst.positions.end(), src.positions.begin(), src.positions.end());
                dst.normals.insert(dst.normals.end(), src.normals.begin(), src.normals.end());
                for (int32_t i : src.indices) {
                    dst.indices.push_back(base + i);
                }
            }
        }
        SharedLooks looks;
        build_structure_nodes(ghost, mesh, looks, p_ok ? ghost_good : ghost_bad);
    }
    ghost->set_global_transform(frame_xform() * structure_xform(p_frame));
    ghost->set_visible(true);
    ghost_on = true;
}

std::string PieceHand::obstacle(Player *p_player, const Structure &p_frame, const Piece &p_piece, int p_structure) const {
    const PieceDef *def = pieces::find(p_piece.item);
    if (!def || !p_player->is_inside_tree()) {
        return std::string();
    }
    double cx, cy, cz, hx, hy, hz;
    piece_bounds(p_frame, p_piece, cx, cy, cz, hx, hy, hz);
    if (def->role == PieceRole::FOUNDATION) {
        // 地基：只查地坪以上一人高（台基本来就埋在地里）
        cy = 1.1;
        hy = 0.95;
    }
    const real_t shrink = 0.06f;
    Ref<BoxShape3D> box;
    box.instantiate();
    box->set_size(Vector3(std::max(real_t(0.04), real_t(2.0 * hx) - shrink), std::max(real_t(0.04), real_t(2.0 * hy) - shrink), std::max(real_t(0.04), real_t(2.0 * hz) - shrink)));
    Ref<PhysicsShapeQueryParameters3D> query;
    query.instantiate();
    query->set_shape(box);
    query->set_transform(frame_xform() * structure_xform(p_frame) * Transform3D(Basis(), Vector3(real_t(cx), real_t(cy), real_t(cz))));
    query->set_collision_mask(1);
    TypedArray<RID> exclude;
    exclude.push_back(p_player->get_rid());
    if (view && p_structure >= 0) {
        if (StaticBody3D *body = view->get_structure_body(p_structure)) {
            exclude.push_back(body->get_rid());
        }
    }
    query->set_exclude(exclude);
    const TypedArray<Dictionary> hits = p_player->get_world_3d()->get_direct_space_state()->intersect_shape(query, 1);
    if (hits.is_empty()) {
        return std::string();
    }
    Object *hit = Object::cast_to<Object>(Dictionary(hits[0])["collider"]);
    if (ScatterStream::is_trunk(hit)) {
        return "树挡着";
    }
    for (Node *node = Object::cast_to<Node>(hit); node; node = node->get_parent()) {
        if (Object::cast_to<Settlement>(node)) {
            return "房子挡着";
        }
        if (Object::cast_to<Ship>(node)) {
            return "船挡着";
        }
        if (Object::cast_to<PlacedProps>(node)) {
            return "摆着的东西挡着";
        }
        if (Object::cast_to<WorksView>(node)) {
            return "别的一座挡着";
        }
    }
    return def->role == PieceRole::FOUNDATION ? "有东西挡着" : "地面挡着：先挖平";
}

void PieceHand::look(const WorksSite &p_site, MapHost *p_map, Player *p_player, const std::string &p_held, const AimHit &p_aim, bool p_free) {
    site = p_site;
    view = site.view.is_valid() ? Object::cast_to<WorksView>(ObjectDB::get_instance(site.view)) : nullptr;
    look_structure = look_piece = -1;
    if (!site.works) {
        hide_ghost();
        return;
    }
    const Transform3D xf = frame_xform();
    const Transform3D inv = xf.affine_inverse();
    const Vector3 at = inv.xform(p_player->get_global_position());
    // 准星对着哪一件（够得着的）
    if (view && p_aim.hit) {
        int sid = -1;
        Vector3 local;
        if (view->structure_hit(p_aim.collider, p_aim.point - p_aim.normal * 0.03f, sid, local)) {
            const Structure *s = site.works->structures.find(sid);
            if (s && Vector2(p_aim.point.x - p_player->get_global_position().x, p_aim.point.z - p_player->get_global_position().z).length() <= rules.reach_m) {
                const int pid = piece_at(*s, local.x, local.y, local.z);
                if (pid >= 0) {
                    look_structure = sid;
                    look_piece = pid;
                }
            }
        }
    }
    held_item = p_held;
    const PieceDef *holding = pieces::find(held_item);
    if (style_index < 0 && !holding) {
        hide_ghost();
        return;
    }
    Input *input = Input::get_singleton();
    turns += input->is_action_just_pressed(input_actions::ROTATE_RIGHT) ? 1 : 0;
    turns -= input->is_action_just_pressed(input_actions::ROTATE_LEFT) ? 1 : 0;
    if (!p_aim.hit) {
        ghost_ok = false;
        ghost_why = "准星对准地面或一座";
        hide_ghost();
        return;
    }
    PieceAim aim;
    const Vector3 hit = inv.xform(p_aim.point);
    aim.x = hit.x, aim.y = hit.y, aim.z = hit.z;
    aim.from_x = at.x, aim.from_y = at.y, aim.from_z = at.z;
    const double frame_yaw = xf.basis.get_euler().y;
    aim.look_yaw = p_player->get_look_yaw() - frame_yaw;
    aim.turns = turns;
    const MapFrameGround ground(p_map, xf);
    if (style_index >= 0) {
        style_plan = plan_structure_style(site.works->structures, styles[size_t(style_index)], aim, ground, rules);
        ghost_ok = style_plan.ok;
        ghost_why = style_plan.why;
        if (ghost_ok) {
            // 整座的每一件查挡没挡（地基只查地坪以上）
            for (const Piece &p : style_plan.structure.pieces) {
                const std::string why = obstacle(p_player, style_plan.structure, p, -1);
                if (!why.empty()) {
                    ghost_ok = false;
                    ghost_why = why;
                    break;
                }
            }
        }
        Structure empty = style_plan.structure;
        empty.pieces.clear();
        show_ghost(empty, style_plan.structure.pieces, ghost_ok);
        return;
    }
    plan = plan_piece(site.works->structures, *holding, aim, ground, rules);
    ghost_ok = plan.ok;
    ghost_why = plan.why;
    Piece probe;
    probe.item = held_item;
    probe.spot = plan.spot;
    probe.bottom = plan.bottom;
    const int structure_id = plan.structure >= 0 ? site.works->structures.get()[size_t(plan.structure)].id : -1;
    if (ghost_ok) {
        const std::string why = obstacle(p_player, plan.frame, probe, structure_id);
        if (!why.empty()) {
            ghost_ok = false;
            ghost_why = why;
        }
    }
    if (!plan.located) {
        hide_ghost(); // 连个位置都没有（准星离哪一座都远，也不是地基）
        return;
    }
    show_ghost(plan.frame, { probe }, ghost_ok);
}

bool PieceHand::claims(const std::string &p_held, bool p_interact, bool p_remove) const {
    if (p_remove && look_piece >= 0) {
        return true;
    }
    if (!p_interact) {
        return false;
    }
    if (holds(p_held)) {
        return true;
    }
    // 看着门：E 开关
    if (look_piece >= 0 && site.works) {
        const Structure *s = site.works->structures.find(look_structure);
        const Piece *p = s ? s->find(look_piece) : nullptr;
        return p && pieces::find(p->item) && pieces::find(p->item)->role == PieceRole::DOOR;
    }
    return false;
}

PieceHand::Step PieceHand::act(Player *p_player, PackHand &p_pack, const std::string &p_held, bool p_interact, bool p_remove, bool p_free, const Say &p_say) {
    Step out;
    if (!site.works) {
        return out;
    }
    Structures &list = site.works->structures;
    const Structure *looked = look_structure >= 0 ? list.find(look_structure) : nullptr;
    const Piece *looked_piece = looked ? looked->find(look_piece) : nullptr;
    const PieceDef *looked_def = looked_piece ? pieces::find(looked_piece->item) : nullptr;
    if (p_remove && looked_piece) {
        out.used = true;
        const std::string item = looked_piece->item;
        if (!p_free && p_pack.items().room_for(item) < 1) {
            p_say(String(U"背包满了，拆不回来"));
            return out;
        }
        std::string why;
        if (!list.remove(look_structure, look_piece, why)) {
            p_say(String(U"拆不了：") + utf8(why));
            return out;
        }
        if (!p_free) {
            p_pack.items().put(item, 1);
            p_pack.refresh_bar();
        }
        look_structure = look_piece = -1;
        p_say(String(U"拆下了") + piece_name(item));
        return out;
    }
    if (!p_interact) {
        return out;
    }
    // 看着门、手上没拿着要摆的（或摆不下）：开关门，往离人远的那面开
    if (looked_def && looked_def->role == PieceRole::DOOR && !(holds(p_held) && ghost_on && ghost_ok)) {
        out.used = true;
        int open = 0;
        if (looked_piece->open == 0) {
            double lx, ly, lz;
            const Vector3 at = frame_xform().affine_inverse().xform(p_player->get_global_position());
            looked->to_local(at.x, at.y, at.z, lx, ly, lz);
            const double n = looked_piece->spot.dir == 0 ? lz - looked_piece->spot.z * piece_dims::GRID_M : lx - looked_piece->spot.x * piece_dims::GRID_M;
            open = n > 0.0 ? -1 : 1;
        }
        list.set_open(look_structure, look_piece, open);
        p_say(open == 0 ? String(U"关上了门") : String(U"开了门"));
        return out;
    }
    if (style_index >= 0) {
        out.used = true;
        const StructureStyle &style = styles[size_t(style_index)];
        if (!ghost_on || !ghost_ok) {
            p_say(String(U"摆不了「") + utf8(style.name) + String(U"」：") + utf8(ghost_why));
            return out;
        }
        if (!p_free) {
            std::string missing;
            for (const auto &b : style_bill(style)) {
                const int have = p_pack.items().count(b.first);
                if (have < b.second) {
                    missing += (missing.empty() ? "" : "、") + catalog::item_name(b.first) + " " + std::to_string(b.second - have);
                }
            }
            if (!missing.empty()) {
                p_say(String(U"件不够：还缺 ") + utf8(missing));
                return out;
            }
            for (const auto &b : style_bill(style)) {
                p_pack.items().take(b.first, b.second);
            }
            p_pack.refresh_bar();
        }
        list.add_structure(style_plan.structure);
        p_say(String(U"摆下了一整座「") + utf8(style.name) + String(U"」"));
        set_style(-1);
        return out;
    }
    const PieceDef *holding = pieces::find(p_held);
    if (!holding) {
        return out;
    }
    out.used = true;
    if (!ghost_on || !ghost_ok) {
        p_say(String(U"摆不了") + piece_name(holding->item) + String(U"：") + utf8(ghost_why.empty() ? std::string("准星对准地面或一座") : ghost_why));
        return out;
    }
    if (!p_free && p_pack.items().take_at(p_pack.get_selected(), 1) != 1) {
        p_say(String(U"手上没有了"));
        return out;
    }
    list.add(holding->item, plan);
    if (!p_free) {
        p_pack.refresh_bar();
    }
    p_say(String(U"摆下了") + piece_name(holding->item));
    return out;
}

void PieceHand::set_style(int p_index) {
    style_index = p_index >= 0 && p_index < int(styles.size()) ? p_index : -1;
    turns = 0;
    if (style_index < 0) {
        hide_ghost();
    }
}

int PieceHand::save_looked_as_style(String &r_message) {
    const Structure *s = site.works && look_structure >= 0 ? site.works->structures.find(look_structure) : nullptr;
    if (!s) {
        r_message = String(U"准星对着自己盖的一座才能另存");
        return -1;
    }
    DirAccess::make_dir_recursive_absolute(USER_STYLES);
    int n = 1;
    String id;
    do {
        id = vformat("mine_%d", n++);
    } while (FileAccess::file_exists(String(USER_STYLES).path_join(id + ".txt")));
    const String name = vformat(String(U"我的样式 %d"), n - 1);
    const StructureStyle style = style_from_structure(*s, id.utf8().get_data(), name.utf8().get_data());
    const std::string text = save_structure_style(style);
    Ref<FileAccess> f = FileAccess::open(String(USER_STYLES).path_join(id + ".txt"), FileAccess::WRITE);
    if (f.is_null()) {
        r_message = String(U"写不了样式文件");
        return -1;
    }
    PackedByteArray bytes;
    bytes.resize(int64_t(text.size()));
    std::copy(text.begin(), text.end(), bytes.ptrw());
    f->store_buffer(bytes);
    f->close();
    styles.push_back(style);
    r_message = String(U"另存成样式「") + name + vformat(String(U"」（%d 件）"), int(style.pieces.size()));
    return int(styles.size()) - 1;
}

void PieceHand::refresh_reports() {
    if (!site.works) {
        reports.clear();
        return;
    }
    if (site.works->structures.get_revision() == report_revision && reports.size() == size_t(site.works->structures.size())) {
        return;
    }
    report_revision = site.works->structures.get_revision();
    reports.clear();
    for (const Structure &s : site.works->structures.get()) {
        reports.push_back({ s.id, check_house(s) });
    }
}

const HouseReport *PieceHand::report_of(int p_structure) {
    refresh_reports();
    for (const auto &r : reports) {
        if (r.first == p_structure) {
            return &r.second;
        }
    }
    return nullptr;
}

bool PieceHand::inside_house(const Vector3 &p_global) {
    if (!site.works) {
        return false;
    }
    refresh_reports();
    const Vector3 at = frame_xform().affine_inverse().xform(p_global);
    for (const Structure &s : site.works->structures.get()) {
        const HouseReport *r = report_of(s.id);
        double lx, ly, lz;
        s.to_local(at.x, at.y, at.z, lx, ly, lz);
        if (r && inside_complete_room(s, *r, lx, ly, lz)) {
            return true;
        }
    }
    return false;
}

String PieceHand::prompt(const std::string &p_held) {
    String house;
    if (look_structure >= 0) {
        if (const HouseReport *r = report_of(look_structure)) {
            house = r->complete ? String(U" · 这座是一座完整的屋") : String(U" · 这座还缺：") + utf8(r->missing);
        }
    }
    if (style_index >= 0) {
        const StructureStyle &style = styles[size_t(style_index)];
        String bill;
        for (const auto &b : style_bill(style)) {
            bill += (bill.is_empty() ? String() : String(U"、")) + piece_name(b.first) + vformat(" %d", b.second);
        }
        if (ghost_on && ghost_ok) {
            return String(U"按 E 摆下整座「") + utf8(style.name) + String(U"」（要 ") + bill + String(U"）· Q/R 转向 · G 放下样式");
        }
        return String(U"摆不了「") + utf8(style.name) + String(U"」：") + utf8(ghost_why) + String(U" · G 放下样式");
    }
    const PieceDef *holding = pieces::find(p_held);
    if (holding) {
        String extra;
        if (holding->role == PieceRole::ROOF && ghost_on) {
            extra = vformat(String(U"（往%s低）"), String::utf8(dir_name(plan.spot.dir)));
        } else if (holding->role == PieceRole::LADDER && ghost_on) {
            extra = vformat(String(U"（靠%s边）"), String::utf8(dir_name(plan.spot.dir)));
        }
        if (ghost_on && ghost_ok) {
            return String(U"按 E 摆下") + piece_name(holding->item) + extra + String(U" · Q/R 转") + house;
        }
        return String(U"摆不了") + piece_name(holding->item) + String(U"：") + utf8(ghost_why.empty() ? std::string("准星对准地面或一座") : ghost_why) + house;
    }
    if (look_piece >= 0 && site.works) {
        const Structure *s = site.works->structures.find(look_structure);
        const Piece *p = s ? s->find(look_piece) : nullptr;
        if (p) {
            const PieceDef *def = pieces::find(p->item);
            const String door = def && def->role == PieceRole::DOOR ? (p->open == 0 ? String(U" · E 开门") : String(U" · E 关门")) : String();
            return piece_name(p->item) + String(U" · X 拆回背包") + door + house;
        }
    }
    return String();
}

Dictionary PieceHand::ghost_state() const {
    Dictionary d;
    d["on"] = ghost_on;
    d["ok"] = ghost_ok;
    d["why"] = utf8(ghost_why);
    d["style"] = style_index;
    d["structure"] = plan.structure >= 0 && site.works && plan.structure < site.works->structures.size() ? site.works->structures.get()[size_t(plan.structure)].id : -1;
    d["slot"] = int(plan.spot.slot);
    d["x"] = plan.spot.x;
    d["z"] = plan.spot.z;
    d["level"] = plan.spot.level;
    d["tier"] = plan.spot.tier;
    d["dir"] = plan.spot.dir;
    d["snap"] = frame_xform().xform(Vector3(real_t(plan.snap_x), real_t(plan.snap_y), real_t(plan.snap_z)));
    return d;
}

Dictionary PieceHand::house_state(int p_structure) {
    Dictionary d;
    const HouseReport *r = report_of(p_structure);
    if (!r) {
        return d;
    }
    d["complete"] = r->complete;
    d["missing"] = utf8(r->missing);
    d["rooms"] = int(r->rooms.size());
    return d;
}
