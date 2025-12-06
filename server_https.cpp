#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "model.h"
#include "data.h"
#include "generator.h"
#include "validator.h"
#include "api_dto.h"
#include "api_json.h"
#include "logger.h"
#include <vector>  
#include <string>

int main() {
    // HTTPS сервер: нужен cert + key
    httplib::SSLServer svr("server-cert.pem", "server-key.pem");

    if (!svr.is_valid()) {
        std::cerr << "SSL server has an issue (check cert/key)\n";
        return 1;
    }

    svr.Get("/api/schedule", [](const httplib::Request& req, httplib::Response& res) {
        std::string algorithm = "graph";
        if (req.has_param("algo")) {
            auto v = req.get_param_value("algo");
            if (v == "simple" || v == "graph") {
                algorithm = v;
            }
        }

        std::vector<ExamAssignment> assignments;
        if (algorithm == "graph") {
            assignments = generateSchedule(exams, groups, subjects, timeslots, rooms);
        } else {
            assignments = generateScheduleSimple(exams, groups, subjects, timeslots, rooms);
        }

        ScheduleValidator validator;
        ValidationResult vr = validator.checkAll(
            exams,
            groups,
            teachers,
            rooms,
            timeslots,
            assignments,
            sessionStart,
            sessionEnd,
            maxExamsPerDayForGroup
        );

        ApiResponse resp;
        resp.algorithm = algorithm;
        resp.schedule  = buildExamViews(
            exams, groups, teachers, subjects, rooms, timeslots, assignments
        );
        resp.ok        = vr.ok;
        resp.errors    = vr.errors;

        std::string body = buildApiResponseJsonString(resp);

        res.set_content(body, "application/json; charset=utf-8");
    });

    // На время разработки CORS (если React будет на другом порту)
    svr.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, OPTIONS"},
    });

    std::cout << "HTTPS server started on https://localhost:8443\n";
    svr.listen("0.0.0.0", 8443);

    return 0;
}
