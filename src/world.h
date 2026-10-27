#pragma once

#include <cmath>
#include <map>
#include <memory>
#include <vector>

#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "earthworks.h"
#include "ground_host.h"
#include "ground_works.h"
#include "job_pool.h"
#include "map_host.h"
#include "resource_scatter.h"
#include "river_mesh.h"
#include "shared_looks.h"
#include "stream_meter.h"
#include "terrain_tile.h"
#include "planet_weather.h"
#include "story_start.h"
#include "weather.h"
#include "world_grid.h"
#include "world_sites.h"
#include "world_terrain.h"

class ScatterStream;
class SiteStream;
class WorksView;

// 后台备好的一块地面：网格与碰撞面都已转成引擎的数组（主线程只建节点、挂上）
struct TileArrays : JobResult {
    TileKey key;
    std::vector<int64_t> holes; // 叶块按哪几块改过的地留的洞（交活时的账，terrain_tile.h）
    double origin_x = 0.0, origin_z = 0.0;
    godot::PackedVector3Array vertices, normals;
    godot::PackedColorArray colors;
    godot::PackedByteArray season; // 季相份额（CUSTOM0，RGBA8）
    godot::PackedByteArray surface; // 实际渲染表面（CUSTOM1，RGBA8）
    godot::PackedInt32Array indices;
    // 碰撞三角面（只有叶块有），切成几段：一段一个凹多边形，主线程一段段挂（整块一次挂要十几毫秒）
    std::vector<godot::PackedVector3Array> face_parts;

    bool empty() const { return indices.is_empty(); }
};

// 后台备好的一块水面
struct RiverArrays : JobResult {
    RiverChunkKey key;
    double origin_x = 0.0, origin_z = 0.0;
    double min_y = 0.0, max_y = 0.0;
    godot::PackedVector3Array vertices, normals;
    godot::PackedVector2Array uvs;
    godot::PackedFloat32Array custom0, custom1, custom2;
    godot::PackedInt32Array indices;

    bool empty() const { return indices.is_empty(); }
};

