#include "validator.h"
#include "logger.h"
#include <map>

std::string findGroupNameById(const std::vector<Group>& groups, int groupId) {
    for (const Group& g : groups) if (g.id == groupId) return g.name;
    return "Ошибка";
}

std::string findTeacherNameById(const std::vector<Teacher>& teacher, int teacherId) {
    for (const Teacher& t : teacher) if (t.id == teacherId) return t.name;
    return "Ошибка";
}

std::string findRoomNameById(const std::vector<Room>& rooms, int roomId) {
    for (const Room& r : rooms) if (r.id == roomId) return r.name;
    return "Ошибка";
}

const Room* findRoomById(const std::vector<Room>& rooms, int roomId) {
    for (const Room& r : rooms) if (r.id == roomId) return &r;
    return nullptr;
}

const Group* findGroupById(const std::vector<Group>& groups, int groupId) {
    for (const Group& g : groups) if (g.id == groupId) return &g;
    return nullptr;
}

const Timeslot* findTimeslotById(const std::vector<Timeslot>& timeslots, int timeslotId) {
    for (const Timeslot& t : timeslots) if (t.id == timeslotId) return &t;
    return nullptr;
}

const Group* findGroupByIdForScheduler(const std::vector<Group>& groups, int groupId) {
    for (const Group& g : groups) if (g.id == groupId) return &g;
    return nullptr;
}

std::string twoDigits(int x) {
    if (x < 10) return "0" + std::to_string(x);
    return std::to_string(x);
}

std::string makeDate(const Timeslot& timeslot) {
    std::string date = timeslot.date;

    int startHour = timeslot.startMinutes / 60;
    int startMinutes = timeslot.startMinutes % 60;

    int endHour = timeslot.endMinutes / 60;
    int endMinutes = timeslot.endMinutes % 60;

    date += " " + twoDigits(startHour) + ":" + twoDigits(startMinutes)
            + "-" + twoDigits(endHour) + ":" + twoDigits(endMinutes);

    return date;
}

std::string findTimeslotDescription(const std::vector<Timeslot>& timeslots, int timeslotId) {
    // вид "2025-01-20 09:00–11:00"
    for (const Timeslot& t : timeslots) if (t.id == timeslotId) return makeDate(t);
    return "Ошибка";
}

void ScheduleValidator::checkGroupConflicts(
    const std::vector<Exam>& exams,
    const std::vector<Group>& groups,
    const std::vector<Timeslot>& timeslots,
    const std::vector<ExamAssignment>& assignments,
    ValidationResult& result
) {
    std::map<std::pair<int, int>, std::vector<int>> table;

    for (const ExamAssignment& a : assignments) {
        int examIndex = a.examIndex;
        int timeslotId = a.timeslotId;

        const Exam& exam = exams[examIndex];
        int groupId = exam.groupId;

        std::pair<int, int> key(groupId, timeslotId);
        table[key].push_back(examIndex);
    }

    for (const std::pair<std::pair<int, int>, std::vector<int>>& p : table) {
        if (p.second.size() > 1) {
            result.ok = false;

            int groupId = p.first.first;
            int timeslotId = p.first.second;

            std::string groupName = findGroupNameById(groups, groupId);
            std::string timeslotInfo = findTimeslotDescription(timeslots, timeslotId);
                
            std::string errorMessage = "Конфликт для группы " + groupName +
                                       " в " + timeslotInfo +
                                       ": назначено " + std::to_string(p.second.size()) +
                                       " экзамен(ов) одновременно.";

            result.errors.push_back(errorMessage);
            logError("[GroupConflict] " + errorMessage);
        }
    }
}


void ScheduleValidator::checkTeacherConflicts(
    const std::vector<Exam>& exams,
    const std::vector<Teacher>& teachers,
    const std::vector<Timeslot>& timeslots,
    const std::vector<ExamAssignment>& assignments,
    ValidationResult& result
) {
    std::map<std::pair<int, int>, std::vector<int>> table;

    for (const ExamAssignment& a : assignments) {
        int examIndex = a.examIndex;
        int timeslotId = a.timeslotId;

        const Exam& exam = exams[examIndex];
        int teacherId = exam.teacherId;

        std::pair<int, int> key(teacherId, timeslotId);
        table[key].push_back(examIndex);
    }

    for (const std::pair<std::pair<int, int>, std::vector<int>>& p : table) {
        if (p.second.size() > 1) {
            result.ok = false;

            int teacherId = p.first.first;
            int timeslotId = p.first.second;

            std::string teacherName = findTeacherNameById(teachers, teacherId);
            std::string timeslotInfo = findTimeslotDescription(timeslots, timeslotId);
                
            std::string errorMessage = "Конфликт для преподавателя " + teacherName +
                                       " в " + timeslotInfo +
                                       ": назначено " + std::to_string(p.second.size()) +
                                       " экзамен(ов) одновременно.";

            result.errors.push_back(errorMessage);
            logError("[TeacherConflict] " + errorMessage);
        }
    }
}


