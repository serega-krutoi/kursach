#pragma once

#include <string>
#include <optional>
#include <chrono>

struct JwtPayload {
    long userId;
    std::string role;
    std::chrono::system_clock::time_point exp;
};

std::string createJwt(long userId,
                      const std::string& role,
                      const std::string& secret,
                      int ttlSeconds);

std::optional<JwtPayload> verifyJwt(const std::string& token,
                                    const std::string& secret);
