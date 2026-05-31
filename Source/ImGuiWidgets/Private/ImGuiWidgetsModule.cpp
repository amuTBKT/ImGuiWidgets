// Copyright 2026 Amit Kumar Mehar. All Rights Reserved.

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

		const FVector2f Icon16x16 = FVector2f(16.f, 16.f);
		
		Set("ImIcon.Warning", new IMAGE_BRUSH_SVG("Icons/im_alert_triangle", Icon16x16));
		Set("ImIcon.Search", new IMAGE_BRUSH_SVG("Icons/im_search", Icon16x16));
		Set("ImIcon.Edit", new IMAGE_BRUSH_SVG("Icons/im_edit", Icon16x16));
		Set("ImIcon.Statistics", new IMAGE_BRUSH_SVG("Icons/im_statistics", Icon16x16));
		Set("ImIcon.Cross", new IMAGE_BRUSH_SVG("Icons/im_cross", Icon16x16));
		Set("ImIcon.Delete", new IMAGE_BRUSH_SVG("Icons/im_delete", Icon16x16));
		Set("ImIcon.Clipboard", new IMAGE_BRUSH_SVG("Icons/im_clipboard", Icon16x16));

		Set("ImIcon.CollapseAll", new IMAGE_BRUSH_SVG("Icons/im_collapse_all", Icon16x16));
		Set("ImIcon.ExpandAll", new IMAGE_BRUSH_SVG("Icons/im_expand_all", Icon16x16));
		Set("ImIcon.Save", new IMAGE_BRUSH_SVG("Icons/im_save", Icon16x16));
		Set("ImIcon.CheckerPattern", new IMAGE_BRUSH_SVG("Icons/im_checker_pattern", Icon16x16));
		Set("ImIcon.Find", new IMAGE_BRUSH_SVG("Icons/im_find", Icon16x16));
		Set("ImIcon.FrameSelected", new IMAGE_BRUSH_SVG("Icons/im_frame_selected", Icon16x16));
		Set("ImIcon.EngineFolder", new IMAGE_BRUSH_SVG("Icons/im_engine_content", Icon16x16));
		Set("ImIcon.ProjectFolder", new IMAGE_BRUSH_SVG("Icons/im_project_content", Icon16x16));
		Set("ImIcon.PluginFolder", new IMAGE_BRUSH_SVG("Icons/im_plugin_content", Icon16x16));
		Set("ImIcon.DeveloperFolder", new IMAGE_BRUSH_SVG("Icons/im_developer_content", Icon16x16));
		Set("ImIcon.AssetCollection", new IMAGE_BRUSH_SVG("Icons/im_collection_content", Icon16x16));
		Set("ImIcon.LocalizedFolder", new IMAGE_BRUSH_SVG("Icons/im_localized_content", Icon16x16));
		Set("ImIcon.DropDownArrow", new IMAGE_BRUSH_SVG("Icons/im_dropdown_arrow", Icon16x16));
		Set("ImIcon.UseSelectedAsset", new IMAGE_BRUSH_SVG("Icons/im_use_selected_asset", Icon16x16));
		Set("ImIcon.ResetToDefault", new IMAGE_BRUSH_SVG("Icons/im_reset_to_default", Icon16x16));
		Set("ImIcon.BrowseToAsset", new IMAGE_BRUSH_SVG("Icons/im_browse_to_asset", Icon16x16));
		Set("ImIcon.FallbackAssetIcon", new IMAGE_BRUSH_SVG("Icons/im_fallback_asset_icon", Icon16x16));
		Set("ImIcon.PlusCircle", new IMAGE_BRUSH_SVG("Icons/im_plus_circle", Icon16x16));

		Set("ImIcon.StatsVisualizer", new IMAGE_BRUSH_SVG("Icons/Tools/im_tool_stats_visualizer", Icon16x16));
		Set("ImIcon.NiagaraProfiler", new IMAGE_BRUSH_SVG("Icons/Tools/im_tool_niagara_profiler", Icon16x16));
		Set("ImIcon.TextureVisualizer", new IMAGE_BRUSH_SVG("Icons/Tools/im_tool_texture_visualizer", Icon16x16));

		Set("ImTex.DashLine.Vertical", new IMAGE_BRUSH("Common/im_dash_line_vertical", FVector2f(1.f, 10.f), FLinearColor::White, ESlateBrushTileType::Vertical));
		Set("ImTex.DashLine.Horizontal", new IMAGE_BRUSH("Common/im_dash_line_horizontal", FVector2f(10.f, 1.f), FLinearColor::White, ESlateBrushTileType::Horizontal));
		Set("ImTex.DropDropArea.Background", new BOX_BRUSH("Common/im_dragdrop_area_background", FMargin(6.0f / 64.0f)));

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
