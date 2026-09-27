#pragma once
#include "Sentinel/Channels/ChannelCore.hpp"
#include "Sentinel/Operations/Messaging.hpp"
#include <memory>

namespace sentinel::channels {

class LocalSimulationChannelAdapter final : public IChannelAdapter {
public:
    explicit LocalSimulationChannelAdapter(std::unique_ptr<operations::IMessageAdapter> legacy)
        : legacy_(std::move(legacy)) {}

    std::string AdapterName() const override { return "SARA Local Simulation"; }
    ChannelType Type() const override { return ChannelType::LocalSimulation; }
    ChannelCapabilities Capabilities() const override;
    bool Connected() const override { return legacy_ && legacy_->Connected(); }
    std::vector<RawChannelEvent> Poll() override { return {}; }
    SendResult SendText(const OutboundText&) override;
    SendResult SendMedia(const OutboundMedia&) override;
    bool MarkRead(std::string_view,std::string_view) override { return false; }
    bool SetTyping(std::string_view,bool) override { return false; }

private:
    std::unique_ptr<operations::IMessageAdapter> legacy_;
};

}
