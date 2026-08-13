#pragma once

#include <string>
#include <vector>
#include <sqlite3.h>

struct SignUpResult
{
    bool success = false;
    std::string message;
};

struct LoginResult
{
    bool success = false;
    std::string message;
    int gold = 0;
};

struct ShopItem
{
    int id = 0;
    std::string name;
    int price = 0;
};

struct InventoryItem
{
    int id = 0;
    std::string name;
};

struct PurchaseResult
{
    bool success = false;
    std::string message;
    int goldRemaining = 0;
};

// SQLite 기반 게임 데이터 저장소 (계정 + 상점/인벤토리).
//
// 이 클래스는 JobQueue 워커 스레드에서만 호출된다는 전제로 만들어서 내부에
// 별도 락이 없다. TCP 게임 로직(PacketHandler)과 HTTP 상점 API(HttpServer)가
// 둘 다 이 클래스를 건드리는데, 둘 다 같은 JobQueue를 거쳐서 호출하기 때문에
// 실제로는 항상 워커 스레드 하나에서 순차적으로만 실행된다 - 그래서 SQLite
// 동시 쓰기 문제를 걱정할 필요가 없다.
class GameDB
{
public:
    bool Open(const std::string &path);
    void Close();

    SignUpResult SignUp(const std::string &username, const std::string &password);
    LoginResult Login(const std::string &username, const std::string &password);

    std::vector<ShopItem> GetShopItems();
    std::vector<InventoryItem> GetInventory(const std::string &username);
    // 골드 확인 -> 차감 -> 인벤토리 추가를 트랜잭션으로 묶어서 처리한다.
    PurchaseResult Purchase(const std::string &username, int itemId);

private:
    bool EnsureSchema();
    void SeedShopItemsIfEmpty();

    sqlite3 *db_ = nullptr;
};
