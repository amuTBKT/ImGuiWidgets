// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

#include "ImGuiPluginTypes.h"
#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"
#include "Interfaces/IPluginManager.h"

#include "Styling/AppStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"

class FImGuiStyleSet final : public FSlateStyleSet
{
public:
	FImGuiStyleSet()
		: FSlateStyleSet("ImGuiStyle")
	{
		SetParentStyleName(FAppStyle::GetAppStyleSetName());

		const FString IconDirectory = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("ImGuiWidgets"))->GetBaseDir(), TEXT("Content/Editor/Slate"));
		SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));
		SetContentRoot(IconDirectory);

		const FVector2D Icon16x16 = FVector2D(16.0, 16.0);

		Set("Icon.EngineFolder", new IMAGE_BRUSH_SVG("Icons/icon_engine_content", Icon16x16));
		Set("Icon.ProjectFolder", new IMAGE_BRUSH_SVG("Icons/icon_project_content", Icon16x16));
		Set("Icon.PluginFolder", new IMAGE_BRUSH_SVG("Icons/icon_plugin_content", Icon16x16));
		Set("Icon.DeveloperFolder", new IMAGE_BRUSH_SVG("Icons/icon_developer_content", Icon16x16));
		Set("Icon.AssetCollection", new IMAGE_BRUSH_SVG("Icons/icon_collection_content", Icon16x16));
		Set("Icon.LocalizedFolder", new IMAGE_BRUSH_SVG("Icons/icon_localized_content", Icon16x16));
		Set("Icon.DropDownArrow", new IMAGE_BRUSH_SVG("Icons/icon_dropdown_arrow", Icon16x16));
		Set("Icon.UseSelectedAsset", new IMAGE_BRUSH_SVG("Icons/icon_use_selected_asset", Icon16x16));
		Set("Icon.ResetToDefault", new IMAGE_BRUSH_SVG("Icons/icon_reset_to_default", Icon16x16));
		Set("Icon.BrowseToAsset", new IMAGE_BRUSH_SVG("Icons/icon_browse_to_asset", Icon16x16));
		Set("Icon.FallbackAssetIcon", new IMAGE_BRUSH_SVG("Icons/icon_fallback_asset_icon", Icon16x16));
		Set("Icon.CheckerPattern", new IMAGE_BRUSH_SVG("Icons/icon_checker_pattern", Icon16x16));
		Set("Icon.Find", new IMAGE_BRUSH_SVG("Icons/icon_find", Icon16x16));
		Set("Icon.FrameSelected", new IMAGE_BRUSH_SVG("Icons/icon_frame_selected", Icon16x16));
		Set("Icon.Save", new IMAGE_BRUSH_SVG("Icons/icon_save", Icon16x16));
		Set("Icon.CollapseAll", new IMAGE_BRUSH_SVG("Icons/icon_collapse_all", Icon16x16));
		Set("Icon.ExpandAll", new IMAGE_BRUSH_SVG("Icons/icon_expand_all", Icon16x16));

		Set("DashLine.Vertical", new IMAGE_BRUSH("Common/dash_line_vertical", FVector2D(1, 10), FLinearColor::White, ESlateBrushTileType::Vertical));
		Set("DashLine.Horizontal", new IMAGE_BRUSH("Common/dash_line_horizontal", FVector2D(10, 1), FLinearColor::White, ESlateBrushTileType::Horizontal));
		Set("DropDropArea.Background", new BOX_BRUSH("Common/dragdrop_area_background", FMargin(6.0f / 64.0f)));

		FSlateStyleRegistry::RegisterSlateStyle(*this);
	}

	~FImGuiStyleSet()
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*this);
	}
};

class FImGuiWidgetsModule : public IModuleInterface
{
	virtual void StartupModule() override
	{
		SETUP_DEFAULT_IMGUI_ALLOCATOR();

		m_ImGuiStyleSet = MakeUnique<FImGuiStyleSet>();
	}

	virtual void ShutdownModule() override
	{
		m_ImGuiStyleSet.Reset();
	}

	TUniquePtr<FImGuiStyleSet> m_ImGuiStyleSet = nullptr;
};

IMPLEMENT_MODULE(FImGuiWidgetsModule, ImGuiWidgets)
