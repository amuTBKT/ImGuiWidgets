// Copyright 2026 Amit Kumar Mehar. All Rights Reserved.

#include "ImGuiClassPicker.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#endif

#include "ImGuiSubsystem.h"
#include "Engine/Blueprint.h"
#include "HAL/LowLevelMemStats.h"
#include "AssetRegistry/AssetData.h"
#include "UObject/UObjectIterator.h"
#include "Blueprint/BlueprintSupport.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "AssetRegistry/AssetRegistryModule.h"

DECLARE_LLM_MEMORY_STAT(TEXT("ImGuiClassPicker"), STAT_ImGuiClassPickerLLM, STATGROUP_ImGui);
LLM_DEFINE_TAG(ImGuiClassPicker, TEXT("ClassPicker"), TEXT("ImGui"), GET_STATFNAME(STAT_ImGuiClassPickerLLM));

namespace ClassPickerUtils
{
	static FORCEINLINE FSoftClassPath GetClassPathForAsset(const FString& AssetObjectPath)
	{
		// NOTE: FSoftClassPath(ClassPtr) expects '_C' to be present
		return FSoftClassPath(AssetObjectPath + TEXT("_C"));
	}
	static FORCEINLINE FSoftClassPath GetClassPathForAsset(const FAssetData& Asset)
	{
		return GetClassPathForAsset(Asset.GetObjectPathString());
	}
	static FORCEINLINE FAssetData MakeAssetDataFromClassPath(const FSoftObjectPath& AssetClassPath, const FSoftClassPath& BaseClassPath)
	{
		FString PackageName = AssetClassPath.GetLongPackageName();
		FString AssetName = AssetClassPath.GetAssetPathString();
		AssetName.RemoveFromEnd(TEXT("_C"));

		return FAssetData(MoveTemp(PackageName), MoveTemp(AssetName), FTopLevelAssetPath{ BaseClassPath.ToString()});
	}

	static TMap<FSoftClassPath, FSoftClassPath> ParentClassMap;
	static void CacheAssetParentClass(const FAssetData& AssetData)
	{
		FString ParentClassPathString;
		if (AssetData.GetTagValue(FBlueprintTags::ParentClassPath, ParentClassPathString))
		{
			FSoftClassPath ClassPath = GetClassPathForAsset(AssetData);
			ParentClassMap.FindOrAdd(ClassPath) = FSoftClassPath(ParentClassPathString);
		}
	}
	static bool IsClassChildOf(const FSoftClassPath& ClassPath, const FSoftClassPath& ParentClassPath)
	{
		if (ClassPath == ParentClassPath)
		{
			return true;
		}

		const FSoftClassPath* Found = ParentClassMap.Find(ClassPath);
		while (Found)
		{
			if (*Found == ParentClassPath)
			{
				return true;
			}
			Found = ParentClassMap.Find(*Found);
		}
		return false;
	}

	class FClassContainer : FNoncopyable
	{
	public:
		struct FClassData
		{
			FSoftClassPath ClassPath;
			FString ParentClassPath;
			FAnsiString ObjectPath;
			FAnsiString DisplayName;

			uint8 bIsAsset : 1 = false;
			uint8 bIsAbstractClass : 1 = false;

			TArray<FSoftClassPath> ImplementedInterfaces;

			friend bool operator==(const FClassData& A, const FClassData& B)
			{
				return A.ClassPath == B.ClassPath;
			}

			bool operator==(const FSoftClassPath& Path) const
			{
				return ClassPath == Path;
			}
		};

		FClassContainer()
		{
			LLM_SCOPE_BYTAG(ImGuiClassPicker);

			if (auto AssetRegistryModulePtr = FModuleManager::GetModulePtr<FAssetRegistryModule>(FName("AssetRegistry")))
			{
				IAssetRegistry& AssetRegistry = AssetRegistryModulePtr->Get();

				auto AddAsset = [&](const FAssetData& AssetData)
				{
					FClassData ClassData{};
					ClassData.ClassPath = GetClassPathForAsset(AssetData);
					ClassData.DisplayName = TCHAR_TO_UTF8(*AssetData.AssetName.ToString());
					ClassData.ObjectPath = TCHAR_TO_UTF8(*ClassData.ClassPath.ToString());
					ClassData.bIsAsset = true;

					CacheAssetMetadata(ClassData, AssetData);

					AvailableClasses.AddUnique(ClassData);
				};

				TArray<FAssetData> BlueprintAssets;
				TArray<FAssetData> GeneratedBlueprintAssets;
				AssetRegistry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), BlueprintAssets, /*bSearchSubClasses=*/true);
				AssetRegistry.GetAssetsByClass(UBlueprintGeneratedClass::StaticClass()->GetClassPathName(), GeneratedBlueprintAssets, /*bSearchSubClasses=*/true);

				AvailableClasses.Reserve(BlueprintAssets.Num() + GeneratedBlueprintAssets.Num());
				for (const FAssetData& Asset : BlueprintAssets)
				{
					AddAsset(Asset);
				}
				for (const FAssetData& Asset : GeneratedBlueprintAssets)
				{
					AddAsset(Asset);
				}

