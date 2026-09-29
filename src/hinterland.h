#pragma once

#include <future>
#include <map>
#include <string>
#include <memory>
#include <vector>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include "world_ledger.h"

class GroupWorld;
class SiteStream;

// 远处账房：岛群里每一处村落与散户（开局那群 #6329 是 1,474 处）一本账，节点拆了账还在，在这里按粗粒度接着记。
// 场景里挂在 World、SiteStream 旁边；SiteStream 招村民时从这里借账本给 Villagers（checkout），村民散了账本收回来接着在远处记。
//
// 开账（world_ledger：铺村清单 + 布置模式的改动、分派住户、看附近有没有田和水）放在后台线程里做，由近及远，近处要招村民的（request）插到最前面；
// 账从账房开张那一刻记起，开得晚的村子在远处先补上前面那段。
// 远处推进是 village_sim 的远处模式（不寻路、不建导航格，走路按直线估时间），与近处逐帧同一套规矩：
// 一处落后满 settle_hours（半个游戏小时）就轮到它，一片一片（slice_hours）往前结到此刻；每帧花的时间有上限（budget_ms），
// 每结完一片看一眼表，超了就留到下一帧接着结。实测花费从 get_stats 读。
class Hinterland : public godot::Node {
    GDCLASS(Hinterland, godot::Node)

    godot::NodePath world_path = "../World";
    godot::NodePath sites_path = "../SiteStream";
    double budget_ms = 0.5;
    double settle_hours = 0.5;
    double slice_hours = 1.0 / 12.0; // 结一片补几个钟头（五分钟）：一片的花费就是预算的粒度
    int max_openings = 2; // 同时在后台开几本账

    GroupWorld *world = nullptr;
    SiteStream *stream = nullptr;
    SimRules rules;
    std::shared_ptr<ClaimRegistry> taken;
    std::vector<std::unique_ptr<WorldLedger>> ledgers; // 与 sites.settlements 一一对应；没开账为 nullptr
    std::vector<std::future<WorldLedger>> in_flight;
    std::vector<int> in_flight_index;
    double started_at = -1.0; // 账房开张时的钟（所有村子的账都从这一秒记起）
    size_t ring = 0; // 轮到第几处
    int current = -1; // 正在结的那一处（一处要结好几片时跨帧接着结）
    std::vector<int> urgent; // 近处要招村民、等着开账的（先开这几处）
    // 上回离开细版时存下的账（sites.settlements 下标 → save_village_state 的文本；GroupStream 交过来）：开账时补回，离开超过一天的只补最后一天
    std::map<int, std::string> saved;
    double saved_at = -1.0;
    int restored = 0; // 补回了几本
    double skipped_hours = 0.0; // 补回时按汇总跳过的钟头（离开超过一天的部分）

    // 实测
    double frame_us = 0.0, frame_max_us = 0.0, frame_avg_us = 0.0;
    int64_t frames = 0, slices = 0;
    double slice_us_total = 0.0, slice_max_us = 0.0, settled_hours = 0.0;
    int opened = 0;

    bool ready_to_work();
    WorldLedgerSpec spec_of(int p_index) const;
    void install(int p_index, WorldLedger &&p_ledger);
    void launch_openings();
    void collect_openings(bool p_install);
    void settle_slice(WorldLedger &r_ledger, double p_until);

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    void _ready() override;
    void _physics_process(double p_delta) override;

    // 开没开账（能不能马上借出去）；没开就排到最前面开（后台，下一帧起）
    bool is_open(int p_index) const { return p_index >= 0 && p_index < int(ledgers.size()) && ledgers[size_t(p_index)] != nullptr; }
    void request(int p_index);
    // 借出第 p_index 处（sites.settlements 下标）的账本给近处的 Villagers：还没开账就当场开（流式加载先 request、等 is_open 再借，不在主线程上开）；
    // p_layout 是近处盖出来的清单（布置模式改过就和账上的不一样：记下新清单，Villagers 会按它重招人）
    std::shared_ptr<VillageState> checkout(int p_index, const SettlementLayout &p_layout);
    // 近处的清单变了（村民散了之前）：记下来，远处照新清单记账
    void set_layout(int p_index, const SettlementLayout &p_layout);
    // 模拟分级（行星计划 P3，计划 4.6）：离开细版前把开了的账存成文本（后台在开的先等它开完）、此刻的钟；
    // 回来时（新的账房）交回来，开账时照文本补回，从存下的那一刻接着结——离开超过 1 个游戏日的只补最后 1 日，前面的视为按汇总过了
    void save_states(std::map<int, std::string> &r_states, double &r_at);
    // 同 save_states，只是存成文本那一步在后台做（行星计划 P4：大群几百本账，主线程上要五六十毫秒）。
    // 调之前近处的村民得先散（SiteStream::retire_folk），之后账房不再动这些账（GroupStream 把它埋了）
    std::shared_future<std::map<int, std::string>> save_states_async(double &r_at);
    // 后台还有没开完的账（std::async 读着地形）：GroupStream 拆它之前等这些做完（出树时它要等，行星计划 P4：别让主线程卡在这里）
    bool openings_pending() const;
    void set_saved(const std::map<int, std::string> &p_states, double p_at);
    int get_restored() const { return restored; }
    std::shared_ptr<ClaimRegistry> get_registry() const { return taken; }
    const WorldLedger *get_ledger(int p_index) const;
    VillageState *get_state(int p_index); // 这处的账本（跑货从它的仓里扣、往里交）；没开账为 nullptr

    // 查询（给测试、HUD）
    int find_site(int p_site_id, bool p_hamlet) const; // sites.settlements 下标；没有 −1
    godot::Dictionary get_ledger_summary(int p_site_id, bool p_hamlet) const; // opened / deployed / clock / grain / water / harvested / people / working / store
    godot::Dictionary get_stats() const;
    godot::String get_ledger_text(int p_site_id, bool p_hamlet) const; // save_village_state 的文本

    void set_world_path(const godot::NodePath &p_value) { world_path = p_value; }
    godot::NodePath get_world_path() const { return world_path; }
    void set_sites_path(const godot::NodePath &p_value) { sites_path = p_value; }
    godot::NodePath get_sites_path() const { return sites_path; }
    void set_budget_ms(double p_value) { budget_ms = p_value; }
    double get_budget_ms() const { return budget_ms; }
    void set_settle_hours(double p_value) { settle_hours = p_value; }
    double get_settle_hours() const { return settle_hours; }
    void set_slice_hours(double p_value) { slice_hours = p_value > 0.001 ? p_value : 0.001; }
    double get_slice_hours() const { return slice_hours; }
    void set_max_openings(int p_value) { max_openings = p_value; }
    int get_max_openings() const { return max_openings; }
};
