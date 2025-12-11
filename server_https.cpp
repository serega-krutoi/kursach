// server_https.cpp
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"

#include <string>
#include <vector>
#include <cstdio>
#include <optional>
#include <cstdlib>
#include <cctype>

#include <pqxx/pqxx>

#include "jwt_utils.h"
#include "auth.h"
#include "db.h"

#include "model.h"
#include "generator.h"
#include "graph.h"
#include "validator.h"
#include "api_dto.h"
#include "logger.h"

using nlohmann::json;

std::vector<ExamView> buildExamViews(
    const std::vector<Exam>& exams,
    const std::vector<Group>& groups,
    const std::vector<Teacher>& teachers,
    const std::vector<Subject>& subjects,
    const std::vector<Room>& rooms,
    const std::vector<Timeslot>& timeslots,
    const std::vector<ExamAssignment>& assignments
);

std::string buildApiResponseJsonString(const ApiResponse& resp);

// --- глобальные дефолтные данные из data.cpp ---
extern std::vector<Group> groups;
extern std::vector<Teacher> teachers;
extern std::vector<Room> rooms;
extern std::vector<Subject> subjects;
extern std::vector<Timeslot> timeslots;
extern std::vector<Exam> exams;

extern std::string sessionStart;
extern std::string sessionEnd;
extern int maxExamsPerDayForGroup;

// --------- хелпер: генерация JSON-ответа по данным ---------

static std::string makeJsonResponse(
    const std::vector<Group>& groupsLocal,
    const std::vector<Teacher>& teachersLocal,
    const std::vector<Room>& roomsLocal,
    const std::vector<Subject>& subjectsLocal,
    const std::vector<Timeslot>& timeslotsLocal,
    const std::vector<Exam>& examsLocal,
    int maxPerDay,
    const std::string& sessionStartLocal,
    const std::string& sessionEndLocal
) {
    logInfo("Запускаем graph-генератор (maxPerDay=" + std::to_string(maxPerDay) + ")");

    std::vector<ExamAssignment> assignments = generateSchedule(
        examsLocal,
        groupsLocal,
        subjectsLocal,
        timeslotsLocal,
        roomsLocal,
        maxPerDay
    );

    ScheduleValidator validator;
    ValidationResult vr = validator.checkAll(
        examsLocal,
        groupsLocal,
        teachersLocal,
        roomsLocal,
        timeslotsLocal,
        assignments,
        sessionStartLocal,
        sessionEndLocal,
        maxPerDay
    );

    ApiResponse resp;
    resp.algorithm = "graph";
    resp.schedule  = buildExamViews(
        examsLocal,
        groupsLocal,
        teachersLocal,
        subjectsLocal,
        roomsLocal,
        timeslotsLocal,
        assignments
    );
    resp.ok     = vr.ok;
    resp.errors = vr.errors;

    return buildApiResponseJsonString(resp);
}

// --------- JWT helpers ---------

static std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

static std::optional<std::string> getTokenFromCookie(const std::string& cookieHeader) {
    // Cookie: key1=val1; auth_token=...; other=...
    size_t pos = 0;
    while (pos < cookieHeader.size()) {
        size_t sep = cookieHeader.find(';', pos);
        std::string part = (sep == std::string::npos)
            ? cookieHeader.substr(pos)
            : cookieHeader.substr(pos, sep - pos);

        part = trim(part);
        const std::string prefix = "auth_token=";
        if (part.rfind(prefix, 0) == 0) { // начинается с auth_token=
            return part.substr(prefix.size());
        }

        if (sep == std::string::npos) break;
        pos = sep + 1;
    }
    return std::nullopt;
}

static std::optional<std::string> getTokenFromAuthorization(const std::string& authHeader) {
    // Authorization: Bearer <token>
    const std::string bearer = "Bearer ";
    if (authHeader.rfind(bearer, 0) == 0) {
        return authHeader.substr(bearer.size());
    }
    return std::nullopt;
}

