#pragma once

#include "pipeline/TrailRouter.h"

#include <mutex>
#include <vector>

namespace scenicflow {

// Shared in-process cache of the trail graph. Loaded once from the DB,
// safe for concurrent read-only use afterwards.
struct TrailGraph {
    std::vector<TrailNodeInfo> nodes;
    std::vector<TrailEdgeInfo> edges;
};

const TrailGraph& cachedTrailGraph();

} // namespace scenicflow
