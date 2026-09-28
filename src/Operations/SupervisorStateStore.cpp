#include "Sentinel/Operations/SupervisorStateStore.hpp"
#include <sqlite3.h>
#include <stdexcept>

namespace sentinel::operations {
namespace {

void Bind(sqlite3_stmt* s,int index,const std::string& value) {
    sqlite3_bind_text(s,index,value.c_str(),-1,SQLITE_TRANSIENT);
}

std::string Col(sqlite3_stmt* s,int index) {
    const auto* value=reinterpret_cast<const char*>(sqlite3_column_text(s,index));
    return value?value:"";
}

SupervisorControlState ReadState(sqlite3_stmt* s) {
    SupervisorControlState state;
    state.operationKey=Col(s,0);
    state.investigatorTakeover=sqlite3_column_int(s,1)!=0;
    state.takeoverActor=Col(s,2);
    state.takeoverNote=Col(s,3);
    state.takeoverUtc=Col(s,4);
    state.releasedBy=Col(s,5);
    state.releasedUtc=Col(s,6);
    state.updatedUtc=Col(s,7);
    return state;
}

}

std::optional<SupervisorControlState>
SupervisorStateStore::Get(std::string_view operationKey) const {
    if(operationKey.empty()) return std::nullopt;

    sqlite3_stmt* s{};
    const char* sql=
        "SELECT operation_key,investigator_takeover,takeover_actor,takeover_note,"
        "takeover_utc,released_by,released_utc,updated_utc "
        "FROM operation_supervisor_state WHERE operation_key=? LIMIT 1";
    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK)
        return std::nullopt;

    Bind(s,1,std::string(operationKey));
    std::optional<SupervisorControlState> out;
    if(sqlite3_step(s)==SQLITE_ROW) out=ReadState(s);
    sqlite3_finalize(s);
    return out;
}

SupervisorControlState SupervisorStateStore::SetTakeover(
    std::string operationKey,
    bool active,
    std::string actor,
    std::string note)
{
    if(operationKey.empty()) throw std::runtime_error("supervisor operation key is required");
    if(actor.empty()) throw std::runtime_error("supervisor actor is required");

    sqlite3_stmt* s{};
    const char* sql=active
        ? "INSERT INTO operation_supervisor_state("
          "operation_key,investigator_takeover,takeover_actor,takeover_note,takeover_utc,"
          "released_by,released_utc,updated_utc"
          ") VALUES(?,1,?,?,CURRENT_TIMESTAMP,'','',CURRENT_TIMESTAMP) "
          "ON CONFLICT(operation_key) DO UPDATE SET "
          "investigator_takeover=1,takeover_actor=excluded.takeover_actor,"
          "takeover_note=excluded.takeover_note,takeover_utc=CURRENT_TIMESTAMP,"
          "released_by='',released_utc='',updated_utc=CURRENT_TIMESTAMP"
        : "INSERT INTO operation_supervisor_state("
          "operation_key,investigator_takeover,takeover_actor,takeover_note,takeover_utc,"
          "released_by,released_utc,updated_utc"
          ") VALUES(?,0,'','','',?,CURRENT_TIMESTAMP,CURRENT_TIMESTAMP) "
          "ON CONFLICT(operation_key) DO UPDATE SET "
          "investigator_takeover=0,released_by=excluded.released_by,"
          "released_utc=CURRENT_TIMESTAMP,updated_utc=CURRENT_TIMESTAMP";

    if(sqlite3_prepare_v2(db_.Handle(),sql,-1,&s,nullptr)!=SQLITE_OK)
        throw std::runtime_error(std::string("prepare supervisor takeover update failed: ")+
            sqlite3_errmsg(db_.Handle()));

    Bind(s,1,operationKey);
    if(active) {
        Bind(s,2,actor);
        Bind(s,3,note);
    } else {
        Bind(s,2,actor);
    }

    if(sqlite3_step(s)!=SQLITE_DONE) {
        const std::string error=sqlite3_errmsg(db_.Handle());
        sqlite3_finalize(s);
        throw std::runtime_error("supervisor takeover update failed: "+error);
    }
    sqlite3_finalize(s);

    auto saved=Get(operationKey);
    if(!saved) throw std::runtime_error("supervisor takeover state was saved but could not be reloaded");
    return *saved;
}

}
