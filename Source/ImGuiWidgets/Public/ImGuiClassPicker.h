// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ImGuiCommonWidgets.h"
#include "UObject/WeakObjectPtr.h"

class FImGuiClassPicker
{
public:
	struct FFilters
	{
		// allow abstract
		// allowed classes
		// disallowed classes
		// required interface
	};
	IMGUIWIDGETS_API static FImGuiClassPicker MakeWidget(const TNonNullPtr<UClass>& Class, FFilters OptionalFilters = {});
	
	FORCEINLINE bool Draw(ImGuiContext* Context, const char* Label, UClass*& InOutSelectedClassPtr)
	{
		if (!BaseClassType)
		{
			DrawInvalidWidget(Context, Label, "'BaseClassType' unset!");
			return false;
		}

		UClass* SelectedClass = InOutSelectedClassPtr;
		if (DrawInternal(Context, Label, SelectedClass))
		{
			InOutSelectedClassPtr = SelectedClass;
			return true;
		}
		return false;
	}

	template <typename TAssetType>
	FORCEINLINE bool Draw(ImGuiContext* Context, const char* Label, TWeakObjectPtr<TAssetType>& InOutSelectedClassPtr)
	{
		UClass* SelectedClass = InOutSelectedClassPtr.Get();
		if (Draw(Context, Label, SelectedClass))
		{
			InOutSelectedClassPtr = SelectedClass;
			return true;
		}
		return false;
	}

private:
	IMGUIWIDGETS_API void DrawInvalidWidget(ImGuiContext* Context, const char* Label, const char* ErrorMessage);
	IMGUIWIDGETS_API bool DrawInternal(ImGuiContext* Context, const char* Label, UClass*& InOutSelectedClass);
	void FilterAvailableClasses();

private:
	const UClass* BaseClassType = nullptr;
	FImGuiTextFilter TextFilter = FImGuiTextFilter::MakeWidget(32u);

	TArray<int32> FilteredClassIndices;
	uint8 PackedClassFilter : 7 = 0;
	uint8 bIsClassViewerVisible : 1 = false;
	uint32 ContainerRevisionId = UINT32_MAX;
	int32 LastSelectedClassIndex = INDEX_NONE;
	int32 LastSelectedClassIndexInFilteredList = INDEX_NONE;
	TWeakObjectPtr<UClass> LastSelectedClassPtr;
};
