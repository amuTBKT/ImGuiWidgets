// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

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
#include "AssetRegistry/AssetData.h"
#include "UObject/UObjectIterator.h"
#include "Blueprint/BlueprintSupport.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "AssetRegistry/AssetRegistryModule.h"

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

				if (CurrentClass->GetSuperClass())
				{
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

	private:
		TArray<FClassData> AvailableClasses;
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

FImGuiClassPicker FImGuiClassPicker::MakeWidget(const FSoftClassPath& ClassPath, FFilters OptionalFilters)
{
	FImGuiClassPicker Widget = {};
	Widget.BaseClassPath = ClassPath;
	return Widget;
}

void FImGuiClassPicker::DrawInvalidWidget(FImGuiTickContext* Context, const char* Label, const char* ErrorMessage)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ClassPicker::Draw"), STAT_ImGuiClassPicker_Draw, STATGROUP_ImGui);

	FImGui::DrawWarningMessageBox(Context, 4.f, ImVec4(1.f, 0.f, 0.f, 1.f), *FAnsiString::Printf("ClassPicker('%s') : %s", Label, ErrorMessage));
}

bool FImGuiClassPicker::DrawInternal(FImGuiTickContext* Context, const char* Label, FSoftObjectPtr& InOutSelectedClass)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ClassPicker::Draw"), STAT_ImGuiClassPicker_Draw, STATGROUP_ImGui);

	if (!ensure(BaseClassPath.IsValid()))
	{
		return false;
	}

	FImGui::EnsureValidImGuiContext(Context);

	FImGuiNamedWidgetScope WidgetScope{ Label };

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
	}

#if WITH_EDITOR
	TOptional<FSoftObjectPtr> DraggedClassPath;
	const bool bIsDragDropOperationValid = Context->DragDropOperation.IsValid();
	if (bIsDragDropOperationValid && Context->DragDropOperation->IsOfType<FAssetDragDropOp>())
	{
		TSharedPtr<FAssetDragDropOp> DragDropOp = StaticCastSharedPtr<FAssetDragDropOp>(Context->DragDropOperation);
		if (DragDropOp->GetAssets().Num() == 1 && ClassPickerUtils::IsClassChildOf(ClassPickerUtils::GetClassPathForAsset(DragDropOp->GetAssets()[0]), BaseClassPath))
		{
			DraggedClassPath = FSoftObjectPtr{ ClassPickerUtils::GetClassPathForAsset(DragDropOp->GetAssets()[0]) };
		}
	}
#else
	const bool bIsDragDropOperationValid = false;
