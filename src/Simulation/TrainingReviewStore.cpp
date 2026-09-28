#include "Sentinel/Simulation/TrainingReviewStore.hpp"

#include <sqlite3.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sentinel::simulation {
namespace {

void Check(int rc, sqlite3* db, const char* what) {
    if(rc==SQLITE_OK || rc==SQLITE_ROW || rc==SQLITE_DONE) return;
    throw std::runtime_error(std::string(what)+": "+(db?sqlite3_errmsg(db):"sqlite error"));
}

std::string Col(sqlite3_stmt* s,int i) {
    const auto* p=reinterpret_cast<const char*>(sqlite3_column_text(s,i));
    return p?p:"";
}

std::string NewId() {
    const auto now=std::chrono::high_resolution_clock::now().time_since_epoch().count();
    static unsigned long long seq=0;
    std::ostringstream out;
    out<<"train-"<<std::hex<<now<<"-"<<++seq;
    return out.str();
}

std::string JsonEscape(std::string_view input) {
    std::ostringstream out;
    for(unsigned char c:input) {
        switch(c) {
            case '\\': out<<"\\\\"; break;
            case '"': out<<"\\\""; break;
            case '\n': out<<"\\n"; break;
            case '\r': out<<"\\r"; break;
            case '\t': out<<"\\t"; break;
            default:
                if(c<0x20) {
                    out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<(int)c<<std::dec;
                } else out<<(char)c;
        }
    }
    return out.str();
}

TrainingReviewItem ReadItem(sqlite3_stmt* s) {
    TrainingReviewItem item;
    item.id=Col(s,0);
    item.sourceLogId=Col(s,1);
    item.conversationId=Col(s,2);
    item.personaName=Col(s,3);
    item.modelName=Col(s,4);
    item.inputText=Col(s,5);
    item.outputText=Col(s,6);
    item.personaSummary=Col(s,7);
    item.recalledMemory=Col(s,8);
    item.contextJson=Col(s,9);
    item.status=(TrainingReviewStatus)sqlite3_column_int(s,10);
    item.reviewer=Col(s,11);
    item.notes=Col(s,12);
    item.createdUtc=Col(s,13);
    item.reviewedUtc=Col(s,14);
    return item;
}

}

std::string ToString(TrainingReviewStatus status) {
    switch(status) {
        case TrainingReviewStatus::Pending: return "PENDING";
        case TrainingReviewStatus::Approved: return "APPROVED";
        case TrainingReviewStatus::Rejected: return "REJECTED";
    }
    return "PENDING";
}

std::optional<TrainingReviewItem> TrainingReviewStore::StageLatestReply(std::string_view conversationId) {
    if(conversationId.empty()) return std::nullopt;
    auto* db=db_.Handle();

    sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "SELECT id,conversation_id,persona_name,model_name,input_text,output_text,"
        "persona_summary,recalled_memory,context_json "
        "FROM persona_conversation_logs "
        "WHERE conversation_id=? AND output_text<>'' "
        "AND event_kind IN ('reactive_reply','proactive_follow_up') "
        "ORDER BY created_utc DESC,rowid DESC LIMIT 1",
        -1,&s,nullptr),db,"prepare latest persona log");
    sqlite3_bind_text(s,1,std::string(conversationId).c_str(),-1,SQLITE_TRANSIENT);

    if(sqlite3_step(s)!=SQLITE_ROW) {
        sqlite3_finalize(s);
        return std::nullopt;
    }

    const std::string sourceId=Col(s,0);
    const std::string conversation=Col(s,1);
    const std::string persona=Col(s,2);
    const std::string model=Col(s,3);
    const std::string input=Col(s,4);
    const std::string output=Col(s,5);
    const std::string summary=Col(s,6);
    const std::string memory=Col(s,7);
    const std::string context=Col(s,8);
    sqlite3_finalize(s);

    Check(sqlite3_prepare_v2(db,
        "SELECT id,source_log_id,conversation_id,persona_name,model_name,input_text,output_text,"
        "persona_summary,recalled_memory,context_json,status,reviewer,notes,created_utc,reviewed_utc "
        "FROM training_review_items WHERE source_log_id=? LIMIT 1",
        -1,&s,nullptr),db,"prepare existing training item");
    sqlite3_bind_text(s,1,sourceId.c_str(),-1,SQLITE_TRANSIENT);
    if(sqlite3_step(s)==SQLITE_ROW) {
        auto item=ReadItem(s);
        sqlite3_finalize(s);
        return item;
    }
    sqlite3_finalize(s);

    const auto id=NewId();
    Check(sqlite3_prepare_v2(db,
        "INSERT INTO training_review_items("
        "id,source_log_id,conversation_id,persona_name,model_name,input_text,output_text,"
        "persona_summary,recalled_memory,context_json,status"
        ") VALUES(?,?,?,?,?,?,?,?,?,?,0)",
        -1,&s,nullptr),db,"prepare training item insert");
    const std::string values[]={id,sourceId,conversation,persona,model,input,output,summary,memory,context};
    for(int i=0;i<10;i++) sqlite3_bind_text(s,i+1,values[i].c_str(),-1,SQLITE_TRANSIENT);
    Check(sqlite3_step(s),db,"insert training review item");
    sqlite3_finalize(s);
    return Get(id);
}

