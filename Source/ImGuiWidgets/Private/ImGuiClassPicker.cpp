// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#include "ImGuiClassPicker.h"

#if WITH_EDITOR
#include "Editor.h"
#include "EditorUtilityLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#endif

#include "ImGuiSubsystem.h"
#include "Engine/Blueprint.h"
#include "AssetRegistry/AssetData.h"
#include "UObject/UObjectIterator.h"
#include "Blueprint/BlueprintSupport.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "AssetRegistry/AssetRegistryModule.h"

namespace ClassPickerUtils
{
	static FORCEINLINE FSoftClassPath GetClassPathForAsset(const FAssetData& Asset)
	{
		// NOTE: FSoftClassPath(ClassPtr) expects '_C' to be present
		return FSoftClassPath(Asset.GetObjectPathString() + TEXT("_C"));
	}

	static TMap<FSoftClassPath, FSoftClassPath> BlueprintAssetParentClassMap;
	static void CacheAssetParentClass(const FAssetData& AssetData)
	{
		FString ParentClassPathString;
		if (AssetData.GetTagValue(FBlueprintTags::ParentClassPath, ParentClassPathString))
		{
			FSoftClassPath ClassPath = GetClassPathForAsset(AssetData);
			BlueprintAssetParentClassMap.FindOrAdd(ClassPath) = FSoftClassPath(ParentClassPathString);
		}
	}
	static bool IsClassDerivedFrom(const FSoftClassPath& ClassPath, const FSoftClassPath& ParentClassPath)
	{
		const FSoftClassPath* Found = BlueprintAssetParentClassMap.Find(ClassPath);
		while (Found)
		{
			if (*Found == ParentClassPath)
			{
				return true;
			}
			Found = BlueprintAssetParentClassMap.Find(*Found);
		}
		return false;
	}

	class FClassContainer : FNoncopyable
	{
	public:
		struct FClassData
		{
			FSoftClassPath ClassPath;
			FAnsiString ObjectPath;
			FAnsiString DisplayName;

			uint8 bIsAsset : 1 = false;

			mutable TWeakObjectPtr<UClass> ResolvedClassPtr;
			UClass* ResolveClass() const
			{
				UClass* ClassPtr = ResolvedClassPtr.Get();
				if (!ClassPtr)
				{
					UObject* Object = ClassPath.TryLoad();
					if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
					{
						ClassPtr = Blueprint->GeneratedClass;
					}
					else
					{
						ClassPtr = Cast<UClass>(Object);
					}

					ResolvedClassPtr = ClassPtr;
				}
				return ClassPtr;
			}

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
			if (auto AssetRegistryModulePtr = FModuleManager::GetModulePtr<FAssetRegistryModule>(FName("AssetRegistry")))
			{
				IAssetRegistry& AssetRegistry = AssetRegistryModulePtr->Get();
				
				auto AddAsset = [&](const FAssetData& Asset)
				{
					FClassData ClassData{};
					ClassData.ClassPath = GetClassPathForAsset(Asset);
					ClassData.DisplayName = TCHAR_TO_ANSI(*Asset.AssetName.ToString());
					ClassData.ObjectPath = TCHAR_TO_ANSI(*ClassData.ClassPath.ToString());
					ClassData.bIsAsset = true;
					AvailableClasses.AddUnique(ClassData);

					CacheAssetParentClass(Asset);
				};

				TArray<FAssetData> Assets;
				AssetRegistry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses=*/true);
				for (const FAssetData& Asset : Assets)
				{
					AddAsset(Asset);
				}

				Assets.Reset();
				AssetRegistry.GetAssetsByClass(UBlueprintGeneratedClass::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses=*/true);
				for (const FAssetData& Asset : Assets)
				{
					AddAsset(Asset);
				}

				AssetRegistry.OnAssetAdded().AddRaw(this, &FClassContainer::OnAssetAdded);
				AssetRegistry.OnAssetRemoved().AddRaw(this, &FClassContainer::OnAssetRemoved);
				AssetRegistry.OnAssetRenamed().AddRaw(this, &FClassContainer::OnAssetRenamed);
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
				ClassData.DisplayName = TCHAR_TO_ANSI(*CurrentClass->GetName());
				ClassData.ObjectPath = TCHAR_TO_ANSI(*ClassData.ClassPath.ToString());
				AvailableClasses.AddUnique(ClassData);
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
			}
		}

		uint32 GetRevisionId()							const { return RevisionId; }
		const TArray<FClassData>& GetAvailableClasses()	const { return AvailableClasses; }

