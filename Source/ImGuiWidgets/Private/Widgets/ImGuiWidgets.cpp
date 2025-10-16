// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

#include "ImGuiWidgets.h"
#include "ImGuiSubsystem.h"

#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/ConfigCacheIni.h"

namespace FImGuiSettings
{
	static const FString& GetConfigFilepath()
	{
		const TCHAR* UserSettingsDir = FPlatformProcess::UserSettingsDir();
		const FString EngineVersion = FEngineVersion::Current().ToString(EVersionComponent::Minor);
		static const FString ConfigFilepath =
			FPaths::Combine(UserSettingsDir, *FApp::GetEpicProductIdentifier(), EngineVersion, TEXT("Config/ImGui/ImGuiWidgets.ini"));
		return ConfigFilepath;
	}

	static FConfigFile* InitializeConfigFile()
	{
		const FString& ConfigFilepath = GetConfigFilepath();

		FConfigFile* ConfigFile = GConfig->Find(ConfigFilepath);
		if (!ConfigFile)
		{
			ConfigFile = &GConfig->Add(ConfigFilepath, FConfigFile{});
		}

		if (ConfigFile)
		{
			// needed to enable saving
			ConfigFile->NoSave = false;
		}

		return ConfigFile;
	}

	FConfigFile* GetConfigFile()
	{
		static FConfigFile* SettingsConfigFile = InitializeConfigFile();
		return SettingsConfigFile;
	}

	bool SaveConfigFile()
	{
		if (GetConfigFile())
		{
			GConfig->Flush(false, GetConfigFilepath());
			return true;
		}
		return false;
	}
}

namespace FImGui
{
	void DrawWarningMessageBox(FImGuiTickContext* context, float padding, const ImVec4& col, const char* message)
	{
		EnsureValidImGuiContext(context);

		UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
		const FImGuiImageBindingParams WarningIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.Warning"), ImGui::GetFontSize());

		/*
		TODO: there must be a better way to add this ^^'
		_____________________________________
		|<PADDING>							|
		|<PADDING><IMAGE><MESSAGE><PADDING>	|
		|<PADDING>							|
		-------------------------------------
		*/

		ImDrawList* DrawList = ImGui::GetWindowDrawList();

		// need to split here as background needs correct size info to render later
		DrawList->ChannelsSplit(2);

		// render foreground elements
		DrawList->ChannelsSetCurrent(1);
		ImGui::BeginGroup();
		{
			ImGui::Dummy(ImVec2(0, padding)); // <padding top>

			ImGui::Dummy(ImVec2(padding, 0.f)); ImGui::SameLine(); // <padding left>
			FImGui::Image(WarningIcon, col); ImGui::SameLine(); ImGui::Text(message); // <content>
			ImGui::SameLine(); ImGui::Dummy(ImVec2(padding, 0.f)); // <padding right>

			ImGui::Dummy(ImVec2(0, padding)); // <padding bottom>
		}
		ImGui::EndGroup();
		DrawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(col), 0.f, ImDrawFlags_None, 1.f);
		
