#include "Sentinel/Identity/IdentityResearchPackage.hpp"

#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace sentinel::identity {
namespace {

std::string Escape(std::string_view input)
{
    std::string out;
    out.reserve(input.size());
    for(char ch:input) {
        switch(ch) {
            case '\\': out+="\\\\"; break;
            case '\n': out+="\\n"; break;
            case '\r': out+="\\r"; break;
            case '\t': out+="\\t"; break;
            default: out+=ch; break;
        }
    }
    return out;
}

std::string Unescape(std::string_view input)
{
    std::string out;
    out.reserve(input.size());
    bool escaped=false;
    for(char ch:input) {
        if(!escaped) {
            if(ch=='\\') escaped=true;
            else out+=ch;
            continue;
        }
        switch(ch) {
            case 'n': out+='\n'; break;
            case 'r': out+='\r'; break;
            case 't': out+='\t'; break;
            case '\\': out+='\\'; break;
            default: out+=ch; break;
        }
        escaped=false;
    }
    if(escaped) out+='\\';
    return out;
}

std::map<std::string,std::string> ParseFields(
    const std::string& text,
    std::string_view expectedHeader)
{
    std::istringstream in(text);
    std::string line;
    if(!std::getline(in,line) || line!=expectedHeader)
        throw std::runtime_error("unsupported SARA identity research package format");

    std::map<std::string,std::string> fields;
    while(std::getline(in,line)) {
        if(line.empty()) continue;
        const auto tab=line.find('\t');
        if(tab==std::string::npos) continue;
        fields[line.substr(0,tab)]=Unescape(line.substr(tab+1));
    }
    return fields;
}

std::string Required(
    const std::map<std::string,std::string>& fields,
    const char* key)
{
    auto it=fields.find(key);
    if(it==fields.end() || it->second.empty())
        throw std::runtime_error(std::string("research package is missing required field: ")+key);
    return it->second;
}

std::string Optional(
    const std::map<std::string,std::string>& fields,
    const char* key)
{
    auto it=fields.find(key);
    return it==fields.end()?std::string{}:it->second;
}

void SaveText(const std::filesystem::path& path,const std::string& text)
{
    if(path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    if(!out) throw std::runtime_error("unable to create identity research package");
    out.write(text.data(),(std::streamsize)text.size());
    if(!out) throw std::runtime_error("unable to write identity research package");
}

std::string LoadText(const std::filesystem::path& path)
{
    std::ifstream in(path,std::ios::binary);
    if(!in) throw std::runtime_error("unable to open identity research result package");
    return {
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()
    };
}

void Field(std::ostringstream& out,const char* key,const std::string& value)
{
    out<<key<<'\t'<<Escape(value)<<'\n';
}

}

std::string SerializeResearchRequestPackage(
    const IdentityResearchRequestPackage& p)
{
    if(p.taskId.empty() || p.caseId.empty() || p.subjectId.empty() ||
       p.researchType.empty() || p.providerId.empty() ||
       p.queryText.empty() || p.purpose.empty())
        throw std::runtime_error("identity research request package is incomplete");

    std::ostringstream out;
    out<<"SARA_IDENTITY_RESEARCH_REQUEST_V1\n";
    Field(out,"task_id",p.taskId);
    Field(out,"case_id",p.caseId);
    Field(out,"subject_id",p.subjectId);
    Field(out,"subject_display_name",p.subjectDisplayName);
    Field(out,"research_type",p.researchType);
    Field(out,"provider_id",p.providerId);
    Field(out,"provider_display_name",p.providerDisplayName);
    Field(out,"access_mode",p.accessMode);
    Field(out,"endpoint_hint",p.endpointHint);
    Field(out,"query_reference",p.queryText);
    Field(out,"purpose_legal_basis",p.purpose);
    Field(out,"created_utc",p.createdUtc);
    Field(out,"notice",
        "No credentials are included. Perform only authorized research. "
        "Returned findings remain investigative leads until separately verified.");
    return out.str();
}

std::string SerializeResearchResultPackage(
    const IdentityResearchResultPackage& p)
{
    if(p.taskId.empty() || p.providerId.empty())
        throw std::runtime_error("identity research result package is incomplete");

    std::ostringstream out;
    out<<"SARA_IDENTITY_RESEARCH_RESULT_V1\n";
    Field(out,"task_id",p.taskId);
    Field(out,"provider_id",p.providerId);
    Field(out,"result_summary",p.resultSummary);
    Field(out,"result_reference",p.resultReference);
    Field(out,"provenance",p.provenance);
    Field(out,"notice",
        "Import does not verify or confirm identity. Human review remains required.");
    return out.str();
}

IdentityResearchRequestPackage ParseResearchRequestPackage(
    const std::string& text)
{
    const auto fields=ParseFields(
        text,"SARA_IDENTITY_RESEARCH_REQUEST_V1");
    IdentityResearchRequestPackage p;
    p.taskId=Required(fields,"task_id");
    p.caseId=Required(fields,"case_id");
    p.subjectId=Required(fields,"subject_id");
    p.subjectDisplayName=Optional(fields,"subject_display_name");
    p.researchType=Required(fields,"research_type");
    p.providerId=Required(fields,"provider_id");
    p.providerDisplayName=Optional(fields,"provider_display_name");
    p.accessMode=Optional(fields,"access_mode");
    p.endpointHint=Optional(fields,"endpoint_hint");
    p.queryText=Required(fields,"query_reference");
    p.purpose=Required(fields,"purpose_legal_basis");
    p.createdUtc=Optional(fields,"created_utc");
    return p;
}

IdentityResearchResultPackage ParseResearchResultPackage(
    const std::string& text)
{
    const auto fields=ParseFields(
        text,"SARA_IDENTITY_RESEARCH_RESULT_V1");
    IdentityResearchResultPackage p;
    p.taskId=Required(fields,"task_id");
    p.providerId=Required(fields,"provider_id");
    p.resultSummary=Required(fields,"result_summary");
    p.resultReference=Optional(fields,"result_reference");
    p.provenance=Required(fields,"provenance");
    return p;
}

void SaveResearchRequestPackage(
    const std::filesystem::path& path,
    const IdentityResearchRequestPackage& package)
{
    SaveText(path,SerializeResearchRequestPackage(package));
}

void SaveResearchResultPackage(
    const std::filesystem::path& path,
    const IdentityResearchResultPackage& package)
{
    SaveText(path,SerializeResearchResultPackage(package));
}

IdentityResearchResultPackage LoadResearchResultPackage(
    const std::filesystem::path& path)
{
    return ParseResearchResultPackage(LoadText(path));
}

}
