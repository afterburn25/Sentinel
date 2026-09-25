#pragma once
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace sentinel::operations {

enum class DeliveryState { Draft, Queued, Sent, Delivered, Read, Failed };

struct NormalizedMessage {
    std::string id;
    std::string conversationId;
    std::string sender;
    std::string text;
    std::chrono::system_clock::time_point timestamp;
    DeliveryState state{DeliveryState::Draft};
    bool inbound{false};
};

class IMessageAdapter {
public:
    virtual ~IMessageAdapter()=default;
    virtual std::string ProviderName() const=0;
    virtual bool Connected() const=0;
    virtual std::vector<NormalizedMessage> Poll(const std::string& conversationId)=0;
    virtual NormalizedMessage QueueOperatorApproved(const std::string& conversationId,const std::string& text)=0;
};

std::unique_ptr<IMessageAdapter> CreateInMemoryMessageAdapter();

}
