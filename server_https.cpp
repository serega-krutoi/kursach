// server_https.cpp
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"

#include <string>
#include <vector>
#include <cstdio>

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

int main() {
    logInfo("=== Запуск HTTPS сервера на 0.0.0.0:8443 ===");

    httplib::SSLServer svr("server-cert.pem", "server-key.pem");

    if (!svr.is_valid()) {
        logError("SSLServer невалиден. Проверь server-cert.pem и server-key.pem");
        return 1;
    }

    // Простой корень
    svr.Get("/", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(
            "HTTPS exam schedule server is running.\n"
            "GET  /api/schedule?maxPerDay=N  (дефолтные данные из data.cpp)\n"
            "POST /api/schedule              (данные из config, JSON от фронта)",
            "text/plain; charset=utf-8"
        );
    });    

    // --- GET /api/schedule — режим с дефолтным data.cpp ---
    // --- GET /api/schedule — режим с дефолтным data.cpp ---
    svr.Get("/api/schedule", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
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

    // --- POST /api/schedule — основной режим с config из фронта ---
    svr.Post("/api/schedule", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");

        try {
            json j = json::parse(req.body);

            if (!j.contains("config") || !j["config"].is_object()) {
                res.status = 400;
                res.set_content(
                    R"({"error":"missing config object"})",
                    "application/json; charset=utf-8"
                );
                return;
            }

            json cfg = j["config"];

            // --- session ---
            std::string sessionStartLocal = sessionStart;
            std::string sessionEndLocal   = sessionEnd;
            int maxPerDay                 = maxExamsPerDayForGroup;

            if (cfg.contains("session") && cfg["session"].is_object()) {
                auto js = cfg["session"];
                if (js.contains("start") && js["start"].is_string())
                    sessionStartLocal = js["start"].get<std::string>();
                if (js.contains("end") && js["end"].is_string())
                    sessionEndLocal = js["end"].get<std::string>();
                if (js.contains("maxExamsPerDayForGroup") &&
                    js["maxExamsPerDayForGroup"].is_number_integer())
                    maxPerDay = js["maxExamsPerDayForGroup"].get<int>();
            }

            // --- groups ---
            std::vector<Group> groupsLocal;
            if (cfg.contains("groups") && cfg["groups"].is_array()) {
                for (auto& jg : cfg["groups"]) {
                    int id         = jg.value("id", 0);
                    std::string name = jg.value("name", std::string("Группа"));
                    int size      = jg.value("size", 0);
                    groupsLocal.push_back(Group{id, name, size});
                }
            }

            // --- teachers ---
            std::vector<Teacher> teachersLocal;
            if (cfg.contains("teachers") && cfg["teachers"].is_array()) {
                for (auto& jt : cfg["teachers"]) {
                    int id         = jt.value("id", 0);
                    std::string name = jt.value("name", std::string("Преподаватель"));
                    teachersLocal.push_back(Teacher{id, name, ""});
                }
            }

            // --- rooms ---
            std::vector<Room> roomsLocal;
            if (cfg.contains("rooms") && cfg["rooms"].is_array()) {
                for (auto& jr : cfg["rooms"]) {
                    int id         = jr.value("id", 0);
                    std::string name = jr.value("name", std::string("Аудитория"));
                    int cap       = jr.value("capacity", 0);
                    roomsLocal.push_back(Room{id, name, cap});
                }
            }

            // --- subjects ---
            std::vector<Subject> subjectsLocal;
            if (cfg.contains("subjects") && cfg["subjects"].is_array()) {
                for (auto& jsu : cfg["subjects"]) {
                    int id         = jsu.value("id", 0);
                    std::string name = jsu.value("name", std::string("Предмет"));
                    int diff      = jsu.value("difficulty", 3);
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

            // --- timeslots: если в config нет timeslots, создаём дефолтные ---
            std::vector<Timeslot> timeslotsLocal;

            if (cfg.contains("timeslots") && cfg["timeslots"].is_array()) {
                // (на будущее: если решишь хранить таймслоты в config)
                for (auto& jt : cfg["timeslots"]) {
                    int id         = jt.value("id", 0);
                    std::string date = jt.value("date", std::string("2025-01-20"));
                    int start     = jt.value("startMinutes", 9 * 60);
                    int end       = jt.value("endMinutes", 11 * 60);
                    timeslotsLocal.push_back(Timeslot{id, date, start, end});
                }
            } else {
                // Автоматически генерим 4 дня сессии по 2 слота (09–11, 12–14)
                // на основе sessionStartLocal (формат "YYYY-MM-DD")
                std::string base = sessionStartLocal;
                if (base.size() < 10) {
                    base = "2025-01-20";
                }

                int day = 1;
                try {
                    if (base.size() >= 10) {
                        day = std::stoi(base.substr(8, 2));
                    }
                } catch (...) {
                    day = 20;
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
                    // слот 1: 09:00–11:00
                    timeslotsLocal.push_back(Timeslot{
                        nextId++,
                        d,
                        9 * 60,
                        11 * 60
                    });
                    // слот 2: 12:00–14:00
                    timeslotsLocal.push_back(Timeslot{
                        nextId++,
                        d,
                        12 * 60,
                        14 * 60
                    });
                }
            }

            logInfo(
                "POST /api/schedule "
                "groups="   + std::to_string(groupsLocal.size()) +
                " teachers=" + std::to_string(teachersLocal.size()) +
                " rooms="    + std::to_string(roomsLocal.size()) +
                " subjects=" + std::to_string(subjectsLocal.size()) +
                " exams="    + std::to_string(examsLocal.size()) +
                " timeslots=" + std::to_string(timeslotsLocal.size()) +
                " maxPerDay=" + std::to_string(maxPerDay)
            );

            if (groupsLocal.empty() || examsLocal.empty()) {
                res.status = 400;
                res.set_content(
                    R"({"error":"config must contain non-empty groups and exams"})",
                    "application/json; charset=utf-8"
                );
                return;
            }

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

            res.set_content(jsonResp, "application/json; charset=utf-8");
        } catch (const std::exception& ex) {
            logError(std::string("Ошибка парсинга/обработки POST /api/schedule: ") + ex.what());
            res.status = 400;
            res.set_content(
                R"({"error":"invalid JSON or config"})",
                "application/json; charset=utf-8"
            );
        } catch (...) {
            logError("Неизвестная ошибка в POST /api/schedule");
            res.status = 500;
            res.set_content(
                R"({"error":"internal server error"})",
                "application/json; charset=utf-8"
            );
        }
    });

    // CORS preflight
    svr.Options("/api/schedule", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    bool ok = svr.listen("0.0.0.0", 8443);
    if (!ok) {
        logError("Не удалось запустить HTTPS сервер на порту 8443");
        return 1;
    }

    return 0;
}