				AssetRegistry.OnAssetAdded().AddRaw(this, &FClassContainer::OnAssetAdded);
				AssetRegistry.OnAssetRemoved().AddRaw(this, &FClassContainer::OnAssetRemoved);
				AssetRegistry.OnAssetRenamed().AddRaw(this, &FClassContainer::OnAssetRenamed);
				AssetRegistry.OnAssetUpdated().AddRaw(this, &FClassContainer::OnAssetUpdated);
			}

			for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
			{
				UClass* CurrentClass = *ClassIt;
				// Ignore deprecated and temporary trash classes.
				if (CurrentClass->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Hidden) ||
#if WITH_EDITOR
					FKismetEditorUtilities::IsClassABlueprintSkeleton(CurrentClass) ||
#endif
					FBlueprintSupport::IsClassPlaceholder(CurrentClass))
				{
					continue;
				}

				FClassData ClassData{};
				ClassData.ClassPath = FSoftClassPath(CurrentClass);
				ClassData.DisplayName = TCHAR_TO_UTF8(*CurrentClass->GetName());
				ClassData.ObjectPath = TCHAR_TO_UTF8(*ClassData.ClassPath.ToString());
				ClassData.bIsAsset = false;
				ClassData.bIsAbstractClass = CurrentClass->HasAnyClassFlags(EClassFlags::CLASS_Abstract);

				ClassData.ImplementedInterfaces.Reserve(CurrentClass->Interfaces.Num());
				for (FImplementedInterface& Interface : CurrentClass->Interfaces)
				{
					ClassData.ImplementedInterfaces.Emplace(Interface.Class);
				}

				AvailableClasses.AddUnique(ClassData);

