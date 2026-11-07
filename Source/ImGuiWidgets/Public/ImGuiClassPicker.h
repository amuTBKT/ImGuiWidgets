// Copyright 2026 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ImGuiWidgets.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/WeakObjectPtr.h"

enum class EImGuiClassPickerFlags
{
	None		 = 0,
	ShowListOnly = 1 << 0,	// Draw only asset list box, can be used with ImGui::BeginMenu
};
ENUM_CLASS_FLAGS(EImGuiClassPickerFlags);

class FImGuiClassPicker
{
public:
	using FFilter = TVariant<FImGuiAllowedClassFilter, FImGuiDisallowedClassFilter, FImGuiRequiredInterfaceFilter, FImGuiDisallowAbstractClassFilter>;

	[[nodiscard]] IMGUIWIDGETS_API static FImGuiClassPicker MakeWidget(const FSoftClassPath& ClassPath, TArray<FFilter> OptionalFilters = {});

	FORCEINLINE FImGuiClassPicker& SetMaxRowsVisible(uint8 Count)
	{
		MaxRowsVisible = Count;
		return *this;
	}

	template <typename TImGuiFilterType>
	[[nodiscard]] FORCEINLINE FImGuiClassPicker& AddFilter(TImGuiFilterType Filter)
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

	template <typename TClassType>
	FORCEINLINE bool Draw(FImGuiTickContext* Context, const char* Label, TSoftClassPtr<TClassType>& InOutSelectedClassPtr, EImGuiClassPickerFlags Flags = EImGuiClassPickerFlags::None)
	{
		const UClass* BaseClass = GetBaseClass();
		if (!BaseClass)
		{
			DrawInvalidWidget(Context, Label, "'BaseClassType' unset!");
			return false;
		}

		FSoftObjectPtr SelectedClass{ InOutSelectedClassPtr.ToSoftObjectPath() };
		if (DrawInternal(Context, Label, SelectedClass, Flags))
		{
			InOutSelectedClassPtr = TSoftClassPtr<TClassType>{ SelectedClass.ToSoftObjectPath() };
			return true;
		}
		return false;
	}

	FORCEINLINE bool Draw(FImGuiTickContext* Context, const char* Label, UClass*& InOutSelectedClassPtr, EImGuiClassPickerFlags Flags = EImGuiClassPickerFlags::None)
	{
		const UClass* BaseClass = GetBaseClass();
		if (!BaseClass)
		{
			DrawInvalidWidget(Context, Label, "'BaseClassType' unset!");
			return false;
		}

		FSoftObjectPtr SelectedClass{ InOutSelectedClassPtr };
		if (DrawInternal(Context, Label, SelectedClass, Flags))
		{
			InOutSelectedClassPtr = Cast<UClass>(SelectedClass.LoadSynchronous());
			return true;
		}
		return false;
	}

	FORCEINLINE bool Draw(FImGuiTickContext* Context, const char* Label, TWeakObjectPtr<UClass>& InOutSelectedClassPtr, EImGuiClassPickerFlags Flags = EImGuiClassPickerFlags::None)
	{
		UClass* SelectedClass = InOutSelectedClassPtr.Get();
		if (Draw(Context, Label, SelectedClass, Flags))
		{
			InOutSelectedClassPtr = SelectedClass;
			return true;
		}
		return false;
	}

private:
	IMGUIWIDGETS_API void DrawInvalidWidget(FImGuiTickContext* Context, const char* Label, const char* ErrorMessage);
	IMGUIWIDGETS_API bool DrawInternal(FImGuiTickContext* Context, const char* Label, FSoftObjectPtr& InOutSelectedClass, EImGuiClassPickerFlags Flags);
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

	FImGuiTextFilter TextFilter;

	TArray<int32> FilteredClassIndices;
	uint32 PackedClassFilter	 : 7 = 0;
	uint32 bIsBaseClassValid	 : 1 = true;
	uint32 MaxRowsVisible		 : 8 = 10;
	uint32 ContainerRevisionId = UINT32_MAX;
	int32 LastSelectedClassIndex = INDEX_NONE;
	int32 LastSelectedClassIndexInFilteredList = INDEX_NONE;
	FSoftObjectPtr LastSelectedClassPtr;
};
