// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "ImGuiPluginTypes.h"

#if WITH_IMGUI

#include "ImGuiSubsystem.h"
#include "Styling/AppStyle.h"

template <size_t MaxLength = 64>
class FImGuiTextFilter : FNoncopyable
{
public:
	FORCEINLINE void Reset()
	{
		FilterString[0] = '\0';
		FilterKeywords.Reset();

		SearchIconTint = 0.75f;
		ClearIconTint = 0.75f;
	}

	FORCEINLINE bool IsActive() const
	{
		return !FilterKeywords.IsEmpty();
	}

	FORCEINLINE bool PassFilter(const FString& StringToCheck) const
	{
		if (!IsActive())
		{
			return true;
		}

		bool bPassedFilter = true;
		for (const FString& Keyword : FilterKeywords)
		{
			bPassedFilter &= StringToCheck.Contains(Keyword, ESearchCase::IgnoreCase);
		}
		return bPassedFilter;
	}

	FORCEINLINE bool PassFilter(const char* StringToCheck) const
	{
		return PassFilter(FString(ANSI_TO_TCHAR(StringToCheck)));
	}

	bool Draw(const char* WidgetName, const char* HintText = nullptr, bool bSetFocus = false, float WidgetWidth = 0.f)
	{
		UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
		const FImGuiImageBindingParams SearchIcon = ImGuiSubsystem->RegisterOneFrameResource(FAppStyle::GetBrush("Icons.Search"), FVector2D(ImGui::GetFontSize()), 1.f);
		const FImGuiImageBindingParams ClearIcon = ImGuiSubsystem->RegisterOneFrameResource(FAppStyle::GetBrush("Icons.X"), FVector2D(ImGui::GetFontSize()), 1.f);

		FImGuiNamedWidgetScope WidgetScope{ WidgetName };

		bool bFilterChanged = false;
		bool bSearchBoxHasFocus = false;

		ImGui::BeginGroup();
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));

			if (FilterKeywords.IsEmpty())
			{
				ImGui::ImageButton("Search", SearchIcon.Id, SearchIcon.Size, SearchIcon.UV0, SearchIcon.UV1, ImVec4(0, 0, 0, 0), ImVec4(SearchIconTint, SearchIconTint, SearchIconTint, 1.f));
			}
			else
			{
				if (ImGui::ImageButton("ClearFilter", ClearIcon.Id, ClearIcon.Size, ClearIcon.UV0, ClearIcon.UV1, ImVec4(0, 0, 0, 0), ImVec4(ClearIconTint, ClearIconTint, ClearIconTint, 1.f)))
				{
					bFilterChanged = true;
					FilterString[0] = '\0';
					FilterKeywords.Reset();

					// set focus to input text
					bSetFocus = true;
				}
				ClearIconTint = ImGui::IsItemHovered() ? 1.f : 0.75f;
			}

			ImGui::SameLine();

			const float SearchBoxWidth = WidgetWidth - SearchIcon.Size.x - ImGui::GetStyle().FramePadding.x * 2.f;
			if (SearchBoxWidth > KINDA_SMALL_NUMBER)
			{
				ImGui::SetNextItemWidth(SearchBoxWidth);
			}
			if (bSetFocus)
			{
				ImGui::SetKeyboardFocusHere();
			}
			const bool bInputTextChanged = HintText ? ImGui::InputTextWithHint("###Filter", HintText, FilterString, sizeof(FilterString)) : ImGui::InputText("###Filter", FilterString, sizeof(FilterString));
			if (bInputTextChanged)
			{
				bFilterChanged = true;
				FString(ANSI_TO_TCHAR(FilterString)).ParseIntoArray(FilterKeywords, TEXT(" "));
			}
			bSearchBoxHasFocus = ImGui::IsItemActive();
			SearchIconTint = bSearchBoxHasFocus ? 1.f : 0.75f;

			ImGui::PopStyleVar(1);
			ImGui::PopStyleColor(3);
		}
		ImGui::EndGroup();

		if (bSearchBoxHasFocus)
		{
			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			DrawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(ImVec4(0.26f, 0.59f, 0.98f, 0.67f)), 0.f, ImDrawFlags_None, 1.f);
		}

		return bFilterChanged;
	}

private:
	char FilterString[MaxLength] = { 0 };
	TArray<FString> FilterKeywords;
	float SearchIconTint = 0.75f;
	float ClearIconTint = 0.75f;
};

#endif //#if WITH_IMGUI
