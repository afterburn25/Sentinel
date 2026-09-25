#pragma once
#include <string>

namespace sentinel::update {

struct UpdateInfo {
    std::string version;
    std::string downloadUrl;
    std::string sha256;
    std::string notes;
    bool newer{false};
};

class UpdateService {
public:
    UpdateInfo Check(const std::string& manifestUrl,const std::string& currentVersion) const;
    static bool IsNewerVersion(const std::string& candidate,const std::string& current);
};

}
