#include "Sentinel/Identity/IdentityResearchProviderAdapter.hpp"

#include <algorithm>
#include <stdexcept>

namespace sentinel::identity {

void IdentityResearchProviderAdapterRegistry::Register(
    std::unique_ptr<IIdentityResearchProviderAdapter> adapter)
{
    if(!adapter) throw std::runtime_error("identity research adapter is required");
    const auto id=adapter->ProviderId();
    if(id.empty()) throw std::runtime_error("identity research adapter provider id is required");
    if(Find(id)) throw std::runtime_error("identity research adapter provider id is already registered");
    adapters_.push_back(std::move(adapter));
}

IIdentityResearchProviderAdapter* IdentityResearchProviderAdapterRegistry::Find(
    std::string_view providerId) const
{
    auto it=std::find_if(adapters_.begin(),adapters_.end(),[&](const auto& adapter){
        return adapter && adapter->ProviderId()==providerId;
    });
    return it==adapters_.end()?nullptr:it->get();
}

bool IdentityResearchProviderAdapterRegistry::CanExecute(
    std::string_view providerId,
    IdentityResearchType type) const
{
    auto* adapter=Find(providerId);
    return adapter && adapter->Configured() && adapter->Supports(type);
}

size_t IdentityResearchProviderAdapterRegistry::ExecutableCount() const noexcept
{
    return (size_t)std::count_if(adapters_.begin(),adapters_.end(),[](const auto& adapter){
        return adapter && adapter->Configured();
    });
}

std::vector<std::string> IdentityResearchProviderAdapterRegistry::ProviderIds() const
{
    std::vector<std::string> out;
    out.reserve(adapters_.size());
    for(const auto& adapter:adapters_)
        if(adapter) out.push_back(adapter->ProviderId());
    return out;
}

IdentityResearchExecutionResult ManualIdentityResearchProviderAdapter::Execute(
    const IdentityResearchExecutionRequest&)
{
    IdentityResearchExecutionResult result;
    result.completed=false;
    result.error=
        "Manual / Authorized Source requires investigator-performed research; "
        "SARA will not execute it automatically.";
    return result;
}

}
