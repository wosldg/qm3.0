#include "network/PlanController.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/TimeUtil.h"
#include "model/Enums.h"
#include "network/ApiResult.h"
#include "network/Mappers.h"
#include "pipeline/PlanSegments.h"
#include "pipeline/TrailGraphStore.h"
#include "storage/Database.h"

using namespace scenicflow;

namespace {

constexpr int kMaxWaypoints = 8;      // 中途景点上限（2^8 × 8 的 DP 状态数，毫秒级）
constexpr double kBetaMinutes = 60.0; // 避峰惩罚：拥挤度满格 1.0 等价于多走 60 分钟
constexpr int kDwellMinutesPerSpot = 30; // 中途景点停留时长（与 recommendRoutes 口径一致）

struct SpotRow {
    std::string spotId;
    std::string name;
    double longitude = 0;
    double latitude = 0;
    int currentVisitors = 0;
    int capacity = 0;
    double arrivalRate = 0;    // 人/分钟
    double departureRate = 0;  // 人/分钟
    bool isGate = false;
};

double ratioOf(const SpotRow& s) {
    return s.capacity > 0 ? static_cast<double>(s.currentVisitors) / s.capacity : 0.0;
}

// Δt 分钟后的短时外推占比：current + (arrival − departure) × Δt，clamp 到 [0, capacity]。
double predictedRatio(const SpotRow& s, double dtMinutes) {
    const double projected = s.currentVisitors + (s.arrivalRate - s.departureRate) * dtMinutes;
    const double clamped = std::max(0.0, std::min(projected, static_cast<double>(s.capacity)));
    return s.capacity > 0 ? clamped / s.capacity : 0.0;
}

std::string hhmmOf(std::time_t t) { return formatTime(t, "%H:%M"); }

std::vector<SpotRow> loadSpots() {
    std::vector<SpotRow> spots;
    auto rows = Database::instance().query(
        "SELECT spot_id,name,longitude,latitude,current_visitors,instant_capacity,"
        "arrival_rate,departure_rate,is_gate FROM scenic_spot WHERE status=1 ORDER BY id");
    for (const auto& r : rows) {
        SpotRow s;
        s.spotId = field(r, "spot_id");
        s.name = field(r, "name");
        s.longitude = toDouble(field(r, "longitude"));
        s.latitude = toDouble(field(r, "latitude"));
        s.currentVisitors = toInt(field(r, "current_visitors"));
        s.capacity = toInt(field(r, "instant_capacity"));
        s.arrivalRate = toDouble(field(r, "arrival_rate"));
        s.departureRate = toDouble(field(r, "departure_rate"));
        s.isGate = toInt(field(r, "is_gate")) != 0;
        spots.push_back(s);
    }
    return spots;
}

Json::Value spotBriefJson(const SpotRow& s) {
    Json::Value j;
    j["spotId"] = s.spotId;
    j["name"] = s.name;
    j["longitude"] = s.longitude;
    j["latitude"] = s.latitude;
    return j;
}

// Held-Karp 子集 DP。cost(i, j, arriveOffsetMin) 由回调给出，支持避峰时间惩罚；
// time(i, j) 是纯耗时（步行 + 出发点停留），用于维护到达时刻的近似估计。
// 固定头 = 入口、尾 = 出口，中间排 m 个中途点，返回中途点的最优访问顺序。
std::vector<int> heldKarpOrder(int m, int entryIdx, int exitIdx,
                               const std::function<double(int, int, double)>& cost,
                               const std::function<double(int, int)>& time) {
    // dp[mask][j]：从 entry 出发，访问 mask 集合（含 j），最后停在 j 的最小代价。
    const int full = (1 << m) - 1;
    const double INF = std::numeric_limits<double>::infinity();
    std::vector<std::vector<double>> dp(1 << m, std::vector<double>(m, INF));
    std::vector<std::vector<int>> parent(1 << m, std::vector<int>(m, -1));
    // 到达每个状态末尾点的近似时刻偏移（分钟），用于避峰模式的 predictedRatio。
    std::vector<std::vector<double>> arriveMin(1 << m, std::vector<double>(m, INF));

    for (int j = 0; j < m; ++j) {
        dp[1 << j][j] = cost(entryIdx, j, 0.0);
        arriveMin[1 << j][j] = time(entryIdx, j);
    }
    for (int mask = 1; mask <= full; ++mask) {
        for (int j = 0; j < m; ++j) {
            if (!(mask & (1 << j)) || dp[mask][j] == INF) continue;
            for (int k = 0; k < m; ++k) {
                if (mask & (1 << k)) continue;
                const int nmask = mask | (1 << k);
                const double step = cost(j, k, arriveMin[mask][j]);
                if (step == INF) continue;
                const double cand = dp[mask][j] + step;
                if (cand < dp[nmask][k]) {
                    dp[nmask][k] = cand;
                    parent[nmask][k] = j;
                    arriveMin[nmask][k] = arriveMin[mask][j] + time(j, k);
                }
            }
        }
    }

    double best = INF;
    int last = -1;
    for (int j = 0; j < m; ++j) {
        if (dp[full][j] == INF) continue;
        const double tail = cost(j, exitIdx, arriveMin[full][j]);
        if (tail == INF) continue;
        const double total = dp[full][j] + tail;
        if (total < best) {
            best = total;
            last = j;
        }
    }

    std::vector<int> order;
    int mask = full;
    int cur = last;
    while (cur != -1) {
        order.push_back(cur);
        const int p = parent[mask][cur];
        mask &= ~(1 << cur);
        cur = p;
    }
    std::reverse(order.begin(), order.end());
    return order;
}

} // namespace