// 一个真实比例的岛群（skyisle-gen 第三层产物，经 tools/export_skyisle.py 转成 data_dir 下的 terrain.bin + world.json）。
// 地面是流式的：焦点（上层每帧 set_focus，通常是玩家）一动，就按 TileLayout 重新选块，缺的块丢给后台线程建网格，
// 建好的在主线程挂进场景；被取代的旧块要等盖住它的新块都到齐了才拆，所以地面不会闪出窟窿。最细一级（叶块）带碰撞。
// 瞬移前调 prepare_at：同步建好落点周围的叶块，人落下去脚下一定有地。
// 码头与船坞坪：加载时按 sites.tsv 把全部码头规划好（dock_site）、主码头旁找好船坞坪（yard_site），压 / 凿进连续地形里再建第一块地面；
// 泊位接触线、出生位姿由此给出。栈桥与村落这些实物不归这里管（SiteStream 节点，走近了才建）。
// 它是地面宿主（GroundHost）：下面挂的 Farm（整个岛群一片农田，地块格与岛群坐标对齐）与 Shipyard（主码头旁）用它的坐标；
// 也是地图宿主（MapHost）：码头、脚下的聚落（SiteStream 铺好的村）、流式焦点、岛群地图面板都由它回答。
// 河（world_river.h）：栅格有水文就在建地形时建好河网、凿好河道；weather.tsv 给一年的水情。水面按 river_mesh 切成块，
// 焦点 river_radius 内的块挂上、远了拆；共用一份水面材质（game/shaders/river.gdshader），每帧按全局钟算出今天的径流指数设进去，
// 所以水涨水落不用重建网格。径流按全局钟的历日取（WorldClock 的日历：历日 0 = weather.tsv 的第 0 天）；runoff_override ≥ 0 时不看钟（测试、调试用）。
// 天气（weather.h）：同一份 weather.tsv 建天气模型（海拔分箱按栅格的高低），地图宿主 / 地面宿主把它交给天与光（Atmosphere）、村子、农田、账房；
// 地面块的顶点带季相份额，地皮着色器（game/shaders/ground.gdshader）按时令换季、按雪线盖雪。
// 资源（resource_sites.h、resource_scatter.h）：加载时按 resources.tsv 把全部采场与地标规划好（只有位置），采场的存量账也在这里（按全局钟回满）；
// 实物由 SiteStream 走近了才摆。地上一株株的散布物由 ResourceScatter 按块生成（ScatterStream 交给这里的任务池），
// 摆放、碰撞在 ScatterStream 节点；散布物采空、长回的账（ScatterLedger）也在这里。
// 后台任务池（job_pool.h）：地面块、水面块、散布块、村子与村民的备料都交给这一个池，按离焦点的远近先后做；
// 做好的在主线程按每帧的时间预算由近及远挂上（挂一块只建节点：引擎的数组在后台就备好了）。
// 加载完发 rebuilt（只一次）。
//
// 行星计划 P3：一个场景里可以有好几个岛群（GroupWorld 可以多实例，GroupStream 按距离建 / 拆）：
//   任务池是场景里共用的那一个（PlanetJobs），这一群领一条通道（lane）；场景里没有 PlanetJobs 就自己开一个池（兼容原来的 World）。
//   async_load：包在后台读、地形在后台建（GroupWorld 在任务池里一件 GROUP_LOAD 活），做完在主线程装上材质再发 rebuilt；否则 _ready 里同步读完。
//   锚群（D7）放在场景原点、不转，世界坐标即局部坐标，带碰撞；邻群由 GroupStream 摆一个刚体变换（绕 y 转 + 水平平移：它的平面坐标换到锚群的平面坐标，
//   在相机处取线性近似），地面块照样建碰撞体但撤出物理（physics_on = false）。剔除、自己按视锥剔都按全局变换算。
// World（下面）是它的兼容名：场景里摆的、只有一个群的那种。
class GroupWorld : public godot::Node3D, public GroundHost, public MapHost {
    GDCLASS(GroupWorld, godot::Node3D)

    godot::String data_dir = godot::String(story_start::GROUPS_DIR).path_join(godot::itos(story_start::SPAWN_NODE)); // 缺省：开局那群（story_start.h）
    godot::NodePath sites_path = "../SiteStream"; // 可以不接：那就没有码头与村落的实物，也靠不了岸
    godot::NodePath scatter_path = "../ScatterStream"; // 可以不接：那就没有地上的树、灌丛、石块
    int64_t terrain_seed = story_start::SPAWN_NODE; // 地形细节、铺村的种子 = 节点号
    double split = 1.25; // 焦点到块的距离 < 块边长 × split 就细分
    int build_threads = 4; // 场景里没有 PlanetJobs 时，自己的任务池几条线程
    double retarget_distance = 12.0; // 焦点挪了这么远才重新选块
    bool async_load = false; // 在后台载入（GroupStream 建的邻群）
    bool loading = false; // 后台正在读
    bool physics_on = true; // 锚群才有碰撞
    int node_id = -1; // world.json 的 source.node（生成器的节点号）
    // 行星天气场（行星计划 P5，planet_weather.h）：GroupStream 在载入前设，载入时挂到天气模型上、把这一群的 weather.tsv 登记进场（细版为准）
    std::shared_ptr<PlanetWeather> planet_weather;
    int planet_node = -1;
    GroupFrame planet_frame;
    void attach_planet_weather();

    WorldGrid grid;
    WorldTerrain terrain;
    TileLayout layout;
    godot::Dictionary info; // world.json
    WorldSites sites; // sites.tsv
    WorldWorks works; // 码头与船坞坪，与 sites.docks 一一对应
    std::vector<FreightPort> freight_ports; // 港口村（跑货）
    ResourceData resource_data; // resources.tsv（没有这份文件就是空的）
    ResourcePlan resource_plan; // 落了地的采场与地标
    std::vector<ResourceStock> resource_stocks; // 与 resource_plan.sites 一一对应
    ResourceScatter scatter; // 地上的散布物（栅格没有资源层就不生成）
    ScatterLedger scatter_ledger;
    bool loaded = false;
    SiteStream *site_stream = nullptr;
    ScatterStream *scatter_stream = nullptr;
    StreamMeter meter; // 流式加载的主线程分项计时（World 与挂在它周围的 SiteStream、ScatterStream、账房都往这里报）
    SharedLooks looks; // 岛群里各村共用的材质与网格

