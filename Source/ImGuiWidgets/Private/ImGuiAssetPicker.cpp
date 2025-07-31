// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#if WITH_IMGUI

#include "ImGuiAssetPicker.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ClassIconFinder.h"
#include "EditorUtilityLibrary.h"
#include "Styling/SlateIconFinder.h"
#include "ContentBrowserDataUtils.h"
#include "Subsystems/AssetEditorSubsystem.h"
#endif

namespace FImGuiContentBrowserUtils
{
	bool bShowProjectConent = true;
	bool bShowEngineConent = true;
	bool bShowPluginConent = true;
	bool bShowDeveloperConent = true;

#if WITH_EDITOR
	const FSlateBrush* GetIconForClass(UClass* AssetClass)
	{
		return FClassIconFinder::FindThumbnailForClass(AssetClass, NAME_None);
	}

	bool FilterAsset(FName AssetPath)
	{
		EContentBrowserItemAttributeFilter ContentBrowserItemFilter = EContentBrowserItemAttributeFilter::IncludeNone;
		if (bShowProjectConent)
		{
			ContentBrowserItemFilter |= EContentBrowserItemAttributeFilter::IncludeProject;
		}
		if (bShowEngineConent)
		{
			ContentBrowserItemFilter |= EContentBrowserItemAttributeFilter::IncludeEngine;
		}
		if (bShowPluginConent)
		{
			ContentBrowserItemFilter |= EContentBrowserItemAttributeFilter::IncludePlugins;
		}
		if (bShowDeveloperConent)
		{
			ContentBrowserItemFilter |= EContentBrowserItemAttributeFilter::IncludeDeveloper;
		}

		FNameBuilder PathBufferStr(AssetPath);
		return ContentBrowserDataUtils::PathPassesAttributeFilter(PathBufferStr, 0, ContentBrowserItemFilter);
	}

	UObject* GetSelectedAsset(UClass* AssetClass)
	{
		for (auto Asset : UEditorUtilityLibrary::GetSelectedAssets())
		{
			if (Asset->IsA(AssetClass))
			{
				return Asset;
			}
		}
		return nullptr;
	}

	void OpenEditorForAsset(UObject* Asset)
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (AssetEditorSubsystem)
		{
			AssetEditorSubsystem->OpenEditorForAsset(Asset);
		}
	}

	void SyncContentBrowserToAsset(UObject* Asset)
	{
		TArray<UObject*> Objects;
		Objects.Add(Asset);
		GEditor->SyncBrowserToObjects(Objects);
	}

#else
	const FSlateBrush* GetIconForClass(UClass* AssetClass)
	{
		return nullptr;
	}

	bool FilterAsset(FName AssetPath)
	{
		// TODO: add some basic asset path filtering for runtime
		return true;
	}

	UObject* GetSelectedAsset(UClass* AssetClass)
	{
		return nullptr;
	}

	void OpenEditorForAsset(UObject* Asset)
	{
	}

	void SyncContentBrowserToAsset(UObject* Asset)
	{
	}
#endif
}

#endif //#if WITH_IMGUI