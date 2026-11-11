#include "pipeline/PlanSegments.h"

#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/Logger.h"

using namespace scenicflow;

namespace {

/** 解析 geometry JSON 字符串为 [[lng, lat], ...] 坐标数组。
 *  返回空 vector 表示没有 geometry 或解析失败（老数据兼容）。 */
std::vector<std::pair<double, double>> parseGeometry(const std::string& geomJson) {
    std::vector<std::pair<double, double>> result;
    if (geomJson.empty()) return result;

    std::istringstream iss(geomJson);
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, iss, &root, &errors)) {
        return result;
    }
    if (!root.isArray() || root.size() == 0) return result;

    for (const auto& pt : root) {
        if (pt.isArray() && pt.size() >= 2 && pt[0].isNumeric() && pt[1].isNumeric()) {
            result.emplace_back(pt[0].asDouble(), pt[1].asDouble());
        }
    }
    return result;
}

} // namespace

std::unique_ptr<Json::Value> scenicflow::buildSegmentJson(const TrailGraph& g, double fromLng, double fromLat,
                                          double toLng, double toLat, std::string& err) {
    // 堆分配避免栈溢出（图数据较大）
    auto router = std::make_unique<TrailRouter>();
    router->build(g.nodes, g.edges);
    auto plan = std::make_unique<decltype(router->plan(0,0,0,0))>(
        router->plan(fromLng, fromLat, toLng, toLat));
    if (!plan->ok) {
        err = plan->error;
        return nullptr;
    }

    std::map<int, TrailNodeInfo> nodeById;
    for (const auto& n : g.nodes) nodeById[n.nodeId] = n;
    std::map<int, TrailEdgeInfo> edgeById;
    for (const auto& e : g.edges) edgeById[e.edgeId] = e;

    // 将稀疏图路径展开为密集的几何坐标。
    // 遍历路径上的每条边，从边的 geometry 字段解析中间坐标。
    Json::Value data;
    Json::Value nodesJson(Json::arrayValue);

    bool firstChunk = true;
    for (int i = 0; i < static_cast<int>(plan->nodeIds.size()) - 1; ++i) {
        const int fromId = plan->nodeIds[i];
        const int toId = plan->nodeIds[i + 1];
        const auto fromIt = nodeById.find(fromId);
        const auto toIt = nodeById.find(toId);
        if (fromIt == nodeById.end() || toIt == nodeById.end()) continue;

        // 找到这条路径对应的边
        const auto edgeIt = edgeById.find(plan->edgeIds[i]);
        if (edgeIt == edgeById.end()) continue;

        // 解析边的 geometry
        auto coords = parseGeometry(edgeIt->second.geometry);
        std::cerr << "DEBUG: edge " << edgeIt->second.edgeId
                  << " geom_len=" << edgeIt->second.geometry.length()
                  << " coords_parsed=" << coords.size() << std::endl;

        if (!coords.empty()) {
            // 调试：输出前3个坐标
            if (i == 0) {
                for (size_t c = 0; c < std::min(coords.size(), (size_t)3u); ++c) {
                    std::cerr << "  coord " << c << ": (" << coords[c].first << ", " << coords[c].second << ")" << std::endl;
                }
            }
            // 有 geometry：使用中间弯曲坐标
            for (size_t c = 0; c < coords.size(); ++c) {
                // 跳过第一个点（与上一个边的最后一个点重复）
                if (!firstChunk && c == 0) continue;
                Json::Value n;
                n["nodeId"] = 0; // 中间点没有 nodeId
                n["longitude"] = coords[c].first;
                n["latitude"] = coords[c].second;
                nodesJson.append(n);
            }
            firstChunk = false;
        } else {
            // 没有 geometry：退化为端点
            if (!firstChunk) {
                // 添加前一个端点（与上一个边的最后一个点衔接）
            }
            Json::Value n;
            n["nodeId"] = fromIt->second.nodeId;
            n["name"] = fromIt->second.name;
            n["longitude"] = fromIt->second.longitude;
            n["latitude"] = fromIt->second.latitude;
            n["nodeType"] = fromIt->second.nodeType;
            nodesJson.append(n);
            firstChunk = false;
        }
    }
    // 添加最后一个节点
    {
        const int lastId = plan->nodeIds.back();
        const auto lastIt = nodeById.find(lastId);
        if (lastIt != nodeById.end()) {
            Json::Value n;
            n["nodeId"] = lastIt->second.nodeId;
            n["name"] = lastIt->second.name;
            n["longitude"] = lastIt->second.longitude;
            n["latitude"] = lastIt->second.latitude;
            n["nodeType"] = lastIt->second.nodeType;
            nodesJson.append(n);
        }
    }
    data["nodes"] = nodesJson;
    data["totalDistanceM"] = std::round(plan->totalDistanceM * 10.0) / 10.0;
    data["walkMinutes"] =
        static_cast<int>(std::lround(plan->totalDistanceM / kWalkSpeedMetersPerMinute));
    data["startSnapM"] = std::round(plan->startSnapM * 10.0) / 10.0;
    data["endSnapM"] = std::round(plan->endSnapM * 10.0) / 10.0;

    // Junction guidance
    Json::Value hints(Json::arrayValue);
    double walked = 0;
    for (size_t i = 0; i < plan->nodeIds.size(); ++i) {
        const auto nodeIt = nodeById.find(plan->nodeIds[i]);
        if (i + 1 < plan->nodeIds.size()) {
            const auto nextIt = nodeById.find(plan->nodeIds[i + 1]);
            if (nodeIt != nodeById.end() && nextIt != nodeById.end()) {
                const bool isJunction = nodeIt->second.nodeType == "JUNCTION";
                const bool forward = i < plan->edgeForward.size() && plan->edgeForward[i];
                std::string hint;
                if (forward && i < plan->edgeIds.size()) {
                    const auto edgeIt = edgeById.find(plan->edgeIds[i]);
                    if (edgeIt != edgeById.end()) hint = edgeIt->second.directionHint;
                }
                if (isJunction) {
                    Json::Value h;
                    h["nodeId"] = nodeIt->second.nodeId;
                    h["name"] = nodeIt->second.name;
                    h["nodeType"] = nodeIt->second.nodeType;
                    h["directionHint"] = hint;
                    h["imageUrl"] = nodeIt->second.imageUrl;
                    h["distanceFromStartM"] = std::round(walked * 10.0) / 10.0;
                    hints.append(h);
                }
                walked += TrailRouter::haversineMeters(nodeIt->second.longitude,
                                                       nodeIt->second.latitude,
                                                       nextIt->second.longitude,
                                                       nextIt->second.latitude);
            }
        }
    }
    data["hints"] = hints;
    return std::make_unique<Json::Value>(std::move(data));
}
