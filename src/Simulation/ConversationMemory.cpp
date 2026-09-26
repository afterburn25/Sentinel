#include "Sentinel/Simulation/ConversationMemory.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sentinel::simulation {
namespace {

void Check(int rc,sqlite3* db,const char* what) {
    if(rc!=SQLITE_OK && rc!=SQLITE_DONE && rc!=SQLITE_ROW)
        throw std::runtime_error(std::string(what)+": "+sqlite3_errmsg(db));
}

std::string ColumnText(sqlite3_stmt* s,int col) {
    const auto* p=sqlite3_column_text(s,col);
    if(!p) return {};
    return reinterpret_cast<const char*>(p);
}

std::string NewId(sqlite3* db) {
    sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,"SELECT lower(hex(randomblob(16)))",-1,&s,nullptr),db,"prepare random id");
    std::string id;
    if(sqlite3_step(s)==SQLITE_ROW) id=ColumnText(s,0);
    sqlite3_finalize(s);
    if(id.empty()) throw std::runtime_error("failed to generate conversation id");
    return id;
}

std::string Lower(std::string_view input) {
    std::string out(input);
    std::transform(out.begin(),out.end(),out.begin(),[](unsigned char c){return (char)std::tolower(c);});
    return out;
}

std::vector<std::string> Tokens(std::string_view input) {
    static const std::set<std::string> stop={
        "the","and","that","this","with","from","have","what","when","where","which","about",
        "would","could","should","your","you","are","was","were","did","does","for","but","not",
        "just","into","then","than","them","they","our","out","all","can","how","why","who",
        "remember","remembered","talked","conversation","before","previous","last","time"
    };
    std::vector<std::string> out;
    std::string word;
    auto flush=[&]{
        if(word.size()>=3 && !stop.contains(word)) out.push_back(word);
        word.clear();
    };
    for(unsigned char c:input) {
        if(std::isalnum(c) || c=='\'') word+=(char)std::tolower(c);
        else flush();
    }
    flush();
    std::sort(out.begin(),out.end());
    out.erase(std::unique(out.begin(),out.end()),out.end());
    return out;
}

const char* SpeakerName(int speaker) {
    switch((ChatTurn::Speaker)speaker) {
        case ChatTurn::Speaker::Investigator: return "Investigator";
        case ChatTurn::Speaker::SyntheticSubject: return "Synthetic Subject";
        case ChatTurn::Speaker::ModelSuggestion: return "Model Suggestion";
    }
    return "Unknown";
}

}

std::string ConversationMemoryStore::StartConversation(
    std::string_view title,
    std::string_view personaName,
    std::string_view scenario)
{
    auto* db=db_.Handle();
    const auto id=NewId(db);
    sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "INSERT INTO simulation_conversations(id,title,persona_name,scenario) VALUES(?,?,?,?)",
        -1,&s,nullptr),db,"prepare conversation insert");
    sqlite3_bind_text(s,1,id.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,2,std::string(title).c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,3,std::string(personaName).c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(s,4,std::string(scenario).c_str(),-1,SQLITE_TRANSIENT);
    Check(sqlite3_step(s),db,"insert conversation");
    sqlite3_finalize(s);
    return id;
}

void ConversationMemoryStore::Append(
    std::string_view conversationId,
    ChatTurn::Speaker speaker,
    std::string_view text)
{
    if(conversationId.empty() || text.empty()) return;
    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "INSERT INTO simulation_messages(conversation_id,speaker,body) VALUES(?,?,?)",
        -1,&s,nullptr),db,"prepare message insert");
    sqlite3_bind_text(s,1,std::string(conversationId).c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int(s,2,(int)speaker);
    sqlite3_bind_text(s,3,std::string(text).c_str(),-1,SQLITE_TRANSIENT);
    Check(sqlite3_step(s),db,"insert simulation message");
    sqlite3_finalize(s);

    Check(sqlite3_prepare_v2(db,
        "UPDATE simulation_conversations SET updated_utc=CURRENT_TIMESTAMP WHERE id=?",
        -1,&s,nullptr),db,"prepare conversation touch");
    sqlite3_bind_text(s,1,std::string(conversationId).c_str(),-1,SQLITE_TRANSIENT);
    Check(sqlite3_step(s),db,"touch conversation");
    sqlite3_finalize(s);
}

std::vector<ArchivedConversation> ConversationMemoryStore::List(size_t limit) const {
    std::vector<ArchivedConversation> out;
    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "SELECT c.id,c.title,c.persona_name,c.scenario,c.created_utc,c.updated_utc,"
        "(SELECT COUNT(*) FROM simulation_messages m WHERE m.conversation_id=c.id) "
        "FROM simulation_conversations c ORDER BY c.updated_utc DESC LIMIT ?",
        -1,&s,nullptr),db,"prepare conversation list");
    sqlite3_bind_int(s,1,(int)std::min<size_t>(limit,500));
    while(sqlite3_step(s)==SQLITE_ROW) {
        ArchivedConversation c;
        c.id=ColumnText(s,0);
        c.title=ColumnText(s,1);
        c.personaName=ColumnText(s,2);
        c.scenario=ColumnText(s,3);
        c.createdUtc=ColumnText(s,4);
        c.updatedUtc=ColumnText(s,5);
        c.messageCount=sqlite3_column_int(s,6);
        out.push_back(std::move(c));
    }
    sqlite3_finalize(s);
    return out;
}

