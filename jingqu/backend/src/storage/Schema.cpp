#define _USE_MATH_DEFINES
#include "storage/Schema.h"

#include "core/Logger.h"
#include "core/TimeUtil.h"
#include "model/Enums.h"
#include "storage/Database.h"

#include <json/json.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace scenicflow {

namespace {

bool isEmptyTable(const std::string& table) {
    auto rows = Database::instance().query("SELECT COUNT(*) AS c FROM " + table);
    if (rows.empty()) return true;
    return rows[0]["c"] == "0";
}

// Internal map grid (0..100) maps onto the real Wulingyuan / Wujiayu area
// (lng 110.4473..110.4802, lat 29.3326..29.3539, ~3 km across).
inline double mapToLat(double y) { return 29.3326 + y / 100.0 * 0.0213; }
inline double mapToLng(double x) { return 110.4473 + x / 100.0 * 0.0329; }
inline int mapDistance(double x1, double y1, double x2, double y2) {
    const double dx = x1 - x2, dy = y1 - y2;
    return static_cast<int>(std::lround(std::sqrt(dx * dx + dy * dy) * 20.0));
}

inline double toDoubleLocal(const std::string& s, double def = 0) {
    try { return std::stod(s); } catch (...) { return def; }
}

void seedSpots() {
    if (!isEmptyTable("scenic_spot")) return;
    // Real-world points around the Wulingyuan / Wujiayu entrance (Zhangjiajie).
    // location_x/y stay on the internal 0..100 map grid (derived from the real
    // coordinates) so walking distances and route planning keep working;
    // latitude/longitude carry the real values used by the Tianditu map page.
    struct SpotSeed { const char* id; const char* name; int cap; int daily; int cur; int cum;
                      double x; double y; double lat; double lng;
                      const char* open; const char* close; };
    const SpotSeed seeds[] = {
        {"spot_01", "大氧吧广场", 15000, 60000, 3000, 8200,  67, 100, 29.3539, 110.4695, "07:00", "18:00"},
        {"spot_02", "金鞭溪入口",  8000, 40000, 5600, 9600,  57,  83, 29.3502, 110.4661, "07:00", "18:00"},
        {"spot_03", "千里相会",    5000, 20000, 1500, 4300,  43,  60, 29.3454, 110.4613, "07:00", "18:00"},
        {"spot_04", "闺门迎宾",    6000, 16000, 4800, 5900,  31,  43, 29.3418, 110.4576, "07:00", "18:00"},
        {"spot_05", "黄石寨入口",  8000, 30000, 2400, 5100,  16,  21, 29.3371, 110.4527, "07:00", "18:30"},
        {"spot_06", "鹞子寨入口",  3000, 12000,  300, 1500,   0,   0, 29.3326, 110.4473, "07:30", "17:30"},
    };
    for (const auto& s : seeds) {
        Database::instance().execParams(
            "INSERT INTO scenic_spot(spot_id,name,instant_capacity,daily_capacity,current_visitors,"
            "cumulative_visitors,open_time,close_time,location_x,location_y,latitude,longitude,status)"
            " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,1)",
            {s.id, s.name, std::to_string(s.cap), std::to_string(s.daily),
             std::to_string(s.cur), std::to_string(s.cum),
             s.open, s.close, std::to_string(s.x), std::to_string(s.y),
             std::to_string(s.lat), std::to_string(s.lng)});
    }
    logInfo("seeded scenic_spot (6 rows, real Zhangjiajie points)");
}

void seedParkings() {
    if (!isEmptyTable("parking_lot")) return;
    // Only one real parking lot from the reference material: outside the east gate.
    Database::instance().exec(
        "INSERT INTO parking_lot(parking_id,name,total_spaces,occupied_spaces,distance_m,latitude,longitude,status)"
        " VALUES('park_01','东门外部停车场',800,520,180,29.3395,110.4789,1)");
    logInfo("seeded parking_lot (1 row)");
}

void seedShuttles() {
    if (!isEmptyTable("shuttle_bus")) return;
    const char* stations[] = {"吴家峪门票站", "大氧吧广场", "金鞭溪入口", "千里相会", "闺门迎宾", "黄石寨入口", "鹞子寨入口"};
    const char* statuses[] = {"RUNNING", "RUNNING", "IDLE", "RUNNING", "MAINTENANCE"};
    for (int i = 1; i <= 15; ++i) {
        char id[16];
        char name[32];
        std::snprintf(id, sizeof(id), "shuttle_%02d", i);
        std::snprintf(name, sizeof(name), "环保车%02d号", i);
        const char* status = statuses[i % 5];
        const char* station = stations[i % 7];
        int passengers = (std::string(status) == "RUNNING") ? (i * 3) % 21 : 0;
        Database::instance().execParams(
            "INSERT INTO shuttle_bus(shuttle_id,name,capacity,current_station,current_passengers,status)"
            " VALUES(?,?,20,?,?,?)",
            {id, name, station, std::to_string(passengers), status});
    }
    logInfo("seeded shuttle_bus (15 rows)");
}

void seedRules() {
    if (!isEmptyTable("rule")) return;
    struct R { const char* spot; const char* metric; const char* op; double threshold; const char* action; const char* level; int priority; int enabled; };
    const R seeds[] = {
        {"*",       "visitor_ratio", ">=", 0.9,  "触发拥挤告警",             "HIGH",     10, 1},
        {"*",       "visitor_ratio", ">=", 1.0,  "触发超载告警并生成分流建议","CRITICAL",  1, 1},
        {"*",       "arrival_rate",  ">",  40,   "关注到达速率异常",         "INFO",     50, 1},
        {"spot_01", "visitor_ratio", ">=", 0.85, "大氧吧广场提前预警",       "WARNING",  20, 1},
        {"*",       "parking_free",  "<",  30,   "触发停车引导建议",         "WARNING",  30, 1},
        {"*",       "shuttle_load",  ">=", 0.95, "触发运力不足告警",         "WARNING",  35, 1},
        {"spot_05", "visitor_ratio", ">=", 0.95, "黄石寨入口限制进入",       "CRITICAL",  5, 1},
        {"*",       "arrival_rate",  "<",  5,    "数据异常排查",             "INFO",     60, 0},
    };
    for (const auto& r : seeds) {
        Database::instance().execParams(
            "INSERT INTO rule(spot_id,metric,operator,threshold,action,level,priority,status)"
            " VALUES(?,?,?,?,?,?,?,?)",
            {r.spot, r.metric, r.op, std::to_string(r.threshold), r.action, r.level,
             std::to_string(r.priority), std::to_string(r.enabled)});
    }
    logInfo("seeded rule (8 rows)");
}

// 7 days of 10-minute flow history so charts and statistics have data before
// the simulation is started. Each record represents one sampling point.
void seedFlowHistory() {
    if (!isEmptyTable("flow_record")) return;

    struct Curve { const char* id; double peakRatio; };
    const Curve curves[] = {
        {"spot_01", 0.55}, {"spot_02", 0.85}, {"spot_03", 0.45}, {"spot_04", 0.90},
        {"spot_05", 0.50}, {"spot_06", 0.30},
    };

    auto spots = Database::instance().query("SELECT * FROM scenic_spot WHERE status=1 ORDER BY id");
    int rowsInserted = 0;

    Database::instance().exec("BEGIN");
    for (const auto& spot : spots) {
        const std::string spotId = spot.count("spot_id") ? spot.at("spot_id") : "";
        const int capacity = spot.count("instant_capacity") ? std::stoi(spot.at("instant_capacity")) : 0;
        const int cumulative = spot.count("cumulative_visitors") ? std::stoi(spot.at("cumulative_visitors")) : 0;

        double peak = 0.5;
        for (const auto& c : curves) {
            if (spotId == c.id) { peak = c.peakRatio; break; }
        }

        constexpr int kPoints = 7 * 24 * 6;      // 7 days at 10-minute intervals
        constexpr int kStepSeconds = 600;
        const int startCumulative = static_cast<int>(cumulative * 0.15);
        const int span = std::max(0, cumulative - startCumulative);

        for (int i = 0; i < kPoints; ++i) {
            const std::time_t t = std::time(nullptr) - static_cast<std::time_t>(kPoints - 1 - i) * kStepSeconds;
            const std::string ts = formatTime(t, "%Y-%m-%d %H:%M:%S");
            const std::tm tm = toLocalTm(t);
            const double hour = tm.tm_hour + tm.tm_min / 60.0;
            // Daily curve: low at night, peaking around 14:00.
            const double dayCurve = 0.55 + 0.45 * std::sin((hour - 6.0) / 24.0 * 2.0 * 3.14159265358979);
            const int current = std::max(0, static_cast<int>(std::lround(capacity * peak * dayCurve)));
            const int cum = startCumulative + static_cast<int>(span * (i + 1) / kPoints);
            const double ratio = capacity > 0 ? static_cast<double>(current) / capacity : 0.0;

            if (!Database::instance().execParams(
                    "INSERT INTO flow_record(record_time,spot_id,current_visitors,cumulative_visitors,"
                    "congestion_level,arrival_rate,departure_rate,is_deleted) VALUES(?,?,?,?,?,0,0,0)",
                    {ts, spotId, std::to_string(current), std::to_string(cum),
                     toString(levelFromRatio(ratio))})) {
                break;
            }
            ++rowsInserted;
        }
    }
    Database::instance().exec("COMMIT");
    logInfo("seeded flow_record (" + std::to_string(rowsInserted) + " rows)");
}

void seedEvents() {
    if (!isEmptyTable("event")) return;

    struct E { const char* type; const char* level; int minsAgo; int spotIdx; const char* desc; const char* suggestion; const char* status; };
    const E seeds[] = {
        {"OVERLOAD",          "CRITICAL",   3, 0, "当前在园 15600 人，超过瞬时容量 15000 人，已触发超载告警", "建议将部分游客分流至鹞子寨入口", "ACTIVE"},
        {"CONGESTION",        "HIGH",       8, 4, "当前在园 5600 人，拥挤度 70%，建议启动分流",           "建议将部分游客分流至闺门迎宾", "ACKNOWLEDGED"},
        {"PARKING_SHORTAGE",  "WARNING",   15, 0, "东门外部停车场剩余车位 12 个，低于阈值 30",             "建议引导车辆前往周边临时停车区", "ACTIVE"},
        {"CONGESTION",        "HIGH",      25, 0, "当前在园 10500 人，拥挤度 70%，持续 12 分钟",          "建议将部分游客分流至千里相会", "RESOLVED"},
        {"CAPACITY_SHORTAGE", "WARNING",   40, 5, "鹞子寨入口候车人数 180，环保车运力不足",                "建议调派最近空闲环保车前往支援", "ACTIVE"},
        {"CONGESTION",        "WARNING",   62, 1, "金鞭溪入口拥挤度 58%，接近阈值",                        "建议将部分游客分流至闺门迎宾", "CLOSED"},
        {"OVERLOAD",          "CRITICAL",  95, 0, "大氧吧广场在园 16200 人，超过瞬时容量 15000 人",         "建议将部分游客分流至金鞭溪入口", "RESOLVED"},
        {"PARKING_SHORTAGE",  "WARNING",  130, 1, "东门外部停车场剩余车位 22 个，低于阈值 30",             "建议引导车辆前往周边临时停车区", "IGNORED"},
        {"DATA_ANOMALY",      "INFO",     180, 0, "大氧吧广场流量数据 3 分钟未更新，已自动补位",           "", "RESOLVED"},
        {"CONGESTION",        "HIGH",     240, 2, "千里相会拥挤度 74%，持续 15 分钟",                      "建议将部分游客分流至闺门迎宾", "RESOLVED"},
        {"CAPACITY_SHORTAGE", "WARNING",  300, 4, "黄石寨入口站点环保车满载率 100%，等待时间上升",        "建议调派最近空闲环保车前往支援", "RESOLVED"},
        {"SYSTEM_ERROR",      "CRITICAL", 360, -1,"数据写库线程异常重启，已自动恢复",                    "", "CLOSED"},
        {"CONGESTION",        "WARNING",  420, 3, "闺门迎宾拥挤度 61%",                                    "建议将部分游客分流至金鞭溪入口", "CLOSED"},
        {"PARKING_SHORTAGE",  "INFO",     480, 2, "东门外部停车场剩余车位 120 个，恢复正常",               "", "CLOSED"},
        {"OVERLOAD",          "HIGH",     540, 0, "大氧吧广场在园 14400 人，持续 8 分钟后回落",            "建议将部分游客分流至黄石寨入口", "CLOSED"},
        {"CONGESTION",        "WARNING",  640, 1, "金鞭溪入口拥挤度 55%，趋势上升",                       "建议将部分游客分流至千里相会", "CLOSED"},
        {"DATA_ANOMALY",      "INFO",     720, 2, "检测到停车场上报数据跳变，已记录",                     "", "CLOSED"},
        {"CONGESTION",        "HIGH",     800, 4, "黄石寨入口拥挤度 78%，建议分流至金鞭溪入口",             "建议将部分游客分流至金鞭溪入口", "CLOSED"},
        {"CAPACITY_SHORTAGE", "WARNING",  900, 1, "环保车 07 号故障，在线运力下降",                        "建议调派最近空闲环保车前往支援", "CLOSED"},
        {"CONGESTION",        "INFO",    1000, 0, "吴家峪门票站到达速率 45 人/分钟，高于日常均值",          "", "CLOSED"},
    };

    for (const auto& e : seeds) {
        char spotId[16] = {0};
        if (e.spotIdx >= 0) std::snprintf(spotId, sizeof(spotId), "spot_%02d", e.spotIdx + 1);
        const std::string occur = timeStringMinutesAgo(e.minsAgo);
        const std::string updated =
            std::string(e.status) == "ACTIVE" ? "" : timeStringMinutesAgo(std::max(1, e.minsAgo - 5));
        Database::instance().execParams(
            "INSERT INTO event(event_type,occur_time,spot_id,level,description,suggestion,status,updated_at)"
            " VALUES(?,?,?,?,?,?,?,?)",
            {e.type, occur, spotId, e.level, e.desc, e.suggestion, e.status, updated});
    }
    logInfo("seeded event (20 rows)");
}

void seedSuggestions() {
    if (!isEmptyTable("scheduling_record")) return;

    struct S { const char* type; const char* content; const char* detail; int minsAgo; int spotIdx; const char* status; };
    const S seeds[] = {
        {"DIVERSION",         "大氧吧广场 → 金鞭溪入口：分流 120 人",   "目标景点剩余容量 2400，距离约 394 米，满足 90% 安全余量约束", 3,   1, "PENDING"},
        {"SHUTTLE_DISPATCH",  "调派环保车03号前往鹞子寨入口",           "鹞子寨入口候车 180 人，03 号当前空闲且距站点最近（1.2 公里）", 12,  6, "EXECUTED"},
        {"PARKING_GUIDE",     "引导车辆前往周边临时停车区",             "东门外部停车场剩余 12 位，已接近饱和",                        18,  0, "EXECUTED"},
        {"ENTRANCE_LIMIT",    "吴家峪门票站启动限流",                   "全景区在园人数达总容量 92%，建议入口放行速率降至 30 人/分钟", 30,  0, "PENDING"},
        {"RESOURCE_WARNING",  "环保车运力预警",                         "在线 12/15，满载率超过 85% 的车辆有 4 辆",                   45,  6, "EXECUTED"},
        {"DIVERSION",         "大氧吧广场 → 千里相会：分流 200 人",     "大氧吧广场拥挤度 108%，目标景点剩余容量 3500",               60,  1, "EXECUTED"},
        {"SHUTTLE_DISPATCH",  "调派环保车09号前往黄石寨入口",           "黄石寨入口站点满载率 100%，09 号距站点 0.8 公里",            95,  5, "REJECTED"},
        {"PARKING_GUIDE",     "东门外部停车场入口引导",                 "剩余车位 22 个，建议引导至周边临时停车区",                   130, 2, "EXECUTED"},
        {"DIVERSION",         "千里相会 → 闺门迎宾：分流 80 人",        "千里相会拥挤度 74%，闺门迎宾剩余容量 1200",                  180, 3, "EXECUTED"},
        {"RESOURCE_WARNING",  "停车场整体饱和预警",                     "全景区剩余车位 280，低于总车位 35%",                         240, 1, "REJECTED"},
        {"ENTRANCE_LIMIT",    "吴家峪门票站恢复正常放行",               "全景区在园人数回落至总容量 78%",                             300, 3, "EXECUTED"},
        {"SHUTTLE_DISPATCH",  "增开环保车加班车 2 趟",                  "早高峰预测到达率上升，建议提前增开",                         360, 4, "EXECUTED"},
    };

    for (const auto& s : seeds) {
        char spotId[16] = {0};
        std::snprintf(spotId, sizeof(spotId), "spot_%02d", std::min(6, std::max(1, s.spotIdx)) );
        const std::string taskId = "task_" + std::to_string(s.minsAgo);
        Database::instance().execParams(
            "INSERT INTO scheduling_record(task_id,spot_id,schedule_type,content,detail,"
            "suggestion_status,create_time,is_deleted) VALUES(?,?,?,?,?,?,?,0)",
            {taskId, spotId, s.type, s.content, s.detail, s.status,
             timeStringMinutesAgo(s.minsAgo)});
    }
    logInfo("seeded scheduling_record (12 rows)");
}

// Shuttle routes grouped by line; the user side shows per-route wait times.
// Distances derived from the internal grid (mapDistance).
void seedShuttleRoutes() {
    if (!isEmptyTable("shuttle_route")) return;
    struct R { const char* route; const char* from; const char* to; int dist; int wait; };
    const R seeds[] = {
        // L1: 东门 → 天子山索道下站
        {"L1 天子山索道线", "吴家峪门票站", "天子山索道下站", 1200, 5},
        // L2: 东门 → 百龙天梯下站（山下主线）
        {"L2 山下主线",     "吴家峪门票站", "三所路口站",    800, 8},
        {"L2 山下主线",     "三所路口站",   "十里画廊站",    650, 8},
        {"L2 山下主线",     "十里画廊站",   "水绕四门站",    900, 8},
        {"L2 山下主线",     "水绕四门站",   "百龙天梯下站",  450, 8},
        // L3: 索道上山接驳
        {"L3 索道上山线",   "天子山索道上站", "贺龙公园",     350, 3},
        // L4: 山上主线
        {"L4 山上主线",     "贺龙公园",      "乌龙寨",        800, 10},
        {"L4 山上主线",     "乌龙寨",        "袁家界",        600, 10},
        // L5: 接百龙天梯下山
        {"L5 下山接驳线",   "袁家界",        "百龙天梯上站",  500, 5},
        // L6: 东南门进出
        {"L6 东南门线",     "梓木岗门票站",  "水绕四门站",    1100, 12},
        // L7: 北门上山
        {"L7 北门线",       "天子山门票站",  "三岔路口",      700, 8},
        {"L7 北门线",       "三岔路口",      "贺龙公园",      550, 8},
        // L8: 南门接驳
        {"L8 南门接驳线",   "老磨湾",        "黄石寨索道下站", 950, 10},
    };
    for (const auto& r : seeds) {
        Database::instance().execParams(
            "INSERT INTO shuttle_route(route_name,from_station,to_station,distance,waiting_minutes,status)"
            " VALUES(?,?,?,?,?,1)",
            {r.route, r.from, r.to, std::to_string(r.dist), std::to_string(r.wait)});
    }
    logInfo("seeded shuttle_route (11 rows)");
}

// POI navigation points. The gate and parking lot come from the reference
// material (real coordinates); service facilities sit next to the ticket
// station / main square at estimated positions.
void seedPois() {
    if (!isEmptyTable("poi")) return;
    struct P { const char* id; const char* name; const char* cat; double x; double y; const char* desc; };
    const P seeds[] = {
        // 闸机
        {"gate_01",    "吴家峪东门闸机口",     "GATE",    100, 38, "景区主入口检票闸机"},
        {"gate_02",    "梓木岗闸机口",         "GATE",    30, 25, "东南门检票闸机"},
        {"gate_03",    "天子山北门闸机口",     "GATE",    50, 55, "北门检票闸机"},
        {"gate_04",    "森林公园南门闸机口",   "GATE",    45, 15, "南门检票闸机"},
        // 停车场
        {"park_p01",   "东门外部停车场",       "PARKING",  96, 32, "小型车 / 大巴"},
        // 环保车站点
        {"station_01", "环保车乘降点 · 门票站", "STATION",  99, 37, "L1/L2/L5 起点"},
        {"station_02", "环保车乘降点 · 大氧吧广场", "STATION", 66, 97, "L2 途经"},
        {"station_03", "环保车乘降点 · 天子山索道下站", "STATION", 75, 78, "L1 终点"},
        {"station_04", "环保车乘降点 · 三所路口站", "STATION", 80, 85, "L2 途经"},
        {"station_05", "环保车乘降点 · 十里画廊站", "STATION", 70, 92, "L2 途经"},
        {"station_06", "环保车乘降点 · 水绕四门站", "STATION", 65, 100, "L2/L6 途经"},
        {"station_07", "环保车乘降点 · 百龙天梯下站", "STATION", 60, 102, "L2 终点"},
        {"station_08", "环保车乘降点 · 天子山索道上站", "STATION", 82, 70, "L3 起点"},
        {"station_09", "环保车乘降点 · 贺龙公园", "STATION", 85, 65, "L3/L4/L7 途经"},
        {"station_10", "环保车乘降点 · 乌龙寨", "STATION", 88, 58, "L4 途经"},
        {"station_11", "环保车乘降点 · 袁家界", "STATION", 90, 52, "L4/L5 途经"},
        {"station_12", "环保车乘降点 · 百龙天梯上站", "STATION", 85, 48, "L5 终点"},
        {"station_13", "环保车乘降点 · 梓木岗门票站", "STATION", 35, 28, "L6 起点"},
        {"station_14", "环保车乘降点 · 天子山门票站", "STATION", 55, 60, "L7 起点"},
        {"station_15", "环保车乘降点 · 三岔路口", "STATION", 72, 58, "L7 途经"},
        {"station_16", "环保车乘降点 · 老磨湾", "STATION", 40, 18, "L8 起点"},
        {"station_17", "环保车乘降点 · 黄石寨索道下站", "STATION", 50, 22, "L8 终点"},
        // 卫生间
        {"toilet_01",  "卫生间 · 门票站",      "TOILET",   98, 36, "门票站广场东侧"},
        {"toilet_02",  "卫生间 · 大氧吧广场",  "TOILET",   66, 97, "大氧吧广场北侧"},
        {"toilet_03",  "卫生间 · 天子山索道",  "TOILET",   75, 78, "索道下站旁"},
        {"toilet_04",  "卫生间 · 袁家界",      "TOILET",   90, 52, "袁家界服务中心旁"},
        {"toilet_05",  "卫生间 · 贺龙公园",   "TOILET",   85, 65, "贺龙公园广场"},
        // 服务设施
        {"service_01", "游客服务中心",         "SERVICE",  98, 39, "咨询 / 寄存 / 租赁"},
        {"service_02", "医务室 · 大氧吧广场",  "SERVICE",  64, 95, "紧急医疗"},
        {"service_03", "餐厅 · 袁家界",        "SERVICE",  88, 50, "简餐 / 饮水"},
    };
    for (const auto& p : seeds) {
        Database::instance().execParams(
            "INSERT INTO poi(poi_id,name,category,location_x,location_y,latitude,longitude,description,status)"
            " VALUES(?,?,?,?,?,?,?,?,1)",
            {p.id, p.name, p.cat, std::to_string(p.x), std::to_string(p.y),
             std::to_string(mapToLat(p.y)), std::to_string(mapToLng(p.x)), p.desc});
    }

    // Link every active spot to all POIs with coordinate-derived distances.
    const auto spots = Database::instance().query("SELECT spot_id,location_x,location_y FROM scenic_spot WHERE status=1");
    const auto pois = Database::instance().query("SELECT poi_id,location_x,location_y FROM poi");
    int links = 0;
    Database::instance().exec("BEGIN");
    for (const auto& s : spots) {
        const double sx = toDoubleLocal(s.count("location_x") ? s.at("location_x") : "0");
        const double sy = toDoubleLocal(s.count("location_y") ? s.at("location_y") : "0");
        const std::string spotId = s.count("spot_id") ? s.at("spot_id") : "";
        for (const auto& p : pois) {
            const double px = toDoubleLocal(p.count("location_x") ? p.at("location_x") : "0");
            const double py = toDoubleLocal(p.count("location_y") ? p.at("location_y") : "0");
            Database::instance().execParams(
                "INSERT OR IGNORE INTO spot_poi(spot_id,poi_id,distance_m) VALUES(?,?,?)",
                {spotId, p.count("poi_id") ? p.at("poi_id") : "",
                 std::to_string(mapDistance(sx, sy, px, py))});
            ++links;
        }
    }
    Database::instance().exec("COMMIT");
    logInfo("seeded poi (6 rows, " + std::to_string(links) + " spot links)");
}

// Hourly whole-park forecast for today. The runtime refresh in
// UserSideController recomputes this from the last 7 days of flow_record
// once per hour; this seed only guarantees non-empty data on first boot.
void seedForecast() {
    const std::string today = dateStringDaysAgo(0);
    auto rows = Database::instance().queryParams(
        "SELECT COUNT(*) AS c FROM flow_forecast WHERE forecast_date=?", {today});
    if (!rows.empty() && rows[0]["c"] != "0") return;

    Database::instance().execParams("DELETE FROM flow_forecast WHERE forecast_date<?", {today});
    // Whole-park capacity = sum of all spot instant capacities.
    int totalCapacity = 0;
    auto capRows = Database::instance().query("SELECT COALESCE(SUM(instant_capacity),0) AS c FROM scenic_spot WHERE status=1");
    if (!capRows.empty()) totalCapacity = std::stoi(capRows[0]["c"]);
    if (totalCapacity <= 0) totalCapacity = 15432;
    Database::instance().exec("BEGIN");
    for (int hour = 0; hour < 24; ++hour) {
        const double dayCurve = 0.55 + 0.45 * std::sin((hour - 6.0) / 24.0 * 2.0 * 3.14159265358979);
        const int predicted = std::max(0, static_cast<int>(std::lround(totalCapacity * 0.42 * dayCurve)));
        const double ratio = static_cast<double>(predicted) / totalCapacity;
        Database::instance().execParams(
            "INSERT OR REPLACE INTO flow_forecast(forecast_date,hour,predicted_visitors,predicted_level)"
            " VALUES(?,?,?,?)",
            {today, std::to_string(hour), std::to_string(predicted), toString(levelFromRatio(ratio))});
    }
    Database::instance().exec("COMMIT");
    logInfo("seeded flow_forecast (24 rows for " + today + ")");
}

// ---------------------------------------------------------------------------
// Park walking-trail network (user-side trail navigation).
// Coordinates are GCJ-02 so the network aligns with the AMap basemap.
// Nodes are generated along hand-drawn corridor polylines at ~70 m spacing so
// the rendered polyline stays glued to the real trail instead of cutting
// through the forest. Segment lengths are NOT stored: the router derives them
// from the node coordinates with the haversine formula.
// ---------------------------------------------------------------------------
struct TrailWaypointSeed {
    double lng = 0;
    double lat = 0;
    std::string type = "PATH";
    std::string name;
    std::string linkKey;   // non-empty: reuse the node already created for this key
    std::string hint;      // hint for the first edge leaving this waypoint
    std::string note;
    std::string image;
};

using TrailCorridorSeed = std::vector<TrailWaypointSeed>;

TrailWaypointSeed wp(double lng, double lat, const std::string& type = "PATH",
                     const std::string& name = "", const std::string& linkKey = "",
                     const std::string& hint = "", const std::string& note = "",
                     const std::string& image = "") {
    TrailWaypointSeed w;
    w.lng = lng;
    w.lat = lat;
    w.type = type;
    w.name = name;
    w.linkKey = linkKey;
    w.hint = hint;
    w.note = note;
    w.image = image;
    return w;
}

// Local planar approximation, plenty accurate for 20..100 m steps.
double trailStepMeters(double lng1, double lat1, double lng2, double lat2) {
    constexpr double kMetersPerDegLat = 111000.0;
    constexpr double kMetersPerDegLng = 97000.0;   // 111000 * cos(29.34 deg)
    const double dx = (lng1 - lng2) * kMetersPerDegLng;
    const double dy = (lat1 - lat2) * kMetersPerDegLat;
    return std::sqrt(dx * dx + dy * dy);
}

bool hasColumn(const std::string& table, const std::string& column) {
    auto rows = Database::instance().query("PRAGMA table_info(" + table + ")");
    for (const auto& r : rows) {
        if (r.count("name") && r.at("name") == column) return true;
    }
    return false;
}

// 老库升级：CREATE TABLE IF NOT EXISTS 不会补列，这里做一次幂等迁移。
void ensureTrailColumns() {
    if (!hasColumn("trail_node", "source")) {
        Database::instance().exec(
            "ALTER TABLE trail_node ADD COLUMN source TEXT NOT NULL DEFAULT 'manual'");
    }
    if (!hasColumn("trail_node", "osm_id")) {
        Database::instance().exec("ALTER TABLE trail_node ADD COLUMN osm_id TEXT");
    }
    if (!hasColumn("trail_edge", "source")) {
        Database::instance().exec(
            "ALTER TABLE trail_edge ADD COLUMN source TEXT NOT NULL DEFAULT 'manual'");
    }
}

// 老库升级：scenic_spot 加 is_gate 门标记，并补齐真实门票站。
// 起止门用景区真实门票站坐标（GCJ-02）；location_x/y 仅供内部示意图，
// 由经纬度按景观点阵线性映射后截断到 0..100。
void ensureSpotGateColumn() {
    if (!hasColumn("scenic_spot", "is_gate")) {
        Database::instance().exec(
            "ALTER TABLE scenic_spot ADD COLUMN is_gate INTEGER NOT NULL DEFAULT 0");
    }
    // 早期版本误把大氧吧广场当作门，这里纠正（它是广场，不是门票站）。
    Database::instance().exec(
        "UPDATE scenic_spot SET is_gate=0 WHERE spot_id='spot_01'");

    struct GateSeed {
        const char* id; const char* name; double lat; double lng; double x; double y;
    };
    const GateSeed gates[] = {
        {"gate_01", "东门 · 吴家峪门票站", 29.340694, 110.480200, 99, 38},
        {"gate_02", "南门 · 森林公园门票站", 29.317500, 110.432500, 0, 0},
        {"gate_03", "西门 · 杨家界门票站", 29.348000, 110.432000, 0, 72},
        {"gate_04", "北门 · 天子山门票站", 29.390000, 110.499000, 100, 100},
    };
    for (const auto& g : gates) {
        auto hit = Database::instance().queryParams(
            "SELECT spot_id FROM scenic_spot WHERE spot_id=?", {g.id});
        if (!hit.empty()) continue;
        Database::instance().execParams(
            "INSERT INTO scenic_spot(spot_id,name,instant_capacity,daily_capacity,current_visitors,"
            "cumulative_visitors,open_time,close_time,location_x,location_y,latitude,longitude,"
            "status,is_gate) VALUES(?,?,0,0,0,0,'00:00','23:59',?,?,?,?,1,1)",
            {g.id, g.name, std::to_string(g.x), std::to_string(g.y),
             std::to_string(g.lat), std::to_string(g.lng)});
    }
}

// sql/schema.sql -> 项目 data/trail_osm.json
std::string trailOsmPathFromSchema(const std::string& schemaSqlPath) {
    std::string dir = schemaSqlPath;
    size_t sep = dir.find_last_of("/\\");
    if (sep != std::string::npos) dir = dir.substr(0, sep);   // .../sql
    sep = dir.find_last_of("/\\");
    if (sep != std::string::npos) dir = dir.substr(0, sep);   // 项目根目录
    return dir + "/data/trail_osm.json";
}

// OpenStreetMap 高密度路网导入。
// - OSM 坐标为 WGS-84，导入脚本已在构建期转换为 GCJ-02，这里不再纠偏。
// - 以 source='osm' 作为空表守卫，重复启动不会重复插入。
// - 手工主干（source='manual'）保留不动，只在锚点处补接入边，保证两套路网连通。
void importTrailOsm(const std::string& jsonPath) {
    auto existing = Database::instance().query(
        "SELECT COUNT(*) AS c FROM trail_node WHERE source='osm'");
    if (!existing.empty() && existing[0].count("c") && existing[0].at("c") != "0") return;

    std::ifstream in(jsonPath, std::ios::binary);
    if (!in.is_open()) {
        logInfo("trail OSM import skipped, missing file: " + jsonPath);
        return;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errors;
    if (!reader->parse(content.c_str(), content.c_str() + content.size(), &root, &errors)) {
        logError("trail OSM import failed, invalid JSON: " + errors);
        return;
    }

    struct OsmNode { std::string code; int id = 0; double lng = 0; double lat = 0; };
    std::vector<OsmNode> osmNodes;
    std::unordered_map<std::string, int> ids;

    const Json::Value& nodesJson = root["nodes"];
    const Json::Value& edgesJson = root["edges"];
    int nodeCount = 0;
    int edgeCount = 0;

    Database::instance().exec("BEGIN");
    for (const auto& n : nodesJson) {
        if (!n.isObject() || !n["code"].isString()) continue;
        const std::string code = n["code"].asString();
        const double lng = n["lng"].asDouble();
        const double lat = n["lat"].asDouble();
        const std::string osmId = code.rfind("osm/", 0) == 0 ? code.substr(4) : code;

        Database::instance().execParams(
            "INSERT INTO trail_node(node_code,name,longitude,latitude,node_type,note,image_url,"
            "source,osm_id,status) VALUES(?,'',?,?, 'PATH','','', 'osm',?,1)",
            {code, std::to_string(lng), std::to_string(lat), osmId});
        const int id = static_cast<int>(Database::instance().lastInsertId());
        ids[code] = id;
        osmNodes.push_back(OsmNode{code, id, lng, lat});
        ++nodeCount;
    }

    for (const auto& e : edgesJson) {
        if (!e.isObject()) continue;
        const auto fromIt = ids.find(e["from"].asString());
        const auto toIt = ids.find(e["to"].asString());
        if (fromIt == ids.end() || toIt == ids.end() || fromIt->second == toIt->second) continue;
        const int oneWay = e["oneWay"].asInt();
        Database::instance().execParams(
            "INSERT INTO trail_edge(start_node_id,end_node_id,one_way,direction_hint,source,status)"
            " VALUES(?,?,?,'','osm',1)",
            {std::to_string(fromIt->second), std::to_string(toIt->second), std::to_string(oneWay)});
        ++edgeCount;
    }
    Database::instance().exec("COMMIT");

    // 锚点接入：手工骨架的出入口 / 岔路口 / 观景台需要与 OSM 主步道连通，
    // 否则两套路网是两个互不可达的连通分量，跨区规划会直接 404。
    const char* kRequiredAnchors[] = {
        "大氧吧广场", "黄石寨入口", "黄石寨观景台", "鹞子寨入口", "鹞子寨观景台"};

    auto manualRows = Database::instance().query(
        "SELECT id,name,longitude,latitude FROM trail_node"
        " WHERE source='manual' AND node_type<>'PATH' AND status=1");
    int connectorCount = 0;
    for (const auto& r : manualRows) {
        const int manualId = static_cast<int>(std::stoi(r.at("id")));
        const std::string name = r.count("name") ? r.at("name") : "";
        const double lng = std::stod(r.at("longitude"));
        const double lat = std::stod(r.at("latitude"));

        int nearestId = 0;
        double nearestM = 1e18;
        for (const auto& n : osmNodes) {
            const double d = trailStepMeters(lng, lat, n.lng, n.lat);
            if (d < nearestM) {
                nearestM = d;
                nearestId = n.id;
            }
        }
        if (nearestId == 0) continue;

        bool required = false;
        for (const char* anchor : kRequiredAnchors) {
            if (name == anchor) {
                required = true;
                break;
            }
        }
        // 重叠区域（金鞭溪主干）自动补短接入边；指定锚点无论多远都补，避免孤立。
        if (!required && nearestM > 80.0) continue;

        Database::instance().execParams(
            "INSERT INTO trail_edge(start_node_id,end_node_id,one_way,direction_hint,source,status)"
            " VALUES(?,?,0,'','manual',1)",
            {std::to_string(manualId), std::to_string(nearestId)});
        ++connectorCount;
        logInfo("trail anchor connected: " + name + " -> osm node " +
                std::to_string(nearestId) + " (" +
                std::to_string(static_cast<int>(std::lround(nearestM))) + " m)");
    }

    logInfo("imported trail OSM (" + std::to_string(nodeCount) + " nodes, " +
            std::to_string(edgeCount) + " edges, " + std::to_string(connectorCount) +
            " anchor connectors)");
}

std::string tracksPathFromSchema(const std::string& schemaSqlPath) {
    std::string dir = schemaSqlPath;
    size_t sep = dir.find_last_of("/\\");
    if (sep != std::string::npos) dir = dir.substr(0, sep);
    sep = dir.find_last_of("/\\");
    if (sep != std::string::npos) dir = dir.substr(0, sep);
    return dir + "/data/tracks.json";
}

// 预置成熟游览路线轨迹。数据由离线脚本 backend/tools/build_tracks.py 生成，
// 运行时只做一次导入，避免把上千点的手工数据写进 schema.sql 重复插入。
void seedTracks(const std::string& jsonPath) {
    if (!isEmptyTable("route_track")) return;

    std::ifstream in(jsonPath, std::ios::binary);
    if (!in.is_open()) {
        logInfo("track seed skipped, missing file: " + jsonPath);
        return;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errors;
    if (!reader->parse(content.c_str(), content.c_str() + content.size(), &root, &errors)) {
        logError("track seed failed, invalid JSON: " + errors);
        return;
    }

    int trackCount = 0;
    int pointCount = 0;
    Database::instance().exec("BEGIN");
    for (const auto& t : root["tracks"]) {
        if (!t.isObject()) continue;
        Database::instance().execParams(
            "INSERT INTO route_track(name,description,total_length_m,estimated_minutes,status)"
            " VALUES(?,?,?,?,1)",
            {t["name"].asString(), t["description"].asString(),
             std::to_string(t["totalLengthM"].asDouble()),
             std::to_string(t["estimatedMinutes"].asInt())});
        const int trackId = static_cast<int>(Database::instance().lastInsertId());

        for (const auto& p : t["points"]) {
            Database::instance().execParams(
                "INSERT INTO track_point(track_id,seq,longitude,latitude,point_type,name,"
                "direction_hint,image_url,status) VALUES(?,?,?,?,?,?,?,?,1)",
                {std::to_string(trackId), std::to_string(p["seq"].asInt()),
                 std::to_string(p["longitude"].asDouble()),
                 std::to_string(p["latitude"].asDouble()),
                 p["pointType"].asString(), p["name"].asString(),
                 p["directionHint"].asString(), p["imageUrl"].asString()});
            ++pointCount;
        }
        ++trackCount;
    }
    Database::instance().exec("COMMIT");
    logInfo("seeded " + std::to_string(trackCount) + " curated tracks (" +
            std::to_string(pointCount) + " points)");
}

void seedTrail() {
    if (!isEmptyTable("trail_node")) return;

    constexpr double kNodeSpacingM = 70.0;

    // One corridor = one continuous walkable path. Waypoints with a linkKey are
    // shared between corridors, which is how branches connect to a junction.
    const TrailCorridorSeed corridors[] = {
        // 主步道：东门 → 大氧吧广场
        {
            wp(110.4789, 29.3395, "ENTRANCE", "东门入口", "gate",
               "沿石板路西北行 → 大氧吧广场 1.6km", "吴家峪门票站外侧，步道起点"),
            wp(110.4770, 29.3412),
            wp(110.4752, 29.3430),
            wp(110.4740, 29.3448, "JUNCTION", "观景环线岔路口", "j1",
               "直行 → 大氧吧广场 1.2km", "可左转前往溪边观景台", "/trail/junction_01.jpg"),
            wp(110.4726, 29.3472),
            wp(110.4713, 29.3498),
            wp(110.4703, 29.3518),
            wp(110.4695, 29.3539, "VIEWPOINT", "大氧吧广场", "dyb", "",
               "景区核心集散广场"),
        },
        // 支线：观景环线岔路口 → 溪边观景台
        {
            wp(110.4740, 29.3448, "JUNCTION", "观景环线岔路口", "j1",
               "左转 → 溪边观景台 0.6km"),
            wp(110.4752, 29.3462),
            wp(110.4762, 29.3478),
            wp(110.4773, 29.3492, "VIEWPOINT", "溪边观景台", "vs1", "",
               "溪谷观景平台，可俯瞰金鞭溪"),
        },
        // 金鞭溪步道：大氧吧广场 → 金鞭溪入口 → 千里相会 → 闺门迎宾
        {
            wp(110.4695, 29.3539, "VIEWPOINT", "大氧吧广场", "dyb",
               "沿金鞭溪下行 → 金鞭溪入口 0.5km"),
            wp(110.4683, 29.3526),
            wp(110.4672, 29.3513),
            wp(110.4661, 29.3502, "VIEWPOINT", "金鞭溪入口", "jbx",
               "沿溪谷继续下行 → 千里相会 0.8km", "金鞭溪步道起点"),
            wp(110.4648, 29.3488),
            wp(110.4634, 29.3472),
            wp(110.4623, 29.3463, "JUNCTION", "金鞭溪分岔口", "j2",
               "直行 → 千里相会 0.4km", "右转可上黄石寨观景台", "/trail/junction_02.jpg"),
            wp(110.4613, 29.3454, "VIEWPOINT", "千里相会", "qlxh",
               "沿溪谷西行 → 闺门迎宾 0.6km", "金鞭溪经典观景点"),
            wp(110.4598, 29.3442),
            wp(110.4586, 29.3430),
            wp(110.4576, 29.3418, "VIEWPOINT", "闺门迎宾", "gm",
               "", "两山对峙如门"),
        },
        // 支线：金鞭溪分岔口 → 黄石寨观景台
        {
            wp(110.4623, 29.3463, "JUNCTION", "金鞭溪分岔口", "j2",
               "右转 → 黄石寨观景台 1.1km"),
            wp(110.4608, 29.3472),
            wp(110.4590, 29.3476),
            wp(110.4570, 29.3474),
            wp(110.4552, 29.3465),
            wp(110.4534, 29.3457),
            wp(110.4518, 29.3449),
            wp(110.4506, 29.3458, "VIEWPOINT", "黄石寨观景台", "hszt", "",
               "黄石寨山顶观景平台"),
        },
        // 金鞭溪下游：闺门迎宾 → 黄石寨入口 → 鹞子寨分岔口 → 鹞子寨入口
        {
            wp(110.4576, 29.3418, "VIEWPOINT", "闺门迎宾", "gm",
               "沿山道西南行 → 黄石寨入口 0.7km"),
            wp(110.4563, 29.3403),
            wp(110.4549, 29.3389),
            wp(110.4527, 29.3371, "VIEWPOINT", "黄石寨入口", "hsz",
               "沿山道继续西南行 → 鹞子寨入口 0.7km", "黄石寨索道下站"),
            wp(110.4510, 29.3355),
            wp(110.4494, 29.3341),
            wp(110.4482, 29.3332, "JUNCTION", "鹞子寨分岔口", "j3",
               "直行 → 鹞子寨入口 0.2km", "左转可上鹞子寨观景台"),
            wp(110.4473, 29.3326, "VIEWPOINT", "鹞子寨入口", "yz", "",
               "鹞子寨步道起点"),
        },
        // 支线：鹞子寨分岔口 → 鹞子寨观景台
        {
            wp(110.4482, 29.3332, "JUNCTION", "鹞子寨分岔口", "j3",
               "左转 → 鹞子寨观景台 0.7km"),
            wp(110.4488, 29.3336),
            wp(110.4496, 29.3348),
            wp(110.4502, 29.3362),
            wp(110.4506, 29.3376, "VIEWPOINT", "鹞子寨观景台", "yzzt", "",
               "鹞子寨山顶观景平台"),
        },
    };

    int nodeCount = 0;
    int edgeCount = 0;
    std::unordered_map<std::string, int> reusedIds;

    auto createNode = [&](const TrailWaypointSeed& w) -> int {
        if (!w.linkKey.empty()) {
            auto it = reusedIds.find(w.linkKey);
            if (it != reusedIds.end()) return it->second;
        }
        char code[16];
        std::snprintf(code, sizeof(code), "tn_%03d", ++nodeCount);
        Database::instance().execParams(
            "INSERT INTO trail_node(node_code,name,longitude,latitude,node_type,note,image_url,status)"
            " VALUES(?,?,?,?,?,?,?,1)",
            {code, w.name, std::to_string(w.lng), std::to_string(w.lat), w.type, w.note, w.image});
        const int id = static_cast<int>(Database::instance().lastInsertId());
        if (!w.linkKey.empty()) reusedIds[w.linkKey] = id;
        return id;
    };

    auto createEdge = [&](int from, int to, const std::string& hint) {
        // 生成带弯曲的中间坐标作为 geometry。
        // 在端点连线的基础上叠加正弦偏移，模拟真实步道的弯曲。
        // 偏移幅度沿走廊方向逐步变化，避免直线。
        auto buildGeometry = [&](double lng1, double lat1, double lng2, double lat2) -> std::string {
            const double segments = 12;
            Json::Value geom(Json::arrayValue);
            for (size_t s = 0; s <= segments; ++s) {
                const double t = static_cast<double>(s) / segments;
                // 线性插值
                double lng = lng1 + (lng2 - lng1) * t;
                double lat = lat1 + (lat2 - lat1) * t;
                // 正弦偏移：中间大、两头小
                const double envelope = std::sin(t * M_PI);
                // 偏移幅度约为端点间距的 3%~5%
                const double dlng = std::abs(lng2 - lng1) * 0.05 * envelope;
                const double dlat = std::abs(lat2 - lat1) * 0.05 * envelope;
                // 交替方向
                const double sign = (s % 2 == 0) ? 1.0 : -1.0;
                lng += dlng * sign;
                lat += dlat * sign;
                Json::Value pt(Json::arrayValue);
                pt.append(lng);
                pt.append(lat);
                geom.append(pt);
            }
            Json::StreamWriterBuilder swb;
            swb["indentation"] = "";
            std::unique_ptr<Json::StreamWriter> writer(swb.newStreamWriter());
            std::ostringstream oss;
            writer->write(geom, &oss);
            return oss.str();
        };

        // 查找 from/to 节点的坐标
        double fromLng = 0, fromLat = 0, toLng = 0, toLat = 0;
        {
            auto rows = Database::instance().queryParams(
                "SELECT longitude, latitude FROM trail_node WHERE id=?",
                {std::to_string(from)});
            if (!rows.empty()) {
                fromLng = std::stod(rows[0]["longitude"]);
                fromLat = std::stod(rows[0]["latitude"]);
            }
        }
        {
            auto rows = Database::instance().queryParams(
                "SELECT longitude, latitude FROM trail_node WHERE id=?",
                {std::to_string(to)});
            if (!rows.empty()) {
                toLng = std::stod(rows[0]["longitude"]);
                toLat = std::stod(rows[0]["latitude"]);
            }
        }
        std::string geomStr = buildGeometry(fromLng, fromLat, toLng, toLat);

        Database::instance().execParams(
            "INSERT INTO trail_edge(start_node_id,end_node_id,one_way,direction_hint,geometry,status)"
            " VALUES(?,?,0,?,?,1)",
            {std::to_string(from), std::to_string(to), hint, geomStr});
        ++edgeCount;
    };

    Database::instance().exec("BEGIN");
    for (const auto& corridor : corridors) {
        std::vector<int> ids;
        ids.reserve(corridor.size());
        for (const auto& w : corridor) ids.push_back(createNode(w));

        for (size_t i = 0; i + 1 < corridor.size(); ++i) {
            const TrailWaypointSeed& a = corridor[i];
            const TrailWaypointSeed& b = corridor[i + 1];
            const double total = trailStepMeters(a.lng, a.lat, b.lng, b.lat);
            int steps = static_cast<int>(std::ceil(total / kNodeSpacingM));
            if (steps < 1) steps = 1;

            int tailId = ids[i];
            for (int s = 1; s < steps; ++s) {
                const double t = static_cast<double>(s) / steps;
                const int midId =
                    createNode(wp(a.lng + (b.lng - a.lng) * t, a.lat + (b.lat - a.lat) * t));
                createEdge(tailId, midId, s == 1 ? a.hint : std::string());
                tailId = midId;
            }
            createEdge(tailId, ids[i + 1], steps == 1 ? a.hint : std::string());
        }
    }
    Database::instance().exec("COMMIT");

    logInfo("seeded trail_node (" + std::to_string(nodeCount) + " rows, " +
            std::to_string(edgeCount) + " edges)");
}

} // namespace

bool Schema::init(const std::string& schemaSqlPath) {
    std::ifstream in(schemaSqlPath);
    if (!in.is_open()) {
        logError("cannot open schema file: " + schemaSqlPath);
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();

    if (!Database::instance().exec(ss.str())) {
        logError("failed to apply schema");
        return false;
    }

    seedSpots();
    seedParkings();
    seedShuttles();
    seedShuttleRoutes();
    seedRules();
    seedFlowHistory();
    seedEvents();
    seedSuggestions();
    seedPois();
    seedForecast();
    ensureSpotGateColumn();
    ensureTrailColumns();
    seedTrail();
    importTrailOsm(trailOsmPathFromSchema(schemaSqlPath));
    seedTracks(tracksPathFromSchema(schemaSqlPath));
    return true;
}

} // namespace scenicflow