// Возвращает JwtPayload, если токен валиден (из Cookie или Authorization), иначе nullopt
static std::optional<JwtPayload> getUserFromRequest(
    const httplib::Request& req,
    const std::string& jwtSecret
) {
    std::optional<std::string> token;

    // 1) Authorization: Bearer ...
    auto itAuth = req.headers.find("Authorization");
    if (itAuth != req.headers.end()) {
        token = getTokenFromAuthorization(itAuth->second);
    }

    // 2) Cookie: auth_token=...
    if (!token.has_value()) {
        auto itCookie = req.headers.find("Cookie");
        if (itCookie != req.headers.end()) {
            token = getTokenFromCookie(itCookie->second);
        }
    }

    if (!token.has_value()) {
        return std::nullopt;
    }

    return verifyJwt(*token, jwtSecret);
}

int main() {
    logInfo("=== Запуск HTTPS сервера на 127.0.0.1:8443 ===");

    try {
        // JWT secret
        const char* jwtSecretEnv = std::getenv("KURSACH_JWT_SECRET");
        if (!jwtSecretEnv) {
            throw std::runtime_error("Environment variable KURSACH_JWT_SECRET is not set");
        }
        std::string jwtSecret(jwtSecretEnv);

        // Конфиг БД из env
        db::DbConfig dbCfg = db::DbConfig::fromEnv();
        db::ConnectionFactory dbFactory{dbCfg};
        db::UserRepository userRepo{dbFactory};
	db::ScheduleRepository scheduleRepo{dbFactory};

        logInfo("Успешно инициализирована конфигурация БД");

        httplib::SSLServer svr("server-cert.pem", "server-key.pem");

        if (!svr.is_valid()) {
            logError("SSLServer невалиден. Проверь server-cert.pem и server-key.pem");
            return 1;
        }

// --- Публичное расписание, доступное всем (гости/студенты) ---

svr.Get("/api/public/schedule", [&](const httplib::Request& req, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");

    try {
        auto conn = dbFactory.createConnection();
        pqxx::work tx{*conn};

        // Просто берём самое последнее расписание
        auto r = tx.exec(
            "SELECT result_json "
            "FROM exam_schedule "
            "ORDER BY created_at DESC "
            "LIMIT 1"
        );
        tx.commit();

        if (r.empty()) {
            res.status = 404;
            res.set_content(
                R"({"error":"no_schedule"})",
                "application/json; charset=utf-8"
            );
            return;
        }

        std::string jsonResp = r[0][0].c_str();

        res.status = 200;
        res.set_content(jsonResp, "application/json; charset=utf-8");
    } catch (const std::exception& ex) {
        logError(std::string("Error in GET /api/public/schedule: ") + ex.what());
        res.status = 500;
        res.set_content(
            R"({"error":"internal server error"})",
            "application/json; charset=utf-8"
        );
    }
});

// preflight
svr.Options("/api/public/schedule", [](const httplib::Request& req, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    res.status = 204;
});

        // --- ПУБЛИЧНОЕ: последнее сохранённое расписание (config + result) ---
        svr.Get("/api/public/latest", [&](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");

            try {
                auto conn = dbFactory.createConnection();
                pqxx::work tx{*conn};

                auto r = tx.exec(
                    "SELECT id, user_id, COALESCE(name, '') AS name, "
                    "       config_json::text AS config_json, "
                    "       result_json::text AS result_json, "
                    "       created_at, updated_at "
                    "FROM exam_schedule "
                    "ORDER BY created_at DESC, id DESC "
                    "LIMIT 1"
                );
                tx.commit();

                if (r.empty()) {
                    res.status = 404;
                    res.set_content(
                        R"({"error":"no_schedule"})",
                        "application/json; charset=utf-8"
                    );
                    return;
                }

                const auto& row = r[0];

                long id           = row["id"].as<long>();
                std::string name  = row["name"].as<std::string>("");
                std::string createdAt = row["created_at"].as<std::string>("");
                std::string updatedAt = row["updated_at"].as<std::string>("");
                std::string configStr = row["config_json"].as<std::string>("");
                std::string resultStr = row["result_json"].as<std::string>("");

                json configJson;
                json resultJson;

                try {
                    configJson = json::parse(configStr);
                } catch (...) {
                    configJson = json::object();
                }

                try {
                    resultJson = json::parse(resultStr);
                } catch (...) {
                    resultJson = json::object();
                }

                json resp = {
                    {"ok", true},
                    {"schedule", {
                        {"id", id},
                        {"name", name},
                        {"createdAt", createdAt},
                        {"updatedAt", updatedAt},
                        {"config", configJson},
                        {"result", resultJson}
                    }}
                };

                res.status = 200;
                res.set_content(resp.dump(), "application/json; charset=utf-8");
            } catch (const std::exception& ex) {
                logError(std::string("Error in GET /api/public/latest: ") + ex.what());
                res.status = 500;
                res.set_content(
                    R"({"error":"internal server error"})",
                    "application/json; charset=utf-8"
                );
            }
        });

        // preflight для /api/public/latest
        svr.Options("/api/public/latest", [](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
            res.status = 204;
        });


        // --- корень ---
        svr.Get("/", [](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(
                "HTTPS exam schedule server is running.\n"
                "GET  /api/schedule?maxPerDay=N  (дефолтные данные из data.cpp)\n"
                "POST /api/schedule              (данные из config, JSON от фронта)\n"
                "GET  /api/health/db             (проверка подключения к БД)\n"
                "AUTH: /api/auth/login, /api/auth/me, /api/admin/ping\n",
                "text/plain; charset=utf-8"
            );
        });

        // --- GET /api/auth/me ---
        svr.Get("/api/auth/me", [&](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");

            auto payloadOpt = getUserFromRequest(req, jwtSecret);
            if (!payloadOpt.has_value()) {
                res.status = 401;
                res.set_content(
                    R"({"error":"unauthorized"})",
                    "application/json; charset=utf-8"
                );
                return;
            }

            const auto& p = *payloadOpt;

            json resp = {
                {"ok", true},
                {"user", {
                    {"id", p.userId},
                    {"role", p.role}
                }}
            };

            res.status = 200;
            res.set_content(resp.dump(), "application/json; charset=utf-8");
        });

        svr.Options("/api/auth/me", [](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
            res.status = 204;
        });

	svr.Get("/api/schedule/my", [&](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");

        auto payloadOpt = getUserFromRequest(req, jwtSecret);
        if (!payloadOpt.has_value()) {
            res.status = 401;
            res.set_content(
                R"({"error":"unauthorized"})",
                "application/json; charset=utf-8"
            );
            return;
        }

        const auto& p = *payloadOpt; // JwtPayload { userId, role, ... }

        try {
	    auto items = scheduleRepo.findSchedulesByUser(p.userId);

	    nlohmann::json arr = nlohmann::json::array();
	    for (const auto &s : items) {
	        nlohmann::json item;
	        item["id"]        = s.id;
	        item["name"]      = s.name;
	        item["createdAt"] = s.createdAt;
	        arr.push_back(item);
	    }


            nlohmann::json resp = {
                {"ok", true},
                {"items", arr}
            };

            res.status = 200;
            res.set_content(resp.dump(), "application/json; charset=utf-8");
        } catch (const std::exception &ex) {
            logError(std::string("Error in /api/schedule/my: ") + ex.what());
            res.status = 500;
            res.set_content(
                R"({"error":"internal server error"})",
                "application/json; charset=utf-8"
            );
        }
    });

    svr.Options("/api/schedule/my", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.status = 204;
    });

// --- GET /api/schedule/{id} — конкретное расписание пользователя ---
svr.Get(R"(/api/schedule/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");

    // проверяем JWT
    auto payloadOpt = getUserFromRequest(req, jwtSecret);
    if (!payloadOpt.has_value()) {
        res.status = 401;
        res.set_content(R"({"error":"unauthorized"})", "application/json; charset=utf-8");
        return;
    }
    const auto& user = *payloadOpt;

    // вытащим id из пути /api/schedule/{id}
    if (req.matches.size() < 2) {
        res.status = 400;
        res.set_content(R"({"error":"bad_request","message":"missing schedule id"})",
                        "application/json; charset=utf-8");
        return;
    }

    long scheduleId = 0;
    try {
        scheduleId = std::stol(req.matches[1].str());
    } catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"bad_request","message":"invalid schedule id"})",
                        "application/json; charset=utf-8");
        return;
    }

    try {
        auto dbScheduleOpt = scheduleRepo.findScheduleById(user.userId, scheduleId);
        if (!dbScheduleOpt.has_value()) {
            res.status = 404;
            res.set_content(R"({"error":"not_found"})", "application/json; charset=utf-8");
            return;
        }

        const db::DbSchedule& s = *dbScheduleOpt;

        // парсим сохранённые JSON-строки
        json configJson = json::parse(s.configJson);
        json resultJson = json::parse(s.resultJson);

        json resp = {
            {"ok", true},
            {"schedule", {
                {"id", s.id},
                {"name", s.name},
                {"createdAt", s.createdAt},
                {"updatedAt", s.updatedAt},
                {"config", configJson},
                {"result", resultJson}
            }}
        };

        res.status = 200;
        res.set_content(resp.dump(), "application/json; charset=utf-8");
    } catch (const std::exception& ex) {
        logError(std::string("Error in GET /api/schedule/{id}: ") + ex.what());
        res.status = 500;
        res.set_content(R"({"error":"internal server error"})", "application/json; charset=utf-8");
    }
});

	svr.Post("/api/auth/register", [&](const httplib::Request& req, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");

    try {
        json j = json::parse(req.body);

        std::string username = j.value("username", "");
        std::string password = j.value("password", "");
        std::string email    = j.value("email", "");

        if (username.empty() || password.empty()) {
            res.status = 400;
            res.set_content(
                R"({"error":"missing username or password"})",
                "application/json; charset=utf-8"
            );
            return;
        }

        if (username.size() > 64) {
            res.status = 400;
            res.set_content(
                R"({"error":"username too long"})",
                "application/json; charset=utf-8"
            );
            return;
        }

        // проверка длины пароля
        if (password.size() < 6) {
            res.status = 400;
            res.set_content(
                R"({"error":"password too short"})",
                "application/json; charset=utf-8"
            );
            return;
        }

        // хешируем пароль
        std::string passwordHash = hashPassword(password);

        // роль по умолчанию — methodist
        std::optional<std::string> emailOpt;
        if (!email.empty()) {
            emailOpt = email;
        }

        long newId = userRepo.createUser(
            username,
            passwordHash,
            "methodist",
            emailOpt
        );

        // при регистрации сразу выдаём JWT (можно и без этого, если не нужно автологинить)
        std::string token = createJwt(newId, "methodist", jwtSecret, 24 * 3600);

        json resp = {
            {"ok", true},
            {"user", {
                {"id", newId},
                {"username", username},
                {"role", "methodist"}
            }},
            {"token", token}
        };

        res.status = 201;
        res.set_content(resp.dump(), "application/json; charset=utf-8");
    } catch (const pqxx::unique_violation &ex) {
        // username уже занят
        logInfo(std::string("Register failed (unique_violation): ") + ex.what());
        res.status = 409;
        res.set_content(
            R"({"error":"username already exists"})",
            "application/json; charset=utf-8"
        );
    } catch (const std::exception& ex) {
        logError(std::string("Error in /api/auth/register: ") + ex.what());
        res.status = 400;
        res.set_content(
            R"({"error":"invalid json"})",
            "application/json; charset=utf-8"
        );
    } catch (...) {
        logError("Unknown error in /api/auth/register");
        res.status = 500;
        res.set_content(
            R"({"error":"internal server error"})",
            "application/json; charset=utf-8"
        );
    }
});


    // preflight для регистрации
    svr.Options("/api/auth/register", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.status = 204;
    });

        // --- POST /api/auth/login ---
        svr.Post("/api/auth/login", [&](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");

            try {
                json j = json::parse(req.body);

                std::string username = j.value("username", "");
                std::string password = j.value("password", "");

                if (username.empty() || password.empty()) {
                    res.status = 400;
                    res.set_content(
                        R"({"error":"missing username or password"})",
                        "application/json; charset=utf-8"
                    );
                    return;
                }

                auto userOpt = userRepo.findUserByUsername(username);
                if (!userOpt.has_value()) {
                    logInfo("Login failed: user not found: " + username);
                    res.status = 401;
                    res.set_content(
                        R"({"error":"invalid credentials"})",
                        "application/json; charset=utf-8"
                    );
                    return;
                }

                db::DbUser user = *userOpt;

                if (!verifyPassword(password, user.passwordHash)) {
                    logInfo("Login failed: wrong password for user: " + username);
                    res.status = 401;
                    res.set_content(
                        R"({"error":"invalid credentials"})",
                        "application/json; charset=utf-8"
                    );
                    return;
                }

                logInfo("Login success for user: " + username + " (role=" + user.role + ")");

                // JWT на 24 часа
                std::string token = createJwt(user.id, user.role, jwtSecret, 24 * 3600);

                // httpOnly cookie
                std::string cookie =
                    "auth_token=" + token +
                    "; HttpOnly; Secure; Path=/; SameSite=Lax";
                res.set_header("Set-Cookie", cookie);

                json resp = {
                    {"ok", true},
                    {"user", {
                        {"id", user.id},
                        {"username", user.username},
                        {"role", user.role}
                    }},
                    {"token", token}
                };

                res.status = 200;
                res.set_content(resp.dump(), "application/json; charset=utf-8");
            } catch (const std::exception& ex) {
                logError(std::string("Error in /api/auth/login: ") + ex.what());
                res.status = 400;
                res.set_content(
                    R"({"error":"invalid json"})",
                    "application/json; charset=utf-8"
                );
            } catch (...) {
                logError("Unknown error in /api/auth/login");
                res.status = 500;
                res.set_content(
                    R"({"error":"internal server error"})",
                    "application/json; charset=utf-8"
                );
            }
        });

        svr.Options("/api/auth/login", [](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
            res.status = 204;
        });

	// --- POST /api/auth/logout ---
