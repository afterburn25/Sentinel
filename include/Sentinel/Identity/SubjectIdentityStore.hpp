#pragma once
#include "Sentinel/Core/Types.hpp"
#include "Sentinel/Storage/SqliteDatabase.hpp"
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::identity {

enum class SubjectIdentityStatus : int {
    Lead = 0,
    Partial = 1,
    Confirmed = 2
};

enum class IdentityLeadStatus : int {
    Lead = 0,
    Verified = 1,
    Rejected = 2
};

enum class IdentityResearchType : int {
    PublicRecords = 0,
    SocialProfile = 1,
    Username = 2,
    Contact = 3,
    ImageReference = 4
};

enum class IdentityResearchStatus : int {
    Queued = 0,
    Completed = 1,
    PromotedToLead = 2,
    Rejected = 3
};

enum class IdentityResearchAccessMode : int {
    Manual = 0,
    Portal = 1,
    Api = 2
};

struct IdentityResearchProvider {
    std::string id;
    std::string displayName;
    IdentityResearchAccessMode accessMode{IdentityResearchAccessMode::Manual};
    unsigned int supportedTypesMask{0x1Fu};
    std::string endpointHint;
    std::string credentialReference;
    bool enabled{true};
    std::string notes;
    std::string createdUtc;
    std::string updatedUtc;

    [[nodiscard]] bool Supports(IdentityResearchType type) const noexcept {
        const auto bit=1u<<static_cast<unsigned int>(type);
        return (supportedTypesMask & bit)!=0;
    }
};

struct SubjectRecord {
    SubjectId id;
    CaseId caseId;
    std::string displayName;
    std::string legalName;
    std::string aliases;
    std::string usernames;
    std::string contactIdentifiers;
    std::string notes;
    SubjectIdentityStatus identityStatus{SubjectIdentityStatus::Lead};
    std::string createdUtc;
    std::string updatedUtc;
};

struct IdentityLead {
    SubjectIdentityId id;
    SubjectId subjectId;
    std::string sourceType;
    std::string sourceReference;
    std::string leadValue;
    int confidence{};
    IdentityLeadStatus status{IdentityLeadStatus::Lead};
    std::string provenance;
    std::string reviewer;
    std::string reviewNotes;
    std::string createdUtc;
    std::string reviewedUtc;
};

struct IdentityResearchTask {
    std::string id;
    SubjectId subjectId;
    IdentityResearchType type{IdentityResearchType::PublicRecords};
    std::string provider;
    std::string queryText;
    std::string purpose;
    IdentityResearchStatus status{IdentityResearchStatus::Queued};
    std::string resultSummary;
    std::string resultReference;
    std::string provenance;
    std::string promotedLeadId;
    std::string reviewedBy;
    std::string reviewNotes;
    std::string createdUtc;
    std::string updatedUtc;
    std::string completedUtc;
};

struct SubjectIdentityCounts {
    int subjects{};
    int leads{};
    int verifiedLeads{};
    int confirmedSubjects{};
};

std::string ToString(SubjectIdentityStatus status);
std::string ToString(IdentityLeadStatus status);
std::string ToString(IdentityResearchType type);
std::string ToString(IdentityResearchStatus status);
std::string ToString(IdentityResearchAccessMode mode);

class SubjectIdentityStore {
public:
    explicit SubjectIdentityStore(SqliteDatabase& db):db_(db){}

    SubjectRecord CreateSubject(const CaseId& caseId,std::string displayName);
    void SaveSubject(const SubjectRecord& subject);
    std::optional<SubjectRecord> GetSubject(const SubjectId& id) const;
    std::vector<SubjectRecord> ListForCase(const CaseId& caseId,size_t limit=100) const;
    bool DeleteSubject(const SubjectId& id);
    bool SetSubjectStatus(const SubjectId& id,SubjectIdentityStatus status);

    IdentityLead AddLead(
        const SubjectId& subjectId,
        std::string sourceType,
        std::string sourceReference,
        std::string leadValue,
        int confidence,
        std::string provenance);

    std::optional<IdentityLead> GetLead(const SubjectIdentityId& id) const;
    std::vector<IdentityLead> ListLeads(const SubjectId& subjectId,size_t limit=100) const;
    bool ReviewLead(
        const SubjectIdentityId& id,
        IdentityLeadStatus status,
        std::string reviewer,
        std::string reviewNotes);

    IdentityResearchProvider SaveResearchProvider(
        IdentityResearchProvider provider);
    std::optional<IdentityResearchProvider> GetResearchProvider(
        std::string_view id) const;
    std::vector<IdentityResearchProvider> ListResearchProviders(
        bool includeDisabled=false) const;
    bool SetResearchProviderEnabled(
        std::string_view id,
        bool enabled);

    IdentityResearchTask QueueResearch(
        const SubjectId& subjectId,
        IdentityResearchType type,
        std::string provider,
        std::string queryText,
        std::string purpose);
    std::optional<IdentityResearchTask> GetResearch(std::string_view id) const;
    std::vector<IdentityResearchTask> ListResearch(
        const SubjectId& subjectId,
        size_t limit=100) const;
    bool CompleteResearch(
        std::string_view id,
        std::string resultSummary,
        std::string resultReference,
        std::string provenance);
    bool RejectResearch(
        std::string_view id,
        std::string reviewer,
        std::string reviewNotes);
    IdentityLead PromoteResearchToLead(
        std::string_view id,
        int confidence,
        std::string reviewer,
        std::string reviewNotes);

    std::string BuildResearchReport(std::string_view id) const;

    SubjectIdentityCounts CountsForCase(const CaseId& caseId) const;

private:
    SqliteDatabase& db_;
};

}
