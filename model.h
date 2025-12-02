#include <string>
#include <vector>

#pragma once

struct Group {
    int id;
    std::string name;
    int peopleCount;
};

struct Teacher {
    int id;
    std::string name;
    std::string subject;
};

struct Room {
    int id;
    std::string name; 
    int capacity;
};

struct Timeslot {
    int id; 
    std::string date; 
    int startMinutes;  
    int endMinutes;
};

struct Subject {
    int id;
    std::string name;
    int difficulty;
};

struct Exam {
    int id;
    int groupId;
    int teacherId;
    int subjectId;
    int duration;
};