#pragma once
#include "ModuleCodeControl.h"
#include "NativeModuleLibrary.h"
#include <ModuleManager.h>
#include <optional>
namespace pg::module::detail
{
struct ModuleRecord
{
    OwnedModuleDescriptor Descriptor;
    std::vector<DependencyBinding> Bindings;
    ModuleState State{ModuleState::kLoaded};
    bool Resident{};
    ModuleHandle Handle{};
    RegisterModuleFn RegisterModule{};
    std::optional<NativeModuleLibrary> Library;
    std::optional<pg::Error> OriginalError;
    bool ResourcesDrained{};
    bool Delivered{};
    std::shared_ptr<ModuleCodeControl> Code = MakeModuleCodeControl();
};
} // namespace pg::module::detail
