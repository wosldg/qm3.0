#include "pipeline/TrailRouter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>
#include <utility>

namespace scenicflow {

namespace {

constexpr double kEarthRadiusM = 6371000.0;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

} // namespace

double TrailRouter::haversineMeters(double lng1, double lat1, double lng2, double lat2) {
    const double phi1 = lat1 * kDegToRad;
    const double phi2 = lat2 * kDegToRad;
    const double dPhi = (lat2 - lat1) * kDegToRad;
    const double dLambda = (lng2 - lng1) * kDegToRad;
    const double sinPhi = std::sin(dPhi / 2.0);
    const double sinLambda = std::sin(dLambda / 2.0);
    const double h = sinPhi * sinPhi + std::cos(phi1) * std::cos(phi2) * sinLambda * sinLambda;
    return 2.0 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(h)));
}

void TrailRouter::build(const std::vector<TrailNodeInfo>& nodes,
                        const std::vector<TrailEdgeInfo>& edges) {
    m_nodes.clear();
    m_adjacency.clear();

    // 先按原始索引建图，再把零度节点（导入数据里常见的孤立点）剔除，
    // 否则最近节点吸附可能落到一个永远走不出去的孤点上。
    std::vector<std::vector<Arc>> rawAdjacency(nodes.size());
    std::unordered_map<int, int> rawIndexById;
    rawIndexById.reserve(nodes.size() * 2);
    for (size_t i = 0; i < nodes.size(); ++i) {
        rawIndexById[nodes[i].nodeId] = static_cast<int>(i);
    }

    for (const auto& edge : edges) {
        const auto aIt = rawIndexById.find(edge.startNodeId);
        const auto bIt = rawIndexById.find(edge.endNodeId);
        if (aIt == rawIndexById.end() || bIt == rawIndexById.end()) continue;
        const int a = aIt->second;
        const int b = bIt->second;
        if (a == b) continue;

        const auto& na = nodes[static_cast<size_t>(a)];
        const auto& nb = nodes[static_cast<size_t>(b)];
        const double length = haversineMeters(na.longitude, na.latitude, nb.longitude, nb.latitude);

        rawAdjacency[static_cast<size_t>(a)].push_back(Arc{a, b, edge.edgeId, true, length});
        if (!edge.oneWay) {
            rawAdjacency[static_cast<size_t>(b)].push_back(Arc{b, a, edge.edgeId, false, length});
        }
    }

    std::vector<int> remap(nodes.size(), -1);
    m_nodes.reserve(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        const bool validCoords = nodes[i].longitude != 0.0 || nodes[i].latitude != 0.0;
        if (rawAdjacency[i].empty() || !validCoords) continue;
        remap[i] = static_cast<int>(m_nodes.size());
        m_nodes.push_back(nodes[i]);
    }

    m_adjacency.assign(m_nodes.size(), {});
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (remap[i] < 0) continue;
        for (const auto& arc : rawAdjacency[i]) {
            const int to = remap[static_cast<size_t>(arc.to)];
            if (to < 0) continue;
            m_adjacency[static_cast<size_t>(remap[i])].push_back(
                Arc{remap[i], to, arc.edgeId, arc.forward, arc.lengthM});
        }
    }
}

int TrailRouter::nearestNode(double lng, double lat, double& distanceOut) const {
    int best = -1;
    double bestDistance = std::numeric_limits<double>::max();
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const double d = haversineMeters(lng, lat, m_nodes[i].longitude, m_nodes[i].latitude);
        if (d < bestDistance) {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    }
    distanceOut = bestDistance;
    return best;
}

TrailRouter::Plan TrailRouter::plan(double startLng, double startLat,
                                    double endLng, double endLat,
                                    double maxSnapM) const {
    Plan result;
    if (m_nodes.empty()) {
        result.error = "步道网络为空";
        return result;
    }

    double startSnap = 0;
    double endSnap = 0;
    const int startIndex = nearestNode(startLng, startLat, startSnap);
    const int endIndex = nearestNode(endLng, endLat, endSnap);

    result.startSnapM = startSnap;
    result.endSnapM = endSnap;

    if (startSnap > maxSnapM) {
        result.error = "起点距步道过远，无法规划";
        return result;
    }
    if (endSnap > maxSnapM) {
        result.error = "终点距步道过远，无法规划";
        return result;
    }

    result.startNodeId = m_nodes[static_cast<size_t>(startIndex)].nodeId;
    result.endNodeId = m_nodes[static_cast<size_t>(endIndex)].nodeId;

    const int n = static_cast<int>(m_nodes.size());
    std::vector<double> distance(static_cast<size_t>(n), std::numeric_limits<double>::max());
    std::vector<int> previous(static_cast<size_t>(n), -1);
    std::vector<int> previousEdge(static_cast<size_t>(n), -1);
    std::vector<bool> previousForward(static_cast<size_t>(n), true);

    using Entry = std::pair<double, int>;   // (distance, node index)
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> frontier;

    distance[static_cast<size_t>(startIndex)] = 0.0;
    frontier.push({0.0, startIndex});

    while (!frontier.empty()) {
        const auto [currentDistance, current] = frontier.top();
        frontier.pop();
        if (currentDistance > distance[static_cast<size_t>(current)]) continue;
        if (current == endIndex) break;

        for (const auto& arc : m_adjacency[static_cast<size_t>(current)]) {
            const double candidate = currentDistance + arc.lengthM;
            if (candidate < distance[static_cast<size_t>(arc.to)]) {
                distance[static_cast<size_t>(arc.to)] = candidate;
                previous[static_cast<size_t>(arc.to)] = current;
                previousEdge[static_cast<size_t>(arc.to)] = arc.edgeId;
                previousForward[static_cast<size_t>(arc.to)] = arc.forward;
                frontier.push({candidate, arc.to});
            }
        }
    }

    if (distance[static_cast<size_t>(endIndex)] == std::numeric_limits<double>::max()) {
        result.error = "步道网络内无可行路线";
        return result;
    }

    std::vector<int> indexes;
    for (int at = endIndex; at >= 0; at = previous[static_cast<size_t>(at)]) {
        indexes.push_back(at);
        if (at == startIndex) break;
    }
    std::reverse(indexes.begin(), indexes.end());

    result.nodeIds.reserve(indexes.size());
    for (int index : indexes) {
        result.nodeIds.push_back(m_nodes[static_cast<size_t>(index)].nodeId);
    }

    // Edge ids are collected from the end back to the start, then reversed.
    std::vector<int> edgeIds;
    std::vector<bool> edgeForward;
    for (int at = endIndex; at != startIndex; at = previous[static_cast<size_t>(at)]) {
        edgeIds.push_back(previousEdge[static_cast<size_t>(at)]);
        edgeForward.push_back(previousForward[static_cast<size_t>(at)]);
    }
    std::reverse(edgeIds.begin(), edgeIds.end());
    std::reverse(edgeForward.begin(), edgeForward.end());

    result.edgeIds = edgeIds;
    result.edgeForward = edgeForward;
    result.totalDistanceM = distance[static_cast<size_t>(endIndex)];
    result.ok = true;
    return result;
}

} // namespace scenicflow
