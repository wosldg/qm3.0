#pragma once

#include "pipeline/TrailGraphStore.h"

#include <json/json.h>

#include <memory>
#include <string>

namespace scenicflow {

constexpr double kWalkSpeedMetersPerMinute = 66.7;   // 4 km/h

// Plan one walking segment over the trail network and assemble the nodes /
// hints JSON shared by TrailController::route and PlanController::route.
// Returns unique_ptr to avoid large Json::Value copy on return (prevents stack overflow).
std::unique_ptr<Json::Value> buildSegmentJson(const TrailGraph& g, double fromLng, double fromLat,
                                              double toLng, double toLat, std::string& err);

} // namespace scenicflow
