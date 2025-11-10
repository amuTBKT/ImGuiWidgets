// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ImGuiWidgets.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/WeakObjectPtr.h"

class FImGuiClassPicker
{
public:
	using FFilter = TVariant<FImGuiAllowedClassFilter, FImGuiDisallowedClassFilter, FImGuiRequiredInterfaceFilter, FImGuiDisallowAbstractClassFilter>;

	IMGUIWIDGETS_API static FImGuiClassPicker MakeWidget(const FSoftClassPath& ClassPath, TArray<FFilter> OptionalFilters = {});
	
	template <typename TImGuiFilterType>
	FORCEINLINE FImGuiClassPicker& AddFilter(TImGuiFilterType Filter)
	{
		OptionalFilters.Add(FFilter(TInPlaceType<TImGuiFilterType>(), MoveTemp(Filter)));
		return *this;
	}

	template <typename TClassType>
	FORCEINLINE bool Draw(FImGuiTickContext* Context, const char* Label, TSoftClassPtr<TClassType>& InOutSelectedClassPtr)
	{
		const UClass* BaseClass = GetBaseClass();
		if (!BaseClass)
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

	FORCEINLINE bool Draw(FImGuiTickContext* Context, const char* Label, UClass*& InOutSelectedClassPtr)
	{
		const UClass* BaseClass = GetBaseClass();
		if (!BaseClass)
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

	FORCEINLINE bool Draw(FImGuiTickContext* Context, const char* Label, TWeakObjectPtr<UClass>& InOutSelectedClassPtr)
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
	IMGUIWIDGETS_API void DrawInvalidWidget(FImGuiTickContext* Context, const char* Label, const char* ErrorMessage);
	IMGUIWIDGETS_API bool DrawInternal(FImGuiTickContext* Context, const char* Label, FSoftObjectPtr& InOutSelectedClass);
	void FilterAvailableClasses();

	const UClass* GetBaseClass()
	{
		UClass* BaseClass = BaseClassPtr.Get();
		if (BaseClass)
		{
			return BaseClass;
		}

		// check `bIsBaseClassValid` to avoid spamming load
		if (bIsBaseClassValid)
		{
			BaseClassPtr = BaseClass = Cast<UClass>(BaseClassPath.TryLoad());
			if (!BaseClass)
			{
				bIsBaseClassValid = false;
			}
		}
		return BaseClass;
	}

private:
	FSoftClassPath BaseClassPath;
	TWeakObjectPtr<UClass> BaseClassPtr;
	TArray<FFilter> OptionalFilters;

	FImGuiTextFilter TextFilter = FImGuiTextFilter::MakeWidget(32u);

	TArray<int32> FilteredClassIndices;
	uint16 PackedClassFilter	 : 7 = 0;
	uint16 bIsClassViewerVisible : 1 = false;
	uint16 bIsBaseClassValid	 : 1 = true;
	uint32 ContainerRevisionId = UINT32_MAX;
	int32 LastSelectedClassIndex = INDEX_NONE;
	int32 LastSelectedClassIndexInFilteredList = INDEX_NONE;
	FSoftObjectPtr LastSelectedClassPtr;
};
