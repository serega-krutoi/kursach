#include <iostream>
#include <vector>

#include "api_dto.h"
#include "api_json.h"   // <-- СМЕНИЛ на .h, а не .cpp
#include "model.h"
#include "validator.h"
#include "generator.h"
#include "logger.h"

// ГРУППЫ
std::vector<Group> groups = {
    {1, "ИС-31", 22},
    {2, "ИС-32", 28},
    {3, "ПО-41", 30},
    {4, "ПО-42", 18},
    {5, "КС-21", 25}
};

// ПРЕПОДАВАТЕЛИ
std::vector<Teacher> teachers = {
    {1, "Иванов И.И.", "Алгоритмы и структуры данных"},
    {2, "Петров П.П.", "Компьютерные сети"},
    {3, "Сидоров С.С.", "Базы данных"},
    {4, "Кузнецов К.К.", "Операционные системы"},
    {5, "Фёдоров Ф.Ф.", "Информационная безопасность"}
};

// АУДИТОРИИ
std::vector<Room> rooms = {
    {1, "Аудитория 101", 20},
    {2, "Аудитория 202", 30},
    {3, "Аудитория 303", 40}
};

// ПРЕДМЕТЫ
std::vector<Subject> subjects = {
    {1, "Алгоритмы и структуры данных", 4},
    {2, "Компьютерные сети", 3},
    {3, "Базы данных", 3},
    {4, "Операционные системы", 4},
    {5, "Информационная безопасность", 5},
    {6, "Проектирование ПО", 2},
    {7, "Математический анализ", 5},
    {8, "Дискретная математика", 4}
};

// ТАЙМСЛОТЫ
std::vector<Timeslot> timeslots = {
    {1, "2025-01-20",  9*60, 11*60},
    {2, "2025-01-20", 12*60, 14*60},
    {3, "2025-01-21",  9*60, 11*60},
    {4, "2025-01-21", 12*60, 14*60},
    {5, "2025-01-22",  9*60, 11*60},
    {6, "2025-01-22", 12*60, 14*60},
    {7, "2025-01-23",  9*60, 11*60},
    {8, "2025-01-23", 12*60, 14*60}
};

// ЭКЗАМЕНЫ
std::vector<Exam> exams = {
    // ИС-31
    {0, 1, 1, 1, 120},
    {1, 1, 2, 2, 90},
    {2, 1, 3, 3, 90},

    // ИС-32
    {3, 2, 1, 1, 120},
    {4, 2, 4, 4, 90},

    // ПО-41
    {5, 3, 3, 6, 90},
    {6, 3, 5, 5, 120},
    {12, 3, 1, 1, 120},

    // ПО-42
    {7, 4, 2, 2, 90},
    {8, 4, 3, 3, 90},

    // КС-21
    {9, 5, 1, 7, 120},
    {10, 5, 4, 8, 90},

    // доп. нагрузка
    {11, 3, 1, 1, 120}
};

std::string sessionStart = "2025-01-20";
std::string sessionEnd   = "2025-01-23";
int maxExamsPerDayForGroup = 2;

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
