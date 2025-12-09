#pragma once

#include <vector>
#include "model.h"

std::vector<ExamAssignment> generateSchedule(
    const std::vector<Exam>& exams,
    const std::vector<Group>& groups,
    const std::vector<Subject>& subjects,
    const std::vector<Timeslot>& timeslots,
    const std::vector<Room>& rooms,
    int maxExamsPerDayForGroup
);