				if (CurrentClass->GetSuperClass())
				{
					ClassData.ParentClassPath = CurrentClass->GetSuperClass()->GetPathName();
					ParentClassMap.FindOrAdd(FSoftClassPath{ CurrentClass }, FSoftClassPath{ CurrentClass->GetSuperClass() });
				}
			}

			AvailableClasses.Sort([](const auto& A, const auto& B) { return SortClassDataPredicate(A, B); });
		}

		~FClassContainer()
		{
			if (auto AssetRegistryModulePtr = FModuleManager::GetModulePtr<FAssetRegistryModule>(FName("AssetRegistry")))
			{
				IAssetRegistry& AssetRegistry = AssetRegistryModulePtr->Get();
				AssetRegistry.OnAssetAdded().RemoveAll(this);
				AssetRegistry.OnAssetRemoved().RemoveAll(this);
				AssetRegistry.OnAssetRenamed().RemoveAll(this);
				AssetRegistry.OnAssetUpdated().RemoveAll(this);
			}
		}

		uint32		GetRevisionId()			const { return RevisionId; }
		const auto& GetAvailableClasses()	const { return AvailableClasses; }

	private:
		static FORCEINLINE bool SortClassDataPredicate(const FClassData& LHS, const FClassData& RHS)
		{
			return LHS.DisplayName < RHS.DisplayName;
		}

		FORCEINLINE bool FilterAsset(const FAssetData& AssetData) const
		{
			return AssetData.GetClass() && AssetData.GetClass()->IsChildOf(UBlueprint::StaticClass());
		}

		bool CacheAssetMetadata(FClassData& ClassData, const FAssetData& AssetData)
		{
			bool bUpdated = false;

			FString ClassFlagsString;
			if (AssetData.GetTagValue(FBlueprintTags::ClassFlags, ClassFlagsString))
			{
				const uint32 ClassFlags = FCString::Atoi(*ClassFlagsString);
				const bool bIsAbstractClass = (ClassFlags & CLASS_Abstract) > 0u;
				if (ClassData.bIsAbstractClass != bIsAbstractClass)
				{
					ClassData.bIsAbstractClass = bIsAbstractClass;
					bUpdated = true;
				}
			}

			TArray<FSoftClassPath> ImplementedInterfaces;
			const FString ImplementedInterfacesString = AssetData.GetTagValueRef<FString>(FBlueprintTags::ImplementedInterfaces);
			if (!ImplementedInterfacesString.IsEmpty())
			{
				FString FullInterface;
				FString CurrentString = *ImplementedInterfacesString;
				while (CurrentString.Split(TEXT("Interface="), nullptr, &FullInterface))
				{
					// Cutoff at next )
					int32 RightParen = INDEX_NONE;
					if (FullInterface.FindChar(TCHAR(')'), RightParen))
					{
						// Keep parsing
						CurrentString = FullInterface.Mid(RightParen);

						// Strip class name
						FullInterface = *FPackageName::ExportTextPathToObjectPath(FullInterface.Left(RightParen));

						// Handle quotes
						FString InterfacePath;
						const TCHAR* NewBuffer = FPropertyHelpers::ReadToken(*FullInterface, InterfacePath, true);

						if (NewBuffer)
						{
							ImplementedInterfaces.Add(FSoftClassPath(InterfacePath));
						}
					}
				}
			}
			if (ClassData.ImplementedInterfaces != ImplementedInterfaces)
			{
				ClassData.ImplementedInterfaces = ImplementedInterfaces;
				bUpdated = true;
			}

			FString ParentClassPathString;
			if (AssetData.GetTagValue(FBlueprintTags::ParentClassPath, ParentClassPathString))
			{
				if (ClassData.ParentClassPath != ParentClassPathString)
				{
					ClassData.ParentClassPath = ParentClassPathString;
					bUpdated = true;

					CacheAssetParentClass(AssetData);
				}
			}

			return bUpdated;
		}

		void OnAssetAdded(const FAssetData& AssetData)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ClassPicker::OnAssetAdded"), STAT_ImGuiClassPicker_OnAssetAdded, STATGROUP_ImGui);
			LLM_SCOPE_BYTAG(ImGuiClassPicker);

			if (FilterAsset(AssetData))
			{
				RevisionId++;

				FSoftClassPath AssetClassPath = GetClassPathForAsset(AssetData);
				FAnsiString AssetDisplayName = TCHAR_TO_UTF8(*AssetData.AssetName.ToString());

				int32 InsertIndex = Algo::LowerBound(AvailableClasses, AssetDisplayName, [](const auto& A, const auto& B) { return A.DisplayName < B; });
				bool bExists = (AvailableClasses.IsValidIndex(InsertIndex) && AvailableClasses[InsertIndex].ClassPath == AssetClassPath);
				if (!bExists && InsertIndex >= 0)
				{
					FClassData ClassData{};
					ClassData.ClassPath = AssetClassPath;
					ClassData.DisplayName = AssetDisplayName;
					ClassData.ObjectPath = TCHAR_TO_UTF8(*ClassData.ClassPath.ToString());
					ClassData.bIsAsset = true;

					CacheAssetMetadata(ClassData, AssetData);

					AvailableClasses.Insert(ClassData, InsertIndex);
				}
			}
		}

		void OnAssetRemoved(const FAssetData& AssetData)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ClassPicker::OnAssetRemoved"), STAT_ImGuiClassPicker_OnAssetRemoved, STATGROUP_ImGui);
			LLM_SCOPE_BYTAG(ImGuiClassPicker);

			if (FilterAsset(AssetData))
			{
				RevisionId++;

				FSoftClassPath ClassPath = GetClassPathForAsset(AssetData);
				AvailableClasses.RemoveAll([ClassPath](const auto& Entry) { return Entry.ClassPath == ClassPath; });
			}
		}

		void OnAssetRenamed(const FAssetData& AssetData, const FString& OldName)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ClassPicker::OnAssetRenamed"), STAT_ImGuiClassPicker_OnAssetRenamed, STATGROUP_ImGui);
			LLM_SCOPE_BYTAG(ImGuiClassPicker);

			bool bReAddAsset = false;
			if (FilterAsset(AssetData))
			{
				FSoftClassPath ClassPath = GetClassPathForAsset(OldName);

				for (auto Itr = AvailableClasses.CreateIterator(); Itr; ++Itr)
				{
					if (Itr->ClassPath == ClassPath)
					{
						Itr.RemoveCurrent();
						bReAddAsset = true;
					}
				}
			}

			if (bReAddAsset)
			{
				OnAssetAdded(AssetData);
			}
		}

		// triggers when asset is saved!
		void OnAssetUpdated(const FAssetData& AssetData)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ClassPicker::OnAssetUpdated"), STAT_ImGuiClassPicker_OnAssetUpdated, STATGROUP_ImGui);
			LLM_SCOPE_BYTAG(ImGuiClassPicker);

			if (FilterAsset(AssetData))
			{
				bool bContainerModified = false;

				FSoftClassPath ClassPath = GetClassPathForAsset(AssetData);
				for (auto Itr = AvailableClasses.CreateIterator(); Itr; ++Itr)
				{
					if (Itr->ClassPath == ClassPath)
					{
						if (CacheAssetMetadata(*Itr, AssetData))
						{
							bContainerModified = true;
						}
					}
				}

				if (bContainerModified)
				{
					RevisionId++;
				}
			}
		}

	private:
		TArray<FClassData, FImGuiAllocatorWithoutRangeCheck> AvailableClasses;
		uint32 RevisionId = 0;
	};

	static FClassContainer& GetClassContainer()
	{
		static FClassContainer Instance = {};
		return Instance;
	}

	static FSoftObjectPtr GetSelectedClassOfType(const FSoftClassPath& AssetClassPath)
	{
#if WITH_EDITOR
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		TArray<FAssetData> SelectedAssets;
		ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

		for (const FAssetData& Asset : SelectedAssets)
		{
			if (ClassPickerUtils::IsClassChildOf(GetClassPathForAsset(Asset), AssetClassPath))
			{
				return FSoftObjectPtr{ GetClassPathForAsset(Asset) };
			}
		}
		return {};
#else
		return {};
#endif
	}

	static void SyncContentBrowserToAsset(const FSoftObjectPtr& InSoftClassPtr, const FSoftClassPath& BaseClassPath)
	{
#if WITH_EDITOR
		GEditor->SyncBrowserToObjects(TArray<FAssetData>{ MakeAssetDataFromClassPath(InSoftClassPtr.ToSoftObjectPath(), BaseClassPath) });
#endif
	}

	static uint8 GetPackedClassFilter()
	{
		return 0;
	}
}

