#pragma once

#include <string>
#include <vector>

struct ExamView {
    int examId;
    std::string groupName;
    std::string teacherName;
    std::string subjectName;
    std::string roomName;
    std::string date;       // "2025-01-20"
    std::string startTime;  // "09:00"
    std::string endTime;    // "11:00"
};

struct ApiResponse {
    std::string algorithm;              // "graph" / "simple"
    std::vector<ExamView> schedule;     // то, что идёт в mockData.schedule
    bool ok;                            // validation.ok
    std::vector<std::string> errors;    // validation.errors
};

// объявление функций
class Group;
class Teacher;
class Subject;
class Room;
class Timeslot;
class Exam;
struct ExamAssignment;
struct ValidationResult;

std::vector<ExamView> buildExamViews(
    const std::vector<Exam>& exams,
    const std::vector<Group>& groups,
    const std::vector<Teacher>& teachers,
    const std::vector<Subject>& subjects,
    const std::vector<Room>& rooms,
    const std::vector<Timeslot>& timeslots,
    const std::vector<ExamAssignment>& assignments
);

void printApiResponseJson(const ApiResponse& resp);
