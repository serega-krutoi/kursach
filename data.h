// data.h
#pragma once

#include <vector>
#include <string>
#include "model.h"

extern std::vector<Group> groups;
extern std::vector<Teacher> teachers;
extern std::vector<Room> rooms;
extern std::vector<Subject> subjects;
extern std::vector<Timeslot> timeslots;
extern std::vector<Exam> exams;

extern std::string sessionStart;
extern std::string sessionEnd;
extern int maxExamsPerDayForGroup;
