// api_json.cpp (например)
#include "api_dto.h"
#include "json.hpp"

using nlohmann::json;

// как сериализовать один ExamView
void to_json(json& j, const ExamView& e) {
    j = json{
        {"examId",      e.examId},
        {"groupName",   e.groupName},
        {"teacherName", e.teacherName},
        {"subjectName", e.subjectName},
        {"roomName",    e.roomName},
        {"date",        e.date},
        {"startTime",   e.startTime},
        {"endTime",     e.endTime}
    };
}

// как сериализовать общий ответ
void to_json(json& j, const ApiResponse& r) {
    j = json{
        {"algorithm", r.algorithm},
        {"schedule",  r.schedule},          // вектор ExamView — он знает, как его сериализовать
        {"validation", {
            {"ok",     r.ok},
            {"errors", r.errors}
        }}
    };
}
