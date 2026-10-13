// Copyright 2026 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "ImGuiPluginTypes.h"
#include "Input/DragAndDrop.h"

#include "Styling/AppStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/StyleColors.h"

// commonly used function to get ImGui icons/brushes
#define IMGUI_ICON(IconName) IMGUI_STYLE_ICON("ImGuiStyle", IconName)
#define IMGUI_ICON_BRUSH(IconName) IMGUI_STYLE_ICON_BRUSH("ImGuiStyle", IconName)

///////////////////////////////////////////////////////////////////////////////////////////////////////

namespace FImGui
{
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
			const ImU32 ValidColor = IM_COL32(38, 187, 255, 255);
			const ImU32 InvalidColor = IM_COL32(239, 53, 53, 255);
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
	IMGUIWIDGETS_API bool Draw(FImGuiTickContext* Context, const char* Label, const char* HintText = nullptr, bool bSetFocus = false, bool bActivateOnNavFocus = false);
	IMGUIWIDGETS_API void Reset();
	IMGUIWIDGETS_API void SetText(const char* Text);
	FORCEINLINE FImGuiTextFilter& SetStyle(const FSearchBoxStyle& Style)
	{
		SlateStyle = Style;
		return *this;
	}

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
		SlateStyle = Other.SlateStyle;

		FilterStringBuffer = Other.FilterStringBuffer;
		FilterStringBuffer_ANSI = Other.FilterStringBuffer_ANSI;
		FilterKeywordTokens = Other.FilterKeywordTokens;
		FilterKeywordTokens_ANSI = Other.FilterKeywordTokens_ANSI;

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

	void OnFilterChanged();

private:
	// TODO: using optional here as we don't want static initialization (Slate not initialized in time for it)
	TOptional<FSearchBoxStyle> SlateStyle;
	TArray<TCHAR> FilterStringBuffer;
	TArray<ANSICHAR> FilterStringBuffer_ANSI;
	TArray<TPair<uint16, uint16>> FilterKeywordTokens;
	TArray<TPair<uint16, uint16>> FilterKeywordTokens_ANSI;
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