	private:
		static FORCEINLINE bool SortClassDataPredicate(const FClassData& LHS, const FClassData& RHS)
		{
			return LHS.DisplayName < RHS.DisplayName;
		}

		FORCEINLINE bool FilterAsset(const FAssetData& AssetData) const
		{
			return AssetData.GetClass() && AssetData.GetClass()->IsChildOf(UBlueprint::StaticClass());
		}

		void OnAssetAdded(const FAssetData& AssetData)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ClassPicker::OnAssetAdded"), STAT_ImGuiClassPicker_OnAssetAdded, STATGROUP_ImGui);

			if (FilterAsset(AssetData))
			{
				RevisionId++;

				FSoftClassPath AssetClassPath = GetClassPathForAsset(AssetData);
				FAnsiString AssetDisplayName = TCHAR_TO_ANSI(*AssetData.AssetName.ToString());
				
				int32 InsertIndex = Algo::LowerBound(AvailableClasses, AssetDisplayName, [](const auto& A, const auto& B) { return A.DisplayName < B; });
				bool bExists = (AvailableClasses.IsValidIndex(InsertIndex) && AvailableClasses[InsertIndex].ClassPath == AssetClassPath);
				if (!bExists && InsertIndex >= 0)
				{
					FClassData ClassData{};
					ClassData.ClassPath = AssetClassPath;
					ClassData.DisplayName = AssetDisplayName;
					ClassData.ObjectPath = TCHAR_TO_ANSI(*ClassData.ClassPath.ToString());
					ClassData.bIsAsset = true;

					AvailableClasses.Insert(ClassData, InsertIndex);

					CacheAssetParentClass(AssetData);
				}
			}
		}

		void OnAssetRemoved(const FAssetData& AssetData)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ClassPicker::OnAssetRemoved"), STAT_ImGuiClassPicker_OnAssetRemoved, STATGROUP_ImGui);

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

			bool bReAddAsset = false;
			if (FilterAsset(AssetData))
			{
				// NOTE: FSoftClassPath(ClassPtr) expects '_C' to be present
				FSoftClassPath ClassPath = FSoftClassPath(OldName + TEXT("_C"));

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

	private:
		TArray<FClassData> AvailableClasses;
		uint32 RevisionId = 0;
	};

	static FClassContainer& GetClassContainer()
	{
		static FClassContainer Instance = {};
		return Instance;
	}

	static UClass* GetSelectedClassOfType(const UClass* AssetClass)
	{
#if WITH_EDITOR
		for (auto Asset : UEditorUtilityLibrary::GetSelectedAssets())
		{
			UBlueprint* BlueprintAsset = Cast<UBlueprint>(Asset);
			if (BlueprintAsset && BlueprintAsset->GeneratedClass && BlueprintAsset->GeneratedClass->IsChildOf(AssetClass))
			{
				return BlueprintAsset->GeneratedClass;
			}
		}
		return nullptr;
#else
		return nullptr;
#endif
	}

	static void SyncContentBrowserToAsset(UClass* Asset)
	{
#if WITH_EDITOR
		TArray<UObject*> Objects;
		Objects.Add(Asset);
		GEditor->SyncBrowserToObjects(Objects);
#endif
	}

	static uint8 GetPackedClassFilter()
	{
		return 0;
	}
}

FImGuiClassPicker FImGuiClassPicker::MakeWidget(const TNonNullPtr<UClass>& Class, FFilters OptionalFilters)
{
	FImGuiClassPicker Widget = {};
	Widget.BaseClassType = Class;
	return Widget;
}

void FImGuiClassPicker::DrawInvalidWidget(ImGuiContext* Context, const char* Label, const char* ErrorMessage)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ClassPicker::Draw"), STAT_ImGuiClassPicker_Draw, STATGROUP_ImGui);

	FImGui::AddWarningMessageBox(Context, 4.f, ImVec4(1.f, 0.f, 0.f, 1.f), *FAnsiString::Printf("ClassPicker('%s') : %s", Label, ErrorMessage));
}

