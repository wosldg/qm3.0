#pragma once

#include <string>
#include <vector>

namespace scenicflow {

// One vertex of the park's internal walking-trail network (GCJ-02).
struct TrailNodeInfo {
    int nodeId = 0;
    std::string nodeCode;
    std::string name;
    std::string nodeType;   // ENTRANCE / JUNCTION / VIEWPOINT / PATH
    std::string note;
    std::string imageUrl;
    std::string source;   // manual / osm，前端据此区分主干与小路线宽
    double longitude = 0;
    double latitude = 0;
};

// One walkable segment. Length is not stored in the DB: it is derived from the
// node coordinates with the haversine formula at build time.
struct TrailEdgeInfo {
    int edgeId = 0;
    int startNodeId = 0;
    int endNodeId = 0;
    bool oneWay = false;
    std::string directionHint;   // only meaningful walking startNodeId -> endNodeId
    std::string source;          // manual / osm
    std::string geometry;        // JSON [[lng,lat],...] intermediate geometry along this edge
};

// Pure graph algorithm: no database access, so it can be unit tested.
// Nodes are the trail vertices, edge weight is the haversine distance in meters,
// and the shortest path is computed with Dijkstra (the graph has ~100 vertices).
class TrailRouter {
public:
    static double haversineMeters(double lng1, double lat1, double lng2, double lat2);

    struct Plan {
        bool ok = false;
        std::string error;              // filled when ok == false
        std::vector<int> nodeIds;       // ordered polyline vertices
        std::vector<int> edgeIds;       // size nodeIds.size() - 1
        std::vector<bool> edgeForward;  // size nodeIds.size() - 1
        double totalDistanceM = 0;
        double startSnapM = 0;          // distance from the requested start to its node
        double endSnapM = 0;
        int startNodeId = 0;
        int endNodeId = 0;
    };

    void build(const std::vector<TrailNodeInfo>& nodes, const std::vector<TrailEdgeInfo>& edges);
    bool empty() const { return m_nodes.empty(); }

    // 500 m is the maximum accepted distance from a requested coordinate to the
    // trail; beyond it the caller should report an error instead of silently
    // returning a route that starts far away from the user.
    Plan plan(double startLng, double startLat, double endLng, double endLat,
              double maxSnapM = 500.0) const;

    int nearestNode(double lng, double lat, double& distanceOut) const;

private:
    struct Arc {
        int from = 0;       // node index
        int to = 0;         // node index
        int edgeId = 0;
        bool forward = true;   // walked along the stored start -> end direction
        double lengthM = 0;
    };

    std::vector<TrailNodeInfo> m_nodes;
    std::vector<std::vector<Arc>> m_adjacency;   // indexed by node index
};

} // namespace scenicflow
