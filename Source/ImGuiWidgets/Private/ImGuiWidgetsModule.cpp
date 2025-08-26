// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

#include "CoreMinimal.h"
#include "ImGuiPluginTypes.h"
#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"

class FImGuiWidgetsModule : public IModuleInterface
{
	virtual void StartupModule() override
	{
		SETUP_DEFAULT_IMGUI_ALLOCATOR();
	}

	virtual void ShutdownModule() override
	{

	}
};

IMPLEMENT_MODULE(FImGuiWidgetsModule, ImGuiWidgets)