FImGuiClassPicker FImGuiClassPicker::MakeWidget(const FSoftClassPath& ClassPath, TArray<FFilter> OptionalFilters)
{
	FImGuiClassPicker Widget = {};
	Widget.BaseClassPath = ClassPath;
	Widget.OptionalFilters = OptionalFilters;
	return Widget;
}

void FImGuiClassPicker::DrawInvalidWidget(FImGuiTickContext* Context, const char* Label, const char* ErrorMessage)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ClassPicker::Draw"), STAT_ImGuiClassPicker_Draw, STATGROUP_ImGui);
	LLM_SCOPE_BYNAME("ImGui/ClassPicker/DrawWidget");

	TAnsiStringBuilder<128> StringBuilder;
	StringBuilder.Appendf("ClassPicker('%s') : %s", Label, ErrorMessage);
	FImGui::DrawWarningMessageBox(Context, 4.f, ImVec4(1.f, 0.f, 0.f, 1.f), *StringBuilder);
}

bool FImGuiClassPicker::DrawInternal(FImGuiTickContext* Context, const char* Label, FSoftObjectPtr& InOutSelectedClass)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ClassPicker::Draw"), STAT_ImGuiClassPicker_Draw, STATGROUP_ImGui);
	LLM_SCOPE_BYNAME("ImGui/ClassPicker/DrawWidget");

	const UClass* BaseClass = GetBaseClass();
	if (!ensure(BaseClass))
	{
		return false;
	}

	FImGuiTickScope TickScope{ Context };

	FImGuiNamedScope WidgetScope{ Label };

	auto& ClassContainer = ClassPickerUtils::GetClassContainer();
	const auto& AvailableClasses = ClassContainer.GetAvailableClasses();

	FSoftObjectPtr SelectedSoftClassPtr = InOutSelectedClass;

	const bool bClassContainerChanged = (PackedClassFilter != ClassPickerUtils::GetPackedClassFilter()) || (ContainerRevisionId != ClassContainer.GetRevisionId());
	if (bClassContainerChanged || LastSelectedClassPtr.IsStale() || (SelectedSoftClassPtr != LastSelectedClassPtr))
	{
		LastSelectedClassIndex = !SelectedSoftClassPtr.IsNull() ? AvailableClasses.IndexOfByKey(FSoftClassPath(SelectedSoftClassPtr.ToString())) : INDEX_NONE;

		if (LastSelectedClassPtr.IsStale() || (LastSelectedClassIndex == INDEX_NONE))
		{
			SelectedSoftClassPtr.Reset();
		}
	}

	if (bClassContainerChanged)
	{
		FilterAvailableClasses();

		// check if selected class has become invalid now? (not relevant for this class picker)
		if ((LastSelectedClassIndex != INDEX_NONE) && AvailableClasses[LastSelectedClassIndex].bIsAsset && !OptionalFilters.IsEmpty())
		{
			bool bOptionalFiltersPassed = true;
			for (int32 FilterIndex = 0; FilterIndex < OptionalFilters.Num(); ++FilterIndex)
			{
				const FFilter& Filter = OptionalFilters[FilterIndex];
				if (Filter.IsType<FImGuiAllowedClassFilter>())
				{
					bOptionalFiltersPassed &= ClassPickerUtils::IsClassChildOf(AvailableClasses[LastSelectedClassIndex].ClassPath, Filter.Get<FImGuiAllowedClassFilter>().ClassPath);
				}
				else if (Filter.IsType<FImGuiDisallowedClassFilter>())
				{
					bOptionalFiltersPassed &= !ClassPickerUtils::IsClassChildOf(AvailableClasses[LastSelectedClassIndex].ClassPath, Filter.Get<FImGuiDisallowedClassFilter>().ClassPath);
				}
				else if (Filter.IsType<FImGuiRequiredInterfaceFilter>())
				{
					bOptionalFiltersPassed &= AvailableClasses[LastSelectedClassIndex].ImplementedInterfaces.Contains(Filter.Get<FImGuiRequiredInterfaceFilter>().ClassPath);
				}
				else if (Filter.IsType<FImGuiDisallowAbstractClassFilter>())
				{
					bOptionalFiltersPassed &= !AvailableClasses[LastSelectedClassIndex].bIsAbstractClass;
				}
			}
			if (!bOptionalFiltersPassed)
			{
				SelectedSoftClassPtr.Reset();
			}
		}
	}

	const auto* SelectedClassData = AvailableClasses.IsValidIndex(LastSelectedClassIndex) ? &AvailableClasses[LastSelectedClassIndex] : nullptr;

	UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
	const float GlobalScale = ImGui::GetStyle().FontScaleMain;
	static const FButtonStyle& SimpleButtonStyle = FAppStyle::Get().GetWidgetStyle<FButtonStyle>(IMGUI_FNAME("SimpleButton"));

	auto Add_ClassViewer = [&](FSoftObjectPtr& InOutSoftClassPtr) -> ImVec2
	{
		// configuration
		const float ClassViewerWidth = 320.f * GlobalScale;
		const char* ClassViewerPopupName = "ClassViewerPopup";
		const float ClassViewerMaxRowsVisible = 10;
		const float ClassViewerRowHeight = ImGui::GetFontSize();

		const float ClassViewerRowHeightWithSpacing = (ClassViewerRowHeight + ImGui::GetStyle().ItemSpacing.y * GlobalScale);
		const float ClassViewerDesiredHeight = FMath::Min(ClassViewerMaxRowsVisible, FilteredClassIndices.Num()) * ClassViewerRowHeightWithSpacing + ClassViewerRowHeightWithSpacing * 1.5f;
		const float ClassViewerComboxBoxWidth = FMath::Clamp(256.f * GlobalScale, 70.f * GlobalScale, ImGui::GetContentRegionAvail().x - 100.f * GlobalScale);
		constexpr int32 PreviewTextCapacity = 64;
		const int32 PreviewTextMaxLength = FMath::Clamp(FMath::CeilToInt(1.75f * ClassViewerComboxBoxWidth / ImGui::GetFontSize()) - 1, 4, PreviewTextCapacity);

		const float ClassViewerPopupPosX = ImGui::GetCursorScreenPos().x;
		const float AvailableSpaceAbove = ImGui::GetCursorScreenPos().y;
		const float MonitorDisplayHeight = Context->bIsDrawingRemotely ? ImGui::GetIO().DisplaySize.y : ImGui::GetCurrentContext()->PlatformMonitorsFullWorkRect.Max.y;
		const float AvailableSpaceBelow = (MonitorDisplayHeight - ImGui::GetCursorScreenPos().y);
		float ClassViewerPopupHeight = ((AvailableSpaceBelow > ClassViewerDesiredHeight) ? AvailableSpaceBelow : AvailableSpaceAbove) * 0.8f;
		ClassViewerPopupHeight = FMath::Min(ClassViewerPopupHeight, ClassViewerDesiredHeight);

		char PreviewText[PreviewTextCapacity + 1];
		if (SelectedClassData)
		{
			int32 Len = FMath::Min(PreviewTextMaxLength, SelectedClassData->DisplayName.Len());
			FMemory::Memcpy(PreviewText, *SelectedClassData->DisplayName, Len);
			PreviewText[Len] = 0;

			if (SelectedClassData->DisplayName.Len() >= PreviewTextMaxLength)
			{
				// PreviewTextMaxLen...
				PreviewText[PreviewTextMaxLength - 1] = ANSICHAR('.');
				PreviewText[PreviewTextMaxLength - 2] = ANSICHAR('.');
				PreviewText[PreviewTextMaxLength - 3] = ANSICHAR('.');
			}
		}
		else
		{
			const char* NoneLabel = "None";
			FMemory::Memcpy(PreviewText, NoneLabel, FCStringAnsi::Strlen(NoneLabel) + 1);
		}

		ImVec2 ComboBoxSize;
		bool bIsComboBoxVisible;
		{
			static const FComboButtonStyle& ComboButtonStyle = FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>(IMGUI_FNAME("ComboButton"));
			ImGui::SetNextItemWidth(ClassViewerComboxBoxWidth);
			ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.f, 0.5f));
			if (FImGui::ComboButton(Context, "##PopupButton", PreviewText, ComboButtonStyle))
			{
				ImGui::OpenPopup(ClassViewerPopupName);
			}
			ImGui::PopStyleVar();
			if (SelectedClassData && ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_NoSharedDelay | ImGuiHoveredFlags_DelayNormal))
			{
				FImGui::SetItemTooltip("%s", *SelectedClassData->ObjectPath);
			}
			ComboBoxSize = ImGui::GetItemRectSize();
			bIsComboBoxVisible = ImGui::IsItemVisible();
		}

		if ((AvailableSpaceBelow > ClassViewerPopupHeight) || (AvailableSpaceBelow > AvailableSpaceAbove))
		{
			ImGui::SetNextWindowPos(ImVec2(ClassViewerPopupPosX, ImGui::GetCursorScreenPos().y), ImGuiCond_Always, ImVec2(0.f, 0.f));
		}
		else
		{
			const float OffsetY = ComboBoxSize.y + ImGui::GetStyle().ItemSpacing.y * 2.f * GlobalScale;
			ImGui::SetNextWindowPos(ImVec2(ClassViewerPopupPosX, ImGui::GetCursorScreenPos().y - OffsetY), ImGuiCond_Always, ImVec2(0.f, 1.f));
		}

		FImGuiScopedStyleEdit PopupStyle
		{
			ImGuiStyleVar_WindowPadding,	ImVec2(2.f, 2.f),
			ImGuiStyleVar_PopupBorderSize,	1.f,
			ImGuiCol_FrameBg,				ImGui::GetColorU32(0xFF1A1A1A),
			ImGuiCol_PopupBg,				FImGui::GetSlateColorVec4(Context, EStyleColor::Dropdown) * ImVec4(1.f, 1.f, 1.f, 0.94f),
			ImGuiCol_Border,				FImGui::GetSlateColorVec4(Context, EStyleColor::DropdownOutline)
		};

		int32 NewSelectedIndex = INDEX_NONE;
		if (ImGui::BeginPopup(ClassViewerPopupName, ImGuiWindowFlags_NoScrollbar))
		{
			bool bFilterAvailableClasses = false;

			// reset filter text and set focus when class viewer is triggered
			if (ImGui::IsWindowAppearing())
			{
				if (TextFilter.IsActive())
				{
					bFilterAvailableClasses = true;
				}
				TextFilter.Reset();
			}

			ImGui::BeginGroup();
			{
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.f);
				ImGui::SetNextItemWidth(ClassViewerWidth);
				if (TextFilter.Draw(Context, "Filter", "Search", /*bSetFocus=*/ImGui::IsWindowAppearing()))
				{
					bFilterAvailableClasses = true;
				}

				ImVec4 RowColor = FImGui::GetSlateColorVec4(Context, EStyleColor::Recessed);
				ImVec4 RowColor_Active = FImGui::GetSlateColorVec4(Context, EStyleColor::Select);
				ImVec4 RowColor_Hovered = FImGui::GetSlateColorVec4(Context, EStyleColor::SelectHover);
				ImVec4 RowColor_Header = FImGui::GetSlateColorVec4(Context, EStyleColor::SelectInactive);

				FImGuiScopedStyleEdit ListViewStyle
				{
					ImGuiCol_Header,		MoveTemp(RowColor_Header),
					ImGuiCol_HeaderActive,	MoveTemp(RowColor_Active),
					ImGuiCol_HeaderHovered, MoveTemp(RowColor_Hovered),
					ImGuiCol_TableRowBg,	MoveTemp(RowColor),
					ImGuiCol_TableRowBgAlt, MoveTemp(RowColor),
					ImGuiCol_ScrollbarBg,	MoveTemp(RowColor),
				};

				bool bIsListViewHighlighted = false;
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.f);
				if (ImGui::BeginListBox("##ClassList", ImVec2(ClassViewerWidth, FMath::Max(ClassViewerRowHeightWithSpacing, ClassViewerPopupHeight - ImGui::GetItemRectSize().y))))
				{
					auto Add_ListEntry = [&](int32 ClassIndex, int32 RowIndex)
					{
						const bool bWasSelected = (ClassIndex == LastSelectedClassIndex);
						{
							FImGuiNamedScope Scope{ ClassIndex };

							if (ImGui::Selectable("", bWasSelected, ImGuiSelectableFlags_None, ImVec2(0, ClassViewerRowHeight)))
							{
								NewSelectedIndex = ClassIndex;
							}
							if (bWasSelected)
							{
								ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::ColorConvertFloat4ToU32(RowColor_Header));
							}
							else if (!bWasSelected && !ImGui::IsItemHovered())
							{
								ImGui::GetWindowDrawList()->AddRectFilled(
									ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
									ImGui::GetColorU32((RowIndex & 0x1) ? ImGuiCol_TableRowBgAlt : ImGuiCol_TableRowBg));
							}
						}

						ImGui::SameLine();
						ImGui::TextUnformatted(*AvailableClasses[ClassIndex].DisplayName);

						// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
						if (bWasSelected || ((RowIndex == 0) && (LastSelectedClassIndex == INDEX_NONE)))
						{
							// scroll to item when popup is opened
							if (ImGui::IsWindowAppearing())
							{
								ImGui::ScrollToItem();
							}
							ImGui::SetItemDefaultFocus();
						}
					};

					ImGuiListClipper Clipper;
					Clipper.Begin(FilteredClassIndices.Num());
					if (LastSelectedClassIndexInFilteredList != INDEX_NONE)
					{
						Clipper.IncludeItemByIndex(LastSelectedClassIndexInFilteredList);
					}

					ImGui::PushStyleColor(ImGuiCol_NavCursor, RowColor);
					while (Clipper.Step())
					{
						for (int32 Index = Clipper.DisplayStart; Index < Clipper.DisplayEnd; Index++)
						{
							Add_ListEntry(FilteredClassIndices[Index], Index);
						}
					}
					ImGui::PopStyleColor();

					if ((Context->ImguiContext->NavCursorVisible && Context->ImguiContext->NavId == ImGui::GetCurrentWindow()->ChildId) || ImGui::IsWindowFocused())
					{
						bIsListViewHighlighted = true;
					}

					ImGui::PushStyleColor(ImGuiCol_NavCursor, 0);
					ImGui::EndListBox();
					ImGui::PopStyleColor();
				}
				if (bIsListViewHighlighted)
				{
					ImGui::GetWindowDrawList()->AddRect(
						ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
						ImGui::GetColorU32(ImGuiCol_NavCursor),
						ImGui::GetStyle().FrameRounding, 1.f);
				}
			}
			ImGui::EndGroup();

			if (bFilterAvailableClasses)
			{
				FilterAvailableClasses();
			}

			bool bClosePopup = (NewSelectedIndex != INDEX_NONE);
			bClosePopup |= !bIsComboBoxVisible;
			// force close the popup when dragging assets over the window
			const ImGuiHoveredFlags HoverFlags = ImGuiHoveredFlags_RootWindow|ImGuiHoveredFlags_AllowWhenBlockedByPopup|ImGuiHoveredFlags_AllowWhenBlockedByActiveItem;
			bClosePopup |= (Context->DragDropOperation.IsValid() && ImGui::IsWindowHovered(HoverFlags));
			if (bClosePopup)
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		if (NewSelectedIndex != INDEX_NONE)
		{
			InOutSoftClassPtr = { AvailableClasses[NewSelectedIndex].ClassPath };
		}

		return ComboBoxSize;
	};

