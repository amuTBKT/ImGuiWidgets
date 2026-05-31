// Copyright 2026 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "ImGuiPluginTypes.h"
#include "Input/DragAndDrop.h"

// commonly used function to get ImGui icons/brushes
#define IMGUI_ICON(IconName) IMGUI_STYLE_ICON("ImGuiStyle", IconName)
#define IMGUI_ICON_BRUSH(IconName) IMGUI_STYLE_ICON_BRUSH("ImGuiStyle", IconName)

///////////////////////////////////////////////////////////////////////////////////////////////////////

namespace FImGui
{
	// Required to have valid ImGui context across modules
	FORCEINLINE void EnsureValidImGuiContext(FImGuiTickContext* Context)
	{
		check(Context->ImguiContext);
		if (ImGui::GetCurrentContext() != Context->ImguiContext)
		{
			ImGui::SetCurrentContext(Context->ImguiContext);
		}
	}

	// Get ImGui tick context
	FORCEINLINE FImGuiTickContext* GetTickContext(ImGuiContext* Context)
	{
		return FImGuiTickContext::GetTickContext(Context);
	}

	// Get the current ImGui tick context
	FORCEINLINE FImGuiTickContext* GetCurrentTickContext()
	{
		return FImGuiTickContext::GetTickContext(ImGui::GetCurrentContext());
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

	FORCEINLINE void Image(const FImGuiImageBindingParams& image, const ImVec4& tint_col = ImVec4(1, 1, 1, 1))
	{
		ImGui::ImageWithBg(image.Id, image.Size, image.UV0, image.UV1, /*bg_col=*/ImVec4(0, 0, 0, 0), tint_col);
	}

	IMGUIWIDGETS_API void DrawWarningMessageBox(FImGuiTickContext* context, float padding, const ImVec4& col, const char* message);
	
	IMGUIWIDGETS_API void DrawHighlightArea(FImGuiTickContext* context, ImVec2 p_min, ImVec2 p_max, float border_size = 1.f, float border_uv_scale = 1.f, ImU32 tint_col = 0xFFFFFFFF);

	// Hacky/Experimental widget
	IMGUIWIDGETS_API bool SliderWithTwoHandles(FImGuiTickContext* context, const char* label, float& p_data_0, float& p_data_1, float& p_data_min, float& p_data_max, float input_width, float slider_width);
	IMGUIWIDGETS_API bool SliderWithTwoHandles(FImGuiTickContext* context, const char* label, double& p_data_0, double& p_data_1, double& p_data_min, double& p_data_max, float input_width, float slider_width);

	template <typename TDragDropOp, typename Predicate, typename Callback>
	FORCEINLINE bool DrawDragDropArea(FImGuiTickContext* context, const char* str_id, ImRect drag_rect, Predicate pred_func, Callback callback_func)
	{
		FImGuiNamedScope Scope{ str_id };

		const bool bIsDragDropOperationValid = context->DragDropOperation.IsValid();
		bool bPredicatePassed = false;
		if (bIsDragDropOperationValid && context->DragDropOperation->IsOfType<TDragDropOp>())
		{
			TSharedPtr<TDragDropOp> DragDropOp = StaticCastSharedPtr<TDragDropOp>(context->DragDropOperation);
			bPredicatePassed = pred_func(DragDropOp);
		}

		const bool bHighlightArea = bIsDragDropOperationValid && (bPredicatePassed || ImGui::IsMouseHoveringRect(drag_rect.Min, drag_rect.Max));
		if (bHighlightArea)
		{
			const ImU32 ValidColor = 0xFFFFBB26;
			const ImU32 InvalidColor = 0xFF3535EF;
			DrawHighlightArea(context, drag_rect.Min, drag_rect.Max, ImGui::GetStyle().FontScaleMain, ImGui::GetStyle().FontScaleMain, bPredicatePassed ? ValidColor : InvalidColor);
		}

		if (bPredicatePassed && ImGui::IsMouseHoveringRect(drag_rect.Min, drag_rect.Max))
		{
			TSharedPtr<TDragDropOp> DragDropOp = StaticCastSharedPtr<TDragDropOp>(context->TryConsumeDragDropOperation());
			if (DragDropOp)
			{
				callback_func(DragDropOp);
				return true;
			}
		}

		return false;
	}
}

class FImGuiTextFilter
{
public:
	IMGUIWIDGETS_API static FImGuiTextFilter MakeWidget(uint32 MaxLength);
	IMGUIWIDGETS_API bool Draw(FImGuiTickContext* Context, const char* Label, const char* HintText = nullptr, float WidgetWidth = 0.f, bool bSetFocus = false, bool bActivateOnNavFocus = false);
	IMGUIWIDGETS_API void Reset();

