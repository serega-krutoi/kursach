#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"

#include <string>
#include <vector>
#include <pqxx/pqxx>
#include <cstdio>
#include <optional>

#include <cstdlib> 
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

// --- объявления функций из api_json.cpp ---
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
// (их будем использовать для GET, а для POST — брать из config)
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
    // 1. Генерация — всегда графовый алгоритм
    logInfo("Запускаем graph-генератор (maxPerDay=" + std::to_string(maxPerDay) + ")");

    std::vector<ExamAssignment> assignments = generateSchedule(
        examsLocal,
        groupsLocal,
        subjectsLocal,
        timeslotsLocal,
        roomsLocal,
        maxPerDay
    );

    // 2. Валидация
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

    // 3. DTO для фронта
    ApiResponse resp;
    resp.algorithm = "graph"; // зашиваем, что алгоритм графовый
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

    // 4. JSON-строка
    return buildApiResponseJsonString(resp);
}

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

    // 1) Пытаемся взять из Authorization
    auto itAuth = req.headers.find("Authorization");
    if (itAuth != req.headers.end()) {
        token = getTokenFromAuthorization(itAuth->second);
    }

    // 2) Если не нашли, пытаемся взять из Cookie
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

	const char* jwtSecretEnv = std::getenv("KURSACH_JWT_SECRET");
        if (!jwtSecretEnv) {
            throw std::runtime_error("Environment variable KURSACH_JWT_SECRET is not set");
        }
        std::string jwtSecret(jwtSecretEnv);

        // 1. Читаем конфиг БД из переменных окружения
        db::DbConfig dbCfg = db::DbConfig::fromEnv();
        db::ConnectionFactory dbFactory{dbCfg};
        db::UserRepository userRepo{dbFactory};

        logInfo("Успешно инициализирована конфигурация БД");

        httplib::SSLServer svr("server-cert.pem", "server-key.pem");

        if (!svr.is_valid()) {
            logError("SSLServer невалиден. Проверь server-cert.pem и server-key.pem");
            return 1;
        }

        // --- простой корень ---
        svr.Get("/", [](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(
                "HTTPS exam schedule server is running.\n"
                "GET  /api/schedule?maxPerDay=N  (дефолтные данные из data.cpp)\n"
                "POST /api/schedule              (данные из config, JSON от фронта)\n"
                "GET  /api/health/db              (проверка подключения к БД)",
                "text/plain; charset=utf-8"
            );
        });

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


	    svr.Post("/api/auth/login", [&](const httplib::Request& req, httplib::Response& res) {
 		 res.set_header("Access-Control-Allow-Origin", "*"); // или твой домен
	        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");

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

            // создаём JWT на 24 часа
            std::string token = createJwt(user.id, user.role, jwtSecret, 24 * 3600);

            // кладём токен в httpOnly cookie
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
                {"token", token} // бонусом, если захочешь использовать в JS
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
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
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

        // --- твои уже существующие обработчики /api/schedule ---
        svr.Get("/api/schedule", [](const httplib::Request& req, httplib::Response& res) {
            // ... ТВОЙ КОД БЕЗ ИЗМЕНЕНИЙ ...
        });

        svr.Post("/api/schedule", [](const httplib::Request& req, httplib::Response& res) {
            // ... ТВОЙ КОД БЕЗ ИЗМЕНЕНИЙ ...
        });

        svr.Options("/api/schedule", [](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type");
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
