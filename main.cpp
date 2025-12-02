#include <iostream>
#include <vector>
#include "model.h"
#include "validator.h"
#include "generator.h"
// плюс где-то подключишь прототипы generateSchedule / buildConflictGraph / greedyColoring

int main() {
    // -------- 1. Тестовые данные --------
    std::vector<Group> groups = {
        {1, "ИС-31", 25},
        {2, "ИС-32", 30}
    };

    std::vector<Teacher> teachers = {
        {1, "Иванов И.И.", "Алгоритмы"},
        {2, "Петров П.П.", "Сети"}
    };

    std::vector<Room> rooms = {
        {1, "Аудитория 101", 20},
        {2, "Аудитория 202", 40}
    };

    std::vector<Timeslot> timeslots = {
        {1, "2025-01-20", 9*60, 11*60},
        {2, "2025-01-21", 9*60, 11*60},
        {3, "2025-01-22", 9*60, 11*60}
    };

    std::vector<Exam> exams = {
        {0, 1, 1, 1, 90}, // group 1, teacher 1
        {1, 1, 2, 2, 90}, // group 1, teacher 2
        {2, 2, 1, 1, 90}  // group 2, teacher 1
    };

    // -------- 2. Генерация расписания --------
    std::vector<ExamAssignment> assignments = generateSchedule(
        exams,
        groups,
        timeslots,
        rooms
    );

    std::cout << "Сгенерированное расписание:\n";
    for (const ExamAssignment& a : assignments) {
        const Exam& e = exams[a.examIndex];
        std::cout << "Exam id=" << e.id
                  << " groupId=" << e.groupId
                  << " teacherId=" << e.teacherId
                  << " timeslotId=" << a.timeslotId
                  << " roomId=" << a.roomId << "\n";
    }

    // -------- 3. Валидация --------
    ScheduleValidator validator;

    std::string sessionStart = "2025-01-15";
    std::string sessionEnd   = "2025-02-01";
    int maxExamsPerDayForGroup = 1;

    ValidationResult res = validator.checkAll(
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

    std::cout << "\nРезультат проверки:\n";
    if (!res.ok) {
        for (const std::string& err : res.errors) {
            std::cout << " - " << err << "\n";
        }
    } else {
        std::cout << "Расписание корректно.\n";
    }

    return 0;
}
