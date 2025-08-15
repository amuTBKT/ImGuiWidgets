// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#include "ImGuiAssetPicker.h"

#if WITH_EDITOR
#include "Editor.h"
#include "AssetThumbnail.h"
#include "ClassIconFinder.h"
#include "ICollectionManager.h"
#include "EditorUtilityLibrary.h"
#include "CollectionManagerModule.h"
#include "Styling/SlateIconFinder.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#endif

#include "Algo/AllOf.h"
#include "Misc/Paths.h"
#include "ImGuiSubsystem.h"
#include "Engine/Blueprint.h"
#include "Algo/BinarySearch.h"
#include "AssetRegistry/AssetData.h"
#include "Interfaces/IPluginManager.h"
#include "Blueprint/BlueprintSupport.h"
#include "AssetRegistry/AssetRegistryModule.h"

#ifdef TCHAR_TO_ANSI_PATH
#error TCHAR_TO_ANSI_PATH already defined
#endif
#define TCHAR_TO_ANSI_PATH(path) (ANSICHAR*)StringCast<ANSICHAR, FName::StringBufferSize>(static_cast<const TCHAR*>(path)).Get()

namespace AssetPickerUtils
{
	class FAssetContainer : FNoncopyable
	{
	public:
		FAssetContainer(const UClass* Class)
			: AssetType(Class)
		{
#if WITH_EDITOR
			ClassIconBrush = FClassIconFinder::FindThumbnailForClass(AssetType, NAME_None);
#else
			ClassIconBrush = IMGUI_ICON("Icon.FallbackAssetIcon");
#endif

			if (auto AssetRegistryModulePtr = FModuleManager::GetModulePtr<FAssetRegistryModule>(FName("AssetRegistry")))
			{
				IAssetRegistry& AssetRegistry = AssetRegistryModulePtr->Get();
				GatherAssets(AssetRegistry);
				AssetRegistry.OnAssetAdded().AddRaw(this, &FAssetContainer::OnAssetAdded);
				AssetRegistry.OnAssetRemoved().AddRaw(this, &FAssetContainer::OnAssetRemoved);
				AssetRegistry.OnAssetRenamed().AddRaw(this, &FAssetContainer::OnAssetRenamed);
			}
		}

		~FAssetContainer()
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
		const FSlateBrush* GetClassIconBrush()			const { return ClassIconBrush; }
		const TArray<FAssetData>& GetAvailableAssets()	const { return AvailableAssets; }

	private:
		static FORCEINLINE bool SortAssetDataPredicate(const FAssetData& LHS, const FAssetData& RHS)
		{
			if (LHS.AssetName == RHS.AssetName)
			{
				return LHS.PackageName.LexicalLess(RHS.PackageName);
			}
			return LHS.AssetName.LexicalLess(RHS.AssetName);
		}

		FORCEINLINE void GatherAssets(IAssetRegistry& AssetRegistry)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("AssetPicker::GatherAssets"), STAT_ImGuiAssetPicker_GatherAssets, STATGROUP_ImGui);

			// TODO: expose filter settings
			FARFilter Filter;
			Filter.ClassPaths.Add(AssetType->GetClassPathName());
			Filter.bRecursiveClasses = true;
			AssetRegistry.GetAssets(Filter, AvailableAssets);

