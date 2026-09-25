#include "Sentinel/Operations/Messaging.hpp"
#include <atomic>

namespace sentinel::operations {
namespace {
class InMemoryAdapter final : public IMessageAdapter {
public:
    std::string ProviderName() const override { return "Sentinel Local Test Adapter"; }
    bool Connected() const override { return true; }
    std::vector<NormalizedMessage> Poll(const std::string& conversationId) override {
        std::vector<NormalizedMessage> out;
        for(const auto& m:messages_) if(m.conversationId==conversationId) out.push_back(m);
        return out;
    }
    NormalizedMessage QueueOperatorApproved(const std::string& conversationId,const std::string& text) override {
        static std::atomic<unsigned long long> seq{1};
        NormalizedMessage m;
        m.id="local-"+std::to_string(seq.fetch_add(1));
        m.conversationId=conversationId;
        m.sender="operator";
        m.text=text;
        m.timestamp=std::chrono::system_clock::now();
        m.state=DeliveryState::Queued;
        m.inbound=false;
        messages_.push_back(m);
        return m;
    }
private:
    std::vector<NormalizedMessage> messages_;
};
}
std::unique_ptr<IMessageAdapter> CreateInMemoryMessageAdapter() {
    return std::make_unique<InMemoryAdapter>();
}
}
