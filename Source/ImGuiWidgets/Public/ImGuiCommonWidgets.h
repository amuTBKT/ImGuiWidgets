// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#if WITH_IMGUI

#include "ImGuiSubsystem.h"
#include "imgui_internal.h"
#include "String/ParseTokens.h"

namespace FImGui
{
	// Similar to `ImGui::ImageButton` but allows tinting the image depending on button state (inactive/active|hovered)
	FORCEINLINE bool ImageButtonWithTint(const char* str_id, ImTextureID user_texture_id, const ImVec2& image_size, const ImVec2& uv0, const ImVec2& uv1, ImU32 normal_tint_col, ImU32 active_tint_col, ImGuiButtonFlags flags = ImGuiButtonFlags_None)
	{
		ImGuiContext& g = *ImGui::GetCurrentContext();
		ImGuiWindow* window = ImGui::GetCurrentWindow();
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
		const ImU32 tint_col = (held || hovered) ? active_tint_col : normal_tint_col;
		ImGui::RenderNavCursor(bb, id);
		ImGui::RenderFrame(bb.Min, bb.Max, col, true, ImClamp((float)ImMin(padding.x, padding.y), 0.0f, g.Style.FrameRounding));
		window->DrawList->AddImage(user_texture_id, bb.Min + padding, bb.Max - padding, uv0, uv1, ImGui::GetColorU32(tint_col));

		return pressed;
	}

	// Similar to `ImGui::ImageButton` but doesn't render the background
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
		ImGui::RenderNavCursor(bb, id);
		window->DrawList->AddImage(user_texture_id, bb.Min + padding, bb.Max - padding, uv0, uv1, col);

		return pressed;
	}

	FORCEINLINE bool ImageButtonWithTint(const char* str_id, const FImGuiImageBindingParams& image, ImU32 normal_tint_col, ImU32 active_tint_col, ImGuiButtonFlags flags = ImGuiButtonFlags_None)
	{
		return ImageButtonWithTint(str_id, image.Id, image.Size, image.UV0, image.UV1, normal_tint_col, active_tint_col, flags);
	}

	FORCEINLINE bool TransparentImageButton(const char* str_id, const FImGuiImageBindingParams& image, ImGuiButtonFlags flags = ImGuiButtonFlags_None)
	{
		return TransparentImageButton(str_id, image.Id, image.Size, image.UV0, image.UV1, flags);
	}

	FORCEINLINE bool ImageButton(const char* str_id, const FImGuiImageBindingParams& image, const ImVec4& bg_col = ImVec4(0, 0, 0, 0), const ImVec4& tint_col = ImVec4(1, 1, 1, 1))
	{
		return ImGui::ImageButton(str_id, image.Id, image.Size, image.UV0, image.UV1, bg_col, tint_col);
	}

	FORCEINLINE void Image(const FImGuiImageBindingParams& image, const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0))
	{
		ImGui::Image(image.Id, image.Size, image.UV0, image.UV1, tint_col, border_col);
	}
}

template <size_t MaxLength = 64>
class FImGuiTextFilter : FNoncopyable
{
public:
	FORCEINLINE void Reset()
	{
		FilterString_ANSI[0] = '\0';
		FilterKeywords_ANSI.Reset();
		FilterString.Reset();
		FilterKeywords.Reset();

		SearchIconTint = 0.75f;
		ClearIconTint = 0.75f;
	}

	FORCEINLINE bool IsActive() const
	{
		return !FilterKeywords.IsEmpty();
	}

	FORCEINLINE bool PassFilter(FStringView StringToCheck) const
	{
		if (!IsActive())
		{
			return true;
		}

		for (FStringView Keyword : FilterKeywords)
		{
			if ((Keyword.Len() > 1) && (Keyword[0] == TCHAR('!')))
			{
				if (StringToCheck.Contains(Keyword.Right(1), ESearchCase::IgnoreCase))
				{
					return false;
				}
			}
			else if (!StringToCheck.Contains(Keyword, ESearchCase::IgnoreCase))
			{
				return false;
			}
		}
		return true;
	}
	FORCEINLINE bool PassFilter(const FString& StringToCheck) const
	{
		return PassFilter(FStringView{ *StringToCheck, StringToCheck.Len() });
	}