#endif

	const auto* SelectedClassData = AvailableClasses.IsValidIndex(LastSelectedClassIndex) ? &AvailableClasses[LastSelectedClassIndex] : nullptr;

	UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
	const float GlobalScale = ImGui::GetStyle().FontScaleMain;

	auto Add_ClassViewer = [&](FSoftObjectPtr& InOutSoftClassPtr) -> ImVec2
	{
		// configuration
		const float ClassViewerWidth = 256.f * GlobalScale;
		const char* ClassViewerPopupName = "ClassViewerPopup";
		const float ClassViewerMaxRowCount = 10;
		const float ClassViewerRowHeight = 18.f * GlobalScale;
		
		const float ClassViewerRowHeightWithSpacing = ClassViewerRowHeight + ImGui::GetStyle().ItemSpacing.y * GlobalScale;
		const float ClassViewerDesiredHeight = FMath::Min(ClassViewerMaxRowCount, FilteredClassIndices.Num() + 1) * ClassViewerRowHeightWithSpacing;
		const float ClassViewerComboxBoxWidth = FMath::Clamp(ClassViewerWidth, 70.f * GlobalScale, ImGui::GetContentRegionAvail().x - 100.f * GlobalScale);
		const int32 PreviewTextMaxLength = FMath::Clamp(FMath::CeilToInt(1.25f * ClassViewerComboxBoxWidth / ImGui::GetFontSize()) - 1, 4, 32);

		const float ClassViewerPopupPosX = ImGui::GetCursorScreenPos().x;
		const float AvailableSpaceAbove = ImGui::GetCursorScreenPos().y;
		const float MonitorDisplaySize = ImGui::GetPlatformIO().Monitors.empty() ? ImGui::GetWindowHeight() : ImGui::GetPlatformIO().Monitors[0].WorkSize.y;
		const float AvailableSpaceBelow = (MonitorDisplaySize - ImGui::GetCursorScreenPos().y);
		float ClassViewerPopupHeight = ((AvailableSpaceBelow > ClassViewerDesiredHeight) ? AvailableSpaceBelow : AvailableSpaceAbove) * 0.8f;
		ClassViewerPopupHeight = FMath::Min(ClassViewerPopupHeight, ClassViewerDesiredHeight);

		// TODO: `ImGui::RenderTextEllipsis` does something similar
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
			if (ImGui::Button(*PreviewText, ImVec2(ClassViewerComboxBoxWidth, 0.f)))
			{
				ImGui::OpenPopup(ClassViewerPopupName);
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_NoSharedDelay | ImGuiHoveredFlags_DelayNormal))
			{
				ImGui::SetItemTooltip("%s", *SelectedClassData->ObjectPath);
			}
			ComboBoxSize = ImGui::GetItemRectSize();
			
			ImGui::SameLine();
			
			const FImGuiImageBindingParams DropDownArrowIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.DropDownArrow"), FVector2D(ComboBoxSize.y * 0.9f));
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() - DropDownArrowIcon.Size.x * 2.f);
			FImGui::Image(DropDownArrowIcon);
			ImGui::PopStyleVar(1);			
		}
		ImGui::EndGroup();
		
		if ((AvailableSpaceBelow > ClassViewerPopupHeight) || (AvailableSpaceBelow > AvailableSpaceAbove))
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

				if (ImGui::BeginListBox("##ClassList", ImVec2(ClassViewerWidth, FMath::Max(ClassViewerRowHeightWithSpacing, ClassViewerPopupHeight - ImGui::GetItemRectSize().y))))
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

			bool bClosePopup = (NewSelectedIndex != INDEX_NONE);
			// force close the popup when dragging assets over the window
			const ImGuiHoveredFlags HoverFlags = ImGuiHoveredFlags_RootWindow|ImGuiHoveredFlags_AllowWhenBlockedByPopup|ImGuiHoveredFlags_AllowWhenBlockedByActiveItem;
			bClosePopup |= (bIsDragDropOperationValid && ImGui::IsWindowHovered(HoverFlags));
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

	auto Add_UseSelectedAssetButton = [&](FSoftObjectPtr& InOutSoftClassPtr, float IconSize)
	{
		const FImGuiImageBindingParams UseSelectedAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.UseSelectedAsset"), FVector2D(IconSize));
		if (WITH_EDITOR == 0)
		{
			ImGui::BeginDisabled();
			FImGui::TransparentImageButton("UseSelectedAsset", UseSelectedAssetIcon);
			ImGui::EndDisabled();
		}
		else
		{
			if (FImGui::TransparentImageButton("UseSelectedAsset", UseSelectedAssetIcon))
			{
				FSoftObjectPtr ClassPtr = ClassPickerUtils::GetSelectedClassOfType(BaseClassPath);
				if (!ClassPtr.IsNull())
				{
					InOutSoftClassPtr = ClassPtr;
				}
			}
			ImGui::SetItemTooltip("Use Selected Asset from Content Browser");
		}
	};

	auto Add_BrowseToAssetButton = [&](const FSoftObjectPtr& InSoftClassPtr, float IconSize)
	{
		const FImGuiImageBindingParams BrowseToAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.BrowseToAsset"), FVector2D(IconSize));
		if (InSoftClassPtr.IsNull() || (!SelectedClassData || !SelectedClassData->bIsAsset) || (WITH_EDITOR == 0))
		{
			ImGui::BeginDisabled();
			FImGui::TransparentImageButton("BrowseToAsset", BrowseToAssetIcon);
			ImGui::EndDisabled();
		}
		else
		{
			if (FImGui::TransparentImageButton("BrowseToAsset", BrowseToAssetIcon))
			{
				ClassPickerUtils::SyncContentBrowserToAsset(InSoftClassPtr, BaseClassPath);
			}
			ImGui::SetItemTooltip("Browse to '%s' in Content Browser", TCHAR_TO_ANSI(*InSoftClassPtr.GetAssetName()));
		}
	};

	auto Add_CreateBlueprintButton = [&](FSoftObjectPtr& InOutSoftClassPtr, float IconSize)
	{
		const FImGuiImageBindingParams CreateNewBlueprintIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icons.PlusCircle"), FVector2D(IconSize));
#if WITH_EDITOR
		if (FImGui::TransparentImageButton("CreateNewBP", CreateNewBlueprintIcon))
		{
			// TODO: add validation/error handline if the class is invalid
			UClass* Class = Cast<UClass>(BaseClassPath.TryLoad());
			UBlueprint* Blueprint = Class ? FKismetEditorUtilities::CreateBlueprintFromClass(FText::FromString("Create New Blueprint"), Class, FString::Printf(TEXT("New%s"), *BaseClassPath.GetAssetName())) : nullptr;

			UClass* RequiredInterface = nullptr;
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
		ImGui::SetItemTooltip("Create New Blueprint");
#else
		ImGui::BeginDisabled(true);
		FImGui::TransparentImageButton("CreateNewBP", CreateNewBlueprintIcon);
		ImGui::EndDisabled();
#endif
	};

	auto Add_ClearValueButton = [&](FSoftObjectPtr& InOutSoftClassPtr, float IconSize)
	{
		const FImGuiImageBindingParams ClearValueIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icons.X"), FVector2D(IconSize));
		ImGui::BeginDisabled(InOutSoftClassPtr.IsNull());
		if (FImGui::TransparentImageButton("ClearValue", ClearValueIcon))
		{
			InOutSoftClassPtr.Reset();
		}
		if (!InOutSoftClassPtr.IsNull())
		{
			ImGui::SetItemTooltip("Clear");
		}
		ImGui::EndDisabled();
	};

	ImRect AssetDragDropArea;
	{
		ImGui::BeginGroup();
		{
			if (strstr(Label, "##") == nullptr)
			{
				ImGui::BeginGroup();
				ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset = ImGui::GetStyle().FramePadding.y;
				ImGui::TextUnformatted(Label);
				ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset = 0.f;
				ImGui::EndGroup();

				ImGui::SameLine();
			}

			ImGui::BeginGroup();
			{
				const ImVec2 ClassViewerComboBoxSize = Add_ClassViewer(SelectedSoftClassPtr);
				
				const float IconSize = ClassViewerComboBoxSize.y * 0.9f;
				const float IconPaddingTop = ClassViewerComboBoxSize.y * 0.05f;

				ImGui::PushStyleColor(ImGuiCol_Button, 0xBFFFFFFF);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFFFFFFFF);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFFFFFFFF);
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4 * GlobalScale, 0));
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * GlobalScale, 0));

				ImGui::SameLine();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + IconPaddingTop);
				Add_UseSelectedAssetButton(SelectedSoftClassPtr, IconSize);

				ImGui::SameLine();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + IconPaddingTop);
				Add_BrowseToAssetButton(SelectedSoftClassPtr, IconSize);

				ImGui::SameLine();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + IconPaddingTop);
				Add_CreateBlueprintButton(SelectedSoftClassPtr, IconSize);

				ImGui::SameLine();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + IconPaddingTop);
				Add_ClearValueButton(SelectedSoftClassPtr, IconSize);

				ImGui::PopStyleVar(2);
				ImGui::PopStyleColor(3);
			}
			ImGui::EndGroup();
			AssetDragDropArea = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		}
		ImGui::EndGroup();
	}
	
