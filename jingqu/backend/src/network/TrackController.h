#pragma once

#include <drogon/HttpController.h>

#include <functional>

namespace scenicflow {

// Curated mature walking routes (两步路 / 六只脚 style).
// GET /api/track/list     -> all selectable routes (summary only)
// GET /api/track/{id}     -> full ordered track of one route
// Read-only, available to every logged-in role (see AuthService::canAccess).
class TrackController : public drogon::HttpController<TrackController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TrackController::list, "/api/track/list", drogon::Get);
    ADD_METHOD_TO(TrackController::detail, "/api/track/{1}", drogon::Get);
    METHOD_LIST_END

    void list(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    void detail(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                int trackId);
};

} // namespace scenicflow
