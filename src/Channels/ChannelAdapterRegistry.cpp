#include "Sentinel/Channels/ChannelAdapterRegistry.hpp"

namespace sentinel::channels {

void ChannelAdapterRegistry::Register(std::unique_ptr<IChannelAdapter> adapter) {
    if(!adapter) return;
    for(auto& existing:adapters_) {
        if(existing->AdapterName()==adapter->AdapterName()) {
            existing=std::move(adapter);
            return;
        }
    }
    adapters_.push_back(std::move(adapter));
}

IChannelAdapter* ChannelAdapterRegistry::FindByName(std::string_view name) const {
    for(const auto& a:adapters_) if(a->AdapterName()==name) return a.get();
    return nullptr;
}

std::vector<IChannelAdapter*> ChannelAdapterRegistry::FindByType(ChannelType type) const {
    std::vector<IChannelAdapter*> out;
    for(const auto& a:adapters_) if(a->Type()==type) out.push_back(a.get());
    return out;
}

std::vector<IChannelAdapter*> ChannelAdapterRegistry::All() const {
    std::vector<IChannelAdapter*> out;
    out.reserve(adapters_.size());
    for(const auto& a:adapters_) out.push_back(a.get());
    return out;
}

}