bool ConversationMemoryStore::Load(
    std::string_view conversationId,
    ModelContext& context) const
{
    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "SELECT scenario,persona_name FROM simulation_conversations WHERE id=?",
        -1,&s,nullptr),db,"prepare conversation load");
    sqlite3_bind_text(s,1,std::string(conversationId).c_str(),-1,SQLITE_TRANSIENT);
    if(sqlite3_step(s)!=SQLITE_ROW) {
        sqlite3_finalize(s);
        return false;
    }
    const auto scenario=ColumnText(s,0);
    sqlite3_finalize(s);

    ModelContext loaded=context;
    loaded.history.clear();
    loaded.recalledMemory.clear();
    loaded.scenario=scenario;

    Check(sqlite3_prepare_v2(db,
        "SELECT speaker,body FROM simulation_messages WHERE conversation_id=? ORDER BY row_id ASC",
        -1,&s,nullptr),db,"prepare messages load");
    sqlite3_bind_text(s,1,std::string(conversationId).c_str(),-1,SQLITE_TRANSIENT);
    while(sqlite3_step(s)==SQLITE_ROW) {
        int sp=sqlite3_column_int(s,0);
        if(sp<0 || sp>2) continue;
        loaded.history.push_back({(ChatTurn::Speaker)sp,ColumnText(s,1)});
    }
    sqlite3_finalize(s);
    if(loaded.history.empty()) return false;
    context=std::move(loaded);
    return true;
}

std::string ConversationMemoryStore::RecallRelevant(
    std::string_view query,
    std::string_view currentConversationId,
    size_t maxMessages) const
{
    auto tokens=Tokens(query);
    const auto q=Lower(query);
    const bool explicitRecall =
        q.find("remember")!=std::string::npos ||
        q.find("last time")!=std::string::npos ||
        q.find("previous")!=std::string::npos ||
        q.find("before")!=std::string::npos ||
        q.find("you said")!=std::string::npos ||
        q.find("i said")!=std::string::npos ||
        q.find("we talked")!=std::string::npos;

    if(tokens.empty() && !explicitRecall) return {};

    struct Candidate {
        int score{};
        long long row{};
        std::string session;
        int speaker{};
        std::string body;
        std::string updated;
    };
    std::vector<Candidate> candidates;
    auto* db=db_.Handle();
    sqlite3_stmt* s{};
    Check(sqlite3_prepare_v2(db,
        "SELECT m.row_id,m.conversation_id,m.speaker,m.body,c.updated_utc "
        "FROM simulation_messages m JOIN simulation_conversations c ON c.id=m.conversation_id "
        "WHERE m.conversation_id<>? ORDER BY m.row_id DESC LIMIT 1500",
        -1,&s,nullptr),db,"prepare memory scan");
    sqlite3_bind_text(s,1,std::string(currentConversationId).c_str(),-1,SQLITE_TRANSIENT);
    while(sqlite3_step(s)==SQLITE_ROW) {
        Candidate c;
        c.row=sqlite3_column_int64(s,0);
        c.session=ColumnText(s,1);
        c.speaker=sqlite3_column_int(s,2);
        c.body=ColumnText(s,3);
        c.updated=ColumnText(s,4);
        auto bodyLower=Lower(c.body);
        for(const auto& token:tokens) {
            if(bodyLower.find(token)!=std::string::npos) c.score+=3;
        }
        if(explicitRecall && c.score==0 && tokens.empty()) c.score=1;
        if(c.score>0) candidates.push_back(std::move(c));
    }
    sqlite3_finalize(s);

    std::stable_sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){
        if(a.score!=b.score) return a.score>b.score;
        return a.row>b.row;
    });
    if(candidates.size()>maxMessages) candidates.resize(maxMessages);
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){return a.row<b.row;});

    if(candidates.empty()) return {};

    std::ostringstream out;
    out<<"Verbatim recalled messages from earlier Sentinel conversations. Treat these as exact historical quotes, not summaries:\n";
    std::string lastSession;
    for(const auto& c:candidates) {
        if(c.session!=lastSession) {
            lastSession=c.session;
            out<<"[Past conversation "<<c.updated<<" | "<<c.session.substr(0,8)<<"]\n";
        }
        out<<SpeakerName(c.speaker)<<": "<<c.body<<"\n";
    }
    return out.str();
}

}