void PlanController::spots(const drogon::HttpRequestPtr&,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    const auto spots = loadSpots();
    Json::Value data;
    Json::Value arr(Json::arrayValue);
    for (const auto& s : spots) {
        Json::Value j = spotBriefJson(s);
        j["currentVisitors"] = s.currentVisitors;
        j["capacity"] = s.capacity;
        const double ratio = std::round(ratioOf(s) * 100.0) / 100.0;
        j["ratio"] = ratio;
        j["level"] = toString(levelFromRatio(ratio));
        j["levelText"] = levelTextOf(toString(levelFromRatio(ratio)));
        j["isGate"] = s.isGate;
        arr.append(j);
    }
    data["spots"] = arr;
    callback(ok(data));
}

void PlanController::route(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto body = req->getJsonObject();
    if (!body) {
        callback(failed(400, "请求体格式错误"));
        return;
    }
    const Json::Value& json = *body;
    if (!json.isMember("spotIds") || !json["spotIds"].isArray()) {
        callback(failed(400, "缺少 spotIds 数组"));
        return;
    }
    const std::string mode = json.get("mode", "avoid_peak").asString();
    if (mode != "avoid_peak" && mode != "shortest" && mode != "custom") {
        callback(failed(400, "mode 非法，支持 avoid_peak / shortest / custom"));
        return;
    }
    const std::string entryGateId = json.get("entryGateId", "spot_01").asString();
    const std::string exitGateId = json.get("exitGateId", "spot_01").asString();

    // 去重并排除门：门不作为中途景点重复出现。
    std::vector<std::string> spotIds;
    std::set<std::string> seen;
    for (const auto& v : json["spotIds"]) {
        if (!v.isString()) continue;
        const std::string id = v.asString();
        if (id == entryGateId || id == exitGateId) continue;
        if (seen.insert(id).second) spotIds.push_back(id);
    }
    if (spotIds.empty() || static_cast<int>(spotIds.size()) > kMaxWaypoints) {
        callback(failed(400, "中途景点数量需在 1~8 个之间"));
        return;
    }

    const auto allSpots = loadSpots();
    std::unordered_map<std::string, const SpotRow*> byId;
    for (const auto& s : allSpots) byId[s.spotId] = &s;

    const SpotRow* entryGate = nullptr;
    const SpotRow* exitGate = nullptr;
    if (byId.count(entryGateId)) entryGate = byId[entryGateId];
    if (byId.count(exitGateId)) exitGate = byId[exitGateId];
    if (!entryGate || !entryGate->isGate) {
        callback(failed(400, "entryGateId 不存在或不是门"));
        return;
    }
    if (!exitGate || !exitGate->isGate) {
        callback(failed(400, "exitGateId 不存在或不是门"));
        return;
    }

    std::vector<const SpotRow*> mid;
    for (const auto& id : spotIds) {
        if (!byId.count(id)) {
            callback(failed(400, "spotId 不存在: " + id));
            return;
        }
        mid.push_back(byId[id]);
    }

    const TrailGraph& graph = cachedTrailGraph();
    if (graph.nodes.empty() || graph.edges.empty()) {
        callback(failed(404, "步道网络尚未配置"));
        return;
    }

    // pairwise 距离矩阵（Dijkstra，含门）。索引布局：0..m-1 中途点，m=入口，m+1=出口。
    const int m = static_cast<int>(mid.size());
    const int n = m + 2;
    const int entryIdx = m;
    const int exitIdx = m + 1;
    const bool roundTrip = entryGateId == exitGateId;

    TrailRouter router;
    router.build(graph.nodes, graph.edges);

    std::vector<std::vector<double>> dist(n, std::vector<double>(n, std::numeric_limits<double>::infinity()));
    auto coordOf = [&](int idx) -> std::pair<double, double> {
        if (idx == entryIdx) return {entryGate->longitude, entryGate->latitude};
        if (idx == exitIdx) return {exitGate->longitude, exitGate->latitude};
        return {mid[idx]->longitude, mid[idx]->latitude};
    };
    auto nameOf = [&](int idx) -> const std::string& {
        if (idx == entryIdx) return entryGate->name;
        if (idx == exitIdx) return exitGate->name;
        return mid[idx]->name;
    };
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            if (i == j) {
                dist[i][j] = 0;
                continue;
            }
            if (roundTrip && ((i == entryIdx && j == exitIdx) || (i == exitIdx && j == entryIdx))) {
                dist[i][j] = 0;
                continue;
            }
            const auto [lng1, lat1] = coordOf(i);
            const auto [lng2, lat2] = coordOf(j);
            const auto plan = router.plan(lng1, lat1, lng2, lat2);
            if (!plan.ok) {
                callback(failed(400, nameOf(i) + " 至 " + nameOf(j) + " 距步道过远"));
                return;
            }
            dist[i][j] = plan.totalDistanceM;
        }
    }

    // 排序。
    std::vector<int> order;
    if (mode == "custom") {
        order.resize(m);
        for (int i = 0; i < m; ++i) order[i] = i;
    } else {
        const bool avoid = (mode == "avoid_peak");
        // 出发点的停留时长：中途点 30min，门 0min。
        auto dwellOf = [&](int idx) -> double {
            return (idx == entryIdx || idx == exitIdx) ? 0.0
                                                       : static_cast<double>(kDwellMinutesPerSpot);
        };
        auto time = [&](int from, int to) -> double {
            return dist[from][to] / kWalkSpeedMetersPerMinute + dwellOf(from);
        };
        auto cost = [&](int from, int to, double arriveOffsetMin) -> double {
            if (from == to) return 0.0;
            const double walkMin = dist[from][to] / kWalkSpeedMetersPerMinute;
            if (!avoid || to == exitIdx) return walkMin; // 最后一段到出口不加拥堵惩罚
            return walkMin + kBetaMinutes * predictedRatio(*mid[to], arriveOffsetMin + walkMin);
        };
        order = heldKarpOrder(m, entryIdx, exitIdx, cost, time);
        if (order.empty()) {
            callback(failed(400, "无法生成有效顺序"));
            return;
        }
    }

    // 按最终顺序逐段生成路径与拥堵标签。
    Json::Value segments(Json::arrayValue);
    Json::Value ordered(Json::arrayValue);
    double totalDistanceM = 0;
    int totalWalkMin = 0;
    const std::time_t now = std::time(nullptr);
    std::time_t cursor = now;

    std::vector<int> seq;
    seq.push_back(entryIdx);
    for (int idx : order) seq.push_back(idx);
    seq.push_back(exitIdx);

    int prev = entryIdx;
    for (size_t k = 1; k < seq.size(); ++k) {
        const int cur = seq[k];
        std::string err;
        const auto [fromLng, fromLat] = coordOf(prev);
        const auto [toLng, toLat] = coordOf(cur);
        auto seg = buildSegmentJson(graph, fromLng, fromLat, toLng, toLat, err);
        if (!seg) {
            const std::string& aName = (prev == entryIdx) ? entryGate->name
                                      : (prev == exitIdx) ? exitGate->name : mid[prev]->name;
            const std::string& bName = (cur == entryIdx) ? entryGate->name
                                      : (cur == exitIdx) ? exitGate->name : mid[cur]->name;
            callback(failed(400, aName + " 至 " + bName + " 距步道过远"));
            return;
        }
        const double segDist = (*seg)["totalDistanceM"].asDouble();
        const int segWalk = (*seg)["walkMinutes"].asInt();
        (*seg)["fromSpotId"] = (prev == entryIdx) ? entryGateId
                            : (prev == exitIdx) ? exitGateId : mid[prev]->spotId;
        (*seg)["toSpotId"] = (cur == entryIdx) ? entryGateId
                           : (cur == exitIdx) ? exitGateId : mid[cur]->spotId;
        segments.append(*seg);
        totalDistanceM += segDist;
        totalWalkMin += segWalk;
        cursor += static_cast<std::time_t>(segWalk) * 60;

        if (cur != exitIdx) {
            const SpotRow& s = *mid[cur];
            Json::Value item = spotBriefJson(s);
            item["order"] = static_cast<int>(ordered.size()) + 1;
            const double ratio = std::round(ratioOf(s) * 100.0) / 100.0;
            item["ratio"] = ratio;
            item["level"] = toString(levelFromRatio(ratio));
            item["levelText"] = levelTextOf(toString(levelFromRatio(ratio)));
            const double arriveOffset = std::difftime(cursor, now) / 60.0;
            item["predictedRatio"] = std::round(predictedRatio(s, arriveOffset) * 100.0) / 100.0;
            item["suggestedArrival"] = hhmmOf(cursor);
            // peakTip：当前拥挤（ratio ≥ 0.7）且避峰模式下被安排到后段。
            std::string tip;
            if (mode == "avoid_peak" && ratio >= 0.7 && ordered.size() > 0) {
                tip = "当前" + std::string(item["levelText"].asString()) +
                      "，已安排到约 " + item["suggestedArrival"].asString() + " 前往";
            }
            item["peakTip"] = tip;
            ordered.append(item);
            cursor += static_cast<std::time_t>(kDwellMinutesPerSpot) * 60;
        }
        prev = cur;
    }

    Json::Value data;
    data["mode"] = mode;
    data["entryGate"] = spotBriefJson(*entryGate);
    data["exitGate"] = spotBriefJson(*exitGate);
    data["roundTrip"] = roundTrip;
    data["orderedSpots"] = ordered;
    data["segments"] = segments;
    data["totalDistanceM"] = std::round(totalDistanceM * 10.0) / 10.0;
    data["totalWalkMinutes"] = totalWalkMin;
    data["totalDwellMinutes"] = static_cast<int>(ordered.size()) * kDwellMinutesPerSpot;
    data["estimatedFinish"] = hhmmOf(cursor);
    callback(ok(data));
}