			AvailableAssets.Sort([](const auto& A, const auto& B) { return SortAssetDataPredicate(A, B); });
		}

		FORCEINLINE bool FilterAsset(const FAssetData& AssetData) const
		{
			return AssetData.GetClass() && AssetData.GetClass()->IsChildOf(AssetType);
		}

		void OnAssetAdded(const FAssetData& AssetData)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("AssetPicker::OnAssetAdded"), STAT_ImGuiAssetPicker_OnAssetAdded, STATGROUP_ImGui);

			if (FilterAsset(AssetData))
			{
				RevisionId++;

				int32 InsertIndex = Algo::LowerBound(AvailableAssets, AssetData, [](const auto& A, const auto& B) { return SortAssetDataPredicate(A, B); });
				bool bExists = (AvailableAssets.IsValidIndex(InsertIndex) && AvailableAssets[InsertIndex] == AssetData);
				if (!bExists && InsertIndex >= 0)
				{
					AvailableAssets.Insert(AssetData, InsertIndex);
				}
			}
		}

		void OnAssetRemoved(const FAssetData& AssetData)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("AssetPicker::OnAssetRemoved"), STAT_ImGuiAssetPicker_OnAssetRemoved, STATGROUP_ImGui);

			if (FilterAsset(AssetData))
			{
				RevisionId++;
				AvailableAssets.Remove(AssetData);
			}
		}

		void OnAssetRenamed(const FAssetData& AssetData, const FString& OldName)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("AssetPicker::OnAssetRenamed"), STAT_ImGuiAssetPicker_OnAssetRenamed, STATGROUP_ImGui);

			bool bReAddAsset = false;
			if (FilterAsset(AssetData))
			{
				for (auto Itr = AvailableAssets.CreateIterator(); Itr; ++Itr)
				{
					if (Itr->GetSoftObjectPath().ToString().Equals(OldName, ESearchCase::IgnoreCase))
					{
						Itr.RemoveCurrent();
						bReAddAsset = true;
					}
					else if (*Itr == AssetData)
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
		const UClass* AssetType = nullptr;
		TArray<FAssetData> AvailableAssets;
		const FSlateBrush* ClassIconBrush = nullptr;
		uint32 RevisionId = 0;
	};

	static TMap<uint32, TUniquePtr<FAssetContainer>> AssetContainerInstances;
	static FAssetContainer& GetAssetContainer(const UClass* Class)
	{
		const uint32 ContainerHash = PointerHash(Class);
		TUniquePtr<FAssetContainer>* ContainerInstance = AssetContainerInstances.Find(ContainerHash);
		if (!ContainerInstance)
		{
			ContainerInstance = &AssetContainerInstances.Add(ContainerHash, MakeUnique<FAssetContainer>(Class));
		}
		return *ContainerInstance->Get();
	}

#if WITH_EDITOR
	static TMap<uint32, TSharedPtr<FAssetThumbnail>> AssetThumbnails;
	static FSlateShaderResource* GetAssetThumbnail(const FAssetData& AssetData)
	{
		const uint32 AssetTypeHash = GetTypeHash(AssetData);
		TSharedPtr<FAssetThumbnail>* AssetThumbnail = AssetThumbnails.Find(AssetTypeHash);
		if (!AssetThumbnail)
		{
			AssetThumbnail = &AssetThumbnails.Add(AssetTypeHash);
			*AssetThumbnail = MakeShareable(new FAssetThumbnail(AssetData, 50.f, 50.f, UThumbnailManager::Get().GetSharedThumbnailPool()));
		}
		return AssetThumbnail->Get()->GetViewportRenderTargetTexture();
	}
#endif

	static UObject* GetSelectedAssetOfType(const UClass* AssetClass)
	{
#if WITH_EDITOR
		for (auto Asset : UEditorUtilityLibrary::GetSelectedAssets())
		{
			if (Asset->IsA(AssetClass))
			{
				return Asset;
			}
		}
		return nullptr;
#else
		return nullptr;
#endif
	}

	static void OpenEditorForAsset(UObject* Asset)
	{
#if WITH_EDITOR
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (AssetEditorSubsystem)
		{
			AssetEditorSubsystem->OpenEditorForAsset(Asset);
		}
#endif
	}

	static void SyncContentBrowserToAsset(UObject* Asset)
	{
#if WITH_EDITOR
		TArray<UObject*> Objects;
		Objects.Add(Asset);
		GEditor->SyncBrowserToObjects(Objects);
#endif
	}

	static bool bShowProjectContent = true;
	static bool bShowEngineContent = true;
	static bool bShowPluginContent = true;
	static bool bShowDeveloperContent = true;
	static bool bSearchAssetCollections = true;
	static bool bShowLocalizedContent = false;

	FORCEINLINE static uint8 GetPackedAssetPathFilter()
	{
		uint8 Filter = 0u;
		Filter |= bShowProjectContent		? (1u << 0) : 0u;
		Filter |= bShowEngineContent		? (1u << 1) : 0u;
		Filter |= bShowPluginContent		? (1u << 2) : 0u;
		Filter |= bShowDeveloperContent		? (1u << 3) : 0u;
		Filter |= bSearchAssetCollections	? (1u << 4) : 0u;
		Filter |= bShowLocalizedContent		? (1u << 5) : 0u;
		return Filter;
	}

	// NOTE: based on `ContentBrowserDataUtils::PathPassesAttributeFilter` since that is not available at runtime.
	static bool FilterAssetPath(const FStringView InPath)
	{
		static const FString ProjectContentRootName = TEXT("Game");
		static const FString EngineContentRootName = TEXT("Engine");
		static const FString LocalizationFolderName = TEXT("L10N");
		static const FString ExternalActorsFolderName = FPackagePath::GetExternalActorsFolderName();
		static const FString ExternalObjectsFolderName = FPackagePath::GetExternalObjectsFolderName();
		static const FString DeveloperPathWithoutSlash = FPackageName::FilenameToLongPackageName(FPaths::GameDevelopersDir()).LeftChop(1);

		auto GetRootFolderNameFromPath = [](const FStringView InFullPath)
		{
			FStringView Result(InFullPath);

			// Remove '/' from start
			if (Result.StartsWith(TEXT('/')))
			{
				Result.RightChopInline(1);
			}

			// Return up until just before next '/'
			int32 FoundIndex = INDEX_NONE;
			if (Result.FindChar(TEXT('/'), FoundIndex))
			{
				Result.LeftInline(FoundIndex);
			}

			return Result;
		};

		FStringView RootName = GetRootFolderNameFromPath(InPath);
		if (RootName.Len() == 0)
		{
			return false;
		}

		if (!bShowPluginContent || !bShowEngineContent || !bShowProjectContent)
		{
			if (RootName.Equals(ProjectContentRootName))
			{
				if (!bShowProjectContent)
				{
					return false;
				}
			}
			else if (RootName.Equals(EngineContentRootName))
			{
				if (!bShowEngineContent)
				{
					return false;
				}
			}
			else
			{
				if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(RootName))
				{
					if (Plugin->IsEnabled() && Plugin->CanContainContent())
					{
						if (!bShowPluginContent)
						{
							return false;
						}

						const EPluginLoadedFrom PluginSource = Plugin->GetLoadedFrom();
						if (PluginSource == EPluginLoadedFrom::Engine)
						{
							if (!bShowEngineContent)
							{
								return false;
							}
						}
						else if (PluginSource == EPluginLoadedFrom::Project)
						{
							if (!bShowProjectContent)
							{
								return false;
							}
						}
					}
				}
			}
		}

		const FStringView AfterFirstFolder = InPath.RightChop(RootName.Len() + 2);
		if (AfterFirstFolder.StartsWith(ExternalActorsFolderName) && (AfterFirstFolder.Len() == ExternalActorsFolderName.Len() || AfterFirstFolder[ExternalActorsFolderName.Len()] == TEXT('/')))
		{
			return false;
		}
		if (AfterFirstFolder.StartsWith(ExternalObjectsFolderName) && (AfterFirstFolder.Len() == ExternalObjectsFolderName.Len() || AfterFirstFolder[ExternalObjectsFolderName.Len()] == TEXT('/')))
		{
			return false;
		}

		if (!bShowLocalizedContent)
		{
			if (AfterFirstFolder.StartsWith(LocalizationFolderName) && (AfterFirstFolder.Len() == LocalizationFolderName.Len() || AfterFirstFolder[LocalizationFolderName.Len()] == TEXT('/')))
			{
				return false;
			}
		}

		if (!bShowDeveloperContent)
		{
			if (InPath.StartsWith(DeveloperPathWithoutSlash) && (InPath.Len() == DeveloperPathWithoutSlash.Len() || InPath[DeveloperPathWithoutSlash.Len()] == TEXT('/')))
			{
				return false;
			}
		}

		return true;
	}
}