void ScheduleValidator::checkRoomConflicts(
    const std::vector<Exam>& exams,
    const std::vector<Group>& groups,
    const std::vector<Room>& rooms,
    const std::vector<Timeslot>& timeslots,
    const std::vector<ExamAssignment>& assignments,
    ValidationResult& result
) {
    // Конфликт по две пары в одной аудитории одновременно
    // (roomId, timeslotId) -> список examIndex
    // (roomId, timeslotId) -> список examIndex
    std::map<std::pair<int, int>, std::vector<int>> table;

    for (const ExamAssignment& a : assignments) {
        int examIndex = a.examIndex;
        int timeslotId = a.timeslotId;
        int roomId = a.roomId;

        if (roomId < 0) continue; // не учитываем "нет аудитории" в конфликте занятности

        std::pair<int, int> key(roomId, timeslotId);
        table[key].push_back(examIndex);
    }

    for (const auto& p : table) {
        const std::pair<int,int>& key = p.first;
        const std::vector<int>& examIndexes = p.second;

        if (examIndexes.size() > 1) {
            result.ok = false;

            int roomId = key.first;
            int timeslotId = key.second;

            std::string roomName = findRoomNameById(rooms, roomId);
            std::string timeslotInfo = findTimeslotDescription(timeslots, timeslotId);

            std::string errorMessage =
                "Конфликт по аудитории " + roomName +
                " в " + timeslotInfo +
                ": назначено " + std::to_string(examIndexes.size()) +
                " экзамен(ов) одновременно.";

            result.errors.push_back(errorMessage);
            logError("[RoomConflict] " + errorMessage);
        }
    }

    // Проверка вместимости
    for (const ExamAssignment& a : assignments) {
        int examIndex = a.examIndex;
        int roomId = a.roomId;

        if (roomId < 0) {
            result.ok = false;
            std::string msg = "Экзамен с examIndex=" + std::to_string(examIndex) +
                            " не имеет назначенной аудитории (roomId < 0).";
            result.errors.push_back(msg);
            logError("[RoomMissing] " + msg);
            continue;
        }

        const Exam& exam = exams[examIndex];
        int groupId = exam.groupId;

        const Room* room = findRoomById(rooms, roomId);
        const Group* group = findGroupById(groups, groupId);

        if (!room || !group) {
            result.ok = false;
            std::string msg = "Ошибка данных: не найдена аудитория или группа по id (roomId=" +
                            std::to_string(roomId) + ", groupId=" + std::to_string(groupId) + ").";
            result.errors.push_back(msg);
            logError("[RoomDataError] " + msg);
            continue;
        }

        if (group->peopleCount > room->capacity) {
            result.ok = false;

            std::string roomName = room->name;
            std::string errorMessage =
                "Аудитория " + roomName + " слишком мала для группы " + group->name +
                ": capacity=" + std::to_string(room->capacity) +
                ", peopleCount=" + std::to_string(group->peopleCount) + ".";

            result.errors.push_back(errorMessage);
            logError("[RoomCapacity] " + errorMessage);
        }
}

}

void ScheduleValidator::checkSessionBounds(
    const std::vector<Timeslot>& timeslots,
    const std::string& sessionStartDate,
    const std::string& sessionEndDate,
    ValidationResult& result
) {
    // Предполагаем формат даты YYYY-MM-DD, тогда сравнение строк работает
    for (const Timeslot& t : timeslots) {
        if (t.date < sessionStartDate || t.date > sessionEndDate) {
            result.ok = false;

            std::string slotInfo = makeDate(t);
            std::string errorMessage =
                "Слот " + slotInfo +
                " выходит за пределы периода сессии " +
                sessionStartDate + " - " + sessionEndDate + ".";

            result.errors.push_back(errorMessage);
            logError("[SessionBounds] " + errorMessage);
        }
    }
}

