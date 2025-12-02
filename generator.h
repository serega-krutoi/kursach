#include "validator.h"

#pragma once

std::vector<ExamAssignment> generateSchedule(
    const std::vector<Exam>& exams,
    const std::vector<Group>& groups,
    const std::vector<Timeslot>& timeslots,
    const std::vector<Room>& rooms
);