    struct Tile {
        TileKey key;
        godot::Node3D *node = nullptr;
        godot::GeometryInstance3D *view = nullptr; // 画出来的网格（剔除余量按真球面的下弯放，见 update_cull_margins）
        bool far_mode = false; // 离相机远（着色器压近了）：引擎不剔，这里按视锥的四个侧面自己剔；不投影
        bool shown = true;
        godot::Vector3 sphere_center; // 自己剔用的包围球（平面坐标）
        double sphere_radius = 0.0;
        godot::StaticBody3D *body = nullptr; // 叶块的碰撞体（碰撞一段段往上挂）
        int parts_left = 0; // 还有几段碰撞没挂
        uint64_t serial = 0; // 第几回挂上的（同一格拆了又挂，旧的碰撞段不能挂到新的块上）
        std::vector<int64_t> holes; // 叶块：按哪几块改过的地留了洞（切片十一）
        std::map<int64_t, godot::Node3D *> patches; // 叶块：改过的块的补丁（网格 + 碰撞，挂在块的节点下）
    };
    std::map<uint64_t, Tile> tiles; // 已挂进场景的
    struct CollisionPart {
        TileKey key;
        uint64_t serial = 0; // 哪一回挂上的块的段
        godot::PackedVector3Array faces;
    };
    uint64_t tile_serial = 0;
    std::vector<CollisionPart> collision_ready; // 挂上了网格、碰撞还没挂完的段
    std::vector<TileKey> target; // 这一刻该有的
    godot::Vector3 focus;
    godot::Vector3 targeted_focus;
    bool has_target = false;
    // 画面按真球面弯（PlanetView）：远处的块下弯几百米，引擎按平面的包围盒剔除会错剪地平线附近的块——按离相机最远的角的下弯放宽
    bool margins_on = false;
    bool margins_dirty = false;
    godot::Vector3 margin_cam;
    godot::Ref<godot::Material> ground_material; // 地皮着色器；读不了退回顶点色的 StandardMaterial3D（不换季）
    // 每群一份的着色器参数（行星计划 P3）：这一群自己的海拔表（积雪、物候、作物……，按这一群的天气模型每帧算）、分箱的高低、极向，
    // 设在这一群的地皮、水面、散布材质上（weather.gdshaderinc 的 wx_group_*）；另有「世界 → 这一群平面」的变换，碎斑与树种的哈希按它取
    godot::Ref<godot::ImageTexture> elev_texture;
    std::vector<float> elev_values;
    std::vector<godot::Ref<godot::ShaderMaterial>> shaded; // 设了 wx_group_* 的材质
    godot::Transform3D shaded_xform; // 上回设的全局变换
    bool shaded_xform_set = false;
    void update_shading();
    void push_shading(const godot::Ref<godot::ShaderMaterial> &p_material) const;

    // 河
    double river_radius = 4000.0; // 焦点这么远以内的水面块挂上（×1.25 以外拆）
    double runoff_override = -1.0; // ≥ 0：径流指数钉死在这个值
    RiverRegime regime;
    WeatherModel weather; // weather.tsv 的天气（没有这份文件就是没有数据的：常年晴）
    RiverMeshRules river_mesh_rules;
    std::vector<RiverChunkKey> river_chunk_keys; // 有河的块
    std::map<int64_t, godot::Node3D *> river_nodes; // 已挂上的水面块（空块为 nullptr）
    godot::Ref<godot::ShaderMaterial> river_material;
    double runoff = 1.0; // 这一帧的径流指数
    godot::Vector3 river_focus;
    bool river_targeted = false;

