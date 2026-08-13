#include "GameDB.h"
#include "PasswordHash.h"
#include <iostream>

bool GameDB::Open(const std::string &path)
{
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK)
    {
        std::cerr << "[GameDB] sqlite3_open 실패: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    if (!EnsureSchema())
        return false;

    SeedShopItemsIfEmpty();
    return true;
}

void GameDB::Close()
{
    if (db_ != nullptr)
    {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool GameDB::EnsureSchema()
{
    const char *sql =
        "CREATE TABLE IF NOT EXISTS accounts ("
        "  username TEXT PRIMARY KEY,"
        "  salt TEXT NOT NULL,"
        "  password_hash TEXT NOT NULL,"
        "  gold INTEGER NOT NULL DEFAULT 0,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS items ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  price INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS inventory ("
        "  username TEXT NOT NULL,"
        "  item_id INTEGER NOT NULL,"
        "  acquired_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  PRIMARY KEY (username, item_id)"
        ");";

    char *errMsg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg) != SQLITE_OK)
    {
        std::cerr << "[GameDB] 스키마 생성 실패: " << (errMsg ? errMsg : "") << std::endl;
        sqlite3_free(errMsg);
        return false;
    }

    return true;
}

void GameDB::SeedShopItemsIfEmpty()
{
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM items;", -1, &stmt, nullptr);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (count > 0)
        return;

    const char *sql =
        "INSERT INTO items (name, price) VALUES "
        "('카드 뒷면 - 블루', 100),"
        "('카드 뒷면 - 그린', 100),"
        "('카드 뒷면 - 레드', 100),"
        "('승리 이펙트 티켓', 50);";

    char *errMsg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg) != SQLITE_OK)
    {
        std::cerr << "[GameDB] 상점 아이템 초기화 실패: " << (errMsg ? errMsg : "") << std::endl;
        sqlite3_free(errMsg);
    }
}

SignUpResult GameDB::SignUp(const std::string &username, const std::string &password)
{
    SignUpResult result;

    if (username.size() < 3 || username.size() > 20)
    {
        result.message = "아이디는 3~20자여야 합니다.";
        return result;
    }
    if (password.size() < 4)
    {
        result.message = "비밀번호는 4자 이상이어야 합니다.";
        return result;
    }

    std::string salt = PasswordHash::GenerateSaltHex();
    std::string hash = PasswordHash::Hash(password, salt);

    const char *sql = "INSERT INTO accounts (username, salt, password_hash, gold) VALUES (?, ?, ?, 500);";
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        result.message = "서버 내부 오류입니다.";
        return result;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, salt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, hash.c_str(), -1, SQLITE_TRANSIENT);

    int stepResult = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (stepResult == SQLITE_DONE)
    {
        result.success = true;
        result.message = "회원가입 완료. 시작 골드 500 지급.";
    }
    else if (stepResult == SQLITE_CONSTRAINT)
    {
        result.message = "이미 존재하는 아이디입니다.";
    }
    else
    {
        result.message = "회원가입 중 오류가 발생했습니다.";
    }

    return result;
}

LoginResult GameDB::Login(const std::string &username, const std::string &password)
{
    LoginResult result;

    const char *sql = "SELECT salt, password_hash, gold FROM accounts WHERE username = ?;";
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        result.message = "서버 내부 오류입니다.";
        return result;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    // 아이디가 없을 때와 비밀번호가 틀렸을 때 메시지를 일부러 똑같이 준다
    // (아이디 존재 여부를 응답으로 유추할 수 없게 하는 통상적인 보안 관행).
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        std::string salt = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        std::string hash = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        int gold = sqlite3_column_int(stmt, 2);

        if (PasswordHash::Verify(password, salt, hash))
        {
            result.success = true;
            result.message = "로그인 성공.";
            result.gold = gold;
        }
        else
        {
            result.message = "아이디 또는 비밀번호가 틀렸습니다.";
        }
    }
    else
    {
        result.message = "아이디 또는 비밀번호가 틀렸습니다.";
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<ShopItem> GameDB::GetShopItems()
{
    std::vector<ShopItem> result;

    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT id, name, price FROM items ORDER BY id;", -1, &stmt, nullptr);

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        ShopItem item;
        item.id = sqlite3_column_int(stmt, 0);
        item.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        item.price = sqlite3_column_int(stmt, 2);
        result.push_back(item);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<InventoryItem> GameDB::GetInventory(const std::string &username)
{
    std::vector<InventoryItem> result;

    const char *sql =
        "SELECT items.id, items.name FROM inventory "
        "JOIN items ON inventory.item_id = items.id "
        "WHERE inventory.username = ? ORDER BY inventory.acquired_at;";
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        InventoryItem item;
        item.id = sqlite3_column_int(stmt, 0);
        item.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        result.push_back(item);
    }

    sqlite3_finalize(stmt);
    return result;
}

PurchaseResult GameDB::Purchase(const std::string &username, int itemId)
{
    PurchaseResult result;

    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    sqlite3_stmt *stmt = nullptr;

    // 아이템 가격 조회.
    sqlite3_prepare_v2(db_, "SELECT price FROM items WHERE id = ?;", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, itemId);
    if (sqlite3_step(stmt) != SQLITE_ROW)
    {
        sqlite3_finalize(stmt);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        result.message = "존재하지 않는 아이템입니다.";
        return result;
    }
    int price = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    // 이미 보유 중인지 확인.
    sqlite3_prepare_v2(db_, "SELECT 1 FROM inventory WHERE username = ? AND item_id = ?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, itemId);
    bool alreadyOwned = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    if (alreadyOwned)
    {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        result.message = "이미 보유한 아이템입니다.";
        return result;
    }

    // 현재 골드 조회.
    sqlite3_prepare_v2(db_, "SELECT gold FROM accounts WHERE username = ?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_ROW)
    {
        sqlite3_finalize(stmt);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        result.message = "존재하지 않는 계정입니다.";
        return result;
    }
    int gold = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (gold < price)
    {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        result.message = "골드가 부족합니다.";
        result.goldRemaining = gold;
        return result;
    }

    int newGold = gold - price;

    sqlite3_prepare_v2(db_, "UPDATE accounts SET gold = ? WHERE username = ?;", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, newGold);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    sqlite3_prepare_v2(db_, "INSERT INTO inventory (username, item_id) VALUES (?, ?);", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, itemId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);

    result.success = true;
    result.message = "구매 완료.";
    result.goldRemaining = newGold;
    return result;
}