	FORCEINLINE bool PassFilterANSI(FAnsiStringView StringToCheck) const
	{
		if (!IsActive())
		{
			return true;
		}

		for (FAnsiStringView Keyword : FilterKeywords_ANSI)
		{
			if ((Keyword.Len() > 1) && (Keyword[0] == ('!')))
			{
				if (StringToCheck.Contains(Keyword.Right(1), ESearchCase::IgnoreCase))
				{
					return false;
				}
			}
			else if (!StringToCheck.Contains(Keyword, ESearchCase::IgnoreCase))
			{
				return false;
			}
		}
		return true;
	}
	FORCEINLINE bool PassFilterANSI(const FAnsiString& StringToCheck) const
	{
		return PassFilterANSI(FAnsiStringView{ *StringToCheck, StringToCheck.Len() });
	}

	FORCEINLINE const TArray<FStringView>& GetFilterKeywords() const
	{
		return FilterKeywords;
	}
	FORCEINLINE const TArray<FAnsiStringView>& GetFilterKeywordsANSI() const
	{
		return FilterKeywords_ANSI;
	}

	FORCEINLINE FStringView GetFilterString() const
	{
		return FStringView{ *FilterString, FilterString.Len() };
	}
	FORCEINLINE FAnsiStringView GetFilterStringANSI() const
	{
		return FAnsiStringView{ FilterString_ANSI, FilterString.Len()};
	}

	bool Draw(const char* WidgetName, const char* HintText = nullptr, bool bSetFocus = false, float WidgetWidth = 0.f)
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("TextFilter::Draw"), STAT_ImGuiTextFilter_Draw, STATGROUP_ImGui);

		UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
		const FImGuiImageBindingParams SearchIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icons.Search"), FVector2D(ImGui::GetFontSize()), 1.f);
		const FImGuiImageBindingParams ClearIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icons.X"), FVector2D(ImGui::GetFontSize()), 1.f);

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
				FImGui::ImageButton("Search", SearchIcon, ImVec4(0, 0, 0, 0), ImVec4(SearchIconTint, SearchIconTint, SearchIconTint, 1.f));
			}
			else
			{
				if (FImGui::ImageButton("ClearFilter", ClearIcon, ImVec4(0, 0, 0, 0), ImVec4(ClearIconTint, ClearIconTint, ClearIconTint, 1.f)))
				{
					bFilterChanged = true;
					FilterString_ANSI[0] = '\0';
					FilterKeywords_ANSI.Reset();
					FilterString.Reset();
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
			const bool bInputTextChanged = HintText ? ImGui::InputTextWithHint("##Filter", HintText, FilterString_ANSI, sizeof(FilterString_ANSI)) : ImGui::InputText("##Filter", FilterString_ANSI, sizeof(FilterString_ANSI));
			if (bInputTextChanged)
			{
				bFilterChanged = true;

				ANSICHAR* Src = FilterString_ANSI;
				FilterString.Reset();
				while (*Src)
				{
					FilterString.AppendChar(CharCast<TCHAR>(*Src++));
				}

				FilterKeywords.Reset();
				FilterKeywords_ANSI.Reset();
				if (FilterString.Len() > 0)
				{
					static const FAnsiStringView Delimiter = FAnsiStringView{ " " };
					
					const FStringView SourceStringView = GetFilterString();
					const FAnsiStringView SourceStringView_ANSI = GetFilterStringANSI();

					UE::String::EParseTokensOptions ParseOptions = UE::String::EParseTokensOptions::IgnoreCase | UE::String::EParseTokensOptions::SkipEmpty;
					UE::String::ParseTokens(SourceStringView_ANSI, Delimiter,
						[&](FAnsiStringView Token)
						{
							FilterKeywords_ANSI.Emplace(Token);

							// copy matching token to TCHAR version
							FilterKeywords.Emplace(SourceStringView.Mid((int32)std::distance(SourceStringView_ANSI.GetData(), Token.GetData()), Token.Len()));
						},
						ParseOptions);
				}
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
	char FilterString_ANSI[MaxLength] = { 0 };
	TArray<FAnsiStringView> FilterKeywords_ANSI;
	FString FilterString = FString::ConstructWithSlack(TEXT("\0"), MaxLength);
	TArray<FStringView> FilterKeywords;
	float SearchIconTint = 0.75f;
	float ClearIconTint = 0.75f;
};

#endif //#if WITH_IMGUI
