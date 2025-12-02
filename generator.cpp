#include "generator.h"
#include "graph.h"
#include <map>

const Group* findGroupByIdForSchedulerGen(const std::vector<Group>& groups, int groupId) {
    for (const Group& g : groups) if (g.id == groupId) return &g;
    return nullptr;
}

std::vector<ExamAssignment> generateSchedule(
    const std::vector<Exam>& exams,
    const std::vector<Group>& groups,
    const std::vector<Timeslot>& timeslots,
    const std::vector<Room>& rooms
) {
    std::vector<ExamAssignment> assignments;

    if (exams.empty()) return assignments;

    // 1. Строим граф и раскрашиваем
    ConflictGraph g = buildConflictGraph(exams);
    std::vector<int> colors = greedyColoring(g);

    // Найдём максимальный цвет
    int maxColor = 0;
    for (int c : colors) if (c > maxColor) maxColor = c;

    // Проверка, что слотов достаточно (простая версия)
    if ((int)timeslots.size() <= maxColor) {
        // тут можно либо бросить исключение, либо просто ограничиться
        // я сделаю тупо "обрежем" до доступных, но лучше потом поумнеть
        // for safety: maxColor = timeslots.size() - 1;
        // но тогда часть экзаменов попадёт в последний слот
        // можешь заменить на assert / throw по желанию
    }

    // Для контроля занятости аудиторий:
    // timeslotId -> список roomId
    std::map<int, std::vector<int>> usedRooms;

    for (int examIndex = 0; examIndex < (int)exams.size(); ++examIndex) {
        int color = colors[examIndex];
        if (color >= (int)timeslots.size()) {
            // если слотов меньше, чем цветов — ставим всё в последний слот (временный костыль)
            color = (int)timeslots.size() - 1;
        }

        const Timeslot& ts = timeslots[color];
        int timeslotId = ts.id;

        const Exam& exam = exams[examIndex];
        int groupId = exam.groupId;

        const Group* group = findGroupByIdForSchedulerGen(groups, groupId);

        int chosenRoomId = -1;

        // Пытаемся найти аудиторию с достаточной вместимостью и не занятую в этом слоте
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
                break;
            }
        }

        // Если не нашли подходящую аудиторию, ставим первую попавшуюся (валидация потом поймает)
        if (chosenRoomId == -1 && !rooms.empty()) {
            chosenRoomId = rooms[0].id;
            usedRooms[timeslotId].push_back(chosenRoomId);
        }

        ExamAssignment a;
        a.examIndex = examIndex;
        a.timeslotId = timeslotId;
        a.roomId = chosenRoomId;

        assignments.push_back(a);
    }

    return assignments;
}