svr.Post("/api/auth/logout", [&](const httplib::Request& req, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");

    // Затираем куку auth_token с нулевым сроком жизни
    std::string cookie =
        "auth_token=deleted;"
        " HttpOnly; Secure; Path=/; SameSite=Lax; Max-Age=0";
    res.set_header("Set-Cookie", cookie);

    res.status = 200;
    res.set_content(R"({"ok":true})", "application/json; charset=utf-8");
});

svr.Options("/api/auth/logout", [](const httplib::Request& req, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    res.status = 204;
});

        // --- admin-only endpoint: GET /api/admin/ping ---
        svr.Get("/api/admin/ping", [&](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");

            auto payloadOpt = getUserFromRequest(req, jwtSecret);
            if (!payloadOpt.has_value()) {
                res.status = 401;
                res.set_content(
                    R"({"error":"unauthorized"})",
                    "application/json; charset=utf-8"
                );
                return;
            }

            const auto& p = *payloadOpt;
            if (p.role != "admin") {
                res.status = 403;
                res.set_content(
                    R"({"error":"forbidden"})",
                    "application/json; charset=utf-8"
                );
                return;
            }

            res.status = 200;
            res.set_content(
                R"({"ok":true,"message":"admin pong"})",
                "application/json; charset=utf-8"
            );
        });

        svr.Options("/api/admin/ping", [](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
            res.status = 204;
        });

	// --- POST /api/admin/schedule/publish ---
