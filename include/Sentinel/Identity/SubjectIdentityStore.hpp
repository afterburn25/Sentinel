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

struct SubjectIdentityCounts {
    int subjects{};
    int leads{};
    int verifiedLeads{};
    int confirmedSubjects{};
};

std::string ToString(SubjectIdentityStatus status);
std::string ToString(IdentityLeadStatus status);

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

    SubjectIdentityCounts CountsForCase(const CaseId& caseId) const;

private:
    SqliteDatabase& db_;
};

}