#if WITH_EDITOR
	auto Add_UseSelectedAssetButton = [&](FSoftObjectPtr& InOutSoftClassPtr, float IconSize)
	{
		const FImGuiImageBindingParams UseSelectedAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON_BRUSH("ImIcon.UseSelectedAsset"), IconSize);
		if (FImGui::Button(Context, "UseSelectedAsset", UseSelectedAssetIcon, SimpleButtonStyle))
		{
			FSoftObjectPtr ClassPtr = ClassPickerUtils::GetSelectedClassOfType(BaseClassPath);
			if (!ClassPtr.IsNull())
			{
				InOutSoftClassPtr = ClassPtr;
			}
		}
		FImGui::SetItemTooltip("%s", "Use Selected Asset from Content Browser");
	};

	auto Add_BrowseToAssetButton = [&](const FSoftObjectPtr& InSoftClassPtr, float IconSize)
	{
		const FImGuiImageBindingParams BrowseToAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON_BRUSH("ImIcon.BrowseToAsset"), IconSize);
		const bool bIsInvalidAsset = InSoftClassPtr.IsNull() || (!SelectedClassData || !SelectedClassData->bIsAsset);
		ImGui::BeginDisabled(bIsInvalidAsset);
		if (FImGui::Button(Context, "BrowseToAsset", BrowseToAssetIcon, SimpleButtonStyle))
		{
			ClassPickerUtils::SyncContentBrowserToAsset(InSoftClassPtr, BaseClassPath);
		}
		ImGui::EndDisabled();

		if (bIsInvalidAsset)
		{
			FImGui::SetItemTooltip("%s", "Browse to Asset in Content Browser");
		}
		else
		{
			FImGui::SetItemTooltip("Browse to '%s' in Content Browser", TCHAR_TO_UTF8(*InSoftClassPtr.GetAssetName()));
		}
	};

	auto Add_CreateBlueprintButton = [&](FSoftObjectPtr& InOutSoftClassPtr, float IconSize)
	{
		const FImGuiImageBindingParams CreateNewBlueprintIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON_BRUSH("ImIcon.PlusCircle"), IconSize);
		if (FImGui::Button(Context, "CreateNewBP", CreateNewBlueprintIcon, SimpleButtonStyle))
		{
			UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprintFromClass(FText::FromString("Create New Blueprint"), const_cast<UClass*>(BaseClass), FString::Printf(TEXT("New%s"), *BaseClassPath.GetAssetName()));

			UClass* RequiredInterface = nullptr;//TODO: filter not expoxed
			if (Blueprint != NULL && Blueprint->GeneratedClass)
			{
				if (RequiredInterface != nullptr && FKismetEditorUtilities::CanBlueprintImplementInterface(Blueprint, RequiredInterface))
				{
					FBlueprintEditorUtils::ImplementNewInterface(Blueprint, RequiredInterface->GetClassPathName());
				}

				FString GeneratedClassPath = Blueprint->GeneratedClass->GetPathName();
				InOutSoftClassPtr = FSoftObjectPtr{ GeneratedClassPath };
			}
		}
		FImGui::SetItemTooltip("%s", "Create New Blueprint");
	};
