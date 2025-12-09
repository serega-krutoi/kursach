#include "db.h"
#include <cstdlib>
#include <stdexcept>
#include <sstream>
#include <memory>

#include <pqxx/pqxx>

namespace db {

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

}

namespace db {

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

}


namespace db {

UserRepository::UserRepository(ConnectionFactory& f)
    : factory(f) {}

std::optional<DbUser> UserRepository::findUserByUsername(const std::string& username) {
    auto conn = factory.createConnection();
    pqxx::work tx{*conn};

    // Читаем пользователя, а email расшифровываем через pgp_sym_decrypt, если ключ настроен
    auto res = tx.exec_params(
        "SELECT id, username, password_hash, role, "
        "       email_plain, "
        "       CASE WHEN email_encrypted IS NOT NULL "
        "            THEN pgp_sym_decrypt(email_encrypted, current_setting('app.crypto_key')) "
        "            ELSE NULL::text "
        "       END AS email_dec "
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

    if (!row["email_dec"].is_null()) {
        user.email = row["email_dec"].as<std::string>();
    } else if (!row["email_plain"].is_null()) {
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

    pqxx::result res;
    if (email.has_value()) {
        res = tx.exec_params(
            "INSERT INTO app_user (username, password_hash, role, email_plain, email_encrypted) "
            "VALUES ($1, $2, $3, $4, "
            "        pgp_sym_encrypt($4, current_setting('app.crypto_key'))) "
            "RETURNING id",
            username, passwordHash, role, email.value()
        );
    } else {
        res = tx.exec_params(
            "INSERT INTO app_user (username, password_hash, role) "
            "VALUES ($1, $2, $3) "
            "RETURNING id",
            username, passwordHash, role
        );
    }

    long id = res[0]["id"].as<long>();
    tx.commit();
    return id;
}

}

namespace db {

/* std::vector<Group> loadGroups(ConnectionFactory& factory) {
    auto conn = factory.createConnection();
    pqxx::work tx{*conn};

    pqxx::result res = tx.exec(
        "SELECT id, name, size FROM student_group ORDER BY id"
    );

    std::vector<Group> groups;
    groups.reserve(res.size());

    for (const auto& row : res) {
        Group g;
        g.id   = row["id"].as<int>();        // или long long → int, в зависимости от model.h
        g.name = row["name"].as<std::string>();
        g.size = row["size"].as<int>();
        groups.push_back(std::move(g));
    }

    tx.commit();
    return groups;
} */

}
