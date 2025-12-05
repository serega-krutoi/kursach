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

        // Сначала проверим: нет ли в базовом слоте конфликтов по графу
        bool baseSlotHasConflict = false;
        for (const ExamAssignment& other : assignments) {
            if (other.timeslotId != timeslotId) continue;

            int otherExamIndex = other.examIndex;
            if (g.adj[examIndex][otherExamIndex]) {
                baseSlotHasConflict = true;
                break;
            }
        }

        if (!baseSlotHasConflict) {
            // Ищем аудиторию с достаточной вместимостью и незанятую в этот слот
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
        } else {
            logWarning("В базовом слоте " + std::to_string(timeslotId) +
                    " для exam id=" + std::to_string(exam.id) +
                    " найден конфликт по графу. Базовый слот пропускаем.");
        }
    
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
            logWarning("НЕ НАЙДЕНА аудитория в базовом слоте для exam id=" +
                       std::to_string(exam.id) +
                       " (group=" + std::to_string(exam.groupId) +
                       ", slot=" + std::to_string(timeslotId) +
                       "). Пробуем альтернативные слоты.");
    
            int newTimeslotId = timeslotId; // по умолчанию остаётся старый
            int newRoomId = -1;
    
            // перебираем ВСЕ слоты в порядке timeslotOrder
            for (int altOrderIndex = 0; altOrderIndex < (int)timeslotOrder.size(); ++altOrderIndex) {
                int altTsIndex = timeslotOrder[altOrderIndex];
                const Timeslot& altTs = timeslots[altTsIndex];
                int altTimeslotId = altTs.id;
    
                // можно пропустить исходный слот
                if (altTimeslotId == timeslotId) continue;
    
                // 1) проверяем конфликты по графу:
                //    в этом слоте уже стоят какие-то экзамены -> нельзя, если есть ребро
                bool hasConflict = false;
                for (const ExamAssignment& other : assignments) {
                    if (other.timeslotId != altTimeslotId) continue;
    
                    int otherExamIndex = other.examIndex;
                    // если в графе есть ребро, значит экзамены конфликтуют
                    if (g.adj[examIndex][otherExamIndex]) {
                        hasConflict = true;
                        break;
                    }
                }
    
                if (hasConflict) {
                    continue; // этот слот не подходит
                }
    
                // 2) пробуем найти аудиторию в ЭТОМ слоте
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
    
                        logInfo("Переназначили exam id=" + std::to_string(exam.id) +
                                " в альтернативный слот " + std::to_string(newTimeslotId) +
                                " в аудиторию " + r.name +
                                " (capacity=" + std::to_string(r.capacity) + ")");
                        break;
                    }
                }
    
                if (newRoomId != -1) {
                    break; // нашли подходящий слот и аудиторию
                }
            }
    
            if (newRoomId != -1) {
                // удачно переназначили
                timeslotId   = newTimeslotId;
                chosenRoomId = newRoomId;
            } else {
                // даже после попытки переназначения ничего не вышло
                logError("Даже после поиска альтернативных слотов НЕ НАЙДЕНА аудитория для exam id=" +
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

std::vector<ExamAssignment> generateScheduleSimple(
    const std::vector<Exam>& exams,
    const std::vector<Group>& groups,
    const std::vector<Subject>& subjects,   // здесь не используем, но оставляем для совместимости
    const std::vector<Timeslot>& timeslots,
    const std::vector<Room>& rooms
) {
    logInfo("=== Запуск простого генератора расписания (simple) ===");

    std::vector<ExamAssignment> assignments;
    if (exams.empty()) {
        logWarning("Список экзаменов пуст. Простое расписание не будет создано.");
        return assignments;
    }

    // Для контроля занятости:
    // (groupId, timeslotId)   -> уже есть экзамен
    // (teacherId, timeslotId) -> уже есть экзамен
    // (roomId, timeslotId)    -> аудитория занята
    std::map<std::pair<int,int>, bool> usedGroupSlot;
    std::map<std::pair<int,int>, bool> usedTeacherSlot;
    std::map<std::pair<int,int>, bool> usedRoomSlot;

    // Сортируем слоты по дате/времени (как в основном генераторе)
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

    auto findGroupByIdForScheduler = [&](int groupId) -> const Group* {
        for (const Group& g : groups) if (g.id == groupId) return &g;
        return nullptr;
    };

    for (int examIndex = 0; examIndex < (int)exams.size(); ++examIndex) {
        const Exam& exam = exams[examIndex];
        int groupId   = exam.groupId;
        int teacherId = exam.teacherId;

        const Group* group = findGroupByIdForScheduler(groupId);

        bool placed = false;
        int chosenTimeslotId = -1;
        int chosenRoomId = -1;

        logDebug("SIMPLE: пытаемся расположить exam id=" + std::to_string(exam.id) +
                 " (groupId=" + std::to_string(groupId) +
                 ", teacherId=" + std::to_string(teacherId) + ")");

        // Перебираем слоты по порядку
        for (int tsOrderIndex = 0; tsOrderIndex < (int)timeslotOrder.size() && !placed; ++tsOrderIndex) {
            int tsIndex = timeslotOrder[tsOrderIndex];
            const Timeslot& ts = timeslots[tsIndex];
            int timeslotId = ts.id;

            std::pair<int,int> gk(groupId, timeslotId);
            std::pair<int,int> tk(teacherId, timeslotId);

            // проверка, не занята ли группа/препод в этот слот
            if (usedGroupSlot[gk] || usedTeacherSlot[tk]) {
                continue;
            }

            // ищем аудиторию
            for (const Room& r : rooms) {
                std::pair<int,int> rk(r.id, timeslotId);

                if (usedRoomSlot[rk]) continue;

                bool capacityOk = true;
                if (group) {
                    capacityOk = (r.capacity >= group->peopleCount);
                }

                if (!capacityOk) continue;

                // нашли подходящую комнату
                chosenTimeslotId = timeslotId;
                chosenRoomId     = r.id;

                usedGroupSlot[gk]   = true;
                usedTeacherSlot[tk] = true;
                usedRoomSlot[rk]    = true;

                placed = true;

                logInfo("SIMPLE: exam id=" + std::to_string(exam.id) +
                        " поставлен в слот " + std::to_string(timeslotId) +
                        " в аудиторию " + r.name);
                break;
            }
        }

        if (!placed) {
            logError("SIMPLE: не удалось назначить exam id=" + std::to_string(exam.id) +
                     " (groupId=" + std::to_string(groupId) + ")");
        }

        ExamAssignment a;
        a.examIndex  = examIndex;
        a.timeslotId = chosenTimeslotId; // может быть -1, если не нашли слот
        a.roomId     = chosenRoomId;     // может быть -1, если не нашли аудиторию

        assignments.push_back(a);
    }

    logInfo("=== Простой генератор (simple) завершил работу ===");
    return assignments;
}