std::optional<TrainingReviewItem> TrainingReviewStore::Get(std::string_view id) const {
    if(id.empty()) return std::nullopt;
    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "SELECT id,source_log_id,conversation_id,persona_name,model_name,input_text,output_text,"
        "persona_summary,recalled_memory,context_json,status,reviewer,notes,created_utc,reviewed_utc "
        "FROM training_review_items WHERE id=? LIMIT 1",
        -1,&s,nullptr),db,"prepare training review get");
    sqlite3_bind_text(s,1,std::string(id).c_str(),-1,SQLITE_TRANSIENT);
    std::optional<TrainingReviewItem> out;
    if(sqlite3_step(s)==SQLITE_ROW) out=ReadItem(s);
    sqlite3_finalize(s);
    return out;
}

bool TrainingReviewStore::Review(
    std::string_view id,
    TrainingReviewStatus status,
    std::string_view reviewer,
    std::string_view notes)
{
    if(id.empty() || status==TrainingReviewStatus::Pending) return false;
    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "UPDATE training_review_items SET status=?,reviewer=?,notes=?,reviewed_utc=CURRENT_TIMESTAMP "
        "WHERE id=?",
        -1,&s,nullptr),db,"prepare training review update");
    sqlite3_bind_int(s,1,(int)status);
    sqlite3_bind_text(s,2,std::string(reviewer).c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,3,std::string(notes).c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,4,std::string(id).c_str(),-1,SQLITE_TRANSIENT);
    Check(sqlite3_step(s),db,"update training review item");
    const bool changed=sqlite3_changes(db)>0;
    sqlite3_finalize(s);
    return changed;
}

TrainingReviewCounts TrainingReviewStore::Counts() const {
    TrainingReviewCounts out;
    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "SELECT status,COUNT(*) FROM training_review_items GROUP BY status",
        -1,&s,nullptr),db,"prepare training review counts");
    while(sqlite3_step(s)==SQLITE_ROW) {
        const auto status=(TrainingReviewStatus)sqlite3_column_int(s,0);
        const int count=sqlite3_column_int(s,1);
        if(status==TrainingReviewStatus::Pending) out.pending=count;
        else if(status==TrainingReviewStatus::Approved) out.approved=count;
        else if(status==TrainingReviewStatus::Rejected) out.rejected=count;
    }
    sqlite3_finalize(s);
    return out;
}

std::vector<TrainingReviewItem> TrainingReviewStore::ListRecent(
    size_t limit,
    std::optional<TrainingReviewStatus> status) const
{
    std::vector<TrainingReviewItem> out;
    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    const char* sql=status
        ? "SELECT id,source_log_id,conversation_id,persona_name,model_name,input_text,output_text,"
          "persona_summary,recalled_memory,context_json,status,reviewer,notes,created_utc,reviewed_utc "
          "FROM training_review_items WHERE status=? "
          "ORDER BY created_utc DESC,rowid DESC LIMIT ?"
        : "SELECT id,source_log_id,conversation_id,persona_name,model_name,input_text,output_text,"
          "persona_summary,recalled_memory,context_json,status,reviewer,notes,created_utc,reviewed_utc "
          "FROM training_review_items "
          "ORDER BY created_utc DESC,rowid DESC LIMIT ?";
    Check(sqlite3_prepare_v2(db,sql,-1,&s,nullptr),db,"prepare recent training reviews");
    int bind=1;
    if(status) sqlite3_bind_int(s,bind++,(int)*status);
    sqlite3_bind_int(s,bind,(int)std::min<size_t>(limit,500));
    while(sqlite3_step(s)==SQLITE_ROW) out.push_back(ReadItem(s));
    sqlite3_finalize(s);
    return out;
}

size_t TrainingReviewStore::ExportApprovedJsonl(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::trunc|std::ios::binary);
    if(!out) throw std::runtime_error("unable to open training dataset export");

    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "SELECT id,source_log_id,conversation_id,persona_name,model_name,input_text,output_text,"
        "persona_summary,recalled_memory,context_json,status,reviewer,notes,created_utc,reviewed_utc "
        "FROM training_review_items WHERE status=1 ORDER BY reviewed_utc ASC,created_utc ASC",
        -1,&s,nullptr),db,"prepare approved dataset export");

    size_t count=0;
    while(sqlite3_step(s)==SQLITE_ROW) {
        const auto item=ReadItem(s);
        out<<"{"
           <<"\"id\":\""<<JsonEscape(item.id)<<"\","
           <<"\"source_log_id\":\""<<JsonEscape(item.sourceLogId)<<"\","
           <<"\"conversation_id\":\""<<JsonEscape(item.conversationId)<<"\","
           <<"\"persona_name\":\""<<JsonEscape(item.personaName)<<"\","
           <<"\"model_name\":\""<<JsonEscape(item.modelName)<<"\","
           <<"\"input\":\""<<JsonEscape(item.inputText)<<"\","
           <<"\"output\":\""<<JsonEscape(item.outputText)<<"\","
           <<"\"persona_summary\":\""<<JsonEscape(item.personaSummary)<<"\","
           <<"\"recalled_memory\":\""<<JsonEscape(item.recalledMemory)<<"\","
           <<"\"context\":"<<(item.contextJson.empty()?"{}":item.contextJson)<<","
           <<"\"reviewer\":\""<<JsonEscape(item.reviewer)<<"\","
           <<"\"reviewed_utc\":\""<<JsonEscape(item.reviewedUtc)<<"\""
           <<"}\n";
        ++count;
    }
    sqlite3_finalize(s);
    return count;
}

}