FImGuiAssetPicker::FFilter FImGuiAssetPicker::MakeBlueprintSubClassFilter(const TNonNullPtr<UClass>& ParentClass)
{
	FFilter TagAndValue;
	TagAndValue.AssetTag = FBlueprintTags::NativeParentClassPath;
	TagAndValue.TagValue = FObjectPropertyBase::GetExportPath(ParentClass);
	return TagAndValue;
}

FImGuiAssetPicker FImGuiAssetPicker::MakeWidget(const TNonNullPtr<UClass>& Class, TArray<FFilter> OptionalFilters)
{
	FImGuiAssetPicker Widget = {};
	Widget.AssetType = Class;
	Widget.OptionalFilters = MoveTemp(OptionalFilters);
	return Widget;
}

void FImGuiAssetPicker::DrawInvalidWidget(ImGuiContext* Context, const char* Label, const char* ErrorMessage)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("AssetPicker::Draw"), STAT_ImGuiAssetPicker_Draw, STATGROUP_ImGui);

	FImGui::AddWarningMessageBox(Context, 16.f, ImVec4(1.f, 0.f, 0.f, 1.f), *FAnsiString::Printf("AssetPicker('%s') : %s", Label, ErrorMessage));
}

bool FImGuiAssetPicker::DrawInternal(ImGuiContext* Context, const char* Label, UObject*& InOutSelectedAsset)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("AssetPicker::Draw"), STAT_ImGuiAssetPicker_Draw, STATGROUP_ImGui);

	if (!ensure(AssetType))
	{
		return false;
	}

	FImGui::EnsureValidImGuiContext(Context);

	FImGuiNamedWidgetScope WidgetScope{ Label };

	auto& AssetContainer = AssetPickerUtils::GetAssetContainer(AssetType);
	const TArray<FAssetData>& AvailableAssets = AssetContainer.GetAvailableAssets();

	FSlateShaderResource* SelectedAssetTexture = nullptr;
	UObject* SelectedAsset = InOutSelectedAsset;

	const bool bAssetCountainerChanged = (PackedAssetPathFilter != AssetPickerUtils::GetPackedAssetPathFilter()) || (ContainerRevisionId != AssetContainer.GetRevisionId());
	if (bAssetCountainerChanged || LastSelectedAssetPtr.IsStale() || (SelectedAsset != LastSelectedAssetPtr.Get()))
	{
		if (LastSelectedAssetPtr.IsStale())
		{
			LastSelectedAssetPtr.Reset();
		}
		LastSelectedAssetIndex = AvailableAssets.IndexOfByKey(FAssetData(SelectedAsset, FAssetData::ECreationFlags::SkipAssetRegistryTagsGathering|FAssetData::ECreationFlags::AllowBlueprintClass));
	}