bool FImGuiClassPicker::DrawInternal(ImGuiContext* Context, const char* Label, UClass*& InOutSelectedClass)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ClassPicker::Draw"), STAT_ImGuiClassPicker_Draw, STATGROUP_ImGui);

	if (!ensure(BaseClassType))
	{
		return false;
	}

	FImGui::EnsureValidImGuiContext(Context);

	FImGuiNamedWidgetScope WidgetScope{ Label };

	auto& ClassContainer = ClassPickerUtils::GetClassContainer();
	const auto& AvailableClasses = ClassContainer.GetAvailableClasses();

	UClass* SelectedClass = InOutSelectedClass;

	const bool bClassContainerChanged = (PackedClassFilter != ClassPickerUtils::GetPackedClassFilter()) || (ContainerRevisionId != ClassContainer.GetRevisionId());
	if (bClassContainerChanged || LastSelectedClassPtr.IsStale() || (SelectedClass != LastSelectedClassPtr.Get()))
	{
		if (LastSelectedClassPtr.IsStale())
		{
			LastSelectedClassPtr.Reset();
		}
		LastSelectedClassIndex = SelectedClass ? AvailableClasses.IndexOfByKey(FSoftClassPath(SelectedClass)) : INDEX_NONE;
	}

	if (bClassContainerChanged)
	{
		FilterAvailableClasses();
	}

	const auto* SelectedClassData = AvailableClasses.IsValidIndex(LastSelectedClassIndex) ? &AvailableClasses[LastSelectedClassIndex] : nullptr;

	UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();		
	const float GlobalScale = ImGui::GetStyle().FontScaleMain;

	auto Add_ClassViewer = [&](UClass*& InOutClass) -> ImVec2
	{
		// configuration
		const float ClassViewerWidth = 256.f * GlobalScale;
		const char* ClassViewerPopupName = "ClassViewerPopup";
		const float ClassViewerRowHeight = 18.f * GlobalScale;
		const float ClassViewerRowHeightWithSpacing = ClassViewerRowHeight + ImGui::GetStyle().ItemSpacing.y * GlobalScale;
		const int32 PreviewTextMaxLength = 20;

		const float ClassViewerPopupPosX = ImGui::GetCursorScreenPos().x;
		const float AvailableSpaceAbove = ImGui::GetCursorScreenPos().y * 0.65f;
		const float AvailableSpaceBelow = (ImGui::GetWindowHeight() - ImGui::GetCursorScreenPos().y) * 0.75f;
		const float PopupHeight = FMath::Min(FMath::Max(AvailableSpaceBelow, AvailableSpaceAbove), ClassViewerRowHeightWithSpacing * (FilteredClassIndices.Num() + 1));

		// TODO: `ImGui::RenderTextEllipsis` already does something similar
		FAnsiString PreviewText = SelectedClassData ? SelectedClassData->DisplayName : "None";
		if (PreviewText.Len() >= PreviewTextMaxLength)
		{
			PreviewText.MidInline(0, PreviewTextMaxLength);

			// PreviewTextMaxLen...
			PreviewText[PreviewTextMaxLength - 1] = ANSICHAR('.');
			PreviewText[PreviewTextMaxLength - 2] = ANSICHAR('.');
			PreviewText[PreviewTextMaxLength - 3] = ANSICHAR('.');
		}

		if (!ImGui::IsPopupOpen(ClassViewerPopupName))
		{
			bIsClassViewerVisible = false;
		}

		ImVec2 ComboBoxSize;
		ImGui::BeginGroup();
		{			
			ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.f, 0.5f));
			if (ImGui::Button(*PreviewText, ImVec2(ClassViewerWidth * 0.75f, 0.f)))
			{
				ImGui::OpenPopup(ClassViewerPopupName);
			}
			if (SelectedClassData)
			{
				ImGui::SetItemTooltip("%s", *SelectedClassData->DisplayName);
			}
			ComboBoxSize = ImGui::GetItemRectSize();
			
			ImGui::SameLine();
			
			const FImGuiImageBindingParams DropDownArrowIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.DropDownArrow"), FVector2D(ComboBoxSize.y * 0.9f));
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() - DropDownArrowIcon.Size.x * 2.f);
			FImGui::Image(DropDownArrowIcon);
			ImGui::PopStyleVar(1);			
		}
		ImGui::EndGroup();
		
		if (AvailableSpaceBelow > AvailableSpaceAbove)
		{
			ImGui::SetNextWindowPos(ImVec2(ClassViewerPopupPosX, ImGui::GetCursorScreenPos().y), ImGuiCond_Always, ImVec2(0.f, 0.f));
		}
		else
		{
			const float OffsetY = ComboBoxSize.y + ImGui::GetStyle().ItemSpacing.y * 2.f * GlobalScale;
			ImGui::SetNextWindowPos(ImVec2(ClassViewerPopupPosX, ImGui::GetCursorScreenPos().y - OffsetY), ImGuiCond_Always, ImVec2(0.f, 1.f));
		}

		int32 NewSelectedIndex = INDEX_NONE;
		if (ImGui::BeginPopup(ClassViewerPopupName, ImGuiWindowFlags_NoScrollbar))
		{
			bool bFilterAvailableClasses = false;

			// reset filter text and set focus when class viewer is triggered
			if (!bIsClassViewerVisible)
			{
				if (TextFilter.IsActive())
				{
					bFilterAvailableClasses = true;
				}
				TextFilter.Reset();
			}

			ImGui::BeginGroup();
			{
				if (TextFilter.Draw(Context, "Filter", "Search", ClassViewerWidth, /*bSetFocus*/!bIsClassViewerVisible))
				{
					bFilterAvailableClasses = true;
				}

				if (ImGui::BeginListBox("##ClassList", ImVec2(ClassViewerWidth, FMath::Max(ClassViewerRowHeightWithSpacing, PopupHeight - ImGui::GetItemRectSize().y))))
				{
					auto Add_ListEntry = [&](int32 ClassIndex, int32 RowIndex)
					{
						const bool bWasSelected = (ClassIndex == LastSelectedClassIndex);
						{
							FImGuiNamedWidgetScope Scope{ RowIndex };

							if (ImGui::Selectable("", bWasSelected, ImGuiSelectableFlags_None, ImVec2(0, ClassViewerRowHeight)))
							{
								NewSelectedIndex = ClassIndex;
							}
							if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_NoSharedDelay | ImGuiHoveredFlags_DelayNormal))
							{
								ImGui::SetTooltip(*AvailableClasses[ClassIndex].ObjectPath);
							}

							if (!bWasSelected)
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
							if (!bIsClassViewerVisible)
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

					int32 RowIndex = 0;
					while (Clipper.Step() && (NewSelectedIndex == INDEX_NONE))
					{
						for (int32 Index = Clipper.DisplayStart; Index < Clipper.DisplayEnd; Index++)
						{
							Add_ListEntry(FilteredClassIndices[Index], RowIndex++);
						}
					}

					// NOTE: ImGui::BeginListBox can return false on first attempt (rect not visible in the popup window)
					// So this flag is set there, instead of using `ImGui::IsPopupOpen(ClassViewerPopupName)`
					bIsClassViewerVisible = true;

					ImGui::EndListBox();
				}
			}
			ImGui::EndGroup();

			if (bFilterAvailableClasses)
			{
				FilterAvailableClasses();
			}

			if (NewSelectedIndex != INDEX_NONE)
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		if (NewSelectedIndex != INDEX_NONE)
		{
			InOutClass = AvailableClasses[NewSelectedIndex].ResolveClass();
		}

		return ComboBoxSize;
	};

	auto Add_UseSelectedAssetButton = [&](UClass*& InOutClass, float IconSize)
	{
		const FImGuiImageBindingParams UseSelectedAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.UseSelectedAsset"), FVector2D(IconSize));
		if (FImGui::TransparentImageButton("UseSelectedAsset", UseSelectedAssetIcon))
		{
			if (auto SelectedAssetClass = (ClassPickerUtils::GetSelectedClassOfType(BaseClassType)))
			{
				InOutClass = SelectedAssetClass;
			}
		}
		ImGui::SetItemTooltip("Use Selected Asset from Content Browser");
	};

	auto Add_BrowseToAssetButton = [&](UClass* InClass, float IconSize)
	{
		const FImGuiImageBindingParams BrowseToAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.BrowseToAsset"), FVector2D(IconSize));
		if (!InClass || (!SelectedClassData || !SelectedClassData->bIsAsset) || (WITH_EDITOR == 0))
		{
			ImGui::BeginDisabled();
			FImGui::TransparentImageButton("BrowseToAsset", BrowseToAssetIcon);
			ImGui::EndDisabled();
		}
		else
		{
			if (FImGui::TransparentImageButton("BrowseToAsset", BrowseToAssetIcon))
			{
				ClassPickerUtils::SyncContentBrowserToAsset(InClass);
			}
			ImGui::SetItemTooltip("Browse to '%s' in Content Browser", TCHAR_TO_ANSI(*InClass->GetName()));
		}
	};

	auto Add_CreateBlueprintButton = [&](UClass*& InOutClass, float IconSize)
	{
		const FImGuiImageBindingParams CreateNewBlueprintIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icons.PlusCircle"), FVector2D(IconSize));		
#if WITH_EDITOR
		if (FImGui::TransparentImageButton("CreateNewBP", CreateNewBlueprintIcon))
		{
			UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprintFromClass(FText::FromString("Create New Blueprint"), (UClass*)BaseClassType, FString::Printf(TEXT("New%s"), *BaseClassType->GetName()));

			UClass* RequiredInterface = nullptr;
			if (Blueprint != NULL && Blueprint->GeneratedClass)
			{
				if (RequiredInterface != nullptr && FKismetEditorUtilities::CanBlueprintImplementInterface(Blueprint, RequiredInterface))
				{
					FBlueprintEditorUtils::ImplementNewInterface(Blueprint, RequiredInterface->GetClassPathName());
				}

				InOutClass = Blueprint->GeneratedClass;
				//LastSelectedClassPtr = Blueprint->GeneratedClass;
				//LastSelectedClassIndex = INDEX_NONE;
			}
		}
		ImGui::SetItemTooltip("Create New Blueprint");
#else
		ImGui::BeginDisabled(true);
		FImGui::TransparentImageButton("CreateNewBP", CreateNewBlueprintIcon);
		ImGui::EndDisabled();
#endif
	};

	auto Add_ClearValueButton = [&](UClass*& InOutClass, float IconSize)
	{
		const FImGuiImageBindingParams ClearValueIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icons.X"), FVector2D(IconSize));
		ImGui::BeginDisabled(InOutClass == nullptr);
		if (FImGui::TransparentImageButton("ClearValue", ClearValueIcon))
		{
			InOutClass = nullptr;
		}
		if (InOutClass)
		{
			ImGui::SetItemTooltip("Clear");
		}
		ImGui::EndDisabled();
	};

	ImGui::BeginGroup();
	{		
		if (strstr(Label, "##") == nullptr)
		{
			ImGui::BeginGroup();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFontSize() * 0.25f);
			ImGui::TextUnformatted(Label);
			ImGui::EndGroup();

			ImGui::SameLine();
		}

		const ImVec2 ClassViewerComboBoxSize = Add_ClassViewer(SelectedClass);
		
		{
			const float IconSize = ClassViewerComboBoxSize.y * 0.9f;
			const float IconPaddingTop = ClassViewerComboBoxSize.y * 0.05f;

			ImGui::PushStyleColor(ImGuiCol_Button, 0xBFFFFFFF);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFFFFFFFF);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFFFFFFFF);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4 * GlobalScale, 0));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * GlobalScale, 0));

			ImGui::SameLine();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + IconPaddingTop);
			Add_UseSelectedAssetButton(SelectedClass, IconSize);
			
			ImGui::SameLine();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + IconPaddingTop);
			Add_BrowseToAssetButton(SelectedClass, IconSize);
			
			ImGui::SameLine();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + IconPaddingTop);
			Add_CreateBlueprintButton(SelectedClass, IconSize);
			
			ImGui::SameLine();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + IconPaddingTop);
			Add_ClearValueButton(SelectedClass, IconSize);

			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(3);
		}		
	}
	ImGui::EndGroup();
	
	const bool bSelectionChanged = (SelectedClass != InOutSelectedClass);
	if (bSelectionChanged)
	{
		InOutSelectedClass = SelectedClass;
		LastSelectedClassPtr = SelectedClass;
		LastSelectedClassIndex = SelectedClass ? AvailableClasses.IndexOfByKey(FSoftClassPath(SelectedClass)) : INDEX_NONE;
	}
	return bSelectionChanged;
}

