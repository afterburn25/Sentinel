#pragma once

#include <filesystem>
#include <string>

namespace sentinel::identity {

struct IdentityResearchRequestPackage {
    std::string taskId;
    std::string caseId;
    std::string subjectId;
    std::string subjectDisplayName;
    std::string researchType;
    std::string providerId;
    std::string providerDisplayName;
    std::string accessMode;
    std::string endpointHint;
    std::string queryText;
    std::string purpose;
    std::string createdUtc;
};

struct IdentityResearchResultPackage {
    std::string taskId;
    std::string providerId;
    std::string resultSummary;
    std::string resultReference;
    std::string provenance;
};

std::string SerializeResearchRequestPackage(
    const IdentityResearchRequestPackage& package);

std::string SerializeResearchResultPackage(
    const IdentityResearchResultPackage& package);

IdentityResearchRequestPackage ParseResearchRequestPackage(
    const std::string& text);

IdentityResearchResultPackage ParseResearchResultPackage(
    const std::string& text);

void SaveResearchRequestPackage(
    const std::filesystem::path& path,
    const IdentityResearchRequestPackage& package);

void SaveResearchResultPackage(
    const std::filesystem::path& path,
    const IdentityResearchResultPackage& package);

IdentityResearchResultPackage LoadResearchResultPackage(
    const std::filesystem::path& path);

}
