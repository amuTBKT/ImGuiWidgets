// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#if WITH_IMGUI

#include "ImGuiSubsystem.h"
#include "imgui_internal.h"

namespace ImGui
{
	FORCEINLINE bool ImageButtonWithTint(const char* str_id, ImTextureID user_texture_id, const ImVec2& image_size, const ImVec2& uv0, const ImVec2& uv1, ImU32 normal_tint_col, ImU32 active_tint_col, ImGuiButtonFlags flags = ImGuiButtonFlags_None)
	{
		ImGuiContext& g = *ImGui::GetCurrentContext();
		ImGuiWindow* window = GetCurrentWindow();
		if (window->SkipItems)
			return false;

		const ImGuiID id = window->GetID(str_id);

		const ImVec2 padding = g.Style.FramePadding;
		const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + image_size + padding * 2.0f);
		ItemSize(bb);
		if (!ItemAdd(bb, id))
			return false;

		bool hovered, held;
		bool pressed = ButtonBehavior(bb, id, &hovered, &held, flags);

		// Render
		const ImU32 col = GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
		const ImU32 tint_col = (held || hovered) ? active_tint_col : normal_tint_col;
		RenderNavHighlight(bb, id);
		RenderFrame(bb.Min, bb.Max, col, true, ImClamp((float)ImMin(padding.x, padding.y), 0.0f, g.Style.FrameRounding));
		window->DrawList->AddImage(user_texture_id, bb.Min + padding, bb.Max - padding, uv0, uv1, GetColorU32(tint_col));

		return pressed;
	}

	FORCEINLINE bool TransparentImageButton(const char* str_id, ImTextureID user_texture_id, const ImVec2& image_size, const ImVec2& uv0, const ImVec2& uv1, ImGuiButtonFlags flags = ImGuiButtonFlags_None)
	{
		ImGuiContext& g = *ImGui::GetCurrentContext();
		ImGuiWindow* window = g.CurrentWindow;
		if (window->SkipItems)
			return false;

		const ImGuiID id = window->GetID(str_id);

		const ImVec2 padding = g.Style.FramePadding;
		const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + image_size + padding * 2.0f);
		ImGui::ItemSize(bb);
		if (!ImGui::ItemAdd(bb, id))
			return false;

		bool hovered, held;
		bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);

		// Render
		const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
		window->DrawList->AddImage(user_texture_id, bb.Min + padding, bb.Max - padding, uv0, uv1, col);

		return pressed;
	}
}

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

		for (const FString& Keyword : FilterKeywords)
		{
			if ((Keyword.Len() > 1) && (Keyword[0] == TCHAR('!')))
			{
				if (StringToCheck.Contains(&Keyword[1], Keyword.Len() - 1, ESearchCase::IgnoreCase))
				{
					return false;
				}
			}
			else if (!StringToCheck.Contains(&Keyword[0], Keyword.Len(), ESearchCase::IgnoreCase))
			{
				return false;
			}
		}
		return true;
	}

	FORCEINLINE bool PassFilter(const char* StringToCheck) const
	{
		// TODO: could maintain an ANSI version of FilterKeywords
		return PassFilter(FString(ANSI_TO_TCHAR(StringToCheck)));
	}

	FORCEINLINE const TArray<FString>& GetFilterKeywords() const
	{
		return FilterKeywords;
	}

	FORCEINLINE FString GetFilterString() const
	{
		return FString(ANSI_TO_TCHAR(FilterString));
	}

	bool Draw(const char* WidgetName, const char* HintText = nullptr, bool bSetFocus = false, float WidgetWidth = 0.f)
	{
		UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
		const FImGuiImageBindingParams SearchIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_FNAME("Icons.Search"), FVector2D(ImGui::GetFontSize()), 1.f);
		const FImGuiImageBindingParams ClearIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_FNAME("Icons.X"), FVector2D(ImGui::GetFontSize()), 1.f);

		FImGuiNamedWidgetScope WidgetScope{ WidgetName };

		bool bFilterChanged = false;
		bool bSearchBoxHasFocus = false;

		ImGui::BeginGroup();
		{
			// blend button with input text widget
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));

			if (FilterKeywords.IsEmpty())
			{
				// TODO: doesn't need to be a button
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
			DrawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_FrameBgActive), 0.f, ImDrawFlags_None, 1.f);
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
