#include <iostream>
#include <vector>

#include "data.h"
#include "api_dto.h"
#include "api_json.h"   
#include "model.h"
#include "validator.h"
#include "generator.h"
#include "logger.h"

int main(int argc, char** argv) {
    // выбор алгоритма: по умолчанию graph, но можно передать "simple"
    std::string algorithm = "graph";
    if (argc >= 2) {
        std::string arg = argv[1];
        if (arg == "simple" || arg == "graph") {
            algorithm = arg;
        }
    }

    std::vector<ExamAssignment> assignments;
    if (algorithm == "graph") {
        assignments = generateSchedule(
            exams, groups, subjects, timeslots, rooms
        );
    } else {
        assignments = generateScheduleSimple(
            exams, groups, subjects, timeslots, rooms
        );
    }

    ScheduleValidator validator;

    // ВАЖНО: checkAll, а не validate, и с датами сессии
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
    resp.ok     = vr.ok;
    resp.errors = vr.errors;

    printApiResponseJson(resp);

    return 0;
}