    // 后台任务池（共用的，这一群一条通道）；线程只读 terrain / layout / scatter（建好后不再变）。做好的地面块、水面块在这里排着等挂
    std::shared_ptr<JobPool> pool;
    int lane = 1;
    godot::ObjectID jobs_id; // 领通道的 PlanetJobs（拆的时候还回去）
    std::vector<std::unique_ptr<TileArrays>> tile_ready;
    std::vector<std::unique_ptr<RiverArrays>> river_ready;

    // 玩家营造（切片十一，ground_works.h）：这一群的营造账（GroupStream 在载入前交过来，飞走再回来、退出再进都在；场景里单独摆的 World 自己建一份）。
    // 改地形：账换一份新的、交给 WorldTerrain（后台线程量高都看得到）；已挂的叶块按改过的块留洞、挂补丁（主线程当场建：一块 16 m 不到一毫秒），
    // 洞变了（头一回改到某一块、或改回原样）当场重建那一个叶块、碰撞一次挂齐——改完这一帧就站得上去、掉得下去。
    // 搭建件、摆件由 WorksView（子节点）画。
    std::shared_ptr<GroundWorks> player_works;
    WorksView *works_view = nullptr;
    int works_touched = 0; // 改地形每改一格加一（散布据此重查压着的）
    std::vector<uint64_t> edit_stale; // 挂上时洞与账对不上的叶块（交活以后又改了地）：下一帧当场重建
    void adopt_player_works();
    std::vector<int64_t> wanted_holes(const TileKey &p_key) const;
    void mount_patch(Tile &r_tile, int64_t p_chunk);
    void unmount_patch(Tile &r_tile, int64_t p_chunk);
    void rebuild_tile_now(uint64_t p_packed);
    void refresh_edit_chunks(const std::vector<int64_t> &p_chunks);
    std::string edit_blocker(int p_i, int p_j);

    void attach_pool();
    // 读包、建地形与规划：只碰文件与纯 C++ 的数据，可以在后台线程里做（这时主线程不碰这些成员）
    bool load_data(std::string &r_error);
    // 主线程：材质、径流，然后 loaded
    void install();
    JobPool::Work tile_job(const TileKey &p_key) const;
    JobPool::Work river_job(const RiverChunkKey &p_key) const;
    void retarget();
    void adopt(std::unique_ptr<TileArrays> p_tile);
    void adopt_ready_tiles();
    void mount_collision(const CollisionPart &p_part);
    void prune();
    void update_cull_margins();
    bool target_has(const TileKey &p_key) const;
    void update_runoff();
    void stream_rivers(bool p_all);
    void adopt_ready_rivers();
    double river_distance(const RiverChunkKey &p_key) const;
    void adopt_river(std::unique_ptr<RiverArrays> p_river);

protected:
    static void _bind_methods();

public:
    ~GroupWorld();
    void _ready() override;
    void _process(double p_delta) override;

