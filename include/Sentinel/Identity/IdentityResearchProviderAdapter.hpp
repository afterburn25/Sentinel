#pragma once

#include "Sentinel/Identity/SubjectIdentityStore.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::identity {

struct IdentityResearchExecutionRequest {
    std::string taskId;
    SubjectId subjectId;
    IdentityResearchType type{IdentityResearchType::PublicRecords};
    std::string providerId;
    std::string queryText;
    std::string purpose;
};

struct IdentityResearchExecutionResult {
    bool completed{};
    std::string resultSummary;
    std::string resultReference;
    std::string provenance;
    std::string error;
};

class IIdentityResearchProviderAdapter {
public:
    virtual ~IIdentityResearchProviderAdapter()=default;
    virtual std::string ProviderId() const=0;
    virtual bool Configured() const noexcept=0;
    virtual bool Supports(IdentityResearchType type) const noexcept=0;
    virtual IdentityResearchExecutionResult Execute(
        const IdentityResearchExecutionRequest& request)=0;
};

class IdentityResearchProviderAdapterRegistry {
public:
    void Register(std::unique_ptr<IIdentityResearchProviderAdapter> adapter);
    IIdentityResearchProviderAdapter* Find(std::string_view providerId) const;
    bool CanExecute(
        std::string_view providerId,
        IdentityResearchType type) const;
    size_t ExecutableCount() const noexcept;
    std::vector<std::string> ProviderIds() const;

private:
    std::vector<std::unique_ptr<IIdentityResearchProviderAdapter>> adapters_;
};

class ManualIdentityResearchProviderAdapter final
    : public IIdentityResearchProviderAdapter {
public:
    std::string ProviderId() const override { return "manual/authorized"; }
    bool Configured() const noexcept override { return false; }
    bool Supports(IdentityResearchType) const noexcept override { return true; }
    IdentityResearchExecutionResult Execute(
        const IdentityResearchExecutionRequest&) override;
};

}
