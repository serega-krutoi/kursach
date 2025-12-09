#include "auth.h"
#include <bcrypt/BCrypt.hpp>

std::string hashPassword(const std::string& plain) {
    return BCrypt::generateHash(plain);
}

bool verifyPassword(const std::string& plain, const std::string& hashed) {
    return BCrypt::validatePassword(plain, hashed);
}
