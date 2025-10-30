#ifndef IDGENERATOR_H
#define IDGENERATOR_H

#include <string>
#include <random>

class IdGenerator {
public:
    static std::string generateClientId();
    static std::string generateConferenceId();
    static std::string generateStreamId();

private:
    static std::string generateRandomString(size_t length);
    static std::string generateRandomNumberString(size_t length);
    
    static std::mt19937& getGenerator();
};

#endif