// --- POST /api/admin/schedule/publish --- публикация расписания ---
svr.Post("/api/admin/schedule/publish", [&](const httplib::Request& req, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");

    // 1. Проверяем JWT
    auto payloadOpt = getUserFromRequest(req, jwtSecret);
    if (!payloadOpt.has_value()) {
        res.status = 401;
        res.set_content(
            R"({"error":"unauthorized"})",
            "application/json; charset=utf-8"
        );
        return;
    }
    const auto& p = *payloadOpt;

    if (p.role != "admin") {
        res.status = 403;
        res.set_content(
            R"({"error":"forbidden"})",
            "application/json; charset=utf-8"
        );
        return;
    }

    // 2. Разбираем JSON: scheduleId
    long scheduleId = 0;
    try {
        json j = json::parse(req.body);
        if (!j.contains("scheduleId") || !j["scheduleId"].is_number_integer()) {
            res.status = 400;
            res.set_content(
                R"({"error":"bad_request","message":"scheduleId is required"})",
                "application/json; charset=utf-8"
            );
            return;
        }

        scheduleId = j["scheduleId"].get<long>();
    } catch (...) {
        res.status = 400;
        res.set_content(
            R"({"error":"invalid_json"})",
            "application/json; charset=utf-8"
        );
        return;
    }

    // 3. Проверяем, что расписание вообще существует и принадлежит этому админу
    try {
        auto schedOpt = scheduleRepo.findScheduleById(p.userId, scheduleId);
        if (!schedOpt.has_value()) {
            res.status = 404;
            res.set_content(
                R"({"error":"not_found"})",
                "application/json; charset=utf-8"
            );
            return;
        }

        bool okPub = scheduleRepo.publishSchedule(scheduleId);
        if (!okPub) {
            res.status = 500;
            res.set_content(
                R"({"error":"publish_failed"})",
                "application/json; charset=utf-8"
            );
            return;
        }

        json resp = {
            {"ok", true},
            {"scheduleId", scheduleId}
        };

        res.status = 200;
        res.set_content(resp.dump(), "application/json; charset=utf-8");
    } catch (const std::exception& ex) {
        logError(std::string("Error in /api/admin/schedule/publish: ") + ex.what());
        res.status = 500;
        res.set_content(
            R"({"error":"internal server error"})",
            "application/json; charset=utf-8"
        );
    }
});