#endif

	auto Add_ClearValueButton = [&](FSoftObjectPtr& InOutSoftClassPtr, float IconSize)
	{
		const FImGuiImageBindingParams ClearValueIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON_BRUSH("ImIcon.Cross"), IconSize);
		ImGui::BeginDisabled(InOutSoftClassPtr.IsNull());
		if (FImGui::Button(Context, "ClearValue", ClearValueIcon, SimpleButtonStyle))
		{
			InOutSoftClassPtr.Reset();
		}
		FImGui::SetItemTooltip("%s", "Clear");
		ImGui::EndDisabled();
	};

	ImRect AssetDragDropArea;
	{
		ImGui::BeginGroup();
		{
			if (FCStringAnsi::Strstr(Label, "##") == nullptr)
			{
				ImGui::BeginGroup();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(Label);
				ImGui::EndGroup();

				ImGui::SameLine();
			}

			ImGui::BeginGroup();
			{
				const ImVec2 ClassViewerComboBoxSize = Add_ClassViewer(SelectedSoftClassPtr);
				const float IconSize = ImTrunc(ClassViewerComboBoxSize.y * 0.75f);
				const float IconPaddingTop = 2.f;

				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2());
				ImGui::PushStyleVarX(ImGuiStyleVar_ItemSpacing, 4.f);

#if WITH_EDITOR
				ImGui::SameLine();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + IconPaddingTop);
				Add_UseSelectedAssetButton(SelectedSoftClassPtr, IconSize);

				ImGui::SameLine();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + IconPaddingTop);
				Add_BrowseToAssetButton(SelectedSoftClassPtr, IconSize);

				if (FKismetEditorUtilities::CanCreateBlueprintOfClass(BaseClass))
				{
					ImGui::SameLine();
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + IconPaddingTop);
					Add_CreateBlueprintButton(SelectedSoftClassPtr, IconSize);
				}