#if WITH_EDITOR
	if (SelectedAsset)
	{
		SelectedAssetTexture = AssetPickerUtils::GetAssetThumbnail(FAssetData(SelectedAsset, FAssetData::ECreationFlags::SkipAssetRegistryTagsGathering | FAssetData::ECreationFlags::AllowBlueprintClass));
	}
#endif

	if (bAssetCountainerChanged)
	{
		FilterAvailableAssets();
	}

	const float GlobalScale = ImGui::GetStyle().FontScaleMain;

	UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
	const FImGuiImageBindingParams DefaultClassIcon = ImGuiSubsystem->RegisterOneFrameResource(AssetContainer.GetClassIconBrush(), FVector2D(50.) * GlobalScale);
	const FImGuiImageBindingParams UseSelectedAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.UseSelectedAsset"), FVector2D(18.) * GlobalScale);
	const FImGuiImageBindingParams BrowseToAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.BrowseToAsset"), FVector2D(18.) * GlobalScale);
	const FImGuiImageBindingParams ResetToDefaultIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.ResetToDefault"), FVector2D(18.) * GlobalScale);
	const FImGuiImageBindingParams DropDownArrowIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.DropDownArrow"), FVector2D(18.) * GlobalScale);
	const FImGuiImageBindingParams ProjectContentIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.ProjectFolder"), FVector2D(16.) * GlobalScale);
	const FImGuiImageBindingParams EngineContentIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.EngineFolder"), FVector2D(16.) * GlobalScale);
	const FImGuiImageBindingParams PluginContentIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.PluginFolder"), FVector2D(16.) * GlobalScale);
	const FImGuiImageBindingParams DeveloperContentIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.DeveloperFolder"), FVector2D(16.) * GlobalScale);
	const FImGuiImageBindingParams AssetCollectionsIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.AssetCollection"), FVector2D(16.) * GlobalScale);
	const FImGuiImageBindingParams LocalizedContentIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.LocalizedFolder"), FVector2D(16.) * GlobalScale);

	auto Add_AssetThumbnail = [&](FSlateShaderResource* AssetThumbnail, float IconSize, UObject* Asset)
	{
		if (AssetThumbnail)
		{
			const FImGuiImageBindingParams ThumbnailIcon = ImGuiSubsystem->RegisterOneFrameResource(AssetThumbnail);
			ImGui::GetWindowDrawList()->AddCallback(ImDrawCallback_SetRenderState, MakeImGuiRenderState(EImGuiRenderState::DisableAlphaBlending));
			ImGui::Image(ThumbnailIcon.Id, ImVec2(IconSize, IconSize), ThumbnailIcon.UV0, ThumbnailIcon.UV1);
			ImGui::GetWindowDrawList()->AddCallback(ImDrawCallback_SetRenderState, MakeImGuiRenderState(EImGuiRenderState::Default));
		}
		else
		{
			ImGui::Image(DefaultClassIcon.Id, ImVec2(IconSize, IconSize), DefaultClassIcon.UV0, DefaultClassIcon.UV1);
		}

		if (Asset && ImGui::IsItemHovered())
		{
			const ImVec2 BorderRectSize = ImVec2(IconSize, IconSize);
			const ImVec2 CursorPosition = ImGui::GetCursorScreenPos();

			const ImVec2 p0 = ImVec2(CursorPosition.x, CursorPosition.y - BorderRectSize.y - 4.f);
			const ImVec2 p1 = ImVec2(CursorPosition.x + BorderRectSize.x, CursorPosition.y - 4.f);

			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			DrawList->AddRect(p0, p1, 0x80FFFFFF, 0.f, ImDrawFlags_None, 1.f);

			ImGui::SetTooltip("%s", TCHAR_TO_ANSI_PATH(*Asset->GetPathName()));

			if (Asset && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				AssetPickerUtils::OpenEditorForAsset(Asset);
			}
		}
	};

	auto Add_UseSelectedAssetButton = [&](UObject*& InOutAsset)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, 0);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFF404040);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFF404040);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

		if (FImGui::ImageButtonWithTint("UseSelectedAsset", UseSelectedAssetIcon, 0x8FFFFFFF, 0xFFFFFFFF))
		{
			if (auto SelectedAsset = (AssetPickerUtils::GetSelectedAssetOfType(AssetType)))
			{
				InOutAsset = SelectedAsset;
			}
		}
		ImGui::SetItemTooltip("Use Selected Asset from Content Browser");
		
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(3);
	};

	auto Add_BrowseToAssetButton = [&](UObject* InAsset)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, 0);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFF404040);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFF404040);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

		if (!InAsset)
		{
			ImGui::BeginDisabled();
			FImGui::ImageButtonWithTint("BrowseToAsset", BrowseToAssetIcon, 0x8FFFFFFF, 0xFFFFFFFF);
			ImGui::EndDisabled();
		}
		else
		{
			if (FImGui::ImageButtonWithTint("BrowseToAsset", BrowseToAssetIcon, 0x8FFFFFFF, 0xFFFFFFFF))
			{
				AssetPickerUtils::SyncContentBrowserToAsset(InAsset);
			}
			ImGui::SetItemTooltip("Browse to '%s' in Content Browser", TCHAR_TO_ANSI(*InAsset->GetName()));
		}

		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(3);
	};

	auto Add_ResetSelectionButton = [&](UObject*& InOutAsset)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, 0xBFFFFFFF);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFFFFFFFF);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFFFFFFFF);

		if (FImGui::TransparentImageButton("ResetToDefault", ResetToDefaultIcon))
		{
			InOutAsset = nullptr;
		}
		if (InOutAsset)
		{
			ImGui::SetItemTooltip("Reset this property to its default value");
		}

		ImGui::PopStyleColor(3);
	};

	auto Add_AssetViewer = [&](UObject*& InOutAsset)
	{
		// configuration
		const float AssetViewerWidth = 400.f * GlobalScale;
		const char* AssetViewerPopupName = "AssetViewerPopup";
		const float AssetViewerRowHeight = 36.f * GlobalScale;
		const float AssetViewerRowHeightWithSpacing = AssetViewerRowHeight + ImGui::GetStyle().ItemSpacing.y * GlobalScale;
		const int32 PreviewTextMaxLength = 32;

		// TODO: `ImGui::RenderTextEllipsis` already does something similar
		FString PreviewText = InOutAsset ? InOutAsset->GetName() : FString(TEXT("None"));
		if (PreviewText.Len() >= PreviewTextMaxLength)
		{
			PreviewText.MidInline(0, PreviewTextMaxLength);

			// PreviewTextMaxLen...
			PreviewText[PreviewTextMaxLength - 1] = TCHAR('.');
			PreviewText[PreviewTextMaxLength - 2] = TCHAR('.');
			PreviewText[PreviewTextMaxLength - 3] = TCHAR('.');
		}

		if (!ImGui::IsPopupOpen(AssetViewerPopupName))
		{
			bIsAssetViewerVisible = false;
		}

		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.f, 0.5f));
		if (ImGui::Button(TCHAR_TO_ANSI(*PreviewText), ImVec2(AssetViewerWidth * 0.65f, 0.f)))
		{
			ImGui::OpenPopup(AssetViewerPopupName);
		}
		const float ComboBoxHeight = ImGui::GetItemRectSize().y;
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() - DropDownArrowIcon.Size.x * 2.f);
		FImGui::Image(DropDownArrowIcon);
		ImGui::PopStyleVar(1);

		const float AssetViewerPopupPosX = ImGui::GetCursorScreenPos().x;
		const float AvailableSpaceAbove = ImGui::GetCursorScreenPos().y * 0.65f;
		const float AvailableSpaceBelow = (ImGui::GetWindowHeight() - ImGui::GetCursorScreenPos().y) * 0.75f;
		const float PopupHeight = FMath::Min(FMath::Max(AvailableSpaceBelow, AvailableSpaceAbove), AssetViewerRowHeightWithSpacing * (FilteredAssetIndices.Num() + 1));
		if (AvailableSpaceBelow > AvailableSpaceAbove)
		{
			ImGui::SetNextWindowPos(ImVec2(AssetViewerPopupPosX, ImGui::GetCursorScreenPos().y), ImGuiCond_Always, ImVec2(0.f, 0.f));
		}
		else
		{
			const float OffsetY = ComboBoxHeight + ImGui::GetStyle().ItemSpacing.y * 2.f * GlobalScale;
			ImGui::SetNextWindowPos(ImVec2(AssetViewerPopupPosX, ImGui::GetCursorScreenPos().y - OffsetY), ImGuiCond_Always, ImVec2(0.f, 1.f));
		}

		int32 NewSelectedIndex = INDEX_NONE;
		if (ImGui::BeginPopup(AssetViewerPopupName, ImGuiWindowFlags_NoScrollbar))
		{
			bool bFilterAvailableAssets = false;

			// reset filter text and set focus when asset viewer is triggered
			if (!bIsAssetViewerVisible)
			{
				if (TextFilter.IsActive())
				{
					bFilterAvailableAssets = true;
				}
				TextFilter.Reset();
			}

			ImGui::BeginGroup();
			{
				if (TextFilter.Draw(Context, "Filter", "Search Assets", AssetViewerWidth, /*bSetFocus*/!bIsAssetViewerVisible))
				{
					bFilterAvailableAssets = true;
				}

				if (ImGui::BeginListBox("##AssetList", ImVec2(AssetViewerWidth, FMath::Max(AssetViewerRowHeightWithSpacing, PopupHeight - ImGui::GetItemRectSize().y))))
				{
					auto Add_AssetListEntry = [&](int32 AssetIndex, int32 RowIndex)
						{
							FNameBuilder AssetName{ AvailableAssets[AssetIndex].AssetName };
							FNameBuilder AssetPath{ AvailableAssets[AssetIndex].PackagePath };
							const bool bWasSelected = (AssetIndex == LastSelectedAssetIndex);
							{
								FImGuiNamedWidgetScope Scope{ RowIndex };

								if (ImGui::Selectable("", bWasSelected, ImGuiSelectableFlags_None, ImVec2(0, AssetViewerRowHeight)))
								{
									NewSelectedIndex = AssetIndex;
								}
								if (!bWasSelected)
								{
									ImGui::GetWindowDrawList()->AddRectFilled(
										ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
										ImGui::GetColorU32((RowIndex & 0x1) ? ImGuiCol_TableRowBgAlt : ImGuiCol_TableRowBg));
								}
							}
							ImGui::SameLine();

#if WITH_EDITOR
							FSlateShaderResource* PreviewTexture = AssetPickerUtils::GetAssetThumbnail(AvailableAssets[AssetIndex]);
#else
							FSlateShaderResource* PreviewTexture = nullptr;
#endif
							Add_AssetThumbnail(PreviewTexture, AssetViewerRowHeight, nullptr);

							ImGui::SameLine();
							{
								ImGui::BeginGroup();
								ImGui::TextUnformatted(TCHAR_TO_ANSI(*AssetName));
								ImGui::TextUnformatted(TCHAR_TO_ANSI_PATH(*AssetPath));
								ImGui::EndGroup();
							}

							// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
							if (bWasSelected || ((RowIndex == 0) && (LastSelectedAssetIndex == INDEX_NONE)))
							{
								// scroll to item when popup is opened
								if (!bIsAssetViewerVisible)
								{
									ImGui::ScrollToItem();
								}
								ImGui::SetItemDefaultFocus();
							}
						};

					ImGuiListClipper Clipper;
					Clipper.Begin(FilteredAssetIndices.Num());
					if (LastSelectedAssetIndexInFilteredList != INDEX_NONE)
					{
						Clipper.IncludeItemByIndex(LastSelectedAssetIndexInFilteredList);
					}

					int32 RowIndex = 0;
					while (Clipper.Step() && (NewSelectedIndex == INDEX_NONE))
					{
						for (int32 Index = Clipper.DisplayStart; Index < Clipper.DisplayEnd; Index++)
						{
							Add_AssetListEntry(FilteredAssetIndices[Index], RowIndex++);
						}
					}

					// NOTE: ImGui::BeginListBox can return false on first attempt (rect not visible in the popup window)
					// So this flag is set there, instead of using `ImGui::IsPopupOpen(AssetViewerPopupName)`
					bIsAssetViewerVisible = true;

					ImGui::EndListBox();
				}
			}
			ImGui::EndGroup();

			ImGui::SameLine();
			ImGui::BeginGroup();
			{
				auto AddButton = [](const char* Label, bool& bInOutState, const FImGuiImageBindingParams& ImageParams, const char* ToolTip)
					{
						const bool bWasActive = bInOutState;
						if (bWasActive)
						{
							ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive, 0.8f));
							ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImGuiCol_ButtonActive));
							ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetColorU32(ImGuiCol_ButtonActive, 0.8f));
						}
						else
						{
							ImGui::PushStyleColor(ImGuiCol_Button, 0);
							ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFF404040);
							ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFF404040);
						}
						ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2);
						ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

						if (FImGui::ImageButtonWithTint(Label, ImageParams, bWasActive ? 0xFFFFFFFF : 0xC8FFFFFF, 0xFFFFFFFF))
						{
							bInOutState = !bInOutState;
						}
						ImGui::SetItemTooltip(ToolTip);

						ImGui::PopStyleVar(2);
						ImGui::PopStyleColor(3);

						return bWasActive != bInOutState;
					};

				bFilterAvailableAssets |= AddButton("ToggleProjectContent", AssetPickerUtils::bShowProjectContent, ProjectContentIcon, "Show Project Content?");
				bFilterAvailableAssets |= AddButton("ToggleEngineContent", AssetPickerUtils::bShowEngineContent, EngineContentIcon, "Show Engine Content?");
				bFilterAvailableAssets |= AddButton("TogglePluginContent", AssetPickerUtils::bShowPluginContent, PluginContentIcon, "Show Plugin Content?");
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
				bFilterAvailableAssets |= AddButton("ToggleDeveloperContent", AssetPickerUtils::bShowDeveloperContent, DeveloperContentIcon, "Show Developer Folder Content?");
