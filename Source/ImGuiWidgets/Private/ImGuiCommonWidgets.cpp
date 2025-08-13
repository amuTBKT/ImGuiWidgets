// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#include "ImGuiCommonWidgets.h"
#include "ImGuiSubsystem.h"

namespace FImGui
{

	void AddWarningMessageBox(ImGuiContext* Context, float padding, const ImVec4& col, const char* message)
	{
		EnsureValidImGuiContext(Context);

		UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
		const FImGuiImageBindingParams WarningIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icons.Warning"), FVector2D(ImGui::GetFontSize()));

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

}
