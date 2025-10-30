#include "IdGenerator.h"
#include <random>
#include <sstream>

std::mt19937& IdGenerator::getGenerator() {
    static std::random_device rd;
    static std::mt19937 generator(rd());
    return generator;
}

std::string IdGenerator::generateRandomString(size_t length) {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    
    std::uniform_int_distribution<> dist(0, sizeof(alphanum) - 2);
    std::string result;
    result.reserve(length);
    
    auto& gen = getGenerator();
    for (size_t i = 0; i < length; ++i) {
        result += alphanum[dist(gen)];
    }
    
    return result;
}

std::string IdGenerator::generateRandomNumberString(size_t length) {
    std::uniform_int_distribution<int> dist(0, 9);
    std::string result;
    result.reserve(length);
    
    auto& gen = getGenerator();
    for (size_t i = 0; i < length; ++i) {
        result += '0' + dist(gen);
    }
    
    return result;
}

std::string IdGenerator::generateClientId() {
    return "client_" + generateRandomString(8);
}

std::string IdGenerator::generateConferenceId() {
    return "conf_" + generateRandomString(10);
}

std::string IdGenerator::generateStreamId() {
    return "stream_" + generateRandomString(12);
}
