// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#include "CoreMinimal.h"
#include "ImGuiPluginTypes.h"
#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"

class FImGuiEditorWidgetsModule : public IModuleInterface
{
	virtual void StartupModule() override
	{
		SETUP_DEFAULT_IMGUI_ALLOCATOR();
	}

	virtual void ShutdownModule() override
	{

	}
};

IMPLEMENT_MODULE(FImGuiEditorWidgetsModule, ImGuiEditorWidgets)
