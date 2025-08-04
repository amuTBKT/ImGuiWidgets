// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ImGuiCommonWidgets.h"
#include "UObject/WeakObjectPtr.h"

class FImGuiAssetPicker
{
public:
	struct FFilter
	{
		FName AssetTag = NAME_None;
		FString TagValue;
	};
	IMGUIWIDGETS_API static FFilter MakeBlueprintSubClassFilter(const TNonNullPtr<UClass>& ParentClass);

	IMGUIWIDGETS_API static FImGuiAssetPicker MakeWidget(const TNonNullPtr<UClass>& Class, TArray<FFilter> OptionalFilters = {});
	FORCEINLINE static FImGuiAssetPicker MakeWidget(const TNonNullPtr<UClass>& Class, FFilter OptionalFilter)
	{
		return MakeWidget(Class, TArray<FFilter>{ MoveTemp(OptionalFilter) });
	}
	
	template <typename TAssetType>
	FORCEINLINE bool Draw(ImGuiContext* Context, const char* Label, TAssetType*& InOutSelectedAssetPtr)
	{
		if (!IsValid(AssetType))
		{
			DrawInvalidWidget(Context, Label, "'AssetType' unset!");
			return false;
		}
		if (TAssetType::StaticClass() != AssetType)
		{
			DrawInvalidWidget(Context, Label, "Draw() called with unsupported asset type!");
			return false;
		}

		UObject* SelectedAsset = InOutSelectedAssetPtr;
		if (DrawInternal(Context, Label, SelectedAsset))
		{
			InOutSelectedAssetPtr = Cast<TAssetType>(SelectedAsset);
			return true;
		}
		return false;
	}

	template <typename TAssetType>
	FORCEINLINE bool Draw(ImGuiContext* Context, const char* Label, TWeakObjectPtr<TAssetType>& InOutSelectedAssetPtr)
	{
		TAssetType* SelectedAsset = InOutSelectedAssetPtr.Get();
		if (Draw(Context, Label, SelectedAsset))
		{
			InOutSelectedAssetPtr = SelectedAsset;
			return true;
		}
		return false;
	}

private:
	IMGUIWIDGETS_API void DrawInvalidWidget(ImGuiContext* Context, const char* Label, const char* ErrorMessage);
	IMGUIWIDGETS_API bool DrawInternal(ImGuiContext* Context, const char* Label, UObject*& InOutSelectedAsset);
	void FilterAvailableAssets();

private:
	const UClass* AssetType = nullptr;
	TArray<FFilter> OptionalFilters;

	FImGuiTextFilter TextFilter = FImGuiTextFilter::MakeWidget(64u);

	TArray<int32> FilteredAssetIndices;
	uint8 PackedAssetPathFilter : 7 = 0;
	uint8 bIsAssetViewerVisible : 1 = false;
	uint32 ContainerRevisionId = UINT32_MAX;
	int32 LastSelectedAssetIndex = INDEX_NONE;
	int32 LastSelectedAssetIndexInFilteredList = INDEX_NONE;
	TWeakObjectPtr<UObject> LastSelectedAssetPtr;
};
