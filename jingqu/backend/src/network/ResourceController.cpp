#include "network/ResourceController.h"

#include "network/ApiResult.h"
#include "network/Mappers.h"
#include "storage/Database.h"

namespace scenicflow {

Json::Value poiToJson(const Row& r) {
    Json::Value j;
    j["poiId"] = field(r, "poi_id");
    j["name"] = field(r, "name");
    j["category"] = field(r, "category");
    j["longitude"] = toDouble(field(r, "location_x", "0"));
    j["latitude"] = toDouble(field(r, "location_y", "0"));
    j["description"] = field(r, "description", "");
    j["status"] = toInt(field(r, "status", "1"));
    return j;
}

void ResourceController::getPois(const drogon::HttpRequestPtr&,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto rows = Database::instance().query("SELECT * FROM poi WHERE status=1 ORDER BY category, id");
    Json::Value arr(Json::arrayValue);
    for (const auto& r : rows) arr.append(poiToJson(r));
    callback(ok(arr));
}

void ResourceController::getParking(const drogon::HttpRequestPtr&,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto rows = Database::instance().query("SELECT * FROM parking_lot WHERE status=1 ORDER BY id");
    Json::Value arr(Json::arrayValue);
    for (const auto& r : rows) arr.append(parkingToJson(r));
    callback(ok(arr));
}

void ResourceController::getShuttles(const drogon::HttpRequestPtr&,
                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto rows = Database::instance().query("SELECT * FROM shuttle_bus ORDER BY id");
    Json::Value arr(Json::arrayValue);
    for (const auto& r : rows) arr.append(shuttleToJson(r));
    callback(ok(arr));
}

} // namespace scenicflow
