// generator.h
#pragma once

#include <vector>
#include "model.h"
#include "api_dto.h"

// Основной генератор на графе конфликтов + эвристика по сложности
std::vector<ExamAssignment> generateSchedule(
    const std::vector<Exam>& exams,
    const std::vector<Group>& groups,
    const std::vector<Subject>& subjects,
    const std::vector<Timeslot>& timeslots,
    const std::vector<Room>& rooms,
    int maxExamsPerDayForGroup // 0 или меньше = без ограничения
);

// Простой жадный генератор (по слотам подряд)
std::vector<ExamAssignment> generateScheduleSimple(
    const std::vector<Exam>& exams,
    const std::vector<Group>& groups,
    const std::vector<Subject>& subjects,
    const std::vector<Timeslot>& timeslots,
    const std::vector<Room>& rooms,
    int maxExamsPerDayForGroup // 0 или меньше = без ограничения
);