#if WITH_EDITOR
				bFilterAvailableAssets |= AddButton("SearchCollectionNames", AssetPickerUtils::bSearchAssetCollections, AssetCollectionsIcon, "Search Collection Names?");
#endif
				bFilterAvailableAssets |= AddButton("ToggleLocaizedContent", AssetPickerUtils::bShowLocalizedContent, LocalizedContentIcon, "Show Localized Content?");
			}
			ImGui::EndGroup();

			if (bFilterAvailableAssets)
			{
				FilterAvailableAssets();
			}

			if (NewSelectedIndex != INDEX_NONE)
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		if (NewSelectedIndex != INDEX_NONE)
		{
			// TODO: this performs a synchronous load, flushing async loading thread :(
			InOutAsset = (AvailableAssets[NewSelectedIndex].GetAsset());
		}
	};

	ImGui::BeginGroup();
	{
		if (strstr(Label, "##") == nullptr)
		{
			ImGui::BeginGroup();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 25.f * GlobalScale - ImGui::GetFontSize() * 0.5f);
			ImGui::TextUnformatted(Label);
			ImGui::EndGroup();

			ImGui::SameLine();
		}

		ImGui::BeginGroup();
		{
			Add_AssetThumbnail(SelectedAssetTexture, 50.f * GlobalScale, SelectedAsset);
			ImGui::SameLine();

			ImGui::BeginGroup();
			{
				// combo box
				Add_AssetViewer(SelectedAsset);

				// icons
				ImGui::BeginDisabled(WITH_EDITOR == 0);
				Add_UseSelectedAssetButton(SelectedAsset); ImGui::SameLine(); Add_BrowseToAssetButton(SelectedAsset);
				ImGui::EndDisabled();
			}
			ImGui::EndGroup();
		}
		ImGui::EndGroup();
		const ImVec2 GroupSize = ImGui::GetItemRectSize();

		// reset icon
		if (SelectedAsset)
		{
			ImGui::SameLine(); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + GroupSize.y * 0.5f - ResetToDefaultIcon.Size.y);
			Add_ResetSelectionButton(SelectedAsset);
		}
	}
	ImGui::EndGroup();

	const bool bSelectionChanged = (SelectedAsset != InOutSelectedAsset);
	if (bSelectionChanged)
	{
		InOutSelectedAsset = SelectedAsset;
		LastSelectedAssetPtr = SelectedAsset;
		LastSelectedAssetIndex = AvailableAssets.IndexOfByKey(FAssetData(SelectedAsset, FAssetData::ECreationFlags::SkipAssetRegistryTagsGathering|FAssetData::ECreationFlags::AllowBlueprintClass));
		LastSelectedAssetIndexInFilteredList = FilteredAssetIndices.IndexOfByKey(LastSelectedAssetIndex);
	}
	return bSelectionChanged;
}

