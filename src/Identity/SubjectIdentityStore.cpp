#include "Sentinel/Identity/SubjectIdentityStore.hpp"
#include <algorithm>
#include <sqlite3.h>
#include <stdexcept>

namespace sentinel::identity {
namespace {

void Bind(sqlite3_stmt* s,int i,const std::string& value) {
    sqlite3_bind_text(s,i,value.c_str(),-1,SQLITE_TRANSIENT);
}

std::string Col(sqlite3_stmt* s,int i) {
    const auto* p=(const char*)sqlite3_column_text(s,i);
    return p?p:"";
}

void CheckPrepare(int rc,sqlite3* db,const char* message) {
    if(rc!=SQLITE_OK) throw std::runtime_error(std::string(message)+": "+sqlite3_errmsg(db));
}

SubjectRecord ReadSubject(sqlite3_stmt* s) {
    auto id=SubjectId::Parse(Col(s,0));
    auto caseId=CaseId::Parse(Col(s,1));
    if(!id || !caseId) throw std::runtime_error("invalid subject identity record UUID");

    SubjectRecord out;
    out.id=*id;
    out.caseId=*caseId;
    out.displayName=Col(s,2);
    out.legalName=Col(s,3);
    out.aliases=Col(s,4);
    out.usernames=Col(s,5);
    out.contactIdentifiers=Col(s,6);
    out.notes=Col(s,7);
    out.identityStatus=(SubjectIdentityStatus)sqlite3_column_int(s,8);
    out.createdUtc=Col(s,9);
    out.updatedUtc=Col(s,10);
    return out;
}

IdentityLead ReadLead(sqlite3_stmt* s) {
    auto id=SubjectIdentityId::Parse(Col(s,0));
    auto subjectId=SubjectId::Parse(Col(s,1));
    if(!id || !subjectId) throw std::runtime_error("invalid identity lead UUID");

    IdentityLead out;
    out.id=*id;
    out.subjectId=*subjectId;
    out.sourceType=Col(s,2);
    out.sourceReference=Col(s,3);
    out.leadValue=Col(s,4);
    out.confidence=sqlite3_column_int(s,5);
    out.status=(IdentityLeadStatus)sqlite3_column_int(s,6);
    out.provenance=Col(s,7);
    out.reviewer=Col(s,8);
    out.reviewNotes=Col(s,9);
    out.createdUtc=Col(s,10);
    out.reviewedUtc=Col(s,11);
    return out;
}

}

std::string ToString(SubjectIdentityStatus status) {
    switch(status) {
        case SubjectIdentityStatus::Lead: return "LEAD";
        case SubjectIdentityStatus::Partial: return "PARTIAL";
        case SubjectIdentityStatus::Confirmed: return "CONFIRMED";
    }
    return "LEAD";
}

std::string ToString(IdentityLeadStatus status) {
    switch(status) {
        case IdentityLeadStatus::Lead: return "LEAD";
        case IdentityLeadStatus::Verified: return "VERIFIED";
        case IdentityLeadStatus::Rejected: return "REJECTED";
    }
    return "LEAD";
}

SubjectRecord SubjectIdentityStore::CreateSubject(const CaseId& caseId,std::string displayName) {
    if(displayName.empty()) throw std::runtime_error("subject display name is required");

    SubjectRecord subject;
    subject.id=SubjectId::Random();
    subject.caseId=caseId;
    subject.displayName=std::move(displayName);
    SaveSubject(subject);
    auto stored=GetSubject(subject.id);
    if(!stored) throw std::runtime_error("subject was saved but could not be reloaded");
    return *stored;
}

void SubjectIdentityStore::SaveSubject(const SubjectRecord& subject) {
    if(subject.displayName.empty()) throw std::runtime_error("subject display name is required");

    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    const char* sql=
        "INSERT INTO subjects("
        "id,case_id,display_name,legal_name,aliases,usernames,contact_identifiers,notes,status"
        ") VALUES(?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "case_id=excluded.case_id,display_name=excluded.display_name,legal_name=excluded.legal_name,"
        "aliases=excluded.aliases,usernames=excluded.usernames,contact_identifiers=excluded.contact_identifiers,"
        "notes=excluded.notes,status=excluded.status,updated_utc=CURRENT_TIMESTAMP";

    CheckPrepare(sqlite3_prepare_v2(db,sql,-1,&s,nullptr),db,"prepare subject save");
    Bind(s,1,subject.id.ToString());
    Bind(s,2,subject.caseId.ToString());
    Bind(s,3,subject.displayName);
    Bind(s,4,subject.legalName);
    Bind(s,5,subject.aliases);
    Bind(s,6,subject.usernames);
    Bind(s,7,subject.contactIdentifiers);
    Bind(s,8,subject.notes);
    sqlite3_bind_int(s,9,(int)subject.identityStatus);

    if(sqlite3_step(s)!=SQLITE_DONE) {
        const std::string error=sqlite3_errmsg(db);
        sqlite3_finalize(s);
        throw std::runtime_error("subject save failed: "+error);
    }
    sqlite3_finalize(s);
}

std::optional<SubjectRecord> SubjectIdentityStore::GetSubject(const SubjectId& id) const {
    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    const char* sql=
        "SELECT id,case_id,display_name,legal_name,aliases,usernames,contact_identifiers,notes,"
        "status,created_utc,updated_utc "
        "FROM subjects WHERE id=? LIMIT 1";
    if(sqlite3_prepare_v2(db,sql,-1,&s,nullptr)!=SQLITE_OK) return std::nullopt;
    Bind(s,1,id.ToString());
    std::optional<SubjectRecord> out;
    if(sqlite3_step(s)==SQLITE_ROW) out=ReadSubject(s);
    sqlite3_finalize(s);
    return out;
}

std::vector<SubjectRecord> SubjectIdentityStore::ListForCase(const CaseId& caseId,size_t limit) const {
    std::vector<SubjectRecord> out;
    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    const char* sql=
        "SELECT id,case_id,display_name,legal_name,aliases,usernames,contact_identifiers,notes,"
        "status,created_utc,updated_utc "
        "FROM subjects WHERE case_id=? "
        "ORDER BY updated_utc DESC,display_name COLLATE NOCASE LIMIT ?";
    if(sqlite3_prepare_v2(db,sql,-1,&s,nullptr)!=SQLITE_OK) return out;
    Bind(s,1,caseId.ToString());
    sqlite3_bind_int(s,2,(int)std::min<size_t>(limit,500));
    while(sqlite3_step(s)==SQLITE_ROW) out.push_back(ReadSubject(s));
    sqlite3_finalize(s);
    return out;
}

bool SubjectIdentityStore::DeleteSubject(const SubjectId& id) {
    auto* db=db_.Handle();
    SqliteTransaction tx(db_);

    sqlite3_stmt* identities{};
    if(sqlite3_prepare_v2(db,"DELETE FROM subject_identities WHERE subject_id=?",-1,&identities,nullptr)!=SQLITE_OK)
        return false;
    Bind(identities,1,id.ToString());
    if(sqlite3_step(identities)!=SQLITE_DONE) {
        sqlite3_finalize(identities);
        return false;
    }
    sqlite3_finalize(identities);

    sqlite3_stmt* subject{};
    if(sqlite3_prepare_v2(db,"DELETE FROM subjects WHERE id=?",-1,&subject,nullptr)!=SQLITE_OK)
        return false;
    Bind(subject,1,id.ToString());
    const bool ok=sqlite3_step(subject)==SQLITE_DONE && sqlite3_changes(db)>0;
    sqlite3_finalize(subject);
    if(ok) tx.Commit();
    return ok;
}

bool SubjectIdentityStore::SetSubjectStatus(const SubjectId& id,SubjectIdentityStatus status) {
    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    if(sqlite3_prepare_v2(db,
        "UPDATE subjects SET status=?,updated_utc=CURRENT_TIMESTAMP WHERE id=?",
        -1,&s,nullptr)!=SQLITE_OK) return false;
    sqlite3_bind_int(s,1,(int)status);
    Bind(s,2,id.ToString());
    const bool ok=sqlite3_step(s)==SQLITE_DONE && sqlite3_changes(db)>0;
    sqlite3_finalize(s);
    return ok;
}

IdentityLead SubjectIdentityStore::AddLead(
    const SubjectId& subjectId,
    std::string sourceType,
    std::string sourceReference,
    std::string leadValue,
    int confidence,
    std::string provenance)
{
    if(leadValue.empty()) throw std::runtime_error("identity lead value is required");
    confidence=std::clamp(confidence,0,100);

    IdentityLead lead;
    lead.id=SubjectIdentityId::Random();
    lead.subjectId=subjectId;
    lead.sourceType=std::move(sourceType);
    lead.sourceReference=std::move(sourceReference);
    lead.leadValue=std::move(leadValue);
    lead.confidence=confidence;
    lead.provenance=std::move(provenance);

    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    const char* sql=
        "INSERT INTO subject_identities("
        "id,subject_id,identity_type,provider,external_id,display_value,link_state,confidence,source_event_id,"
        "source_reference,provenance,reviewed_by,review_notes,reviewed_utc"
        ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    CheckPrepare(sqlite3_prepare_v2(db,sql,-1,&s,nullptr),db,"prepare identity lead insert");
    Bind(s,1,lead.id.ToString());
    Bind(s,2,subjectId.ToString());
    Bind(s,3,lead.sourceType.empty()?"investigator-lead":lead.sourceType);
    Bind(s,4,"sara-case");
    Bind(s,5,lead.id.ToString());
    Bind(s,6,lead.leadValue);
    sqlite3_bind_int(s,7,(int)lead.status);
    sqlite3_bind_double(s,8,(double)lead.confidence/100.0);
    Bind(s,9,"");
    Bind(s,10,lead.sourceReference);
    Bind(s,11,lead.provenance);
    Bind(s,12,lead.reviewer);
    Bind(s,13,lead.reviewNotes);
    Bind(s,14,lead.reviewedUtc);
    if(sqlite3_step(s)!=SQLITE_DONE) {
        const std::string error=sqlite3_errmsg(db);
        sqlite3_finalize(s);
        throw std::runtime_error("identity lead insert failed: "+error);
    }
    sqlite3_finalize(s);

    auto stored=GetLead(lead.id);
    if(!stored) throw std::runtime_error("identity lead was saved but could not be reloaded");
    return *stored;
}

std::optional<IdentityLead> SubjectIdentityStore::GetLead(const SubjectIdentityId& id) const {
    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    const char* sql=
        "SELECT id,subject_id,identity_type,source_reference,display_value,"
        "CAST(ROUND(confidence*100.0) AS INTEGER),link_state,provenance,"
        "reviewed_by,review_notes,created_utc,reviewed_utc "
        "FROM subject_identities WHERE id=? LIMIT 1";
    if(sqlite3_prepare_v2(db,sql,-1,&s,nullptr)!=SQLITE_OK) return std::nullopt;
    Bind(s,1,id.ToString());
    std::optional<IdentityLead> out;
    if(sqlite3_step(s)==SQLITE_ROW) out=ReadLead(s);
    sqlite3_finalize(s);
    return out;
}

std::vector<IdentityLead> SubjectIdentityStore::ListLeads(const SubjectId& subjectId,size_t limit) const {
    std::vector<IdentityLead> out;
    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    const char* sql=
        "SELECT id,subject_id,identity_type,source_reference,display_value,"
        "CAST(ROUND(confidence*100.0) AS INTEGER),link_state,provenance,"
        "reviewed_by,review_notes,created_utc,reviewed_utc "
        "FROM subject_identities WHERE subject_id=? "
        "ORDER BY link_state ASC,confidence DESC,created_utc DESC LIMIT ?";
    if(sqlite3_prepare_v2(db,sql,-1,&s,nullptr)!=SQLITE_OK) return out;
    Bind(s,1,subjectId.ToString());
    sqlite3_bind_int(s,2,(int)std::min<size_t>(limit,500));
    while(sqlite3_step(s)==SQLITE_ROW) out.push_back(ReadLead(s));
    sqlite3_finalize(s);
    return out;
}

bool SubjectIdentityStore::ReviewLead(
    const SubjectIdentityId& id,
    IdentityLeadStatus status,
    std::string reviewer,
    std::string reviewNotes)
{
    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    const char* sql=
        "UPDATE subject_identities SET link_state=?,reviewed_by=?,review_notes=?,"
        "reviewed_utc=CURRENT_TIMESTAMP,"
        "confirmed_by=CASE WHEN ?=1 THEN ? ELSE NULL END,"
        "confirmed_utc=CASE WHEN ?=1 THEN CURRENT_TIMESTAMP ELSE NULL END "
        "WHERE id=?";
    if(sqlite3_prepare_v2(db,sql,-1,&s,nullptr)!=SQLITE_OK) return false;
    sqlite3_bind_int(s,1,(int)status);
    Bind(s,2,reviewer);
    Bind(s,3,reviewNotes);
    sqlite3_bind_int(s,4,(int)status);
    Bind(s,5,reviewer);
    sqlite3_bind_int(s,6,(int)status);
    Bind(s,7,id.ToString());
    const bool ok=sqlite3_step(s)==SQLITE_DONE && sqlite3_changes(db)>0;
    sqlite3_finalize(s);
    return ok;
}

SubjectIdentityCounts SubjectIdentityStore::CountsForCase(const CaseId& caseId) const {
    SubjectIdentityCounts counts;
    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    const char* sql=
        "SELECT "
        "(SELECT COUNT(*) FROM subjects WHERE case_id=?),"
        "(SELECT COUNT(*) FROM subject_identities l "
        " JOIN subjects s2 ON s2.id=l.subject_id WHERE s2.case_id=?),"
        "(SELECT COUNT(*) FROM subject_identities l "
        " JOIN subjects s3 ON s3.id=l.subject_id WHERE s3.case_id=? AND l.link_state=1),"
        "(SELECT COUNT(*) FROM subjects WHERE case_id=? AND status=2)";
    if(sqlite3_prepare_v2(db,sql,-1,&s,nullptr)!=SQLITE_OK) return counts;
    const std::string id=caseId.ToString();
    for(int i=1;i<=4;i++) Bind(s,i,id);
    if(sqlite3_step(s)==SQLITE_ROW) {
        counts.subjects=sqlite3_column_int(s,0);
        counts.leads=sqlite3_column_int(s,1);
        counts.verifiedLeads=sqlite3_column_int(s,2);
        counts.confirmedSubjects=sqlite3_column_int(s,3);
    }
    sqlite3_finalize(s);
    return counts;
}

}