	bool IsActive()									const { return !FilterKeywordTokens_ANSI.IsEmpty(); }
	bool PassFilter(FStringView StringToCheck)		const { return PassFilterInternal(GetFilterString(), FilterKeywordTokens, StringToCheck); }
	bool PassFilter(FAnsiStringView StringToCheck)  const { return PassFilterInternal(GetFilterStringANSI(), FilterKeywordTokens_ANSI, StringToCheck); }
	FStringView GetFilterString()					const { return FStringView{ FilterStringBuffer.GetData(), FilterStringBuffer.Num() }; }
	FAnsiStringView GetFilterStringANSI()			const { return FAnsiStringView{ FilterStringBuffer_ANSI.GetData(), FilterStringBuffer_ANSI.Num() }; }

	FImGuiTextFilter() noexcept = default;
	~FImGuiTextFilter() noexcept = default;
	FImGuiTextFilter(const FImGuiTextFilter& Other) noexcept
	{
		*this = Other;
	}
	FImGuiTextFilter& operator=(const FImGuiTextFilter& Other) noexcept
	{
		FilterStringBuffer = Other.FilterStringBuffer;
		FilterStringBuffer_ANSI = Other.FilterStringBuffer_ANSI;
		FilterKeywordTokens = Other.FilterKeywordTokens;
		SearchIconTint = Other.SearchIconTint;
		ClearIconTint = Other.ClearIconTint;

		// ensure we have valid buffer capacity
		FilterStringBuffer.Reserve(Other.FilterStringBuffer.Max());
		FilterStringBuffer_ANSI.Reserve(Other.FilterStringBuffer_ANSI.Max());
		if (FilterStringBuffer_ANSI.Num() < FilterStringBuffer_ANSI.Max())
		{
			FilterStringBuffer_ANSI.GetData()[FilterStringBuffer_ANSI.Num()] = '\0';
		}

		return *this;
	}

private:
	template <typename TStringViewType>
	bool PassFilterInternal(TStringViewType SourceString, const TArray<TPair<uint16, uint16>>& KeywordTokens, TStringViewType StringToCheck) const
	{
		if (!IsActive())
		{
			return true;
		}

		for (TPair<uint16, uint16> KeywordToken : KeywordTokens)
		{
			TStringViewType Keyword = SourceString.Mid(KeywordToken.Key, KeywordToken.Value);
			if ((Keyword.Len() > 1) && (Keyword[0] == typename TStringViewType::ElementType('!')))
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
	TArray<TPair<uint16, uint16>> FilterKeywordTokens;
	TArray<TPair<uint16, uint16>> FilterKeywordTokens_ANSI;
	float SearchIconTint = 0.75f;
	float ClearIconTint = 0.75f;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////

// asset/class picker filters
struct FImGuiAssetTagFilter
{
	FName TagName = NAME_None;
	FString ExpectedValue;

	bool operator==(const FImGuiAssetTagFilter& Other) const { return Other.TagName == TagName && Other.ExpectedValue == ExpectedValue; }
};
struct FImGuiAllowedClassFilter
{
	FSoftClassPath ClassPath;

	bool operator==(const FImGuiAllowedClassFilter& Other) const { return Other.ClassPath == ClassPath; }
};
struct FImGuiDisallowedClassFilter
{
	FSoftClassPath ClassPath;

	bool operator==(const FImGuiDisallowedClassFilter& Other) const { return Other.ClassPath == ClassPath; }
};
struct FImGuiRequiredInterfaceFilter
{
	FSoftClassPath ClassPath;

	bool operator==(const FImGuiRequiredInterfaceFilter& Other) const { return Other.ClassPath == ClassPath; }
};
struct FImGuiDisallowAbstractClassFilter
{
	bool operator==(const FImGuiDisallowAbstractClassFilter&) const { return true; }
};

namespace FImGui
{
	IMGUIWIDGETS_API FImGuiAssetTagFilter MakeBlueprintSubClassFilter(const TNonNullPtr<UClass>& ParentClass);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////

template <bool bRequireRangeCheck>
class FImGuiAllocator : public FDefaultAllocator
{
public:
	enum { RequireRangeCheck = bRequireRangeCheck };
	using Super = FDefaultAllocator;
};

using FImGuiAllocatorWithRangeCheck = FImGuiAllocator<true>;
using FImGuiAllocatorWithoutRangeCheck = FImGuiAllocator<false>;

template <>
struct TAllocatorTraits<FImGuiAllocatorWithRangeCheck> : TAllocatorTraits<FImGuiAllocatorWithRangeCheck::Super>
{
};
template <>
struct TAllocatorTraits<FImGuiAllocatorWithoutRangeCheck> : TAllocatorTraits<FImGuiAllocatorWithoutRangeCheck::Super>
{
};

template <>
struct TCanMoveBetweenAllocators<FDefaultAllocator, FImGuiAllocatorWithRangeCheck>
{
	enum { Value = true };
};
template <>
struct TCanMoveBetweenAllocators<FDefaultAllocator, FImGuiAllocatorWithoutRangeCheck>
{
	enum { Value = true };
};
