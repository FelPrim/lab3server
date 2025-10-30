#ifndef CONFERENCE_H
#define CONFERENCE_H

#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

class ClientSession;

class Conference {
public:
    Conference(const std::string& id, const std::string& creatorId);
    ~Conference();

    std::string getId() const;
    std::string getCreatorId() const;
    bool isActive() const;
    
    // Управление участниками
    bool addParticipant(std::shared_ptr<ClientSession> participant);
    bool removeParticipant(const std::string& clientId);
    void close();
    
    // Управление видеостримами
    void addVideoStream(const std::string& clientId, const std::string& streamId);
    void removeVideoStream(const std::string& clientId, const std::string& streamId);
    void removeAllVideoStreams(const std::string& clientId);
    
    // Получение информации
    std::vector<std::string> getParticipants() const;
    std::vector<std::string> getVideoStreams() const;
    std::vector<std::string> getVideoStreamsForParticipant(const std::string& clientId) const;
    std::string getStreamOwner(const std::string& streamId) const;
    
    // Проверки существования
    bool hasParticipant(const std::string& clientId) const;
    bool hasVideoStream(const std::string& streamId) const;
    
    // Статистика
    size_t getParticipantCount() const;
    size_t getVideoStreamCount() const;
    
    // Получение объектов
    std::shared_ptr<ClientSession> getParticipant(const std::string& clientId) const;
    std::vector<std::shared_ptr<ClientSession>> getAllParticipants() const;

private:
    std::string id_;
    std::string creatorId_;
    bool isActive_;
    
    std::unordered_map<std::string, std::shared_ptr<ClientSession>> participants_;
    std::unordered_map<std::string, std::string> videoStreams_; // streamId -> clientId
};

#endif
