// generator.cpp
#include "generator.h"

#include "graph.h"
#include "logger.h"
#include "api_dto.h"

#include <map>
#include <algorithm>
#include <string>

// --- маленькие хелперы ---

static const Subject* findSubjectById(
    const std::vector<Subject>& subjects,
    int subjectId
) {
    for (const Subject& s : subjects) {
        if (s.id == subjectId) return &s;
    }
    return nullptr;
}

static int getExamDifficulty(
    const Exam& exam,
    const std::vector<Subject>& subjects
) {
    const Subject* s = findSubjectById(subjects, exam.subjectId);
    if (!s) return 1; // по умолчанию сложность 1
    return s->difficulty;
}

static const Group* findGroupByIdForScheduler(
    const std::vector<Group>& groups,
    int groupId
) {
    for (const Group& g : groups) {
        if (g.id == groupId) return &g;
    }
    return nullptr;
}

static const Timeslot* findTimeslotById(
    const std::vector<Timeslot>& timeslots,
    int timeslotId
) {
    for (const Timeslot& t : timeslots) {
        if (t.id == timeslotId) return &t;
    }
    return nullptr;
}

// Проверяем, не превышает ли экзамен ограничение maxExamsPerDayForGroup
// для своей группы в день timeslotId.
static bool canPlaceExamForGroupOnDate(
    const Exam& exam,
    int examIndex,
    int candidateTimeslotId,
    const std::vector<ExamAssignment>& assignments,
    const std::vector<Exam>& exams,
    const std::vector<Timeslot>& timeslots,
    int maxExamsPerDayForGroup
) {
    if (maxExamsPerDayForGroup <= 0) {
        // 0 или отрицательное значение — трактуем как "без ограничения"
        return true;
    }

    const Timeslot* tsCandidate = findTimeslotById(timeslots, candidateTimeslotId);
    if (!tsCandidate) {
        // Если вдруг нет слота — пусть этим займётся валидатор
        return true;
    }
    const std::string& day = tsCandidate->date;

    int countForGroupThisDay = 0;

    for (const ExamAssignment& a : assignments) {
        if (a.timeslotId < 0) continue;

        const Timeslot* ts = findTimeslotById(timeslots, a.timeslotId);
        if (!ts) continue;
        if (ts->date != day) continue;

        int otherIndex = a.examIndex;
        if (otherIndex < 0 || otherIndex >= (int)exams.size()) continue;
        const Exam& otherExam = exams[otherIndex];

        if (otherExam.groupId == exam.groupId) {
            countForGroupThisDay++;
            if (countForGroupThisDay >= maxExamsPerDayForGroup) {
                return false;
            }
        }
    }

    return true;
}

// ============================================================================
//                              ГРАФОВЫЙ ГЕНЕРАТОР 
// ============================================================================

