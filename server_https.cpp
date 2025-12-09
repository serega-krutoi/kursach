// server_https.cpp
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"

#include <string>
#include <vector>
#include <pqxx/pqxx>
#include <cstdio>

#include "auth.h"

#include "model.h"
#include "generator.h"
#include "graph.h"
#include "validator.h"
#include "api_dto.h"
#include "logger.h"
#include "db.h"

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

int main() {
    logInfo("=== Запуск HTTPS сервера на 127.0.0.1:8443 ===");

    try {
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

	    // --- POST /api/auth/login ---
    svr.Post("/api/auth/login", [&](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
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

            json resp = {
                {"ok", true},
                {"user", {
                    {"id", user.id},
                    {"username", user.username},
                    {"role", user.role}
                }}
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

    // CORS preflight для /api/auth/login
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