svr.Options("/api/admin/schedule/publish", [](const httplib::Request& req, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    res.status = 204;
});


        // --- health-check БД ---
        svr.Get("/api/health/db", [&](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Content-Type", "application/json; charset=utf-8");

            try {
                auto conn = dbFactory.createConnection();
                pqxx::work tx{*conn};
                auto r = tx.exec("SELECT 1");
                tx.commit();

                if (r.empty()) {
                    res.status = 500;
                    res.set_content(R"({"db":"no rows from SELECT 1"})", "application/json; charset=utf-8");
                } else {
                    res.status = 200;
                    res.set_content(R"({"db":"ok"})", "application/json; charset=utf-8");
                }
            } catch (const std::exception& ex) {
                logError(std::string("DB health check error: ") + ex.what());
                res.status = 500;
                res.set_content(
                    std::string(R"({"db":"error","message":")") + ex.what() + "\"}",
                    "application/json; charset=utf-8"
                );
            }
        });

// --- POST /api/auth/logout ---
svr.Post("/api/auth/logout", [&](const httplib::Request& req, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");

    // Сброс cookie: ставим пустое значение и просроченный срок
    std::string cookie =
        "auth_token=;"
        " HttpOnly; Secure; Path=/; SameSite=Lax;"
        " Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT";
    res.set_header("Set-Cookie", cookie);

    res.status = 200;
    res.set_content(R"({"ok":true})", "application/json; charset=utf-8");
});

