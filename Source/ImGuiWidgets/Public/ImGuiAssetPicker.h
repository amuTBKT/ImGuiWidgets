// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ImGuiCommonWidgets.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/SoftObjectPtr.h"

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
	FORCEINLINE bool Draw(ImGuiContext* Context, const char* Label, TSoftObjectPtr<TAssetType>& InOutSelectedAssetPtr, bool bDrawSimpleWidget = false)
	{
		if (!AssetType)
		{
			DrawInvalidWidget(Context, Label, "'AssetType' unset!", bDrawSimpleWidget);
			return false;
		}
		if (TAssetType::StaticClass() != AssetType)
		{
			DrawInvalidWidget(Context, Label, "Draw() called with unsupported asset type!", bDrawSimpleWidget);
			return false;
		}

		FSoftObjectPtr SelectedAsset{ InOutSelectedAssetPtr.ToSoftObjectPath() };
		if (DrawInternal(Context, Label, SelectedAsset, bDrawSimpleWidget))
		{
			InOutSelectedAssetPtr = TSoftObjectPtr<TAssetType>{ SelectedAsset.ToSoftObjectPath() };
			return true;
		}
		return false;
	}

	template <typename TAssetType>
	FORCEINLINE bool Draw(ImGuiContext* Context, const char* Label, TAssetType*& InOutSelectedAssetPtr, bool bDrawSimpleWidget = false)
	{
		if (!AssetType)
		{
			DrawInvalidWidget(Context, Label, "'AssetType' unset!", bDrawSimpleWidget);
			return false;
		}
		if (TAssetType::StaticClass() != AssetType)
		{
			DrawInvalidWidget(Context, Label, "Draw() called with unsupported asset type!", bDrawSimpleWidget);
			return false;
		}

		FSoftObjectPtr SelectedAsset{ InOutSelectedAssetPtr };
		if (DrawInternal(Context, Label, SelectedAsset, bDrawSimpleWidget))
		{
			InOutSelectedAssetPtr = Cast<TAssetType>(SelectedAsset.LoadSynchronous());
			return true;
		}
		return false;
	}

	template <typename TAssetType>
	FORCEINLINE bool Draw(ImGuiContext* Context, const char* Label, TWeakObjectPtr<TAssetType>& InOutSelectedAssetPtr, bool bDrawSimpleWidget = false)
	{
		TAssetType* SelectedAsset = InOutSelectedAssetPtr.Get();
		if (Draw(Context, Label, SelectedAsset, bDrawSimpleWidget))
		{
			InOutSelectedAssetPtr = SelectedAsset;
			return true;
		}
		return false;
	}

	template <typename TAssetType>
	FORCEINLINE bool Draw(ImGuiContext* Context, const char* Label, TObjectPtr<TAssetType>& InOutSelectedAssetPtr, bool bDrawSimpleWidget = false)
	{
		TAssetType* SelectedAsset = InOutSelectedAssetPtr.Get();
		if (Draw(Context, Label, SelectedAsset, bDrawSimpleWidget))
		{
			InOutSelectedAssetPtr = SelectedAsset;
			return true;
		}
		return false;
	}

private:
	IMGUIWIDGETS_API void DrawInvalidWidget(ImGuiContext* Context, const char* Label, const char* ErrorMessage, bool bDrawSimpleWidget);
	IMGUIWIDGETS_API bool DrawInternal(ImGuiContext* Context, const char* Label, FSoftObjectPtr& InOutSelectedAsset, bool bDrawSimpleWidget);
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
	FSoftObjectPtr LastSelectedAssetPtr;
};
