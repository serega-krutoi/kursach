#include "generator.h"
#include "graph.h"
#include "logger.h"
#include <map>
#include <algorithm> 

const Subject* findSubjectById(const std::vector<Subject>& subjects, int subjectId) {
    for (const Subject& s : subjects) if (s.id == subjectId) return &s;
    return nullptr;
}

int getExamDifficulty(const Exam& exam, const std::vector<Subject>& subjects) {
    const Subject* s = findSubjectById(subjects, exam.subjectId);
    if (!s) return 1; // по умолчанию сложность 1, если вдруг не нашли предмет
    return s->difficulty;
}

const Group* findGroupByIdForSchedulerGen(const std::vector<Group>& groups, int groupId) {
    for (const Group& g : groups) if (g.id == groupId) return &g;
    return nullptr;
}

std::vector<ExamAssignment> generateSchedule(
    const std::vector<Exam>& exams,
    const std::vector<Group>& groups,
    const std::vector<Subject>& subjects,
    const std::vector<Timeslot>& timeslots,
    const std::vector<Room>& rooms
) {
    logInfo("=== Запуск генерации расписания ===");
    logInfo("Экзаменов: " + std::to_string(exams.size()) +
            ", групп: " + std::to_string(groups.size()) +
            ", слотов: " + std::to_string(timeslots.size()) +
            ", аудиторий: " + std::to_string(rooms.size()));

    std::vector<ExamAssignment> assignments;
    if (exams.empty()) {
        logWarning("Список экзаменов пуст. Расписание не будет сгенерировано.");
        return assignments;
    }

    if (exams.empty()) return assignments;

    // Create граф и красить
    ConflictGraph g = buildConflictGraph(exams);
    std::vector<int> colors = greedyColoring(g);

    int n = (int)exams.size();

    // Подсчет среднюю сложность для каждого цвета
    int maxColor = 0;
    for (int c : colors) if (c > maxColor) maxColor = c;
    int colorCount = maxColor + 1;

    std::vector<int> sumDifficulty(colorCount, 0);
    std::vector<int> countPerColor(colorCount, 0);

    for (int i = 0; i < n; ++i) {
        int c = colors[i];
        int diff = getExamDifficulty(exams[i], subjects);
        sumDifficulty[c] += diff;
        countPerColor[c] += 1;
    }

    struct ColorStat {
        int color;
        double avg;
    };

    std::vector<ColorStat> stats;
    for (int c = 0; c < colorCount; ++c) {
        if (countPerColor[c] == 0) continue;
        double avg = (double)sumDifficulty[c] / (double)countPerColor[c];
        stats.push_back({c, avg});
    
        logDebug("Цвет " + std::to_string(c) +
                 ": экзаменов=" + std::to_string(countPerColor[c]) +
                 ", средняя_сложность=" + std::to_string(avg));
    }

    // sort цвета по средней сложности (от лёгких к сложным)
    std::sort(stats.begin(), stats.end(),
        [](const ColorStat& a, const ColorStat& b) {
            return a.avg < b.avg;
        }
    );

    // готовка индексов таймслотов, упорядоченных по дате/времени (если нужно)
    std::vector<int> timeslotOrder(timeslots.size());
    for (int i = 0; i < (int)timeslots.size(); ++i) timeslotOrder[i] = i;

    std::sort(timeslotOrder.begin(), timeslotOrder.end(),
        [&](int i, int j) {
            const Timeslot& a = timeslots[i];
            const Timeslot& b = timeslots[j];
            if (a.date != b.date) return a.date < b.date;
            return a.startMinutes < b.startMinutes;
        }
    );

    // Маппинг: цвет -> индекс таймслота
    // предположим, что таймслотов >= количеству цветов
    std::vector<int> colorToTimeslotIndex(colorCount, 0);

    int limit = std::min((int)stats.size(), (int)timeslotOrder.size());
    for (int i = 0; i < limit; ++i) {
        int color = stats[i].color;
        int tsIndex = timeslotOrder[i];
        colorToTimeslotIndex[color] = tsIndex;

        const Timeslot& ts = timeslots[tsIndex];
        logInfo("Цвет " + std::to_string(color) +
                " (avgDifficulty=" + std::to_string(stats[i].avg) + ")" +
                " -> слот " + std::to_string(ts.id) +
                " (" + ts.date + ")");
    }


    // если цветов больше, чем таймслотов – в последний слот
    for (int i = limit; i < (int)stats.size(); ++i) {
        int color = stats[i].color;
        colorToTimeslotIndex[color] = timeslotOrder.back();
    }

    // Контроль занятости аудиторий: timeslotId -> roomId'ы
    std::map<int, std::vector<int>> usedRooms;

    auto findGroupByIdForScheduler = [&](int groupId) -> const Group* {
        for (const Group& g : groups) if (g.id == groupId) return &g;
        return nullptr;
    };

    for (int examIndex = 0; examIndex < n; ++examIndex) {
        int color = colors[examIndex];
        if (color < 0 || color >= colorCount) {
            logWarning("Некорректный цвет " + std::to_string(color) +
                       " для examIndex=" + std::to_string(examIndex));
            color = 0;
        }
    
        int tsIndex = colorToTimeslotIndex[color];
        const Timeslot& ts = timeslots[tsIndex];
        int timeslotId = ts.id;
    
        const Exam& exam = exams[examIndex];
        int groupId = exam.groupId;
    
        const Group* group = findGroupByIdForScheduler(groupId);
    
        logDebug("Назначаем exam id=" + std::to_string(exam.id) +
                 " (groupId=" + std::to_string(exam.groupId) +
                 ", color=" + std::to_string(color) +
                 ") в слот id=" + std::to_string(timeslotId));
    
        int chosenRoomId = -1;
    
        for (const Room& r : rooms) {
            bool alreadyUsed = false;
            auto it = usedRooms.find(timeslotId);
            if (it != usedRooms.end()) {
                for (int usedId : it->second) {
                    if (usedId == r.id) {
                        alreadyUsed = true;
                        break;
                    }
                }
            }
    
            bool capacityOk = true;
            if (group) {
                capacityOk = (r.capacity >= group->peopleCount);
            }
    
            if (!alreadyUsed && capacityOk) {
                chosenRoomId = r.id;
                usedRooms[timeslotId].push_back(r.id);
    
                logInfo("Экзамен id=" + std::to_string(exam.id) +
                        " назначен в аудиторию " + r.name +
                        " (capacity=" + std::to_string(r.capacity) + ")");
                break;
            }
        }
    
        if (chosenRoomId == -1) {
            logError("НЕ НАЙДЕНА аудитория для exam id=" +
                     std::to_string(exam.id) +
                     " (group=" + std::to_string(exam.groupId) +
                     ", slot=" + std::to_string(timeslotId) + ")");
        }
    
        ExamAssignment a;
        a.examIndex  = examIndex;
        a.timeslotId = timeslotId;
        a.roomId     = chosenRoomId;
    
        assignments.push_back(a);
    }
    

    logInfo("=== Генерация расписания завершена ===");
    return assignments;
}