void ScheduleValidator::checkAllExamsAssigned(
    const std::vector<Exam>& exams,
    const std::vector<ExamAssignment>& assignments,
    ValidationResult& result
) {
    if (exams.empty()) return;

    // Сколько раз каждый экзамен встретился в назначениях
    std::vector<int> counts(exams.size(), 0);

    for (const ExamAssignment& a : assignments) {
        int examIndex = a.examIndex;

        if (examIndex < 0 || examIndex >= (int)exams.size()) {
            result.ok = false;
            result.errors.push_back("Ошибка данных: examIndex вне диапазона в назначениях.");
            continue;
        }

        counts[examIndex]++;
    }

    for (int i = 0; i < (int)exams.size(); ++i) {
        const Exam& exam = exams[i];

        if (counts[i] == 0) {
            result.ok = false;

            std::string errorMessage =
                "Экзамен с id=" + std::to_string(exam.id) +
                " не назначен ни в один слот.";

            result.errors.push_back(errorMessage);
            logError("[ExamNotAssigned] " + errorMessage);
        }
        else if (counts[i] > 1) {
            result.ok = false;

            std::string errorMessage =
                "Экзамен с id=" + std::to_string(exam.id) +
                " назначен " + std::to_string(counts[i]) +
                " раз(а) в расписании.";

            result.errors.push_back(errorMessage);
            logError("[ExamMultiAssigned] " + errorMessage);
        }
    }
}

void ScheduleValidator::checkMaxExamsPerDayForGroup(
    const std::vector<Exam>& exams,
    const std::vector<Group>& groups,
    const std::vector<Timeslot>& timeslots,
    const std::vector<ExamAssignment>& assignments,
    int maxPerDay,
    ValidationResult& result
) {
    // (groupId, date) -> количество экзаменов в этот день
    std::map<std::pair<int, std::string>, int> table;

    for (const ExamAssignment& a : assignments) {
        int examIndex = a.examIndex;
        int timeslotId = a.timeslotId;

        if (examIndex < 0 || examIndex >= (int)exams.size()) {
            result.ok = false;
            result.errors.push_back("Ошибка данных: examIndex вне диапазона в назначениях.");
            continue;
        }

        const Exam& exam = exams[examIndex];
        int groupId = exam.groupId;

        const Timeslot* ts = findTimeslotById(timeslots, timeslotId);
        if (!ts) {
            result.ok = false;
            result.errors.push_back("Ошибка данных: не найден timeslot по id при проверке количества экзаменов в день.");
            continue;
        }

        std::pair<int, std::string> key(groupId, ts->date);
        table[key]++;
    }

    // Теперь проверяем, где превышен лимит
    for (const std::pair<std::pair<int, std::string>, int>& p : table) {
        int groupId = p.first.first;
        const std::string& date = p.first.second;
        int count = p.second;

        if (count > maxPerDay) {
            result.ok = false;

            std::string groupName = findGroupNameById(groups, groupId);

            std::string errorMessage =
                "У группы " + groupName +
                " в день " + date +
                " назначено " + std::to_string(count) +
                " экзамен(ов), что превышает допустимый максимум " +
                std::to_string(maxPerDay) + ".";

            result.errors.push_back(errorMessage);
        }
    }
}

ValidationResult ScheduleValidator::checkAll(
    const std::vector<Exam>& exams,
    const std::vector<Group>& groups,
    const std::vector<Teacher>& teachers,
    const std::vector<Room>& rooms,
    const std::vector<Timeslot>& timeslots,
    const std::vector<ExamAssignment>& assignments,
    const std::string& sessionStartDate,
    const std::string& sessionEndDate,
    int maxExamsPerDayForGroup
) {
    ValidationResult result;
    result.ok = true;

    logInfo("=== Запуск проверки расписания ===");
    logInfo("Экзаменов: " + std::to_string(exams.size()) +
            ", назначений: " + std::to_string(assignments.size()) +
            ", групп: " + std::to_string(groups.size()) +
            ", преподавателей: " + std::to_string(teachers.size()) +
            ", аудиторий: " + std::to_string(rooms.size()) +
            ", слотов: " + std::to_string(timeslots.size()));

    checkAllExamsAssigned(exams, assignments, result);
    checkGroupConflicts(exams, groups, timeslots, assignments, result);
    checkTeacherConflicts(exams, teachers, timeslots, assignments, result);
    checkRoomConflicts(exams, groups, rooms, timeslots, assignments, result);
    checkSessionBounds(timeslots, sessionStartDate, sessionEndDate, result);
    checkMaxExamsPerDayForGroup(exams, groups, timeslots, assignments, maxExamsPerDayForGroup, result);

    if (result.ok) {
        logInfo("Проверка расписания завершена: ошибок не обнаружено.");
    } else {
        logWarning("Проверка расписания завершена: обнаружено ошибок = " +
                   std::to_string(result.errors.size()));
    }

    return result;
}
