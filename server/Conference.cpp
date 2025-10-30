#include "Conference.h"
#include "ClientSession.h"
#include <algorithm>
#include <stdexcept>

Conference::Conference(const std::string& id, const std::string& creatorId)
    : id_(id), creatorId_(creatorId), isActive_(true) {
}

Conference::~Conference() {
    close();
}

std::string Conference::getId() const {
    return id_;
}

std::string Conference::getCreatorId() const {
    return creatorId_;
}

bool Conference::isActive() const {
    return isActive_;
}

bool Conference::addParticipant(std::shared_ptr<ClientSession> participant) {
    if (!isActive_) {
        return false;
    }
    
    std::string clientId = participant->getId();
    
    // Проверяем, не присоединен ли уже участник
    if (participants_.find(clientId) != participants_.end()) {
        return false;
    }
    
    participants_[clientId] = participant;
    participant->joinConference(id_);
    
    return true;
}

bool Conference::removeParticipant(const std::string& clientId) {
    auto it = participants_.find(clientId);
    if (it == participants_.end()) {
        return false;
    }
    
    // Удаляем все видеостримы этого участника
    removeAllVideoStreams(clientId);
    
    it->second->leaveConference();
    participants_.erase(it);
    
    // Если конференция пустая и не создана этим участником, закрываем её
    if (participants_.empty() && creatorId_ != clientId) {
        close();
    }
    
    return true;
}

void Conference::close() {
    if (!isActive_) return;
    
    isActive_ = false;
    
    // Уведомляем всех участников о закрытии
    for (auto& [clientId, participant] : participants_) {
        participant->leaveConference();
    }
    
    participants_.clear();
    videoStreams_.clear();
}

void Conference::addVideoStream(const std::string& clientId, const std::string& streamId) {
    if (!isActive_) {
        throw std::runtime_error("Conference is not active");
    }
    
    // Проверяем, что участник существует в конференции
    if (participants_.find(clientId) == participants_.end()) {
        throw std::runtime_error("Participant not found in conference");
    }
    
    // Проверяем, не существует ли уже стрим с таким ID
    if (videoStreams_.find(streamId) != videoStreams_.end()) {
        throw std::runtime_error("Video stream with this ID already exists");
    }
    
    videoStreams_[streamId] = clientId;
    
    // Уведомляем участника о добавлении стрима
    auto participant = participants_[clientId];
    participant->addVideoStream(streamId);
}

void Conference::removeVideoStream(const std::string& clientId, const std::string& streamId) {
    if (!isActive_) return;
    
    auto streamIt = videoStreams_.find(streamId);
    if (streamIt == videoStreams_.end()) {
        return; // Стрим не найден
    }
    
    // Проверяем, что стрим принадлежит указанному участнику
    if (streamIt->second != clientId) {
        throw std::runtime_error("Video stream does not belong to this participant");
    }
    
    videoStreams_.erase(streamIt);
    
    // Уведомляем участника об удалении стрима
    auto participantIt = participants_.find(clientId);
    if (participantIt != participants_.end()) {
        participantIt->second->removeVideoStream(streamId);
    }
}

void Conference::removeAllVideoStreams(const std::string& clientId) {
    if (!isActive_) return;
    
    // Удаляем все стримы участника
    for (auto it = videoStreams_.begin(); it != videoStreams_.end();) {
        if (it->second == clientId) {
            it = videoStreams_.erase(it);
        } else {
            ++it;
        }
    }
    
    // Уведомляем участника об удалении всех стримов
    auto participantIt = participants_.find(clientId);
    if (participantIt != participants_.end()) {
        // Здесь предполагается, что у ClientSession есть метод для удаления всех стримов
        // Если нет, нужно добавить или вызывать removeVideoStream для каждого стрима
    }
}

std::vector<std::string> Conference::getParticipants() const {
    std::vector<std::string> result;
    for (const auto& [clientId, participant] : participants_) {
        result.push_back(clientId);
    }
    return result;
}

std::vector<std::string> Conference::getVideoStreams() const {
    std::vector<std::string> result;
    for (const auto& [streamId, clientId] : videoStreams_) {
        result.push_back(streamId);
    }
    return result;
}

std::vector<std::string> Conference::getVideoStreamsForParticipant(const std::string& clientId) const {
    std::vector<std::string> result;
    for (const auto& [streamId, streamOwnerId] : videoStreams_) {
        if (streamOwnerId == clientId) {
            result.push_back(streamId);
        }
    }
    return result;
}

std::string Conference::getStreamOwner(const std::string& streamId) const {
    auto it = videoStreams_.find(streamId);
    if (it != videoStreams_.end()) {
        return it->second;
    }
    return "";
}

bool Conference::hasParticipant(const std::string& clientId) const {
    return participants_.find(clientId) != participants_.end();
}

bool Conference::hasVideoStream(const std::string& streamId) const {
    return videoStreams_.find(streamId) != videoStreams_.end();
}

size_t Conference::getParticipantCount() const {
    return participants_.size();
}

size_t Conference::getVideoStreamCount() const {
    return videoStreams_.size();
}

std::shared_ptr<ClientSession> Conference::getParticipant(const std::string& clientId) const {
    auto it = participants_.find(clientId);
    if (it != participants_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::shared_ptr<ClientSession>> Conference::getAllParticipants() const {
    std::vector<std::shared_ptr<ClientSession>> result;
    for (const auto& [clientId, participant] : participants_) {
        result.push_back(participant);
    }
    return result;
}

/*
 *
 * Пример использования:
 * Создание конференции
auto conference = std::make_shared<Conference>("conf123", "user1");

// Добавление участников
conference->addParticipant(client1);
conference->addParticipant(client2);

// Управление стримами
conference->addVideoStream("user1", "stream1");
conference->addVideoStream("user2", "stream2");

// Получение информации
auto participants = conference->getParticipants();
auto streams = conference->getVideoStreams();

// Удаление участника (автоматически удаляет его стримы)
conference->removeParticipant("user1");
 * */
