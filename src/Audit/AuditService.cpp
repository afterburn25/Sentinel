#include "Sentinel/Audit/AuditService.hpp"

#include <stdexcept>

namespace sentinel {
namespace {

std::vector<std::byte> BytesOf(const std::string& value)
{
    return {
        reinterpret_cast<const std::byte*>(value.data()),
        reinterpret_cast<const std::byte*>(value.data()+value.size())
    };
}

std::vector<std::byte> BlobBytes(sqlite3_stmt* stmt,int column)
{
    const auto size=sqlite3_column_bytes(stmt,column);
    const auto* data=static_cast<const std::byte*>(sqlite3_column_blob(stmt,column));
    if(!data || size<=0) return {};
    return {data,data+size};
}

std::string TextColumn(sqlite3_stmt* stmt,int column)
{
    const auto* value=static_cast<const char*>(sqlite3_column_text(stmt,column));
    return value?value:"";
}

}

Hash256 AuditService::GetCurrentHead()
{
    sqlite3_stmt* stmt{};
    if(sqlite3_prepare_v2(
        db_.Handle(),
        "SELECT record_hash FROM audit_records ORDER BY sequence DESC LIMIT 1",
        -1,&stmt,nullptr)!=SQLITE_OK)
        throw std::runtime_error("prepare audit head failed");

    Hash256 head{};
    if(sqlite3_step(stmt)==SQLITE_ROW) {
        if(auto value=Hash256::FromHex(TextColumn(stmt,0)))
            head=*value;
    }
    sqlite3_finalize(stmt);
    return head;
}

AuditId AuditService::Append(const AuditEvent& event)
{
    sqlite3_stmt* stmt{};
    if(sqlite3_prepare_v2(
        db_.Handle(),
        "SELECT COALESCE(MAX(sequence),0),"
        "COALESCE((SELECT record_hash FROM audit_records ORDER BY sequence DESC LIMIT 1),"
        "'0000000000000000000000000000000000000000000000000000000000000000') "
        "FROM audit_records",
        -1,&stmt,nullptr)!=SQLITE_OK)
        throw std::runtime_error("prepare audit sequence failed");

    uint64_t sequence=1;
    std::string previous(64,'0');
    if(sqlite3_step(stmt)==SQLITE_ROW) {
        sequence=(uint64_t)sqlite3_column_int64(stmt,0)+1;
        previous=TextColumn(stmt,1);
    }
    sqlite3_finalize(stmt);

    const auto id=AuditId::Random();
    const auto timestamp=ToIso8601Utc(NowUtc());
    const auto actor=event.actor.ToString();
    const std::string target=event.targetId.value_or("");
    const auto metadataHash=hash_.Sha256(
        std::span<const std::byte>(event.metadata.data(),event.metadata.size())).ToHex();

    // audit-v2 adds a digest of the metadata blob to the record canonical form.
    // Records created before migration 0027 keep metadata_sha256='' and continue
    // to validate against their original audit-v1 canonical form.
    const std::string canonical=
        "audit-v2|"+std::to_string(sequence)+"|"+timestamp+"|"+actor+"|"+
        std::to_string((uint32_t)event.action)+"|"+event.targetType+"|"+
        target+"|"+metadataHash+"|"+previous;
    const auto recordHash=hash_.Sha256(BytesOf(canonical)).ToHex();

    const char* sql=
        "INSERT INTO audit_records("
        "id,sequence,timestamp,actor_id,action,target_type,target_id,metadata,"
        "previous_hash,record_hash,metadata_sha256"
        ") VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&stmt,nullptr)!=SQLITE_OK)
        throw std::runtime_error("prepare audit insert failed");

    const auto idText=id.ToString();
    sqlite3_bind_text(stmt,1,idText.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt,2,(sqlite3_int64)sequence);
    sqlite3_bind_text(stmt,3,timestamp.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,4,actor.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt,5,(int)event.action);
    sqlite3_bind_text(stmt,6,event.targetType.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,7,target.c_str(),-1,SQLITE_TRANSIENT);
    if(event.metadata.empty())
        sqlite3_bind_blob(stmt,8,"",0,SQLITE_STATIC);
    else
        sqlite3_bind_blob(
            stmt,8,event.metadata.data(),(int)event.metadata.size(),SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,9,previous.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,10,recordHash.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,11,metadataHash.c_str(),-1,SQLITE_TRANSIENT);

    if(sqlite3_step(stmt)!=SQLITE_DONE) {
        const std::string error=sqlite3_errmsg(db_.Handle());
        sqlite3_finalize(stmt);
        throw std::runtime_error("audit insert failed: "+error);
    }
    sqlite3_finalize(stmt);
    return id;
}

bool AuditService::VerifyChain()
{
    sqlite3_stmt* stmt{};
    const char* sql=
        "SELECT sequence,timestamp,actor_id,action,target_type,target_id,"
        "metadata,previous_hash,record_hash,COALESCE(metadata_sha256,'') "
        "FROM audit_records ORDER BY sequence ASC";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&stmt,nullptr)!=SQLITE_OK)
        return false;

    std::string previous(64,'0');
    uint64_t expected=1;
    bool valid=true;

    while(sqlite3_step(stmt)==SQLITE_ROW) {
        const uint64_t sequence=(uint64_t)sqlite3_column_int64(stmt,0);
        const auto timestamp=TextColumn(stmt,1);
        const auto actor=TextColumn(stmt,2);
        const int action=sqlite3_column_int(stmt,3);
        const auto targetType=TextColumn(stmt,4);
        const auto targetId=TextColumn(stmt,5);
        const auto metadata=BlobBytes(stmt,6);
        const auto storedPrevious=TextColumn(stmt,7);
        const auto storedHash=TextColumn(stmt,8);
        const auto storedMetadataHash=TextColumn(stmt,9);

        if(sequence!=expected || storedPrevious!=previous) {
            valid=false;
            break;
        }

        std::string canonical;
        if(storedMetadataHash.empty()) {
            canonical=
                "audit-v1|"+std::to_string(sequence)+"|"+timestamp+"|"+actor+"|"+
                std::to_string(action)+"|"+targetType+"|"+targetId+"|"+previous;
        } else {
            const auto actualMetadataHash=hash_.Sha256(
                std::span<const std::byte>(metadata.data(),metadata.size())).ToHex();
            if(actualMetadataHash!=storedMetadataHash) {
                valid=false;
                break;
            }
            canonical=
                "audit-v2|"+std::to_string(sequence)+"|"+timestamp+"|"+actor+"|"+
                std::to_string(action)+"|"+targetType+"|"+targetId+"|"+
                storedMetadataHash+"|"+previous;
        }

        const auto calculated=hash_.Sha256(BytesOf(canonical)).ToHex();
        if(calculated!=storedHash) {
            valid=false;
            break;
        }

        previous=storedHash;
        ++expected;
    }

    sqlite3_finalize(stmt);
    return valid;
}

}