std::vector<ExamAssignment> generateSchedule(
    const std::vector<Exam>& exams,
    const std::vector<Group>& groups,
    const std::vector<Subject>& subjects,
    const std::vector<Timeslot>& timeslots,
    const std::vector<Room>& rooms,
    int maxExamsPerDayForGroup
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
    if (timeslots.empty()) {
        logWarning("Список таймслотов пуст. Расписание не будет сгенерировано.");
        return assignments;
    }

    // 1) Граф конфликтов и раскраска
    ConflictGraph g = buildConflictGraph(exams);
    std::vector<int> colors = greedyColoring(g);
    int n = (int)exams.size();

    // 2) Подсчёт средней сложности по цветам
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

    // 3) Сортируем цвета по средней сложности (от лёгких к сложным)
    std::sort(stats.begin(), stats.end(),
        [](const ColorStat& a, const ColorStat& b) {
            return a.avg < b.avg;
        }
    );

    // 4) Упорядочим таймслоты по дате/времени
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

    // 5) Маппинг цвет -> индекс таймслота
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

    // если цветов больше, чем слотов – кидаем в последний
    for (int i = limit; i < (int)stats.size(); ++i) {
        int color = stats[i].color;
        colorToTimeslotIndex[color] = timeslotOrder.back();
    }

    // 6) Учёт занятости аудиторий в каждом слоте
    std::map<int, std::vector<int>> usedRooms;

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

        const Group* group = findGroupByIdForScheduler(groups, groupId);

        logDebug("Назначаем exam id=" + std::to_string(exam.id) +
                 " (groupId=" + std::to_string(exam.groupId) +
                 ", color=" + std::to_string(color) +
                 ") в слот id=" + std::to_string(timeslotId));

        int chosenRoomId = -1;

        // --- 6.1 Проверка конфликтов в базовом слоте: по графу + по maxPerDay ---
        bool baseSlotHasConflict = false;

        // a) конфликты по графу
        for (const ExamAssignment& other : assignments) {
            if (other.timeslotId != timeslotId) continue;
            int otherExamIndex = other.examIndex;
            if (otherExamIndex < 0 || otherExamIndex >= n) continue;
            if (g.adj[examIndex][otherExamIndex]) {
                baseSlotHasConflict = true;
                break;
            }
        }

        // b) ограничение по maxExamsPerDayForGroup
        if (!baseSlotHasConflict &&
            !canPlaceExamForGroupOnDate(
                exam,
                examIndex,
                timeslotId,
                assignments,
                exams,
                timeslots,
                maxExamsPerDayForGroup
            )
        ) {
            baseSlotHasConflict = true;
            logDebug("Базовый слот " + std::to_string(timeslotId) +
                     " нарушает maxExamsPerDayForGroup для группы " +
                     std::to_string(exam.groupId));
        }

        // --- 6.2 Если базовый слот ОК — пробуем найти аудиторию ---
        if (!baseSlotHasConflict) {
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
                logWarning("Не нашли аудиторию в базовом слоте " +
                           std::to_string(timeslotId) +
                           " для exam id=" + std::to_string(exam.id));
            }
        } else {
            logWarning("В базовом слоте " + std::to_string(timeslotId) +
                       " для exam id=" + std::to_string(exam.id) +
                       " найден конфликт (граф или maxPerDay).");
        }

        // --- 6.3 Если базовый слот не подошёл или не нашли аудиторию —
        // пробуем альтернативные слоты
        if (chosenRoomId == -1) {
            int newTimeslotId = timeslotId; // по умолчанию
            int newRoomId = -1;

            for (int altOrderIndex = 0;
                 altOrderIndex < (int)timeslotOrder.size();
                 ++altOrderIndex)
            {
                int altTsIndex = timeslotOrder[altOrderIndex];
                const Timeslot& altTs = timeslots[altTsIndex];
                int altTimeslotId = altTs.id;

                if (altTimeslotId == timeslotId) continue;

                // 1) графовые конфликты
                bool hasGraphConflict = false;
                for (const ExamAssignment& other : assignments) {
                    if (other.timeslotId != altTimeslotId) continue;
                    int otherExamIndex = other.examIndex;
                    if (otherExamIndex < 0 || otherExamIndex >= n) continue;
                    if (g.adj[examIndex][otherExamIndex]) {
                        hasGraphConflict = true;
                        break;
                    }
                }
                if (hasGraphConflict) continue;

                // 2) ограничение по количеству экзаменов в день
                if (!canPlaceExamForGroupOnDate(
                        exam,
                        examIndex,
                        altTimeslotId,
                        assignments,
                        exams,
                        timeslots,
                        maxExamsPerDayForGroup
                    )
                ) {
                    continue;
                }

                // 3) ищем аудиторию в этом слоте
                for (const Room& r : rooms) {
                    bool alreadyUsed = false;
                    auto it2 = usedRooms.find(altTimeslotId);
                    if (it2 != usedRooms.end()) {
                        for (int usedId : it2->second) {
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
                        newTimeslotId = altTimeslotId;
                        newRoomId = r.id;
                        usedRooms[altTimeslotId].push_back(r.id);

                        logInfo("Переназначили exam id=" +
                                std::to_string(exam.id) +
                                " в альтернативный слот " +
                                std::to_string(newTimeslotId) +
                                " в аудиторию " + r.name +
                                " (capacity=" + std::to_string(r.capacity) + ")");
                        break;
                    }
                }

                if (newRoomId != -1) break;
            }

            if (newRoomId != -1) {
                timeslotId   = newTimeslotId;
                chosenRoomId = newRoomId;
            } else {
                logError("Даже после поиска альтернативных слотов НЕ НАЙДЕНА аудитория/слот для exam id=" +
                         std::to_string(exam.id) +
                         " (group=" + std::to_string(exam.groupId) + ").");
            }
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