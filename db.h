#pragma once

#include <pqxx/pqxx>

#include <memory>
#include <string>
#include <optional>
#include <vector>
#include "model.h"

namespace db {

struct DbUser {
    long id;
    std::string username;
    std::string passwordHash;
    std::string role;                  
    std::optional<std::string> email;  
};

}

namespace db {

struct DbConfig {
    std::string host;
    int         port;
    std::string dbname;
    std::string user;
    std::string password;

    static DbConfig fromEnv();
};

}

namespace db {

class ConnectionFactory {
public:
    explicit ConnectionFactory(const DbConfig& cfg);

    std::unique_ptr<pqxx::connection> createConnection() const;

private:
    DbConfig config;
};

}

namespace db {

class UserRepository {
public:
    explicit UserRepository(ConnectionFactory& factory);

    std::optional<DbUser> findUserByUsername(const std::string& username);
    long createUser(const std::string& username,
                    const std::string& passwordHash,
                    const std::string& role,
                    const std::optional<std::string>& email);

private:
    ConnectionFactory& factory;
};
}

namespace db {
    struct DbSchedule {
    long id;
    long userId;
    std::string name;        // может быть пустой
    std::string configJson;  // JSON-строка config
    std::string resultJson;  // JSON-строка результата
    std::string createdAt;
    std::string updatedAt;
};

class ScheduleRepository {
public:
    explicit ScheduleRepository(ConnectionFactory& factory)
        : factory_(factory) {}

    // Создаёт расписание и возвращает его id
    long createSchedule(
        long userId,
        const std::string& configJson,
        const std::string& resultJson,
        const std::optional<std::string>& name = std::nullopt
    );

    // На будущее: список расписаний пользователя
    std::vector<DbSchedule> findSchedulesByUser(long userId);

    // На будущее: одно расписание по id, но только если оно принадлежит userId
    std::optional<DbSchedule> findScheduleById(long userId, long scheduleId);

private:
    ConnectionFactory& factory_;
};

}

namespace db {

struct DomainData {
    std::vector<Group>   groups;
    std::vector<Teacher> teachers;
    std::vector<Subject> subjects;
    std::vector<Room>    rooms;
    std::vector<Timeslot> timeslots;
    std::vector<Exam>    exams;
};

std::vector<Group> loadGroups(ConnectionFactory& factory);
std::vector<Teacher> loadTeachers(ConnectionFactory& factory);

DomainData loadDomainData(ConnectionFactory& factory);

}