		// render background
		DrawList->ChannelsSetCurrent(0);
		DrawList->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.15f)), 0.f, ImDrawFlags_None);

		DrawList->ChannelsMerge();
	}

	void DrawHighlightArea(FImGuiTickContext* context, ImVec2 p_min, ImVec2 p_max, float border_size, float border_uv_scale, ImU32 tint_col)
	{
		EnsureValidImGuiContext(context);

		UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
		const float GlobalScale = ImGui::GetStyle().FontScaleMain;

		const FImGuiImageBindingParams VerticalImage = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImTex.DashLine.Vertical"));
		const FImGuiImageBindingParams HorizontalImage = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImTex.DashLine.Horizontal"));
		const FImGuiImageBindingParams BackgroundImage = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImTex.DropDropArea.Background"));

		// TODO: scaling the UVs a bit to remove rounded corners (ideally should be using a different texture)
		ImGui::GetWindowDrawList()->AddImage(BackgroundImage.Id, p_min, p_max, BackgroundImage.UV0 + ImVec2(0.001f, 0.001f), BackgroundImage.UV1 - ImVec2(0.001f, 0.001f), tint_col);

		if (border_size > 0.f)
		{
			const float BorderSize = FMath::CeilToFloat(border_size);
			const float TilingU = FMath::RoundFromZero((p_max.x - p_min.x) / (HorizontalImage.Size.x * border_uv_scale));
			const float TilingV = FMath::RoundFromZero((p_max.y - p_min.y) / (VerticalImage.Size.y * border_uv_scale));

			const ImVec2 TopLeftCorner = p_min;
			const ImVec2 TopRightCorner = ImVec2(p_max.x, p_min.y);
			const ImVec2 BottomRightCorner = p_max;
			const ImVec2 BottomLeftCorner = ImVec2(p_min.x, p_max.y);

			// horizontal border
			ImGui::GetWindowDrawList()->AddImage(HorizontalImage.Id, TopLeftCorner, TopRightCorner + ImVec2(0.f, BorderSize), HorizontalImage.UV0, HorizontalImage.UV1 * ImVec2(TilingU, 1.f), tint_col);
			ImGui::GetWindowDrawList()->AddImage(HorizontalImage.Id, BottomLeftCorner + ImVec2(0.f, -BorderSize), BottomRightCorner, HorizontalImage.UV0, HorizontalImage.UV1 * ImVec2(TilingU, 1.f), tint_col);

			// vertical border
			ImGui::GetWindowDrawList()->AddImage(VerticalImage.Id, TopLeftCorner, BottomLeftCorner + ImVec2(BorderSize, 0.f), VerticalImage.UV0, VerticalImage.UV1 * ImVec2(1.f, TilingV), tint_col);
			ImGui::GetWindowDrawList()->AddImage(VerticalImage.Id, TopRightCorner + ImVec2(-BorderSize, 0.f), BottomRightCorner, VerticalImage.UV0, VerticalImage.UV1 * ImVec2(1.f, TilingV), tint_col);
		}
	}

	template <typename TDataType, ImGuiDataType TImGuiDataType>
	bool TSliderWithTwoHandles(FImGuiTickContext* context, const char* label, TDataType& p_data_0, TDataType& p_data_1, TDataType& p_data_min, TDataType& p_data_max, float input_width, float slider_width)
	{
		const TDataType prev_data_0 = p_data_0;
		const TDataType prev_data_1 = p_data_1;
		bool value_changed = false;

		EnsureValidImGuiContext(context);

		FImGuiNamedWidgetScope Scope{ label };

		const float GlobalScale = ImGui::GetStyle().FontScaleMain;

		auto Add_HandlesWidget = [](const char* label, TDataType& p_data_0, TDataType& p_data_1, const TDataType p_min, const TDataType p_max)
		{
			ImGuiWindow* window = ImGui::GetCurrentWindow();
			if (window->SkipItems)
			{
				return false;
			}

			ImGuiContext& g = *GImGui;
			const ImGuiStyle& style = g.Style;
			const ImGuiID id = window->GetID(label);
			const float w = ImGui::CalcItemWidth();

			const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
			const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w, label_size.y + style.FramePadding.y * 2.0f));
			const ImRect total_bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0.0f));

			ImGui::ItemSize(total_bb, style.FramePadding.y);
			if (!ImGui::ItemAdd(total_bb, id, &frame_bb))
			{
				return false;
			}

			const float grab_hsize = frame_bb.GetHeight() * 0.325f;
			const ImVec2 grab_hsize_v = ImVec2(grab_hsize, grab_hsize);

			const bool has_valid_range = !FMath::IsNearlyEqual(p_max, p_min, 0.00001f);
			const float grab_xpos_0 = frame_bb.Min.x + (has_valid_range ? (((p_data_0 - p_min) / (p_max - p_min)) * frame_bb.GetWidth()) : 0.f);
			const float grab_xpos_1 = frame_bb.Min.x + (has_valid_range ? (((p_data_1 - p_min) / (p_max - p_min)) * frame_bb.GetWidth()) : 0.f);
			const float grab_ypos_0 = frame_bb.GetCenter().y - grab_hsize;
			const float grab_ypos_1 = frame_bb.GetCenter().y + grab_hsize;

			ImVec2 grab_pos_0 = ImVec2(grab_xpos_0, grab_ypos_0);
			ImVec2 grab_pos_1 = ImVec2(grab_xpos_1, grab_ypos_1);

			bool hovered = false;
			if (has_valid_range)
			{
				const bool hovered_0 = ImGui::ItemHoverable(ImRect(grab_pos_0 - grab_hsize_v, grab_pos_0 + grab_hsize_v), 0, 0);
				const bool clicked_0 = hovered_0 && ImGui::IsMouseClicked(0, 0, 0);

				const bool hovered_1 = ImGui::ItemHoverable(ImRect(grab_pos_1 - grab_hsize_v, grab_pos_1 + grab_hsize_v), 0, 0);
				const bool clicked_1 = hovered_1 && ImGui::IsMouseClicked(0, 0, 0);

				hovered = (hovered_0 || hovered_1);

				if (clicked_0 || clicked_1)
				{
					ImGui::SetKeyOwner(ImGuiKey_MouseLeft, id);

					ImGui::SetActiveID(id, window);
					ImGui::SetFocusID(id, window);
					ImGui::FocusWindow(window);
					g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);

					g.CurrentWindow->StateStorage.SetBool(id, hovered_0);
				}
			}

			bool value_changed = false;
			if (ImGui::GetActiveID() == id)
			{
				const bool active_0 = g.CurrentWindow->StateStorage.GetBool(id);

				ImRect grab_bb_unused;
				value_changed = ImGui::SliderBehavior(frame_bb, id, TImGuiDataType, active_0 ? &p_data_0 : &p_data_1, &p_min, &p_max, "", ImGuiSliderFlags_NoInput, &grab_bb_unused);
				if (value_changed)
				{
					if (active_0)
					{
						p_data_0 = FMath::Min(p_data_0, p_data_1);
						grab_pos_0.x = frame_bb.Min.x + ((p_data_0 - p_min) / (p_max - p_min)) * frame_bb.GetWidth();
					}
					else
					{
						p_data_1 = FMath::Max(p_data_0, p_data_1);
						grab_pos_1.x = frame_bb.Min.x + ((p_data_1 - p_min) / (p_max - p_min)) * frame_bb.GetWidth();
					}

					ImGui::MarkItemEdited(id);
				}
			}

			// render
			{
				const ImU32 col_dark = ImGui::GetColorU32(ImGuiCol_FrameBg);
				const ImU32 col_light = ImGui::GetColorU32(ImGuiCol_FrameBgActive);
				const ImU32 col_dark_solid = col_dark | 0xFF000000;
				const ImU32 col_light_solid = col_light | 0xFF000000;

				const ImU32 frame_col = (g.ActiveId == id || hovered) ? ImGui::GetColorU32(ImGuiCol_FrameBg, 0.5f) : ImGui::GetColorU32(ImGuiCol_FrameBg);
				ImGui::RenderNavCursor(frame_bb, id);
				ImGui::RenderFrame(frame_bb.Min, frame_bb.Max, frame_col, true, g.Style.FrameRounding);

				window->DrawList->AddRectFilled(frame_bb.Min, ImVec2(grab_pos_0.x, frame_bb.Max.y), col_dark, style.GrabRounding);
				window->DrawList->AddRectFilled(ImVec2(grab_pos_1.x, frame_bb.Min.y), frame_bb.Max, col_light, style.GrabRounding);

				{
					window->DrawList->AddTriangleFilled(
						ImVec2(grab_pos_0 - grab_hsize_v),
						ImVec2(grab_pos_0 + ImVec2(grab_hsize, -grab_hsize)),
						ImVec2(grab_pos_0.x, grab_pos_0.y + grab_hsize),
						col_dark_solid);

					window->DrawList->AddTriangle(
						ImVec2(grab_pos_0 - grab_hsize_v),
						ImVec2(grab_pos_0 + ImVec2(grab_hsize, -grab_hsize)),
						ImVec2(grab_pos_0.x, grab_pos_0.y + grab_hsize),
						col_light_solid, 1.f);
				}

				{
					window->DrawList->AddTriangleFilled(
						ImVec2(grab_pos_1 + ImVec2(-grab_hsize, grab_hsize)),
						ImVec2(grab_pos_1.x, grab_pos_1.y - grab_hsize),
						ImVec2(grab_pos_1 + grab_hsize_v),
						col_light_solid);

					window->DrawList->AddTriangle(
						ImVec2(grab_pos_1 + ImVec2(-grab_hsize, grab_hsize)),
						ImVec2(grab_pos_1.x, grab_pos_1.y - grab_hsize),
						ImVec2(grab_pos_1 + grab_hsize_v),
						col_dark_solid, 1.f);
				}
			}

			return value_changed;
		};

		ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset = ImGui::GetStyle().FramePadding.y;
		ImGui::TextUnformatted(label);
		ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset = 0.f;

		const char* display_format = (p_data_max < 10.) ? "%.8f" : "%.4f";

		ImGui::SameLine(); ImGui::SetNextItemWidth(input_width * GlobalScale);
		TDataType val_min_temp = p_data_0; ImGui::InputScalar("##p_min", TImGuiDataType, &val_min_temp, nullptr, nullptr, display_format);
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			p_data_0 = p_data_min = FMath::Min(p_data_max, val_min_temp);
			p_data_1 = FMath::Max(p_data_0, p_data_1);

			value_changed |= (prev_data_0 != p_data_0) || (prev_data_1 != p_data_1);
		}

		ImGui::SameLine(); ImGui::SetNextItemWidth(slider_width * GlobalScale);
		if (Add_HandlesWidget("##Slider", p_data_0, p_data_1, p_data_min, p_data_max))
		{
			value_changed = true;
		}

		ImGui::SameLine(); ImGui::SetNextItemWidth(input_width * GlobalScale);
		TDataType val_max_temp = p_data_1; ImGui::InputScalar("##p_max", TImGuiDataType, &val_max_temp, nullptr, nullptr, display_format);
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			p_data_1 = p_data_max = FMath::Max(p_data_min, val_max_temp);
			p_data_0 = FMath::Min(p_data_0, p_data_1);

			value_changed |= (prev_data_0 != p_data_0) || (prev_data_1 != p_data_1);
		}

		return value_changed;
	}

	bool SliderWithTwoHandles(FImGuiTickContext* context, const char* label, float& p_data_0, float& p_data_1, float& p_data_min, float& p_data_max, float input_width, float slider_width)
	{
		return TSliderWithTwoHandles<float, ImGuiDataType_Float>(context, label, p_data_0, p_data_1, p_data_min, p_data_max, input_width, slider_width);
	}

	bool SliderWithTwoHandles(FImGuiTickContext* context, const char* label, double& p_data_0, double& p_data_1, double& p_data_min, double& p_data_max, float input_width, float slider_width)
	{
		return TSliderWithTwoHandles<double, ImGuiDataType_Double>(context, label, p_data_0, p_data_1, p_data_min, p_data_max, input_width, slider_width);
	}
}