void FImGuiClassPicker::FilterAvailableClasses()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ClassPicker::FilterClasses"), STAT_ImGuiClassPicker_FilterClasses, STATGROUP_ImGui);

	FilteredClassIndices.Reset();
	LastSelectedClassIndexInFilteredList = INDEX_NONE;
	PackedClassFilter = ClassPickerUtils::GetPackedClassFilter();

	auto& ClassContainer = ClassPickerUtils::GetClassContainer();
	const auto& AvailableClasses = ClassContainer.GetAvailableClasses();

	FSoftClassPath BaseClassPath = FSoftClassPath(BaseClassType);

	for (int32 ClassIndex = 0; ClassIndex < AvailableClasses.Num(); ++ClassIndex)
	{
		if (AvailableClasses[ClassIndex].bIsAsset) //process asset classes without loading/resolving them
		{
			if (!ClassPickerUtils::IsClassDerivedFrom(AvailableClasses[ClassIndex].ClassPath, BaseClassPath))
			{
				continue;
			}
		}
		else if (!AvailableClasses[ClassIndex].ResolveClass()->IsChildOf(BaseClassType))
		{
			continue;
		}

		if (!TextFilter.PassFilter(AvailableClasses[ClassIndex].DisplayName))
		{
			continue;
		}

		if (LastSelectedClassIndex == ClassIndex)
		{
			LastSelectedClassIndexInFilteredList = FilteredClassIndices.Num();
		}
		FilteredClassIndices.Add(ClassIndex);
	}

	ContainerRevisionId = ClassContainer.GetRevisionId();
}
