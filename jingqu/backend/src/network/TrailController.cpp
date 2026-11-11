#include "network/TrailController.h"

#include <cmath>
#include <string>

#include "network/ApiResult.h"
#include "network/Mappers.h"
#include "pipeline/PlanSegments.h"
#include "pipeline/TrailGraphStore.h"

using namespace scenicflow;

namespace {

Json::Value nodeToJson(const TrailNodeInfo& n, bool withMeta) {
    Json::Value j;
    j["nodeId"] = n.nodeId;
    j["nodeCode"] = n.nodeCode;
    j["name"] = n.name;
    j["longitude"] = n.longitude;
    j["latitude"] = n.latitude;
    j["nodeType"] = n.nodeType;
    j["source"] = n.source;
    if (withMeta) {
        j["note"] = n.note;
        j["imageUrl"] = n.imageUrl;
    }
    return j;
}

} // namespace

void TrailController::network(const drogon::HttpRequestPtr&,
                              std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    const TrailGraph& graph = cachedTrailGraph();
    const auto& nodes = graph.nodes;
    const auto& edges = graph.edges;

    Json::Value data;
    Json::Value nodesJson(Json::arrayValue);
    for (const auto& n : nodes) nodesJson.append(nodeToJson(n, true));
    data["nodes"] = nodesJson;

    Json::Value edgesJson(Json::arrayValue);
    for (const auto& e : edges) {
        Json::Value j;
        j["edgeId"] = e.edgeId;
        j["startNodeId"] = e.startNodeId;
        j["endNodeId"] = e.endNodeId;
        j["oneWay"] = e.oneWay ? 1 : 0;
        j["directionHint"] = e.directionHint;
        j["source"] = e.source;
        edgesJson.append(j);
    }
    data["edges"] = edgesJson;

    callback(ok(data));
}

void TrailController::route(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    auto body = req->getJsonObject();
    if (!body) {
        callback(failed(400, "请求体格式错误"));
        return;
    }
    const Json::Value& json = *body;
    if (!json.isMember("start") || !json.isMember("end")) {
        callback(failed(400, "缺少起点或终点坐标"));
        return;
    }
    const Json::Value& start = json["start"];
    const Json::Value& end = json["end"];
    if (!start.isMember("longitude") || !start.isMember("latitude") ||
        !end.isMember("longitude") || !end.isMember("latitude") ||
        !start["longitude"].isNumeric() || !start["latitude"].isNumeric() ||
        !end["longitude"].isNumeric() || !end["latitude"].isNumeric()) {
        callback(failed(400, "起点或终点坐标不合法"));
        return;
    }

    const TrailGraph& graph = cachedTrailGraph();
    if (graph.nodes.empty() || graph.edges.empty()) {
        callback(failed(404, "步道网络尚未配置"));
        return;
    }

    std::string err;
    auto data = buildSegmentJson(graph, start["longitude"].asDouble(),
                                        start["latitude"].asDouble(),
                                        end["longitude"].asDouble(),
                                        end["latitude"].asDouble(), err);
    if (!data) {
        // Snap failures are the caller's fault (400), a disconnected graph is not (404).
        const bool farFromTrail = err.find("过远") != std::string::npos;
        callback(failed(farFromTrail ? 400 : 404, err));
        return;
    }

    callback(ok(*data));
}
