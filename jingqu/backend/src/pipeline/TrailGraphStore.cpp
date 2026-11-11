#include "pipeline/TrailGraphStore.h"

#include "storage/Database.h"
#include "network/Mappers.h"

#include <mutex>
#include <stdexcept>
#include <string>
#include <iostream>

using namespace scenicflow;

namespace {

std::vector<TrailNodeInfo> loadNodes() {
    std::vector<TrailNodeInfo> nodes;
    auto rows = Database::instance().query(
        "SELECT id,node_code,name,longitude,latitude,node_type,note,image_url,source"
        " FROM trail_node WHERE status=1 ORDER BY id");
    for (const auto& r : rows) {
        TrailNodeInfo n;
        n.nodeId = toInt(field(r, "id"));
        n.nodeCode = field(r, "node_code");
        n.name = field(r, "name");
        n.nodeType = field(r, "node_type", "PATH");
        n.note = field(r, "note");
        n.imageUrl = field(r, "image_url");
        n.source = field(r, "source", "manual");
        n.longitude = toDouble(field(r, "longitude"));
        n.latitude = toDouble(field(r, "latitude"));
        nodes.push_back(n);
    }
    return nodes;
}

std::vector<TrailEdgeInfo> loadEdges() {
    std::vector<TrailEdgeInfo> edges;
    auto rows = Database::instance().query(
        "SELECT id,start_node_id,end_node_id,one_way,direction_hint,source,geometry"
        " FROM trail_edge WHERE status=1 ORDER BY id");
    for (const auto& r : rows) {
        TrailEdgeInfo e;
        e.edgeId = toInt(field(r, "id"));
        e.startNodeId = toInt(field(r, "start_node_id"));
        e.endNodeId = toInt(field(r, "end_node_id"));
        e.oneWay = toInt(field(r, "one_way")) != 0;
        e.directionHint = field(r, "direction_hint");
        e.source = field(r, "source", "manual");
        e.geometry = field(r, "geometry");
        edges.push_back(e);
    }
    return edges;
}

} // namespace

const TrailGraph& scenicflow::cachedTrailGraph() {
    static TrailGraph graph;
    static std::mutex mutex;
    static bool loaded = false;
    std::lock_guard<std::mutex> lock(mutex);
    if (!loaded) {
        try {
            graph.nodes = loadNodes();
            std::cerr << "DEBUG: loaded " << graph.nodes.size() << " nodes" << std::endl;
            graph.edges = loadEdges();
            std::cerr << "DEBUG: loaded " << graph.edges.size() << " edges" << std::endl;
            loaded = true;
        } catch (const std::exception& e) {
            std::cerr << "DEBUG: exception in cachedTrailGraph: " << e.what() << std::endl;
            throw;
        }
    }
    return graph;
}
