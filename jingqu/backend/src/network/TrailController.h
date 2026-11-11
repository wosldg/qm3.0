#pragma once

#include <drogon/HttpController.h>

#include <functional>

namespace scenicflow {

// District-internal walking-trail navigation (requirement: trail network routing).
// GET  /api/trail/nodes  -> full trail graph (nodes + segments)
// POST /api/trail/route  -> shortest path between two coordinates
// Both are available to every logged-in role (see AuthService::canAccess).
class TrailController : public drogon::HttpController<TrailController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TrailController::network, "/api/trail/nodes", drogon::Get);
    ADD_METHOD_TO(TrailController::route, "/api/trail/route", drogon::Post);
    METHOD_LIST_END

    void network(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    void route(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

} // namespace scenicflow
