// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "imgui_internal.h"
#include "ImGuiPluginTypes.h"

namespace FImGui
{
	// Required to have valid ImGui context across modules
	FORCEINLINE void EnsureValidImGuiContext(ImGuiContext* Context)
	{
		check(Context);
		if (ImGui::GetCurrentContext() != Context)
		{
			ImGui::SetCurrentContext(Context);
		}
	}

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

	IMGUIWIDGETS_API void AddWarningMessageBox(ImGuiContext* Context, float padding, const ImVec4& col, const char* message);
}

class FImGuiTextFilter
{
public:
	IMGUIWIDGETS_API static FImGuiTextFilter MakeWidget(uint32 MaxLength);
	IMGUIWIDGETS_API bool Draw(ImGuiContext* Context, const char* Label, const char* HintText = nullptr, bool bSetFocus = false, float WidgetWidth = 0.f);
	IMGUIWIDGETS_API void Reset();

	bool IsActive()									const { return !FilterKeywordTokens.IsEmpty(); }
	bool PassFilter(FStringView StringToCheck)		const { return PassFilterInternal(GetFilterString(), StringToCheck); }
	bool PassFilter(FAnsiStringView StringToCheck)  const { return PassFilterInternal(GetFilterStringANSI(), StringToCheck); }
	FStringView GetFilterString()					const { return FStringView{ FilterStringBuffer.GetData(), FilterStringBuffer.Num() }; }
	FAnsiStringView GetFilterStringANSI()			const { return FAnsiStringView{ FilterStringBuffer_ANSI.GetData(), FilterStringBuffer_ANSI.Num() }; }

private:
	template <typename TStringViewType>
	bool PassFilterInternal(TStringViewType SourceString, TStringViewType StringToCheck) const
	{
		if (!IsActive())
		{
			return true;
		}

		for (TPair<int16, int16> KeywordTokens : FilterKeywordTokens)
		{
			TStringViewType Keyword = SourceString.Mid(KeywordTokens.Key, KeywordTokens.Value);
			if ((Keyword.Len() > 1) && (Keyword[0] == TStringViewType::ElementType('!')))
			{
				if (StringToCheck.Contains(Keyword.RightChop(1), ESearchCase::IgnoreCase))
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

private:
	TArray<TCHAR> FilterStringBuffer;
	TArray<ANSICHAR> FilterStringBuffer_ANSI;
	TArray<TPair<int16, int16>> FilterKeywordTokens;
	float SearchIconTint = 0.75f;
	float ClearIconTint = 0.75f;
};
