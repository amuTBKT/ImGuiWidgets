// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"
#include "Interfaces/IPluginManager.h"

class FImGuiWidgetShadersModule : public IModuleInterface
{
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FImGuiWidgetShadersModule, ImGuiWidgetShaders)

void FImGuiWidgetShadersModule::StartupModule()
{
#if WITH_EDITOR
	const FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("ImGuiWidgets"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/ImGuiWidgets"), PluginShaderDir);
#endif
}

void FImGuiWidgetShadersModule::ShutdownModule()
{
}