#endif

				ImGui::SameLine();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + IconPaddingTop);
				Add_ClearValueButton(SelectedSoftClassPtr, IconSize);

				ImGui::PopStyleVar(2);
			}
			ImGui::EndGroup();
			AssetDragDropArea = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		}
		ImGui::EndGroup();
	}

#if WITH_EDITOR
	if (FImGui::DrawDragDropArea<FAssetDragDropOp>(Context, "AssetDragDrop", AssetDragDropArea,
		[&](TSharedPtr<FAssetDragDropOp> DragDropOp) { return DragDropOp->GetAssets().Num() == 1 && ClassPickerUtils::IsClassChildOf(ClassPickerUtils::GetClassPathForAsset(DragDropOp->GetAssets()[0]), BaseClassPath); },
		[&](TSharedPtr<FAssetDragDropOp> DragDropOp) { SelectedSoftClassPtr = ClassPickerUtils::GetClassPathForAsset(DragDropOp->GetAssets()[0]); }))
	{
	}
#endif

	const bool bSelectionChanged = (SelectedSoftClassPtr != InOutSelectedClass);
	if (bSelectionChanged)
	{
		InOutSelectedClass = SelectedSoftClassPtr;
		LastSelectedClassPtr = SelectedSoftClassPtr;
		LastSelectedClassIndex = !SelectedSoftClassPtr.IsNull() ? AvailableClasses.IndexOfByKey(FSoftClassPath(SelectedSoftClassPtr.ToString())) : INDEX_NONE;
		LastSelectedClassIndexInFilteredList = FilteredClassIndices.IndexOfByKey(LastSelectedClassIndex);
	}
	return bSelectionChanged;
}

