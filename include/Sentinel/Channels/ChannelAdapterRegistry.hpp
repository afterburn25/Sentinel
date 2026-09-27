#pragma once
#include "Sentinel/Channels/ChannelCore.hpp"
#include <memory>
#include <string>
#include <vector>

namespace sentinel::channels {

class ChannelAdapterRegistry {
public:
    void Register(std::unique_ptr<IChannelAdapter> adapter);
    [[nodiscard]] IChannelAdapter* FindByName(std::string_view name) const;
    [[nodiscard]] std::vector<IChannelAdapter*> FindByType(ChannelType type) const;
    [[nodiscard]] std::vector<IChannelAdapter*> All() const;
private:
    std::vector<std::unique_ptr<IChannelAdapter>> adapters_;
};

}
