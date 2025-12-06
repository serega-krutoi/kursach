#include "api_dto.h"
#include "model.h"
#include "validator.h"
#include <cstdio>
#include <iostream>

// --- helpers для поиска ---

const Group* findGroupById_ui(const std::vector<Group>& groups, int id) {
    for (const Group& g : groups)
        if (g.id == id) return &g;
    return nullptr;
}

const Teacher* findTeacherById_ui(const std::vector<Teacher>& teachers, int id) {
    for (const Teacher& t : teachers)
        if (t.id == id) return &t;
    return nullptr;
}

const Subject* findSubjectById_ui(const std::vector<Subject>& subjects, int id) {
    for (const Subject& s : subjects)
        if (s.id == id) return &s;
    return nullptr;
}

const Room* findRoomById_ui(const std::vector<Room>& rooms, int id) {
    for (const Room& r : rooms)
        if (r.id == id) return &r;
    return nullptr;
}

const Timeslot* findTimeslotById_ui(const std::vector<Timeslot>& slots, int id) {
    for (const Timeslot& t : slots)
        if (t.id == id) return &t;
    return nullptr;
}

static std::string formatTime(int minutesFromMidnight) {
    int h = minutesFromMidnight / 60;
    int m = minutesFromMidnight % 60;
    char buf[6];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    return std::string(buf);
}

// --- сборка ExamView ---

std::vector<ExamView> buildExamViews(
    const std::vector<Exam>& exams,
    const std::vector<Group>& groups,
    const std::vector<Teacher>& teachers,
    const std::vector<Subject>& subjects,
    const std::vector<Room>& rooms,
    const std::vector<Timeslot>& timeslots,
    const std::vector<ExamAssignment>& assignments
) {
    std::vector<ExamView> result;
    result.reserve(assignments.size());

    for (const ExamAssignment& a : assignments) {
        const Exam& exam = exams[a.examIndex];

        const Group*   g  = findGroupById_ui(groups, exam.groupId);
        const Teacher* t  = findTeacherById_ui(teachers, exam.teacherId);
        const Subject* s  = findSubjectById_ui(subjects, exam.subjectId);
        const Room*    r  = (a.roomId >= 0 ? findRoomById_ui(rooms, a.roomId) : nullptr);
        const Timeslot* ts = findTimeslotById_ui(timeslots, a.timeslotId);

        ExamView ev;
        ev.examId      = exam.id;
        ev.groupName   = g ? g->name : "Неизвестная группа";
        ev.teacherName = t ? t->name : "Неизвестный преподаватель";
        ev.subjectName = s ? s->name : "Неизвестный предмет";
        ev.roomName    = r ? r->name : "Не назначена";

        if (ts) {
            ev.date      = ts->date;
            ev.startTime = formatTime(ts->startMinutes);
            ev.endTime   = formatTime(ts->endMinutes);
        } else {
            ev.date      = "unknown";
            ev.startTime = "--:--";
            ev.endTime   = "--:--";
        }

        result.push_back(ev);
    }

    return result;
}

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