#if WITH_EDITOR
	const bool bDrawDragDropArea = bIsDragDropOperationValid && (DraggedClassPath.IsSet() || ImGui::IsMouseHoveringRect(AssetDragDropArea.Min, AssetDragDropArea.Max));
	if (bDrawDragDropArea)
	{
		static const ImU32 ValidColor = FColorToImU32(FSlateColor(EStyleColor::AccentBlue).GetSpecifiedColor().ToFColor(/*bSRGB=*/true));
		static const ImU32 InvalidColor = FColorToImU32(FSlateColor(EStyleColor::Error).GetSpecifiedColor().ToFColor(/*bSRGB=*/true));

		FImGui::DrawDragDropArea(Context, AssetDragDropArea.Min, AssetDragDropArea.Max, GlobalScale, GlobalScale, DraggedClassPath.IsSet() ? ValidColor : InvalidColor);
	}

	if (DraggedClassPath.IsSet() && ImGui::IsMouseHoveringRect(AssetDragDropArea.Min, AssetDragDropArea.Max))
	{
		if (Context->ConsumeDragDropOperation())
		{
			SelectedSoftClassPtr = DraggedClassPath.GetValue();
		}
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

		if (LastSelectedClassIndex == ClassIndex)
		{
			LastSelectedClassIndexInFilteredList = FilteredClassIndices.Num();
		}
		FilteredClassIndices.Add(ClassIndex);
	}

	ContainerRevisionId = ClassContainer.GetRevisionId();
}
