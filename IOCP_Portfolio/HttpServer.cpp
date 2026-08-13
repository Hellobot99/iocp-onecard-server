#include "HttpServer.h"
#include "Server.h"
#include <httplib.h>
#include <sstream>
#include <future>

extern Server *g_Server;

namespace
{
    // 외부 JSON 라이브러리 없이 응답 문자열을 직접 만든다 - 여기서 다루는 값이
    // id/price 같은 정수와 아이템 이름 정도라 이 정도면 충분하고, 파싱이
    // 필요한 요청 쪽은 JSON 대신 폼 파라미터(get_param_value)를 쓴다.
    std::string JsonEscape(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            if (c == '"' || c == '\\')
                out += '\\';
            out += c;
        }
        return out;
    }
}

HttpServer::HttpServer()
    : server_(std::make_unique<httplib::Server>())
{
}

HttpServer::~HttpServer()
{
    Stop();
}

void HttpServer::Start(int port)
{
    // GET /shop/items - 상점에 있는 아이템 목록.
    server_->Get("/shop/items", [](const httplib::Request &, httplib::Response &res)
                 {
        std::promise<std::vector<ShopItem>> promise;
        std::future<std::vector<ShopItem>> future = promise.get_future();

        g_Server->GetJobQueue().Push([&promise]()
        {
            promise.set_value(g_Server->GetGameDB().GetShopItems());
        });

        std::vector<ShopItem> items = future.get();

        std::ostringstream json;
        json << "[";
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (i > 0)
                json << ",";
            json << "{\"id\":" << items[i].id
                 << ",\"name\":\"" << JsonEscape(items[i].name) << "\""
                 << ",\"price\":" << items[i].price << "}";
        }
        json << "]";

        res.set_content(json.str(), "application/json"); });

    // GET /inventory/{username} - 그 계정이 보유한 아이템 목록.
    server_->Get(R"(/inventory/([a-zA-Z0-9_]+))", [](const httplib::Request &req, httplib::Response &res)
                 {
        std::string username = req.matches[1];

        std::promise<std::vector<InventoryItem>> promise;
        std::future<std::vector<InventoryItem>> future = promise.get_future();

        g_Server->GetJobQueue().Push([&promise, username]()
        {
            promise.set_value(g_Server->GetGameDB().GetInventory(username));
        });

        std::vector<InventoryItem> items = future.get();

        std::ostringstream json;
        json << "[";
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (i > 0)
                json << ",";
            json << "{\"id\":" << items[i].id << ",\"name\":\"" << JsonEscape(items[i].name) << "\"}";
        }
        json << "]";

        res.set_content(json.str(), "application/json"); });

    // POST /shop/purchase (application/x-www-form-urlencoded: username, item_id)
    // 골드 확인 -> 차감 -> 인벤토리 추가를 GameDB::Purchase 안에서 트랜잭션으로
    // 묶어서 처리한다.
    server_->Post("/shop/purchase", [](const httplib::Request &req, httplib::Response &res)
                  {
        std::string username = req.get_param_value("username");
        int itemId = std::atoi(req.get_param_value("item_id").c_str());

        if (username.empty())
        {
            res.status = 400;
            res.set_content("{\"success\":false,\"message\":\"username이 필요합니다.\"}", "application/json");
            return;
        }

        std::promise<PurchaseResult> promise;
        std::future<PurchaseResult> future = promise.get_future();

        g_Server->GetJobQueue().Push([&promise, username, itemId]()
        {
            promise.set_value(g_Server->GetGameDB().Purchase(username, itemId));
        });

        PurchaseResult result = future.get();

        std::ostringstream json;
        json << "{\"success\":" << (result.success ? "true" : "false")
             << ",\"message\":\"" << JsonEscape(result.message) << "\""
             << ",\"gold\":" << result.goldRemaining << "}";

        res.set_content(json.str(), "application/json"); });

    thread_ = std::thread([this, port]()
                           { server_->listen("0.0.0.0", port); });
}

void HttpServer::Stop()
{
    if (server_)
        server_->stop();

    if (thread_.joinable())
        thread_.join();
}
