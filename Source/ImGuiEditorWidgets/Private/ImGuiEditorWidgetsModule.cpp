// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "ImGuiPluginTypes.h"

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