// preflight для logout
svr.Options("/api/auth/logout", [](const httplib::Request& req, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    res.status = 204;
});


        // --- GET /api/schedule — дефолтные данные из data.cpp ---
        svr.Get("/api/schedule", [](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type");

            int maxPerDay = maxExamsPerDayForGroup;
            if (req.has_param("maxPerDay")) {
                try {
                    maxPerDay = std::stoi(req.get_param_value("maxPerDay"));
                } catch (...) {
                    // оставляем дефолт
                }
            }

            logInfo("GET /api/schedule (data.cpp) maxPerDay=" + std::to_string(maxPerDay));

            std::string json = makeJsonResponse(
                groups,
                teachers,
                rooms,
                subjects,
                timeslots,
                exams,
                maxPerDay,
                sessionStart,
                sessionEnd
            );

            res.set_content(json, "application/json; charset=utf-8");
        });

        // --- POST /api/schedule — из config, ТОЛЬКО для залогиненных ---
	svr.Post("/api/schedule", [&](const httplib::Request& req, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");

    // --- ПРОВЕРКА JWT ---
    auto payloadOpt = getUserFromRequest(req, jwtSecret);
    if (!payloadOpt.has_value()) {
        res.status = 401;
        res.set_content(
            R"({"error":"unauthorized"})",
            "application/json; charset=utf-8"
        );
        return;
    }
    const auto& authUser = *payloadOpt;

    try {
        json j = json::parse(req.body);

        if (!j.contains("config") || !j["config"].is_object()) {
            res.status = 400;
            res.set_content(
                R"({"error":"invalid JSON or config"})",
                "application/json; charset=utf-8"
            );
            return;
        }

        json cfg = j["config"];

	// --- необязательное имя расписания ---
std::optional<std::string> scheduleName;
if (j.contains("scheduleName") && j["scheduleName"].is_string()) {
    std::string tmp = j["scheduleName"].get<std::string>();
    if (!tmp.empty()) {
        scheduleName = tmp;
    }
}
        // --- session ---
        std::string sessionStartLocal = sessionStart;
        std::string sessionEndLocal   = sessionEnd;
        int maxPerDay                 = maxExamsPerDayForGroup;

        if (cfg.contains("session") && cfg["session"].is_object()) {
            auto jsess = cfg["session"];
            if (jsess.contains("start") && jsess["start"].is_string())
                sessionStartLocal = jsess["start"].get<std::string>();
            if (jsess.contains("end") && jsess["end"].is_string())
                sessionEndLocal = jsess["end"].get<std::string>();
            if (jsess.contains("maxExamsPerDayForGroup") &&
                jsess["maxExamsPerDayForGroup"].is_number_integer())
                maxPerDay = jsess["maxExamsPerDayForGroup"].get<int>();
        }

        // --- groups ---
        std::vector<Group> groupsLocal;
        if (cfg.contains("groups") && cfg["groups"].is_array()) {
            for (auto& jg : cfg["groups"]) {
                int id           = jg.value("id", 0);
                std::string name = jg.value("name", std::string("Группа"));
                int size         = jg.value("size", 0);
                groupsLocal.push_back(Group{id, name, size});
            }
        }

        // --- teachers ---
        std::vector<Teacher> teachersLocal;
        if (cfg.contains("teachers") && cfg["teachers"].is_array()) {
            for (auto& jt : cfg["teachers"]) {
                int id           = jt.value("id", 0);
                std::string name = jt.value("name", std::string("Преподаватель"));
                teachersLocal.push_back(Teacher{id, name, ""});
            }
        }

        // --- rooms ---
        std::vector<Room> roomsLocal;
        if (cfg.contains("rooms") && cfg["rooms"].is_array()) {
            for (auto& jr : cfg["rooms"]) {
                int id           = jr.value("id", 0);
                std::string name = jr.value("name", std::string("Аудитория"));
                int cap          = jr.value("capacity", 0);
                roomsLocal.push_back(Room{id, name, cap});
            }
        }

        // --- subjects ---
        std::vector<Subject> subjectsLocal;
        if (cfg.contains("subjects") && cfg["subjects"].is_array()) {
            for (auto& jsu : cfg["subjects"]) {
                int id           = jsu.value("id", 0);
                std::string name = jsu.value("name", std::string("Предмет"));
                int diff         = jsu.value("difficulty", 3);
                subjectsLocal.push_back(Subject{id, name, diff});
            }
        }

        // --- exams ---
        std::vector<Exam> examsLocal;
        if (cfg.contains("exams") && cfg["exams"].is_array()) {
            for (auto& je : cfg["exams"]) {
                int id        = je.value("id", 0);
                int groupId   = je.value("groupId", 0);
                int teacherId = je.value("teacherId", 0);
                int subjectId = je.value("subjectId", 0);
                int duration  = je.value("durationMinutes", 120);
                examsLocal.push_back(Exam{id, groupId, teacherId, subjectId, duration});
            }
        }

        // --- timeslots ---
        std::vector<Timeslot> timeslotsLocal;
        if (cfg.contains("timeslots") && cfg["timeslots"].is_array()) {
            for (auto& jt : cfg["timeslots"]) {
                int id           = jt.value("id", 0);
                std::string date = jt.value("date", std::string("2025-01-20"));
                int start        = jt.value("startMinutes", 9 * 60);
                int end          = jt.value("endMinutes", 11 * 60);
                timeslotsLocal.push_back(Timeslot{id, date, start, end});
            }
        } else {
            // автогенерация 4 дней по 2 слота
            std::string base = sessionStartLocal;
            if (base.size() < 10) base = "2025-01-20";

            int day = 1;
            try {
                if (base.size() >= 10) {
                    day = std::stoi(base.substr(8, 2));
                }
            } catch (...) {
                day  = 20;
                base = "2025-01-20";
            }

            int nextId = 1;
            for (int i = 0; i < 4; ++i) {
                std::string d = base;
                if (d.size() >= 10) {
                    int dd = day + i;
                    char buf[3];
                    std::snprintf(buf, sizeof(buf), "%02d", dd);
                    d[8] = buf[0];
                    d[9] = buf[1];
                }
                timeslotsLocal.push_back(Timeslot{nextId++, d, 9 * 60, 11 * 60});
                timeslotsLocal.push_back(Timeslot{nextId++, d, 12 * 60, 14 * 60});
            }
        }

        logInfo(
            "POST /api/schedule "
            "userId=" + std::to_string(authUser.userId) +
            " groups="   + std::to_string(groupsLocal.size()) +
            " teachers=" + std::to_string(teachersLocal.size()) +
            " rooms="    + std::to_string(roomsLocal.size()) +
            " subjects=" + std::to_string(subjectsLocal.size()) +
            " exams="    + std::to_string(examsLocal.size()) +
            " timeslots="+ std::to_string(timeslotsLocal.size()) +
            " maxPerDay="+ std::to_string(maxPerDay)
        );

        if (groupsLocal.empty() || examsLocal.empty()) {
            res.status = 400;
            res.set_content(
                R"({"error":"config must contain non-empty groups and exams"})",
                "application/json; charset=utf-8"
            );
            return;
        }

        // --- вызываем генератор+валидатор ---
        std::string jsonResp = makeJsonResponse(
            groupsLocal,
            teachersLocal,
            roomsLocal,
            subjectsLocal,
            timeslotsLocal,
            examsLocal,
            maxPerDay,
            sessionStartLocal,
            sessionEndLocal
        );

        // --- пробуем сохранить расписание в БД ---
        long scheduleId = -1;
        try {
            std::string configDump = cfg.dump();
	    scheduleId = scheduleRepo.createSchedule(
    		static_cast<long>(authUser.userId),
    		configDump,
   	 	jsonResp,
    	        scheduleName   // <- передаём, если есть
	    );
            logInfo("Saved schedule id=" + std::to_string(scheduleId) +
                    " for userId=" + std::to_string(authUser.userId));
        } catch (const std::exception& ex) {
            logError(std::string("Failed to save schedule: ") + ex.what());
        } catch (...) {
            logError("Unknown error while saving schedule");
        }

        // --- если сохранили, добавим scheduleId в ответ ---
	if (scheduleId > 0) {
    try {
        json respJson = json::parse(jsonResp);
        respJson["scheduleId"] = scheduleId;
        if (scheduleName.has_value()) {
            respJson["scheduleName"] = *scheduleName;
        }
        res.set_content(respJson.dump(), "application/json; charset=utf-8");
    } catch (...) {
        res.set_content(jsonResp, "application/json; charset=utf-8");
    }
} else {
    res.set_content(jsonResp, "application/json; charset=utf-8");
}


    } catch (const std::exception& ex) {
        logError(std::string("Error in POST /api/schedule: ") + ex.what());
        res.status = 400;
        res.set_content(
            R"({"error":"invalid JSON or config"})",
            "application/json; charset=utf-8"
        );
    } catch (...) {
        logError("Unknown error in POST /api/schedule");
        res.status = 500;
        res.set_content(
            R"({"error":"internal server error"})",
            "application/json; charset=utf-8"
        );
    }
});


        svr.Options("/api/schedule", [](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
            res.status = 204;
        });

        bool ok = svr.listen("127.0.0.1", 8443);
        if (!ok) {
            logError("Не удалось запустить HTTPS сервер на порту 8443");
            return 1;
        }

    } catch (const std::exception& ex) {
        logError(std::string("Fatal error on startup: ") + ex.what());
        return 1;
    } catch (...) {
        logError("Unknown fatal error on startup");
        return 1;
    }

    return 0;
}
