#include "jwt_utils.h"
#include "json.hpp"

#include <openssl/hmac.h>
#include <openssl/evp.h>

#include <string>
#include <vector>
#include <stdexcept>
#include <ctime>

using nlohmann::json;

// ---- base64 / base64url helpers ----

static std::string base64Encode(const std::string& data) {
    int encodedLen = 4 * ((data.size() + 2) / 3);
    std::vector<unsigned char> out(encodedLen + 1);

    int len = EVP_EncodeBlock(
        out.data(),
        reinterpret_cast<const unsigned char*>(data.data()),
        data.size()
    );
    if (len < 0) {
        throw std::runtime_error("EVP_EncodeBlock failed");
    }
    return std::string(reinterpret_cast<char*>(out.data()), len);
}

static std::string base64Decode(const std::string& b64) {
    std::string tmp = b64;
    while (tmp.size() % 4 != 0) {
        tmp.push_back('=');
    }

    int decodedLen = 3 * (tmp.size() / 4);
    std::vector<unsigned char> out(decodedLen + 1);

    int len = EVP_DecodeBlock(
        out.data(),
        reinterpret_cast<const unsigned char*>(tmp.data()),
        tmp.size()
    );
    if (len < 0) {
        throw std::runtime_error("EVP_DecodeBlock failed");
    }
    return std::string(reinterpret_cast<char*>(out.data()), len);
}

static std::string toBase64Url(const std::string& b64) {
    std::string res;
    res.reserve(b64.size());
    for (char c : b64) {
        if (c == '+') res.push_back('-');
        else if (c == '/') res.push_back('_');
        else if (c == '=') {
            // padding убираем
        } else res.push_back(c);
    }
    return res;
}

static std::string fromBase64Url(const std::string& b64url) {
    std::string b64;
    b64.reserve(b64url.size());
    for (char c : b64url) {
        if (c == '-') b64.push_back('+');
        else if (c == '_') b64.push_back('/');
        else b64.push_back(c);
    }
    return b64;
}

// ---- HMAC-SHA256 ----

static std::string hmacSha256(const std::string& data, const std::string& secret) {
    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int macLen = 0;

    HMAC(
        EVP_sha256(),
        reinterpret_cast<const unsigned char*>(secret.data()),
        secret.size(),
        reinterpret_cast<const unsigned char*>(data.data()),
        data.size(),
        mac,
        &macLen
    );

    return std::string(reinterpret_cast<char*>(mac), macLen);
}

// ---- JWT ----

std::string createJwt(long userId,
                      const std::string& role,
                      const std::string& secret,
                      int ttlSeconds) {
    // header
    json header = {
        {"alg", "HS256"},
        {"typ", "JWT"}
    };
    std::string headerStr = header.dump();

    // payload
    auto now = std::chrono::system_clock::now();
    auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()
    ).count();
    long exp = static_cast<long>(nowSec + ttlSeconds);

    json payload = {
        {"sub", userId},
        {"role", role},
        {"exp", exp}
    };
    std::string payloadStr = payload.dump();

    // base64url(header) + "." + base64url(payload)
    std::string headerB64url = toBase64Url(base64Encode(headerStr));
    std::string payloadB64url = toBase64Url(base64Encode(payloadStr));
    std::string toSign = headerB64url + "." + payloadB64url;

    // подпись
    std::string signature = hmacSha256(toSign, secret);
    std::string signatureB64url = toBase64Url(base64Encode(signature));

    return toSign + "." + signatureB64url;
}

std::optional<JwtPayload> verifyJwt(const std::string& token,
                                    const std::string& secret) {
    // header.payload.signature
    size_t dot1 = token.find('.');
    if (dot1 == std::string::npos) return std::nullopt;
    size_t dot2 = token.find('.', dot1 + 1);
    if (dot2 == std::string::npos) return std::nullopt;

    std::string headerB64url   = token.substr(0, dot1);
    std::string payloadB64url  = token.substr(dot1 + 1, dot2 - dot1 - 1);
    std::string signatureB64url = token.substr(dot2 + 1);

    std::string toSign = headerB64url + "." + payloadB64url;

    // проверка подписи
    std::string expected = hmacSha256(toSign, secret);
    std::string expectedB64url = toBase64Url(base64Encode(expected));

    if (expectedB64url != signatureB64url) {
        return std::nullopt;
    }

    // декодируем payload
    std::string payloadJsonStr = base64Decode(fromBase64Url(payloadB64url));
    json payload = json::parse(payloadJsonStr);

    long userId = payload.value("sub", 0L);
    std::string role = payload.value("role", std::string(""));
    long expSec = payload.value("exp", 0L);

    if (userId == 0 || role.empty() || expSec == 0) {
        return std::nullopt;
    }

    auto now = std::chrono::system_clock::now();
    auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()
    ).count();

    if (nowSec > expSec) {
        return std::nullopt; // токен просрочен
    }

    JwtPayload result;
    result.userId = userId;
    result.role = role;
    result.exp = std::chrono::system_clock::time_point(
        std::chrono::seconds(expSec)
    );
    return result;
}
