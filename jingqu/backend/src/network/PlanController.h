#pragma once

#include <drogon/HttpController.h>

#include <functional>

namespace scenicflow {

// Peak-avoidance multi-waypoint route planning (requirement: user selects
// several spots and gets a peak-aware ordered walking itinerary).
// GET  /api/plan/spots  -> selectable spots with real-time congestion
// POST /api/plan/route  -> ordered multi-segment route with peak tips
class PlanController : public drogon::HttpController<PlanController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(PlanController::spots, "/api/plan/spots", drogon::Get);
    ADD_METHOD_TO(PlanController::route, "/api/plan/route", drogon::Post);
    METHOD_LIST_END

    void spots(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    void route(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

} // namespace scenicflow
