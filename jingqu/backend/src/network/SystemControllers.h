#pragma once

#include <drogon/HttpController.h>

#include <functional>

namespace scenicflow {

// GET /api/statistics?range=
class StatisticsController : public drogon::HttpController<StatisticsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(StatisticsController::overview, "/api/statistics", drogon::Get);
    METHOD_LIST_END

    void overview(const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback);
};

} // namespace scenicflow
