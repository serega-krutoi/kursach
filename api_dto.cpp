#include "api_dto.h"
#include "model.h"
#include "validator.h" // если там есть makeDate / twoDigits
#include <vector>
#include <string>

// простая утилита для поиска по id
const Group*  findGroupById_ui (const std::vector<Group>& groups, int id) {
    for (const Group& g : groups) if (g.id == id) return &g;
    return nullptr;
}

const Teacher* findTeacherById_ui (const std::vector<Teacher>& teachers, int id) {
    for (const Teacher& t : teachers) if (t.id == id) return &t;
    return nullptr;
}

const Subject* findSubjectById_ui (const std::vector<Subject>& subjects, int id) {
    for (const Subject& s : subjects) if (s.id == id) return &s;
    return nullptr;
}

const Room* findRoomById_ui (const std::vector<Room>& rooms, int id) {
    for (const Room& r : rooms) if (r.id == id) return &r;
    return nullptr;
}

const Timeslot* findTimeslotById_ui (const std::vector<Timeslot>& slots, int id) {
    for (const Timeslot& t : slots) if (t.id == id) return &t;
    return nullptr;
}

// форматируем время "HH:MM"
std::string formatTime(int minutesFromMidnight) {
    int h = minutesFromMidnight / 60;
    int m = minutesFromMidnight % 60;
    std::string hh = (h < 10 ? "0" : "") + std::to_string(h);
    std::string mm = (m < 10 ? "0" : "") + std::to_string(m);
    return hh + ":" + mm;
}

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

    for (const ExamAssignment& a : assignments) {
        const Exam& exam = exams[a.examIndex];

        const Group*     g = findGroupById_ui(groups, exam.groupId);
        const Teacher*   t = findTeacherById_ui(teachers, exam.teacherId);
        const Subject*   s = findSubjectById_ui(subjects, exam.subjectId);
        const Room*      r = (a.roomId >= 0 ? findRoomById_ui(rooms, a.roomId) : nullptr);
        const Timeslot*  ts = findTimeslotById_ui(timeslots, a.timeslotId);

        ExamView ev;
        ev.examId      = exam.id;
        ev.groupName   = g ? g->name : "Неизвестная группа";
        ev.teacherName = t ? t->name : "Неизвестный преподаватель";
        ev.subjectName = s ? s->name : "Неизвестный предмет";
        ev.roomName    = r ? r->name : "Не назначена";
        if (ts) {
            ev.date       = ts->date;
            ev.startTime  = formatTime(ts->startMinutes);
            ev.endTime    = formatTime(ts->endMinutes);
        } else {
            ev.date       = "Неизвестно";
            ev.startTime  = "--:--";
            ev.endTime    = "--:--";
        }

        result.push_back(ev);
    }

    return result;
}
