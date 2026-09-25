#pragma once
#include "Sentinel/Simulation/IModelAdapter.hpp"
#include <filesystem>

namespace sentinel::simulation {

void SaveSession(const std::filesystem::path& path,const ModelContext& context);
bool LoadSession(const std::filesystem::path& path,ModelContext& context);

}