    void set_focus(const godot::Vector3 &p_focus) { focus = p_focus; }
    godot::Vector3 get_focus() const { return focus; }
    // 焦点挪到该处，并同步建好它周围 p_radius 以内的叶块（瞬移用）
    void prepare_at(const godot::Vector3 &p_point, double p_radius = 160.0);
    // 该处（世界 xz）脚下的叶块已经挂上、碰撞也挂齐了
    bool is_ready_at(const godot::Vector3 &p_point) const;
    // 地面高度；没有地返回 NAN
    double get_surface_height(double p_x, double p_z) const;
    godot::Dictionary get_landscape_at(double p_x, double p_z) const;
    // 该处最近的岛号（没有地也给最近的那座）；未加载返回 -1
    int get_island_at(double p_x, double p_z) const;
    // 该处（世界 xz）的地表份额（与地皮、散布同一份，见 cover_blend.h）：
    // { dominant（份额最大的地表类下标）, name（world.json 的类名）, weights（各类的份额，下标即地表类）}；未加载返回空字典
    godot::Dictionary get_cover_at(double p_x, double p_z) const;
    godot::Dictionary get_info() const { return info; }
    bool is_loaded() const { return loaded; }
    int get_tile_count() const { return int(tiles.size()); }
    // 河：此刻的径流指数（1 = 年均）；该处（世界 xz）的河况——没有河返回空字典，
    // 有则 { distance, bankfull_width, bankfull_depth, floodplain, level, depth, width, flow, wet, bed, acc_km2 }
    double get_runoff() const { return runoff; }
    godot::Dictionary get_river_at(double p_x, double p_z) const;
    int get_river_chunk_count() const;
    // 天气：此刻（全局钟的历日）该处（世界 xz；y 缺省取地面高）的天气，见 WeatherSample：{ kind, kind_name, day_kind, day_kind_name, cloud, cloud_cover, coverage,
    // precip, precip_mm_h, snow_frac, temp_c, wind_x, wind_z, wind_ms, storm, fog, fog_top, wet, front, sailable, snow_mm, ice, green, leaf, autumn, spring, crop }
    godot::Dictionary get_weather_at(double p_x, double p_z, double p_y = NAN) const;
    // 雪线（积雪 > 1 mm 最低的海拔；没有雪为 1e9）、河的冰线
    double get_snow_line() const;
    WeatherModel &get_weather() { return weather; }
    const WeatherModel &get_weather() const { return weather; }
    // 该处附近（p_radius 以内）汇流至少 p_min_acc_km2 的河中线上最近一点：
    // { x, z, dir_x, dir_z（顺水）, width, floodplain, acc_km2, mouth（这条河出不出岸缘）, mouth_x, mouth_z, mouth_dir_x, mouth_dir_z（河口的流向）}；没有返回空字典
    godot::Dictionary find_river_near(double p_x, double p_z, double p_radius, double p_min_acc_km2) const;
    int get_pending_count() const;

    // 码头（下标与 sites.docks 一致）
    int get_dock_count() const { return int(works.docks.size()); }
    int find_dock(int p_id) const; // 按生成器的码头号找下标；没有 −1
    int get_dock_id(int p_index) const;
    bool is_dock_berthable(int p_index) const; // 找到了岸缘，能靠泊
    godot::Transform3D get_dock_berth(int p_index) const; // 泊位接触线（世界坐标）
    godot::Transform3D get_dock_spawn(int p_index) const; // 坪上面朝栈桥的出生位姿（世界坐标）
    godot::Vector3 get_dock_position(int p_index) const; // 岸缘点（栈桥根）

    // 给同在 C++ 里的上层（WorldMap 画底图）
    const WorldGrid &get_grid() const { return grid; }
    const WorldTerrain &get_terrain() const { return terrain; }
    const WorldSites &get_sites() const { return sites; }
    const std::vector<DockPlan> &get_docks() const { return works.docks; }
    int find_freight_port(int p_site_index) const { return find_port(freight_ports, p_site_index); } // sites.settlements 下标 → 托运用的下标；散户 −1
    // 测试与调试：某村（村号）村仓那一点（船台跳板脚前）的全局位置；村子还没铺或没有船台返回 (0,0,0)
    godot::Vector3 get_freight_post(int p_village_id) const;
    godot::PackedInt32Array get_freight_port_ids() const; // 能托运的村的村号
    SiteStream *get_site_stream() const { return site_stream; }
    ScatterStream *get_scatter_stream() const { return scatter_stream; }
    // GroupStream 升到细版时挂上、降回近景时拆掉（场景里摆的 World 在 _ready 里按路径找）
    void set_streams(SiteStream *p_sites, ScatterStream *p_scatter) {
        site_stream = p_sites;
        scatter_stream = p_scatter;
    }
    // 后台载入还没做完：在这条线程上当场做完、装上（瞬移、回位要马上落到这一群）
    void finish_loading_now();

