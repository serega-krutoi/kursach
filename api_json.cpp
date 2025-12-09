#include "api_dto.h"
#include "model.h"
#include "validator.h"
#include <cstdio>
#include <iostream>


static std::string formatTime(int minutesFromMidnight) {
    int h = minutesFromMidnight / 60;
    int m = minutesFromMidnight % 60;
    char buf[6];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    return std::string(buf);
}

// --- сборка ExamView ---


// --- печать JSON ---

static std::string escapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\"') out += "\\\"";
        else out += c;
    }
    return out;
}

#include <sstream>

std::string buildApiResponseJsonString(const ApiResponse& resp) {
    std::ostringstream out;

    out << "{\n";
    out << "  \"algorithm\": \"" << escapeJson(resp.algorithm) << "\",\n";
    out << "  \"validation\": {\n";
    out << "    \"ok\": " << (resp.ok ? "true" : "false") << ",\n";
    out << "    \"errors\": [\n";
    for (size_t i = 0; i < resp.errors.size(); ++i) {
        out << "      \"" << escapeJson(resp.errors[i]) << "\"";
        if (i + 1 != resp.errors.size()) out << ",";
        out << "\n";
    }
    out << "    ]\n";
    out << "  },\n";
    out << "  \"schedule\": [\n";
    for (size_t i = 0; i < resp.schedule.size(); ++i) {
        const ExamView& e = resp.schedule[i];
        out << "    {\n";
        out << "      \"examId\": "      << e.examId                    << ",\n";
        out << "      \"groupName\": \""   << escapeJson(e.groupName)   << "\",\n";
        out << "      \"teacherName\": \"" << escapeJson(e.teacherName) << "\",\n";
        out << "      \"subjectName\": \"" << escapeJson(e.subjectName) << "\",\n";
        out << "      \"roomName\": \""    << escapeJson(e.roomName)    << "\",\n";
        out << "      \"date\": \""        << escapeJson(e.date)        << "\",\n";
        out << "      \"startTime\": \""   << escapeJson(e.startTime)   << "\",\n";
        out << "      \"endTime\": \""     << escapeJson(e.endTime)     << "\"\n";
        out << "    }";
        if (i + 1 != resp.schedule.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";

    return out.str();
}

void printApiResponseJson(const ApiResponse& resp) {
    std::cout << buildApiResponseJsonString(resp);
}
