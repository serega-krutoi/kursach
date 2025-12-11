#include "db.h"

#include <iostream>
#include <optional>
#include <vector>
#include <string>
#include <cstdlib>
#include <stdexcept>
#include <sstream>
#include <memory>

#include <pqxx/pqxx>

namespace db {

// ==================== DbConfig::fromEnv ====================

static std::string getEnvOrThrow(const char* name) {
    const char* val = std::getenv(name);
    if (!val) {
        std::string msg = "Environment variable ";
        msg += name;
        msg += " is not set";
        throw std::runtime_error(msg);
    }
    return std::string(val);
}

DbConfig DbConfig::fromEnv() {
    DbConfig cfg;
    cfg.host     = getEnvOrThrow("KURSACH_DB_HOST");
    cfg.dbname   = getEnvOrThrow("KURSACH_DB_NAME");
    cfg.user     = getEnvOrThrow("KURSACH_DB_USER");
    cfg.password = getEnvOrThrow("KURSACH_DB_PASSWORD");

    const char* portStr = std::getenv("KURSACH_DB_PORT");
    cfg.port = portStr ? std::stoi(portStr) : 5432; // по умолчанию 5432

    return cfg;
}

// ==================== ConnectionFactory ====================

ConnectionFactory::ConnectionFactory(const DbConfig& cfg)
    : config(cfg) {}

std::unique_ptr<pqxx::connection> ConnectionFactory::createConnection() const {
    std::stringstream ss;
    ss << "host=" << config.host
       << " port=" << config.port
       << " dbname=" << config.dbname
       << " user=" << config.user
       << " password=" << config.password;

    return std::make_unique<pqxx::connection>(ss.str());
}

// ==================== UserRepository ====================

UserRepository::UserRepository(ConnectionFactory& f)
    : factory(f) {}

std::optional<DbUser> UserRepository::findUserByUsername(const std::string& username) {
    auto conn = factory.createConnection();
    pqxx::work tx{*conn};

    // Без шифрования, читаем email_plain (так как мы его так и пишем)
    auto res = tx.exec_params(
        "SELECT id, username, password_hash, role, email_plain "
        "FROM app_user "
        "WHERE username = $1",
        username
    );

    if (res.empty()) {
        return std::nullopt;
    }

    const auto& row = res[0];
    DbUser user;
    user.id           = row["id"].as<long>();
    user.username     = row["username"].as<std::string>();
    user.passwordHash = row["password_hash"].as<std::string>();
    user.role         = row["role"].as<std::string>();

    if (!row["email_plain"].is_null()) {
        user.email = row["email_plain"].as<std::string>();
    } else {
        user.email = std::nullopt;
    }

    tx.commit();
    return user;
}

long UserRepository::createUser(const std::string& username,
                                const std::string& passwordHash,
                                const std::string& role,
                                const std::optional<std::string>& email) {
    auto conn = factory.createConnection();
    pqxx::work tx{*conn};

    pqxx::row row;

    if (email.has_value() && !email->empty()) {
        row = tx.exec_params1(
            R"SQL(
                INSERT INTO app_user (username, password_hash, role, email_plain)
                VALUES ($1, $2, $3, $4)
                RETURNING id
            )SQL",
            username,
            passwordHash,
            role,
            *email
        );
    } else {
        row = tx.exec_params1(
            R"SQL(
                INSERT INTO app_user (username, password_hash, role)
                VALUES ($1, $2, $3)
                RETURNING id
            )SQL",
            username,
            passwordHash,
            role
        );
    }

    tx.commit();
    return row["id"].as<long>();
}

// ==================== ScheduleRepository ====================

long ScheduleRepository::createSchedule(
    long userId,
    const std::string& configJson,
    const std::string& resultJson,
    const std::optional<std::string>& name
) {
    auto conn = factory_.createConnection();
    pqxx::work tx{*conn};

    const char* sql = R"SQL(
        INSERT INTO exam_schedule (user_id, name, config_json, result_json)
        VALUES ($1, $2, $3::jsonb, $4::jsonb)
        RETURNING id;
    )SQL";

    std::string nameValue = name.value_or("");

    pqxx::row row = tx.exec_params1(
        sql,
        userId,
        nameValue,
        configJson,
        resultJson
    );

    tx.commit();

    long id = row["id"].as<long>();
    return id;
}

std::vector<DbSchedule> ScheduleRepository::findSchedulesByUser(long userId) {
    std::vector<DbSchedule> result;

    auto conn = factory_.createConnection();
    pqxx::work tx{*conn};

    const char* sql = R"SQL(
        SELECT
            id,
            user_id,
            COALESCE(name, '') AS name,
            config_json::text AS config_json,
            result_json::text AS result_json,
            created_at,
            updated_at
        FROM exam_schedule
        WHERE user_id = $1
        ORDER BY created_at DESC, id DESC;
    )SQL";

    pqxx::result rows = tx.exec_params(sql, userId);
    tx.commit();

    for (const auto& r : rows) {
        DbSchedule s;
        s.id         = r["id"].as<long>();
        s.userId     = r["user_id"].as<long>();
        s.name       = r["name"].as<std::string>();
        s.configJson = r["config_json"].as<std::string>();
        s.resultJson = r["result_json"].as<std::string>();
        s.createdAt  = r["created_at"].c_str();
        s.updatedAt  = r["updated_at"].c_str();
        result.push_back(std::move(s));
    }

    return result;
}

std::optional<DbSchedule> ScheduleRepository::findScheduleById(
    long userId,
    long scheduleId
) {
    auto conn = factory_.createConnection();
    pqxx::work tx{*conn};

    const char* sql = R"SQL(
        SELECT
            id,
            user_id,
            COALESCE(name, '') AS name,
            config_json::text AS config_json,
            result_json::text AS result_json,
            created_at,
            updated_at
        FROM exam_schedule
        WHERE id = $1
          AND user_id = $2
        LIMIT 1;
    )SQL";

    pqxx::result rows = tx.exec_params(sql, scheduleId, userId);
    tx.commit();

    if (rows.empty()) {
        return std::nullopt;
    }

    const auto& r = rows[0];

    DbSchedule s;
    s.id         = r["id"].as<long>();
    s.userId     = r["user_id"].as<long>();
    s.name       = r["name"].as<std::string>();
    s.configJson = r["config_json"].as<std::string>();
    s.resultJson = r["result_json"].as<std::string>();
    s.createdAt  = r["created_at"].c_str();
    s.updatedAt  = r["updated_at"].c_str();

    return s;
}

// ==================== DomainData (пока заглушки) ====================
//
// Если хочешь реально грузить группы/преподов/предметы из БД,
// сюда позже добавим нормальную реализацию. Пока сделаем заглушки,
// чтобы линковка не ругалась, и они просто не будут использоваться.
//

std::vector<Group> loadGroups(ConnectionFactory& /*factory*/) {
    return {};
}

std::vector<Teacher> loadTeachers(ConnectionFactory& /*factory*/) {
    return {};
}

DomainData loadDomainData(ConnectionFactory& /*factory*/) {
    DomainData d;
    return d;
}

} // namespace db