    // 流式加载的主线程分项计时（stream_meter.h）：C++ 里用 StreamTimer 计一段；脚本按帧号查一帧的分项（毫秒），或各项累计
    StreamMeter &get_meter() { return meter; }
    godot::Dictionary get_stream_frame(int64_t p_frame) const; // { 频道名: 毫秒 }；那一帧没记返回空字典
    godot::Dictionary get_stream_totals() const; // { 频道名: { total_ms, max_ms, frames } }
    void reset_stream_meter() { meter.reset(); }

    // 资源：采场（下标与 get_resource_plan().sites 一致）、地标、散布
    const ResourceData &get_resource_data() const { return resource_data; }
    const ResourcePlan &get_resource_plan() const { return resource_plan; }
    ResourceStock *resource_stock(int p_index);
    const std::vector<ResourceStock> &get_resource_stocks() const { return resource_stocks; }
    void set_resource_stocks(const std::vector<ResourceStock> &p_stocks) { resource_stocks = p_stocks; }
    const ResourceScatter &get_scatter() const { return scatter; }
    ScatterLedger &get_scatter_ledger() { return scatter_ledger; }
    const ScatterLedger &get_scatter_ledger() const { return scatter_ledger; }
    // 后台任务池（散布、村子、村民的备料也交给它；优先级填离焦点多远，米）。这一群的活都走自己的通道：键用 job_key，
    // collect / cancel_kind / reprioritize 带 get_lane()
    JobPool &get_pool() { return *pool; }
    std::shared_ptr<JobPool> share_pool() const { return pool; }
    int get_lane() const { return lane; }
    uint64_t job_key(JobKind p_kind, uint64_t p_id) const { return JobPool::key(p_kind, p_id, lane); }
    bool is_loading() const { return loading; }
    int get_node_id() const { return node_id; }
    // 这一群的地方时比钟点早几个钟头（经度差 / 15°；钟点是 PlanetView 的 clock_node 的地方时，D10）；没有行星数据、或就是钟的那一群为 0
    double get_local_offset_hours() const;
    // 行星天气场（行星计划 P5）：这一群是第 p_node 群、平面系 p_frame；载入了就当场挂上，没载入等载入时挂
    void set_planet_weather(const std::shared_ptr<PlanetWeather> &p_weather, int p_node, const GroupFrame &p_frame);
    bool has_planet_weather() const { return planet_weather != nullptr; }
    // 锚群才有碰撞：关了地面块的碰撞体撤出物理（节点、形状都留着，开了立刻回来）
    void set_physics_on(bool p_on);
    bool is_physics_on() const { return physics_on; }
    // 这一群的着色器参数设到一份材质上（地皮、水面在这里设；散布的材质 ScatterStream 交过来）；以后每帧跟着这一群的海拔表与摆放
    void track_shading(const godot::Ref<godot::Material> &p_material);