void FImGuiClassPicker::FilterAvailableClasses()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ClassPicker::FilterClasses"), STAT_ImGuiClassPicker_FilterClasses, STATGROUP_ImGui);
	LLM_SCOPE_BYTAG(ImGuiClassPicker);

	FilteredClassIndices.Reset();
	LastSelectedClassIndexInFilteredList = INDEX_NONE;
	PackedClassFilter = ClassPickerUtils::GetPackedClassFilter();

	auto& ClassContainer = ClassPickerUtils::GetClassContainer();
	const auto& AvailableClasses = ClassContainer.GetAvailableClasses();
	for (int32 ClassIndex = 0; ClassIndex < AvailableClasses.Num(); ++ClassIndex)
	{
		if (!ClassPickerUtils::IsClassChildOf(AvailableClasses[ClassIndex].ClassPath, BaseClassPath))
		{
			continue;
		}

		if (!TextFilter.PassFilter(AvailableClasses[ClassIndex].DisplayName))
		{
			continue;
		}

		if (!OptionalFilters.IsEmpty())
		{
			bool bOptionalFiltersPassed = true;
			for (int32 FilterIndex = 0; FilterIndex < OptionalFilters.Num(); ++FilterIndex)
			{
				const FFilter& Filter = OptionalFilters[FilterIndex];
				if (Filter.IsType<FImGuiAllowedClassFilter>())
				{
					bOptionalFiltersPassed &= ClassPickerUtils::IsClassChildOf(AvailableClasses[ClassIndex].ClassPath, Filter.Get<FImGuiAllowedClassFilter>().ClassPath);
				}
				else if (Filter.IsType<FImGuiDisallowedClassFilter>())
				{
					bOptionalFiltersPassed &= !ClassPickerUtils::IsClassChildOf(AvailableClasses[ClassIndex].ClassPath, Filter.Get<FImGuiDisallowedClassFilter>().ClassPath);
				}
				else if (Filter.IsType<FImGuiRequiredInterfaceFilter>())
				{
					bOptionalFiltersPassed &= AvailableClasses[ClassIndex].ImplementedInterfaces.Contains(Filter.Get<FImGuiRequiredInterfaceFilter>().ClassPath);
				}
				else if (Filter.IsType<FImGuiDisallowAbstractClassFilter>())
				{
					bOptionalFiltersPassed &= !AvailableClasses[ClassIndex].bIsAbstractClass;
				}
				else
				{
					checkNoEntry();
				}
			}
			if (!bOptionalFiltersPassed)
			{
				continue;
			}
		}

		if (LastSelectedClassIndex == ClassIndex)
		{
			LastSelectedClassIndexInFilteredList = FilteredClassIndices.Num();
		}
		FilteredClassIndices.Add(ClassIndex);
	}

	ContainerRevisionId = ClassContainer.GetRevisionId();
}
