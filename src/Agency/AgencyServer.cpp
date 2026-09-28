#include "Sentinel/Agency/AgencyServer.hpp"
#include "Sentinel/Core/Types.hpp"

#include <algorithm>
#include <sqlite3.h>
#include <stdexcept>

namespace sentinel::agency {
namespace {

void Bind(sqlite3_stmt* stmt,int index,const std::string& value)
{
    sqlite3_bind_text(stmt,index,value.c_str(),-1,SQLITE_TRANSIENT);
}

std::string Col(sqlite3_stmt* stmt,int index)
{
    const auto* value=reinterpret_cast<const char*>(sqlite3_column_text(stmt,index));
    return value?value:"";
}

}

void AgencySyncQueue::Enqueue(SyncQueueItem item)
{
    items_.push_back(std::move(item));
}

const std::vector<SyncQueueItem>& AgencySyncQueue::Items() const
{
    return items_;
}

size_t AgencySyncQueue::PendingCount() const
{
    size_t count=0;
    for(const auto& item:items_) if(!item.complete) ++count;
    return count;
}

AgencyServerConfig AgencyServerStore::LoadConfig() const
{
    AgencyServerConfig config;
    config.workstationId="local-workstation";

    sqlite3_stmt* stmt{};
    if(sqlite3_prepare_v2(
        db_.Handle(),
        "SELECT endpoint,agency_id,workstation_id,enabled "
        "FROM agency_server_config WHERE singleton_id=1",
        -1,&stmt,nullptr)!=SQLITE_OK)
        return config;

    if(sqlite3_step(stmt)==SQLITE_ROW) {
        config.endpoint=Col(stmt,0);
        config.agencyId=Col(stmt,1);
        config.workstationId=Col(stmt,2);
        config.enabled=sqlite3_column_int(stmt,3)!=0;
        if(config.workstationId.empty()) config.workstationId="local-workstation";
    }
    sqlite3_finalize(stmt);
    return config;
}

void AgencyServerStore::SaveConfig(const AgencyServerConfig& config)
{
    sqlite3_stmt* stmt{};
    const char* sql=
        "INSERT INTO agency_server_config("
        "singleton_id,endpoint,agency_id,workstation_id,enabled,updated_utc"
        ") VALUES(1,?1,?2,?3,?4,CURRENT_TIMESTAMP) "
        "ON CONFLICT(singleton_id) DO UPDATE SET "
        "endpoint=excluded.endpoint,agency_id=excluded.agency_id,"
        "workstation_id=excluded.workstation_id,enabled=excluded.enabled,"
        "updated_utc=CURRENT_TIMESTAMP";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&stmt,nullptr)!=SQLITE_OK)
        throw std::runtime_error("prepare agency configuration save failed");

    Bind(stmt,1,config.endpoint);
    Bind(stmt,2,config.agencyId);
    Bind(stmt,3,config.workstationId.empty()?"local-workstation":config.workstationId);
    sqlite3_bind_int(stmt,4,config.enabled?1:0);

    if(sqlite3_step(stmt)!=SQLITE_DONE) {
        const std::string error=sqlite3_errmsg(db_.Handle());
        sqlite3_finalize(stmt);
        throw std::runtime_error("agency configuration save failed: "+error);
    }
    sqlite3_finalize(stmt);
}

SyncQueueItem AgencyServerStore::Enqueue(SyncQueueItem item)
{
    if(item.id.empty()) item.id=Uuid::Random().ToString();

    sqlite3_stmt* stmt{};
    const char* sql=
        "INSERT INTO agency_sync_queue("
        "id,item_type,local_reference,attempts,complete,created_utc"
        ") VALUES(?1,?2,?3,?4,?5,CURRENT_TIMESTAMP)";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&stmt,nullptr)!=SQLITE_OK)
        throw std::runtime_error("prepare agency sync queue insert failed");

    Bind(stmt,1,item.id);
    sqlite3_bind_int(stmt,2,(int)item.type);
    Bind(stmt,3,item.localReference);
    sqlite3_bind_int(stmt,4,std::max(0,item.attempts));
    sqlite3_bind_int(stmt,5,item.complete?1:0);

    if(sqlite3_step(stmt)!=SQLITE_DONE) {
        const std::string error=sqlite3_errmsg(db_.Handle());
        sqlite3_finalize(stmt);
        throw std::runtime_error("agency sync queue insert failed: "+error);
    }
    sqlite3_finalize(stmt);
    return item;
}

std::vector<SyncQueueItem> AgencyServerStore::Items(size_t limit) const
{
    std::vector<SyncQueueItem> out;
    sqlite3_stmt* stmt{};
    if(sqlite3_prepare_v2(
        db_.Handle(),
        "SELECT id,item_type,local_reference,attempts,complete "
        "FROM agency_sync_queue ORDER BY created_utc DESC,id DESC LIMIT ?1",
        -1,&stmt,nullptr)!=SQLITE_OK)
        return out;

    sqlite3_bind_int(stmt,1,(int)std::clamp<size_t>(limit,1,500));
    while(sqlite3_step(stmt)==SQLITE_ROW) {
        SyncQueueItem item;
        item.id=Col(stmt,0);
        item.type=(SyncItemType)sqlite3_column_int(stmt,1);
        item.localReference=Col(stmt,2);
        item.attempts=sqlite3_column_int(stmt,3);
        item.complete=sqlite3_column_int(stmt,4)!=0;
        out.push_back(std::move(item));
    }
    sqlite3_finalize(stmt);
    return out;
}

size_t AgencyServerStore::PendingCount() const
{
    sqlite3_stmt* stmt{};
    size_t count=0;
    if(sqlite3_prepare_v2(
        db_.Handle(),
        "SELECT COUNT(*) FROM agency_sync_queue WHERE complete=0",
        -1,&stmt,nullptr)==SQLITE_OK &&
       sqlite3_step(stmt)==SQLITE_ROW)
        count=(size_t)sqlite3_column_int64(stmt,0);
    sqlite3_finalize(stmt);
    return count;
}

bool AgencyServerStore::MarkComplete(std::string_view id)
{
    sqlite3_stmt* stmt{};
    if(sqlite3_prepare_v2(
        db_.Handle(),
        "UPDATE agency_sync_queue SET complete=1,completed_utc=CURRENT_TIMESTAMP "
        "WHERE id=?1 AND complete=0",
        -1,&stmt,nullptr)!=SQLITE_OK)
        return false;

    Bind(stmt,1,std::string(id));
    const bool ok=sqlite3_step(stmt)==SQLITE_DONE && sqlite3_changes(db_.Handle())==1;
    sqlite3_finalize(stmt);
    return ok;
}

}