    // 玩家营造（切片十一）：换上这一群的营造账（载入前后都行：载入了就当场按新账重挂叶块、重画实物）
    void set_player_works(const std::shared_ptr<GroundWorks> &p_works);
    std::shared_ptr<GroundWorks> get_player_works() const { return player_works; }
    WorksView *get_works_view() const { return works_view; }
    // 账的版本（改地形 + 搭建件 + 摆件）：散布据此重查压着的
    int get_works_revision() const;
    // 改地形：一点落在哪一格（营造账的格号）；一格此刻几级；改成几级（当场重挂地面块、挂齐碰撞）；这一格的事实（给挖与垒的规矩）
    bool edit_cell_at(double p_x, double p_z, int &r_i, int &r_j) const;
    int get_edit_step(int p_i, int p_j) const;
    bool set_edit_step(int p_i, int p_j, int p_step);
    int set_edit_steps(const std::vector<EditCellStep> &p_cells); // 一次改多格：返回真改了几格
    EarthFacts earth_facts(int p_i, int p_j, const godot::Vector3 &p_player);
    // 给脚本（测试）：一点所在那格的级数 / 改成几级；某叶块挂着几个补丁；这一群一共改过几格
    int get_edit_step_at(double p_x, double p_z) const;
    bool set_edit_step_at(double p_x, double p_z, int p_step);
    int get_patch_count() const;
    int get_edit_cell_count() const;
    godot::Vector3 get_edit_cell_center(int p_i, int p_j) const; // 格心（全局，y = 此刻的地面）；没有返回 (0, 0, 0)
    godot::Dictionary get_earth_facts_at(double p_x, double p_z, const godot::Vector3 &p_player);
    godot::Ref<godot::Texture2D> get_elev_texture() const { return elev_texture; }
    SharedLooks &get_looks() { return looks; } // 岛群里各村共用的材质与网格
    static double clock_hours(); // 全局钟：从第 0 天子夜起的游戏钟头
    // 测试与调试：按采场号查一处采场 { index, kind, face, pile, item, name, grade, cap, stock（此刻）, island, reach, landing, built }；没落地返回空字典
    godot::Dictionary get_resource_site(int p_working_id) const;
    godot::PackedInt32Array get_resource_ids() const; // 落了地的采场号
    // 离 (x, z) 最近的一处落了地的采场的采场号（p_kind 非空时只找这种：stone / clay / gravel / placer / ore / sulfur）；p_radius 内没有返回 −1
    int find_resource_near(double p_x, double p_z, double p_radius, const godot::String &p_kind = godot::String()) const;
    // 地标（泉眼、温泉、洞穴）：按点号查 { kind, name, position, built }；离 (x, z) 最近的一处的点号（p_kind 非空时只找这种）
    godot::Dictionary get_landmark(int p_id) const;
    int find_landmark_near(double p_x, double p_z, double p_radius, const godot::String &p_kind = godot::String()) const;
    // 散布（测试与调试，按纯算法现生成、不管摆没摆出来）：离 (x, z) 最近的一株 p_kind（散布物的键，空 = 不挑）
    // { key, kind, name, product, position, radius, solid }；p_radius 内没有返回空字典。另：该处能不能长（给测试挑空地）
    godot::Dictionary find_scatter_near(double p_x, double p_z, double p_radius, const godot::String &p_kind = godot::String()) const;
    int get_scatter_count_near(double p_x, double p_z, double p_radius, bool p_solid_only) const;

    // GroundHost（Farm、Shipyard 挂在它下面）
    godot::Node3D *host_node() override { return this; }
    const godot::Node3D *host_node() const override { return this; }
    bool host_ready() const override { return loaded; }
    std::unique_ptr<MapGround> host_ground() const override;
    Farm *host_farm() const override; // 下面的 Farm 子节点
    YardPlan host_yard() const override; // 主码头旁的船坞坪
    void host_origin(double &r_x, double &r_z) const override { r_x = r_z = 0.0; }
    const WeatherModel *host_weather() const override { return loaded ? &weather : nullptr; }

