#include "model.h"

#pragma once

struct ExamAssignment {
    int examIndex;   // индекс экзамена в векторе exams
    int timeslotId;  // какой слот (id из Timeslot)
    int roomId;      // какая аудитория (id из Room)
};

struct ValidationResult {
    bool ok;                             // true, если ошибок нет
    std::vector<std::string> errors;     // фатальные ошибки (конфликты)
    std::vector<std::string> warnings;   // необязательные замечания (по желанию)
};

class ScheduleValidator {
    public:
        ValidationResult checkAll(
            const std::vector<Exam>& exams,
            const std::vector<Group>& groups,
            const std::vector<Teacher>& teachers,
            const std::vector<Room>& rooms,
            const std::vector<Timeslot>& timeslots,
            const std::vector<ExamAssignment>& assignments,
            const std::string& sessionStartDate,
            const std::string& sessionEndDate,
            int maxExamsPerDayForGroup
        ); // передаваемое
    
    private:
        void checkGroupConflicts(
            const std::vector<Exam>& exams,
            const std::vector<Group>& groups,
            const std::vector<Timeslot>& timeslots,
            const std::vector<ExamAssignment>& assignments,
            ValidationResult& result
        ); 
    
        void checkTeacherConflicts(
            const std::vector<Exam>& exams,
            const std::vector<Teacher>& teachers,
            const std::vector<Timeslot>& timeslots,
            const std::vector<ExamAssignment>& assignments,
            ValidationResult& result
        ); 

        void checkRoomConflicts(
            const std::vector<Exam>& exams,
            const std::vector<Group>& groups,
            const std::vector<Room>& rooms,
            const std::vector<Timeslot>& timeslots,
            const std::vector<ExamAssignment>& assignments,
            ValidationResult& result
        );
        
        void checkSessionBounds(
            const std::vector<Timeslot>& timeslots,
            const std::string& sessionStartDate,
            const std::string& sessionEndDate,
            ValidationResult& result
        );

        void checkAllExamsAssigned(
            const std::vector<Exam>& exams,
            const std::vector<ExamAssignment>& assignments,
            ValidationResult& result
        );

        void checkMaxExamsPerDayForGroup(
            const std::vector<Exam>& exams,
            const std::vector<Group>& groups,
            const std::vector<Timeslot>& timeslots,
            const std::vector<ExamAssignment>& assignments,
            int maxPerDay,
            ValidationResult& result
        );
    };