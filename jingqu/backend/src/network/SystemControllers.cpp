#include "network/SystemControllers.h"

#include "core/Logger.h"
#include "core/Settings.h"
#include "core/TimeUtil.h"
#include "model/Enums.h"
#include "network/ApiResult.h"
#include "network/Mappers.h"
#include "pipeline/DataPipeline.h"
#include "storage/Database.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <sstream>

namespace scenicflow {

namespace {

int parseRangeDays(const std::string& range) {
    if (range == "1d" || range == "today") return 1;
    if (range == "7d" || range == "week") return 7;
    if (range == "30d" || range == "month") return 30;
    if (!range.empty()) {
        try { return std::max(1, std::stoi(range)); } catch (...) {}
    }
    return 7;
}

Json::Value simulationStateToJson(bool running, const SimulationConfig& cfg) {
    Json::Value config;
    config["arrivalRate"] = cfg.arrivalRate;
    config["pattern"] = cfg.pattern;
    config["durationMinutes"] = cfg.durationMinutes;

    Json::Value j;
    j["running"] = running;
    j["config"] = config;
    return j;
}

bool isValidPattern(const std::string& p) {
    return p == "STEADY" || p == "PEAK" || p == "RANDOM";
}

} // namespace

// ==================== 统计 ====================

void StatisticsController::overview(const drogon::HttpRequestPtr& req,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    const int days = parseRangeDays(req->getParameter("range"));
    const std::string since = dateStringDaysAgo(days - 1) + " 00:00:00";

    // --- flow: daily arrivals derived from the cumulative counter delta ---
    auto flowRows = Database::instance().queryParams(
        "SELECT substr(record_time,1,10) AS d, spot_id, "
        "MAX(cumulative_visitors) AS mx, MIN(cumulative_visitors) AS mn "
        "FROM flow_record WHERE record_time >= ? GROUP BY d, spot_id",
        {since});
    std::map<std::string, int> daily;
    for (const auto& r : flowRows) {
        const int delta = toInt(field(r, "mx")) - toInt(field(r, "mn"));
        daily[field(r, "d")] += std::max(0, delta);
    }

    Json::Value flow(Json::arrayValue);
    for (int i = days - 1; i >= 0; --i) {
        const std::string date = dateStringDaysAgo(i);
        Json::Value point;
        point["date"] = date.substr(5);   // MM-DD, matches the frontend chart labels
        point["visitors"] = daily.count(date) ? daily[date] : 0;
        flow.append(point);
    }

    // --- congestion: minutes spent at CROWDED/OVERLOADED ---
    // The sampling interval differs between seeded history and live simulation records,
    // so the per-spot interval is derived from the actual sample count.
    auto congRows = Database::instance().queryParams(
        "SELECT s.name AS spot_name, "
        "SUM(CASE WHEN f.congestion_level IN ('CROWDED','OVERLOADED') THEN 1 ELSE 0 END) AS crowded, "
        "COUNT(*) AS total "
        "FROM flow_record f JOIN scenic_spot s ON s.spot_id=f.spot_id "
        "WHERE f.record_time >= ? GROUP BY f.spot_id ORDER BY crowded DESC",
        {since});
    const double rangeMinutes = days * 1440.0;
    Json::Value congestion(Json::arrayValue);
    for (const auto& r : congRows) {
        const int crowded = toInt(field(r, "crowded"));
        const int total = toInt(field(r, "total"));
        Json::Value item;
        item["spotName"] = field(r, "spot_name");
        item["crowdedMinutes"] =
            total > 0 ? static_cast<int>(std::lround(crowded * rangeMinutes / total)) : 0;
        congestion.append(item);
    }
    if (congestion.empty()) {
        auto spots = Database::instance().query("SELECT name FROM scenic_spot WHERE status=1 ORDER BY id");
        for (const auto& s : spots) {
            Json::Value item;
            item["spotName"] = field(s, "name");
            item["crowdedMinutes"] = 0;
            congestion.append(item);
        }
    }

    // --- resource: average utilisation per 2-hour bucket over the last 24h ---
    auto utilRows = Database::instance().queryParams(
        "SELECT substr(f.record_time,12,2) AS h, "
        "AVG(CAST(f.current_visitors AS REAL) / NULLIF(s.instant_capacity,0)) AS r "
        "FROM flow_record f JOIN scenic_spot s ON s.spot_id=f.spot_id "
        "WHERE f.record_time >= ? GROUP BY h",
        {formatTime(std::time(nullptr) - 86400, "%Y-%m-%d %H:%M:%S")});
    double buckets[12] = {0};
    int counts[12] = {0};
    for (const auto& r : utilRows) {
        int hour = toInt(field(r, "h"));
        if (hour < 0 || hour > 23) continue;
        const int idx = hour / 2;
        buckets[idx] += toDouble(field(r, "r")) * 100.0;
        ++counts[idx];
    }
    Json::Value resource(Json::arrayValue);
    for (int i = 0; i < 12; ++i) {
        char label[8];
        std::snprintf(label, sizeof(label), "%02d:00", i * 2);
        Json::Value item;
        item["time"] = std::string(label);
        item["utilization"] = counts[i] > 0 ? static_cast<int>(buckets[i] / counts[i] + 0.5) : 0;
        resource.append(item);
    }

    // --- suggestion type distribution ---
    auto suggRows = Database::instance().query(
        "SELECT schedule_type, COUNT(*) AS cnt FROM scheduling_record "
        "WHERE is_deleted=0 GROUP BY schedule_type");
    std::map<std::string, int> counts2;
    for (const auto& r : suggRows) counts2[field(r, "schedule_type")] = toInt(field(r, "cnt"));

    Json::Value typeCount(Json::arrayValue);
    for (const char* type : {"DIVERSION", "SHUTTLE_DISPATCH", "PARKING_GUIDE",
                             "ENTRANCE_LIMIT", "RESOURCE_WARNING"}) {
        Json::Value item;
        item["type"] = type;
        item["count"] = counts2.count(type) ? counts2[type] : 0;
        typeCount.append(item);
    }

    // --- summary: counts ---
    auto spotCnt = Database::instance().query("SELECT COUNT(*) AS n FROM scenic_spot WHERE status=1 AND is_gate=0");
    auto eventCnt = Database::instance().query("SELECT COUNT(*) AS n FROM event WHERE status = 'ACTIVE'");
    auto parkCnt = Database::instance().query("SELECT COUNT(*) AS n FROM parking_lot WHERE status=1");
    auto shuttleCnt = Database::instance().query("SELECT COUNT(*) AS n FROM shuttle_bus WHERE status='RUNNING'");

    Json::Value data;
    data["flow"] = flow;
    data["congestion"] = congestion;
    data["resource"] = resource;
    data["suggestionTypeCount"] = typeCount;
    data["spotCount"] = spotCnt.empty() ? 0 : toInt(field(spotCnt[0], "n"));
    data["eventCount"] = eventCnt.empty() ? 0 : toInt(field(eventCnt[0], "n"));
    data["parkingCount"] = parkCnt.empty() ? 0 : toInt(field(parkCnt[0], "n"));
    data["shuttleCount"] = shuttleCnt.empty() ? 0 : toInt(field(shuttleCnt[0], "n"));
    callback(ok(data));
}

} // namespace scenicflow