    // MapHost
    godot::Node3D *map_node() override { return this; }
    bool map_ready() const override { return loaded; }
    double map_surface(const godot::Vector3 &p_global) const override { return get_surface_height(p_global.x, p_global.z); }
    int map_island_at(const godot::Vector3 &p_global) const override;
    bool map_footing(const godot::Vector3 &p_global) const override;
    int map_dock_count() const override { return get_dock_count(); }
    int map_dock_id(int p_index) const override { return get_dock_id(p_index); }
    bool map_dock_open(int p_index) const override;
    godot::Transform3D map_dock_berth(int p_index) const override { return get_dock_berth(p_index); }
    godot::Transform3D map_dock_spawn(int p_index) const override { return get_dock_spawn(p_index); }
    godot::Vector3 map_dock_position(int p_index) const override { return get_dock_position(p_index); }
    int map_dock_island(int p_index) const override;
    godot::String map_dock_label(int p_index) const override;
    std::vector<Shipyard *> map_shipyards() const override;
    Farm *map_farm_at(const godot::Vector3 &p_global) const override { return host_farm(); }
    GroundHost *map_settlement_at(const godot::Vector3 &p_global) const override;
    void map_focus(const godot::Vector3 &p_global) override;
    void map_prepare(const godot::Vector3 &p_global) override;
    int map_pending() const override;
    godot::String map_location_text(const godot::Vector3 &p_global) const override;
    WorldMap *map_make_panel() override;
    // 面板照这一群重画（新建、换锚后 PlanetHost 让已有的面板换成新锚群的图）
    void map_fill_panel(WorldMap *p_panel) const;
    void map_refresh_panel(WorldMap *p_panel) const override;
    double map_view_distance() const override { return 60000.0; } // 岛群近百公里宽，远处的岛也要看得见
    const std::vector<FreightPort> &map_freight_ports() const override { return freight_ports; }
    FreightPost map_freight_post_near(const godot::Vector3 &p_global, double p_range) override;
    bool map_freight_target(int p_port, godot::Transform3D &r_target) const override;
    ResourceSpot map_resource_near(const godot::Vector3 &p_global, double p_range) override;
    ResourceSpot map_gather_near(const godot::Vector3 &p_global, double p_face_range, double p_facing_yaw) override;
    void map_resource_taken(const ResourceSpot &p_spot) override;
    bool map_landing_height(double p_x, double p_z, double &r_y) const override { return terrain.surface(p_x, p_z, r_y); }
    bool map_water_at(double p_x, double p_z) const override;
    WeatherModel *map_weather() override { return loaded ? &weather : nullptr; }
    WorksSite map_works_at(const godot::Vector3 &p_global) override;
    bool map_earth_cell(const godot::Vector3 &p_global, int &r_i, int &r_j) const override;
    bool map_earth_cell_center(int p_i, int p_j, godot::Vector3 &r_global) const override;
    bool map_earth_facts(int p_i, int p_j, const godot::Vector3 &p_player, EarthFacts &r_facts) override;
    int map_earth_step(int p_i, int p_j) const override { return get_edit_step(p_i, p_j); }
    bool map_set_earth_step(int p_i, int p_j, int p_step) override { return set_edit_step(p_i, p_j, p_step); }
    int map_set_earth_steps(const std::vector<EditCellStep> &p_cells) override { return set_edit_steps(p_cells); }

    void set_sites_path(const godot::NodePath &p_value) { sites_path = p_value; }
    godot::NodePath get_sites_path() const { return sites_path; }
    void set_scatter_path(const godot::NodePath &p_value) { scatter_path = p_value; }
    godot::NodePath get_scatter_path() const { return scatter_path; }

    void set_data_dir(const godot::String &p_value) { data_dir = p_value; }
    godot::String get_data_dir() const { return data_dir; }
    void set_terrain_seed(int64_t p_value) { terrain_seed = p_value; }
    int64_t get_terrain_seed() const { return terrain_seed; }
    void set_split(double p_value) { split = p_value; }
    double get_split() const { return split; }
    void set_build_threads(int p_value) { build_threads = p_value; }
    int get_build_threads() const { return build_threads; }
    void set_async_load(bool p_value) { async_load = p_value; }
    bool get_async_load() const { return async_load; }
    void set_river_radius(double p_value) { river_radius = p_value; }
    double get_river_radius() const { return river_radius; }
    void set_runoff_override(double p_value);
    double get_runoff_override() const { return runoff_override; }
};

// 兼容名：只有一个群的 GroupWorld（world_seed42.tscn 里摆的那个、脚本里的 `as World`）。什么都不加。
class World : public GroupWorld {
    GDCLASS(World, GroupWorld)

protected:
    static void _bind_methods() {}
};

// 主线程计一段：构造时记下时刻，析构时把这段耗时记到 World 的分项计时上（p_world 为 nullptr 就不记）
class StreamTimer {
    StreamMeter *meter;
    StreamChannel channel;
    uint64_t started;

public:
    StreamTimer(GroupWorld *p_world, StreamChannel p_channel);
    ~StreamTimer();
    StreamTimer(const StreamTimer &) = delete;
    StreamTimer &operator=(const StreamTimer &) = delete;
};
