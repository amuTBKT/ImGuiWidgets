// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ImGuiCommonWidgets.h"
#include "UObject/SoftObjectPtr.h"
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
	IMGUIWIDGETS_API static FImGuiClassPicker MakeWidget(const FSoftClassPath& ClassPath, FFilters OptionalFilters = {});
	
	template <typename TClassType>
	FORCEINLINE bool Draw(ImGuiContext* Context, const char* Label, TSoftClassPtr<TClassType>& InOutSelectedClassPtr)
	{
		if (!BaseClassPath.IsValid())
		{
			DrawInvalidWidget(Context, Label, "'BaseClassType' unset!");
			return false;
		}

		FSoftObjectPtr SelectedClass{ InOutSelectedClassPtr.ToSoftObjectPath() };
		if (DrawInternal(Context, Label, SelectedClass))
		{
			InOutSelectedClassPtr = TSoftClassPtr<TClassType>{ SelectedClass.ToSoftObjectPath() };
			return true;
		}
		return false;
	}

	FORCEINLINE bool Draw(ImGuiContext* Context, const char* Label, UClass*& InOutSelectedClassPtr)
	{
		if (!BaseClassPath.IsValid())
		{
			DrawInvalidWidget(Context, Label, "'BaseClassType' unset!");
			return false;
		}

		FSoftObjectPtr SelectedClass{ InOutSelectedClassPtr };
		if (DrawInternal(Context, Label, SelectedClass))
		{
			InOutSelectedClassPtr = Cast<UClass>(SelectedClass.LoadSynchronous());
			return true;
		}
		return false;
	}

	FORCEINLINE bool Draw(ImGuiContext* Context, const char* Label, TWeakObjectPtr<UClass>& InOutSelectedClassPtr)
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
	IMGUIWIDGETS_API bool DrawInternal(ImGuiContext* Context, const char* Label, FSoftObjectPtr& InOutSelectedClass);
	void FilterAvailableClasses();

private:
	FSoftClassPath BaseClassPath;
	FImGuiTextFilter TextFilter = FImGuiTextFilter::MakeWidget(32u);

	TArray<int32> FilteredClassIndices;
	uint8 PackedClassFilter : 7 = 0;
	uint8 bIsClassViewerVisible : 1 = false;
	uint32 ContainerRevisionId = UINT32_MAX;
	int32 LastSelectedClassIndex = INDEX_NONE;
	int32 LastSelectedClassIndexInFilteredList = INDEX_NONE;
	FSoftObjectPtr LastSelectedClassPtr;
};
