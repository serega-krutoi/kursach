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
    std::string algorithm;              // "graph" или "simple"
    std::vector<ExamView> schedule;
    bool ok;
    std::vector<std::string> errors;
};
