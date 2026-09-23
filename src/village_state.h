#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "field_book.h"
#include "inventory.h"
#include "rng.h"
#include "settlement_model.h"
#include "villager_model.h"

// 一处村落的账本：纯数据，不依赖节点，也不认得地图。不含 Godot 头。
// 村子在近处时 Villagers 节点逐帧推进它、照着它摆小人；走远了节点拆掉，账本留在远处账房（Hinterland）里接着按粗粒度结算；
// 再走近，新的节点照着账本把人摆回各自此刻该在的地方。推进的规矩在 village_sim（远近同一套）。
//
// 坐标是村子（地面宿主）的局部坐标，与 Villagers 一致。钟是全局的秒数（WorldClock）。
// save_village_state / load_village_state 把它写成 / 读回一段文本（先不做存档，接口留在这里）。

enum class FolkPhase : uint8_t {
    INDOORS, // 在屋里（夜里、歇晌，或出门前磨蹭）
    WALKING,
    WORKING,
};

struct PersonState {
    VillagerMind mind; // 下标、住户与职业、站在哪、背着什么、守望还要巡几座
    Rng rng = Rng(1); // 自己的随机流（决策、出门错开）
    Task task; // 眼下这件事
    FolkPhase phase = FolkPhase::INDOORS;
    double timer = 0.0; // 干活：还要干几秒；在屋里：出门前还要磨蹭几秒（只在出工的钟点里往下走）
    double speed = 1.3; // 走路 m/s（钟的秒）
    // 走路：近处沿导航格的路点走（path、next）；远处只记还要走多远（估的，estimated）
    std::vector<NavPoint> path;
    size_t next = 0;
    bool estimated = false;
    double walk_left = 0.0; // 估着走时还剩几米（已乘绕路系数）
};

// 流水：干完了几件、收了多少、吃了多少……（给测试、HUD 与对账）
struct VillageTally {
    int done[16] = {}; // 按 TaskKind 记干完了几件
    int harvested = 0; // 收下的粮（份）
    int sown = 0; // 播掉的种子（份）
    int eaten = 0; // 吃掉的粮（份）
    int hungry = 0; // 开饭时仓里没粮（留够种子之后）的人次
    double water_in = 0.0; // 挑进缸里的水（担，溢出的不算）
    double water_used = 0.0; // 吃饭用掉的水（担）
    int path_failures = 0;
};

struct VillageState {
    uint64_t layout_sig = 0; // 按哪张清单招的人（layout_signature）；清单变了要重招
    bool founded = false;
    double clock = 0.0; // 账记到了第几秒
    bool deployed = false; // 此刻在近处（地块在地图的农田里、人走导航格）
    Inventory store; // 村里的仓
    double water = 0.0; // 堂屋水缸里有几担
    double water_cap = 0.0; // 装得下几担
    std::vector<PersonState> people;
    std::vector<Claim> claims; // 农夫认领的地；owner = −1 是够不着的荒地（谁也别再挑）
    std::vector<HeldPlot> held; // 在远处时，认领的地连同长势记在这里；在近处时为空（地在农田里）
    ArableMask arable; // 村子周围哪些格能垦（开账时勘察好的；空 = 没勘，挑地时现勘）
    VillageTally tally;
};

// 清单的指纹：房子的种类、位置、朝向
uint64_t layout_signature(const SettlementLayout &p_layout);
// 村里的粮：仓里的加上人背着的（作物的产物都算）
int village_grain(const VillageState &p_state);

std::string save_village_state(const VillageState &p_state);
// 读不了返回 false，r_error 说为什么（p_state 此时不可用）
bool load_village_state(const std::string &p_text, VillageState &r_state, std::string &r_error);
