// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ImGuiWidgets.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/SoftObjectPtr.h"

class FImGuiAssetPicker
{
public:
	using FFilter = TVariant<FImGuiAssetTagFilter, FImGuiAllowedClassFilter, FImGuiDisallowedClassFilter>;

	[[nodiscard]] IMGUIWIDGETS_API static FImGuiAssetPicker MakeWidget(const FSoftClassPath& ClassPath, TArray<FFilter> OptionalFilters = {});

	template <typename TImGuiFilterType>
	[[nodiscard]] FORCEINLINE FImGuiAssetPicker& AddFilter(TImGuiFilterType Filter)
	{
		OptionalFilters.Add(FFilter(TInPlaceType<TImGuiFilterType>(), MoveTemp(Filter)));
		return *this;
	}

	template <typename TImGuiFilterType>
	void DisableFilter(TImGuiFilterType Filter)
	{
		for (int32 FilterIndex = 0; FilterIndex < OptionalFilters.Num(); ++FilterIndex)
		{
			const TImGuiFilterType* ExistingFilter = OptionalFilters[FilterIndex].TryGet<TImGuiFilterType>();
			if (ExistingFilter && *ExistingFilter == Filter)
			{
				OptionalFilters.RemoveAt(FilterIndex);
				ContainerRevisionId = UINT32_MAX;
				break;
			}
		}
	}
	template <typename TImGuiFilterType>
	void EnableFilter(TImGuiFilterType Filter)
	{
		for (int32 FilterIndex = 0; FilterIndex < OptionalFilters.Num(); ++FilterIndex)
		{
			const TImGuiFilterType* ExistingFilter = OptionalFilters[FilterIndex].TryGet<TImGuiFilterType>();
			if (ExistingFilter && *ExistingFilter == Filter)
			{
				return;
			}
		}
		OptionalFilters.Add(FFilter(TInPlaceType<TImGuiFilterType>(), MoveTemp(Filter)));
		ContainerRevisionId = UINT32_MAX;
	}

	template <typename TAssetType>
	FORCEINLINE bool Draw(FImGuiTickContext* Context, const char* Label, TSoftObjectPtr<TAssetType>& InOutSelectedAssetPtr, bool bDrawCompactWidget = false)
	{
		const UClass* AssetClass = GetAssetClass();
		if (!AssetClass)
		{
			DrawInvalidWidget(Context, Label, "'AssetType' unset!", bDrawCompactWidget);
			return false;
		}
		if (TAssetType::StaticClass() != AssetClass)
		{
			DrawInvalidWidget(Context, Label, "Draw() called with unsupported asset type!", bDrawCompactWidget);
			return false;
		}

		FSoftObjectPtr SelectedAsset{ InOutSelectedAssetPtr.ToSoftObjectPath() };
		if (DrawInternal(Context, Label, SelectedAsset, bDrawCompactWidget))
		{
			InOutSelectedAssetPtr = TSoftObjectPtr<TAssetType>{ SelectedAsset.ToSoftObjectPath() };
			return true;
		}
		return false;
	}

	template <typename TAssetType>
	FORCEINLINE bool Draw(FImGuiTickContext* Context, const char* Label, TAssetType*& InOutSelectedAssetPtr, bool bDrawCompactWidget = false)
	{
		const UClass* AssetClass = GetAssetClass();
		if (!AssetClass)
		{
			DrawInvalidWidget(Context, Label, "'AssetType' unset!", bDrawCompactWidget);
			return false;
		}
		if (TAssetType::StaticClass() != AssetClass)
		{
			DrawInvalidWidget(Context, Label, "Draw() called with unsupported asset type!", bDrawCompactWidget);
			return false;
		}

		FSoftObjectPtr SelectedAsset{ InOutSelectedAssetPtr };
		if (DrawInternal(Context, Label, SelectedAsset, bDrawCompactWidget))
		{
			InOutSelectedAssetPtr = Cast<TAssetType>(SelectedAsset.LoadSynchronous());
			return true;
		}
		return false;
	}

	template <typename TAssetType>
	FORCEINLINE bool Draw(FImGuiTickContext* Context, const char* Label, TWeakObjectPtr<TAssetType>& InOutSelectedAssetPtr, bool bDrawCompactWidget = false)
	{
		TAssetType* SelectedAsset = InOutSelectedAssetPtr.Get();
		if (Draw(Context, Label, SelectedAsset, bDrawCompactWidget))
		{
			InOutSelectedAssetPtr = SelectedAsset;
			return true;
		}
		return false;
	}

	template <typename TAssetType>
	FORCEINLINE bool Draw(FImGuiTickContext* Context, const char* Label, TObjectPtr<TAssetType>& InOutSelectedAssetPtr, bool bDrawCompactWidget = false)
	{
		TAssetType* SelectedAsset = InOutSelectedAssetPtr.Get();
		if (Draw(Context, Label, SelectedAsset, bDrawCompactWidget))
		{
			InOutSelectedAssetPtr = SelectedAsset;
			return true;
		}
		return false;
	}

private:
	IMGUIWIDGETS_API void DrawInvalidWidget(FImGuiTickContext* Context, const char* Label, const char* ErrorMessage, bool bDrawCompactWidget);
	IMGUIWIDGETS_API bool DrawInternal(FImGuiTickContext* Context, const char* Label, FSoftObjectPtr& InOutSelectedAsset, bool bDrawCompactWidget);
	void FilterAvailableAssets();

	const UClass* GetAssetClass()
	{
		UClass* AssetClass = AssetClassPtr.Get();
		if (AssetClass)
		{
			return AssetClass;
		}

		// check `bIsAssetTypeValid` to avoid spamming load
		if (bIsAssetTypeValid)
		{
			AssetClassPtr = AssetClass = Cast<UClass>(AssetClassPath.TryLoad());
			if (!AssetClass)
			{
				bIsAssetTypeValid = false;
			}
		}
		return AssetClass;
	}

private:
	FSoftClassPath AssetClassPath;
	TWeakObjectPtr<UClass> AssetClassPtr;
	TArray<FFilter> OptionalFilters;

	FImGuiTextFilter TextFilter = FImGuiTextFilter::MakeWidget(64u);

	TArray<int32> FilteredAssetIndices;
	uint32 PackedAssetPathFilter : 7 = 0;
	uint32 bIsAssetTypeValid	 : 1 = true;
	uint32 AssetViewerMinSizeFactor : 8 = 5; 
	uint32 ContainerRevisionId = UINT32_MAX;
	int32 LastSelectedAssetIndex = INDEX_NONE;
	int32 LastSelectedAssetIndexInFilteredList = INDEX_NONE;
	FSoftObjectPtr LastSelectedAssetPtr;
};
