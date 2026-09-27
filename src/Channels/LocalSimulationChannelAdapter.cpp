#include "Sentinel/Channels/LocalSimulationChannelAdapter.hpp"

namespace sentinel::channels {

ChannelCapabilities LocalSimulationChannelAdapter::Capabilities() const {
    ChannelCapabilities c;
    c.Set(Capability::ReceiveText);
    c.Set(Capability::SendText);
    c.Set(Capability::SendImage);
    c.Set(Capability::AutomatedSending);
    return c;
}

SendResult LocalSimulationChannelAdapter::SendText(const OutboundText& out) {
    if(!legacy_) return {false,{},"legacy adapter unavailable"};
    auto m=legacy_->QueueOperatorApproved(out.conversationId,out.text);
    return {true,m.id,{}};
}

SendResult LocalSimulationChannelAdapter::SendMedia(const OutboundMedia& out) {
    if(!legacy_) return {false,{},"legacy adapter unavailable"};
    auto m=legacy_->QueueOperatorApprovedMedia(
        out.conversationId,out.caption,out.localPath,out.sha256);
    return {true,m.id,{}};
}

}