void FImGuiAssetPicker::FilterAvailableAssets()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("AssetPicker::FilterAssets"), STAT_ImGuiAssetPicker_FilterAssets, STATGROUP_ImGui);

	FilteredAssetIndices.Reset();
	LastSelectedAssetIndexInFilteredList = INDEX_NONE;
	PackedAssetPathFilter = AssetPickerUtils::GetPackedAssetPathFilter();

#if WITH_EDITOR
	const ICollectionManager* CollectionManager = AssetPickerUtils::bSearchAssetCollections ? &FCollectionManagerModule::GetModule().Get() : nullptr;
#endif

	const auto& AssetContainer = AssetPickerUtils::GetAssetContainer(AssetType);
	const TArray<FAssetData>& AvailableAssets = AssetContainer.GetAvailableAssets();
	for (int32 AssetIndex = 0; AssetIndex < AvailableAssets.Num(); ++AssetIndex)
	{
		FNameBuilder AssetName{ AvailableAssets[AssetIndex].AssetName };

		bool bCollectionNamePassed = false;
#if WITH_EDITOR
		if (CollectionManager)
		{
			TArray<FName> AssetCollectionNames;
			CollectionManager->GetCollectionsContainingObject(
				AvailableAssets[AssetIndex].ToSoftObjectPath(),
				ECollectionShareType::CST_All,
				AssetCollectionNames,
				ECollectionRecursionFlags::SelfAndChildren);
			
			for (const FName& CollectionName : AssetCollectionNames)
			{
				FNameBuilder Collection{ CollectionName };
				if (TextFilter.PassFilter(Collection.ToView()))
				{
					bCollectionNamePassed = true;
					break;
				}
			}
		}
#endif

		if (!bCollectionNamePassed && !TextFilter.PassFilter(AssetName.ToView()))
		{
			continue;
		}

		FNameBuilder AssetPath{ AvailableAssets[AssetIndex].PackagePath };
		if (!AssetPickerUtils::FilterAssetPath(AssetPath))
		{
			continue;
		}

		if ((OptionalFilters.Num() > 0) && !Algo::AllOf(OptionalFilters,
			[&](const FFilter& RequiredTag)
			{
				FAssetDataTagMapSharedView::FFindTagResult TagResult = AvailableAssets[AssetIndex].TagsAndValues.FindTag(RequiredTag.AssetTag);
				return (TagResult.IsSet() && TagResult.Equals(RequiredTag.TagValue));
			}))
		{
			continue;
		}

		if (LastSelectedAssetIndex == AssetIndex)
		{
			LastSelectedAssetIndexInFilteredList = FilteredAssetIndices.Num();
		}
		FilteredAssetIndices.Add(AssetIndex);
	}

	ContainerRevisionId = AssetContainer.GetRevisionId();
}

#undef TCHAR_TO_ANSI_PATH
