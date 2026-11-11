#include "network/TrackController.h"

#include <string>

#include "network/ApiResult.h"
#include "network/Mappers.h"
#include "storage/Database.h"

using namespace scenicflow;

namespace {

Json::Value pointToJson(const Row& row) {
    Json::Value j;
    j["seq"] = toInt(field(row, "seq"));
    j["longitude"] = toDouble(field(row, "longitude"));
    j["latitude"] = toDouble(field(row, "latitude"));
    j["pointType"] = field(row, "point_type", "PATH");
    j["name"] = field(row, "name");
    j["directionHint"] = field(row, "direction_hint");
    j["imageUrl"] = field(row, "image_url");
    return j;
}

} // namespace

void TrackController::list(const drogon::HttpRequestPtr&,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto rows = Database::instance().query(
        "SELECT t.id,t.name,t.description,t.total_length_m,t.estimated_minutes,"
        " (SELECT COUNT(*) FROM track_point p WHERE p.track_id=t.id AND p.status=1) AS point_count"
        " FROM route_track t WHERE t.status=1 ORDER BY t.id");

    Json::Value tracks(Json::arrayValue);
    for (const auto& r : rows) {
        Json::Value j;
        j["trackId"] = toInt(field(r, "id"));
        j["name"] = field(r, "name");
        j["description"] = field(r, "description");
        j["totalLengthM"] = toDouble(field(r, "total_length_m"));
        j["estimatedMinutes"] = toInt(field(r, "estimated_minutes"));
        j["pointCount"] = toInt(field(r, "point_count"));
        tracks.append(j);
    }

    Json::Value data;
    data["tracks"] = tracks;
    callback(ok(data));
}

void TrackController::detail(const drogon::HttpRequestPtr&,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             int trackId) {
    auto trackRows = Database::instance().queryParams(
        "SELECT id,name,description,total_length_m,estimated_minutes"
        " FROM route_track WHERE id=? AND status=1",
        {std::to_string(trackId)});
    if (trackRows.empty()) {
        callback(failed(404, "游览路线不存在"));
        return;
    }

    const auto& trackRow = trackRows[0];
    auto pointRows = Database::instance().queryParams(
        "SELECT seq,longitude,latitude,point_type,name,direction_hint,image_url"
        " FROM track_point WHERE track_id=? AND status=1 ORDER BY seq",
        {std::to_string(trackId)});

    Json::Value points(Json::arrayValue);
    for (const auto& p : pointRows) points.append(pointToJson(p));

    Json::Value data;
    data["trackId"] = toInt(field(trackRow, "id"));
    data["name"] = field(trackRow, "name");
    data["description"] = field(trackRow, "description");
    data["totalLengthM"] = toDouble(field(trackRow, "total_length_m"));
    data["estimatedMinutes"] = toInt(field(trackRow, "estimated_minutes"));
    data["points"] = points;
    callback(ok(data));
}
