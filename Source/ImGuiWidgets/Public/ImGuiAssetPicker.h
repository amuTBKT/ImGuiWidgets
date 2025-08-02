// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ImGuiCommonWidgets.h"
#include "UObject/WeakObjectPtr.h"

class FImGuiAssetPicker
{
public:
	IMGUIWIDGETS_API static FImGuiAssetPicker MakeWidget(UClass* Class);
	
	template <typename TAssetType>
	FORCEINLINE bool Draw(ImGuiContext* Context, const char* Label, TAssetType*& InOutSelectedAssetPtr)
	{
		if (!ensure(TAssetType::StaticClass() == AssetType))
		{
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
	IMGUIWIDGETS_API bool DrawInternal(ImGuiContext* Context, const char* Label, UObject*& InOutSelectedAsset);
	void FilterAvailableAssets();

private:
	const UClass* AssetType = nullptr;
	const UClass* AssetSubType = nullptr;

	FImGuiTextFilter TextFilter = FImGuiTextFilter::MakeWidget(64u);

	TArray<int32> FilteredAssetIndices;
	uint8 PackedAssetPathFilter : 7 = 0;
	uint8 bIsAssetViewerVisible : 1 = false;
	uint32 ContainerRevisionId = UINT32_MAX;
	int32 LastSelectedAssetIndex = INDEX_NONE;
	int32 LastSelectedAssetIndexInFilteredList = INDEX_NONE;
	TWeakObjectPtr<UObject> LastSelectedAssetPtr;
};
