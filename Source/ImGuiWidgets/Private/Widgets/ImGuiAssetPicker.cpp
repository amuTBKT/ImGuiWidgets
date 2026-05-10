// Copyright 2026 Amit Kumar Mehar. All Rights Reserved.

#include "ImGuiAssetPicker.h"

#if WITH_EDITOR
#include "Editor.h"
#include "AssetThumbnail.h"
#include "ClassIconFinder.h"
#include "ICollectionManager.h"
#include "ContentBrowserModule.h"
#include "CollectionManagerModule.h"
#include "Styling/SlateIconFinder.h"
#include "IContentBrowserSingleton.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#endif

#include "Misc/Paths.h"
#include "ImGuiSubsystem.h"
#include "Algo/BinarySearch.h"
#include "Misc/ConfigCacheIni.h"
#include "AssetRegistry/AssetData.h"
#include "Interfaces/IPluginManager.h"
#include "AssetRegistry/AssetRegistryModule.h"

#ifdef TCHAR_TO_UTF8_PATH
#error TCHAR_TO_UTF8_PATH already defined
#endif
#define TCHAR_TO_UTF8_PATH(path) (ANSICHAR*)StringCast<UTF8CHAR, FName::StringBufferSize>(static_cast<const TCHAR*>(path)).Get()

DECLARE_LLM_MEMORY_STAT(TEXT("ImGuiAssetPicker"), STAT_ImGuiAssetPickerLLM, STATGROUP_ImGui);
LLM_DEFINE_TAG(ImGuiAssetPicker, TEXT("AssetPicker"), TEXT("ImGui"), GET_STATFNAME(STAT_ImGuiAssetPickerLLM));

namespace AssetPickerUtils
{
	class FAssetContainer : FNoncopyable
	{
	public:
		FAssetContainer(const FSoftClassPath& ClassPath)
			: AssetClassPath(ClassPath)
		{
#if WITH_EDITOR
			ClassIconBrush = FClassIconFinder::FindThumbnailForClass(Cast<UClass>(AssetClassPath.TryLoad()), NAME_None);
#else
			ClassIconBrush = IMGUI_ICON("ImIcon.FallbackAssetIcon");
#endif

			if (auto AssetRegistryModulePtr = FModuleManager::GetModulePtr<FAssetRegistryModule>(FName("AssetRegistry")))
			{
				IAssetRegistry& AssetRegistry = AssetRegistryModulePtr->Get();
				GatherAssets(AssetRegistry);
				AssetRegistry.OnAssetAdded().AddRaw(this, &FAssetContainer::OnAssetAdded);
				AssetRegistry.OnAssetRemoved().AddRaw(this, &FAssetContainer::OnAssetRemoved);
				AssetRegistry.OnAssetRenamed().AddRaw(this, &FAssetContainer::OnAssetRenamed);
				AssetRegistry.OnAssetUpdated().AddRaw(this, &FAssetContainer::OnAssetUpdated);
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
				AssetRegistry.OnAssetUpdated().RemoveAll(this);
			}
		}

		uint32				GetRevisionId()			const { return RevisionId; }
		const FSlateBrush*	GetClassIconBrush()		const { return ClassIconBrush; }
		const auto&			GetAvailableAssets()	const { return AvailableAssets; }

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
			LLM_SCOPE_BYTAG(ImGuiAssetPicker);

			// TODO: expose filter settings
			FARFilter Filter;
			Filter.ClassPaths.Add(AssetClassPath.GetAssetPath());
			Filter.bRecursiveClasses = true;
			Filter.bIncludeOnlyOnDiskAssets = true;
			
			TArray<FAssetData> AvailableAssetsTemp;
			AssetRegistry.GetAssets(Filter, AvailableAssetsTemp);
			AvailableAssetsTemp.Sort([](const auto& A, const auto& B) { return SortAssetDataPredicate(A, B); });

			AvailableAssets = TArray<FAssetData, FImGuiAllocatorWithoutRangeCheck>{ MoveTemp(AvailableAssetsTemp) };
		}

		FORCEINLINE bool FilterAsset(const FAssetData& AssetData) const
		{
			return AssetData.GetClass() && AssetData.GetClass()->IsChildOf(Cast<UClass>(AssetClassPath.TryLoad()));
		}

		void OnAssetAdded(const FAssetData& AssetData)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("AssetPicker::OnAssetAdded"), STAT_ImGuiAssetPicker_OnAssetAdded, STATGROUP_ImGui);
			LLM_SCOPE_BYTAG(ImGuiAssetPicker);

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
			LLM_SCOPE_BYTAG(ImGuiAssetPicker);

			if (FilterAsset(AssetData))
			{
				RevisionId++;
				AvailableAssets.Remove(AssetData);
			}
		}

		void OnAssetRenamed(const FAssetData& AssetData, const FString& OldName)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("AssetPicker::OnAssetRenamed"), STAT_ImGuiAssetPicker_OnAssetRenamed, STATGROUP_ImGui);
			LLM_SCOPE_BYTAG(ImGuiAssetPicker);

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

		// triggers when asset is saved!
		void OnAssetUpdated(const FAssetData& AssetData)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("AssetPicker::OnAssetUpdated"), STAT_ImGuiAssetPicker_OnAssetUpdated, STATGROUP_ImGui);
			LLM_SCOPE_BYTAG(ImGuiAssetPicker);

			if (FilterAsset(AssetData))
			{
				FAssetData* ExistingAssetData = AvailableAssets.FindByKey(AssetData);
				if (ExistingAssetData && ExistingAssetData->TagsAndValues != AssetData.TagsAndValues)
				{
					ExistingAssetData->TagsAndValues = AssetData.TagsAndValues;
					RevisionId++;
				}
			}
		}

	private:
		FSoftClassPath AssetClassPath;
		TArray<FAssetData, FImGuiAllocatorWithoutRangeCheck> AvailableAssets;
		const FSlateBrush* ClassIconBrush = nullptr;
		uint32 RevisionId = 0;
	};

	static TMap<uint32, TUniquePtr<FAssetContainer>> AssetContainerInstances;
	static FAssetContainer& GetAssetContainer(const FSoftClassPath& ClassPath)
	{
		const uint32 ContainerHash = GetTypeHash(ClassPath);
		TUniquePtr<FAssetContainer>* ContainerInstance = AssetContainerInstances.Find(ContainerHash);
		if (!ContainerInstance)
		{
			ContainerInstance = &AssetContainerInstances.Add(ContainerHash, MakeUnique<FAssetContainer>(ClassPath));
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

	static FSoftObjectPtr GetSelectedAssetOfType(const UClass* AssetClass)
	{
#if WITH_EDITOR
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		TArray<FAssetData> SelectedAssets;
		ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

		for (const FAssetData& Asset : SelectedAssets)
		{
			if (Asset.GetClass()->IsChildOf(AssetClass))
			{
				return FSoftObjectPtr{ Asset.ToSoftObjectPath() };
			}
		}
		return {};
#else
		return {};
#endif
	}

	static void OpenEditorForAsset(const FSoftObjectPtr& SoftAssetPtr)
	{
#if WITH_EDITOR
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (AssetEditorSubsystem)
		{
			// NOTE: this is the only place where we HAVE to load the asset!
			AssetEditorSubsystem->OpenEditorForAsset(SoftAssetPtr.LoadSynchronous());
		}
#endif
	}

	static void SyncContentBrowserToAsset(FAssetData Asset)
	{
#if WITH_EDITOR
		GEditor->SyncBrowserToObjects(TArray<FAssetData>{ Asset });
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

	// serialization
	static bool bSettingsLoaded = false;
	void LoadSettings()
	{
		if (bSettingsLoaded)
		{
			return;
		}
		bSettingsLoaded = true;

		FConfigFile* WidgetSettings = FImGuiSettings::GetConfigFile();
		if (WidgetSettings)
		{
			int64 PackedFilterSettings;
			if (WidgetSettings->GetInt64(TEXT("AssetPicker"), TEXT("PackedAssetPathFilter"), PackedFilterSettings))
			{
				bShowProjectContent		= (PackedFilterSettings & (1u << 0)) > 0;
				bShowEngineContent		= (PackedFilterSettings & (1u << 1)) > 0;
				bShowPluginContent		= (PackedFilterSettings & (1u << 2)) > 0;
				bShowDeveloperContent	= (PackedFilterSettings & (1u << 3)) > 0;
				bSearchAssetCollections = (PackedFilterSettings & (1u << 4)) > 0;
				bShowLocalizedContent	= (PackedFilterSettings & (1u << 5)) > 0;
			}
		}
	}
	void SaveSettings()
	{
		FConfigFile* WidgetSettings = FImGuiSettings::GetConfigFile();
		if (WidgetSettings)
		{
			WidgetSettings->SetInt64(TEXT("AssetPicker"), TEXT("PackedAssetPathFilter"), (int64)GetPackedAssetPathFilter());
			FImGuiSettings::SaveConfigFile();
		}
	}
}

FImGuiAssetPicker FImGuiAssetPicker::MakeWidget(const FSoftClassPath& ClassPath, TArray<FFilter> OptionalFilters)
{
	FImGuiAssetPicker Widget = {};
	Widget.AssetClassPath = ClassPath;
	Widget.OptionalFilters = MoveTemp(OptionalFilters);
	return Widget;
}

void FImGuiAssetPicker::DrawInvalidWidget(FImGuiTickContext* Context, const char* Label, const char* ErrorMessage, bool bDrawCompactWidget)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("AssetPicker::Draw"), STAT_ImGuiAssetPicker_Draw, STATGROUP_ImGui);
	LLM_SCOPE_BYNAME("ImGui/AssetPicker/DrawWidget");

	FImGui::DrawWarningMessageBox(Context, bDrawCompactWidget ? 4.f : 16.f, ImVec4(1.f, 0.f, 0.f, 1.f), *FAnsiString::Printf("AssetPicker('%s') : %s", Label, ErrorMessage));
}

bool FImGuiAssetPicker::DrawInternal(FImGuiTickContext* Context, const char* Label, FSoftObjectPtr& InOutSelectedAsset, bool bDrawCompactWidget)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("AssetPicker::Draw"), STAT_ImGuiAssetPicker_Draw, STATGROUP_ImGui);
	LLM_SCOPE_BYNAME("ImGui/AssetPicker/DrawWidget");

	const UClass* AssetClass = GetAssetClass();
	if (!ensure(AssetClass))
	{
		return false;
	}

	AssetPickerUtils::LoadSettings();

	FImGui::EnsureValidImGuiContext(Context);

	FImGuiNamedScope WidgetScope{ Label };

	auto& AssetContainer = AssetPickerUtils::GetAssetContainer(AssetClassPath);
	const auto& AvailableAssets = AssetContainer.GetAvailableAssets();

	FSlateShaderResource* SelectedAssetTexture = nullptr;
	FSoftObjectPtr SelectedSoftAssetPtr = InOutSelectedAsset;

	const bool bAssetCountainerChanged = (PackedAssetPathFilter != AssetPickerUtils::GetPackedAssetPathFilter()) || (ContainerRevisionId != AssetContainer.GetRevisionId());
	if (bAssetCountainerChanged || LastSelectedAssetPtr.IsStale() || (SelectedSoftAssetPtr != LastSelectedAssetPtr))
	{
		LastSelectedAssetIndex = AvailableAssets.IndexOfByPredicate(
			[SoftObjectPath=SelectedSoftAssetPtr.ToSoftObjectPath()](const auto& InAssetData) { return InAssetData.GetSoftObjectPath() == SoftObjectPath; });
		
		if (LastSelectedAssetPtr.IsStale() || (LastSelectedAssetIndex == INDEX_NONE))
		{
			SelectedSoftAssetPtr.Reset();
		}
	}

	if (bAssetCountainerChanged)
	{
		FilterAvailableAssets();

		// check if selected asset has become invalid now? (not relevant for this asset picker)
		if ((LastSelectedAssetIndex != INDEX_NONE) && !OptionalFilters.IsEmpty())
		{
			bool bOptionalFiltersPassed = true;
			for (int32 FilterIndex = 0; FilterIndex < OptionalFilters.Num(); ++FilterIndex)
			{
				const FFilter& Filter = OptionalFilters[FilterIndex];
				if (Filter.IsType<FImGuiAssetTagFilter>())
				{
					const FImGuiAssetTagFilter& AssetTagFilter = Filter.Get<FImGuiAssetTagFilter>();
					FAssetDataTagMapSharedView::FFindTagResult TagResult = AvailableAssets[LastSelectedAssetIndex].TagsAndValues.FindTag(AssetTagFilter.TagName);
					bOptionalFiltersPassed &= (TagResult.IsSet() && TagResult.Equals(AssetTagFilter.ExpectedValue));
				}
			}
			if (!bOptionalFiltersPassed)
			{
				SelectedSoftAssetPtr.Reset();
			}
		}
	}

#if WITH_EDITOR
	if (!SelectedSoftAssetPtr.IsNull())
	{
		SelectedAssetTexture = AssetPickerUtils::GetAssetThumbnail(FAssetData(SelectedSoftAssetPtr.ToSoftObjectPath().GetLongPackageName(), SelectedSoftAssetPtr.ToSoftObjectPath().GetAssetPathString(), AssetClassPath.GetAssetPath()));
	}
#endif

	UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
	const float GlobalScale = ImGui::GetStyle().FontScaleMain;

	auto Add_AssetThumbnail = [&](FSlateShaderResource* AssetThumbnail, float IconSize, const FSoftObjectPtr& InSoftAssetPtr)
	{
		const FImGuiImageBindingParams DefaultClassIcon = ImGuiSubsystem->RegisterOneFrameResource(AssetContainer.GetClassIconBrush(), 50.f * GlobalScale);

		if (AssetThumbnail)
		{
			const FImGuiImageBindingParams ThumbnailIcon = ImGuiSubsystem->RegisterOneFrameResource(AssetThumbnail);
			ImGui::GetWindowDrawList()->AddCallback(ImDrawCallback_SetShaderState, MakeImGuiShaderState(EImGuiShaderState::DisableAlphaBlending));
			ImGui::Image(ThumbnailIcon.Id, ImVec2(IconSize, IconSize), ThumbnailIcon.UV0, ThumbnailIcon.UV1);
			ImGui::GetWindowDrawList()->AddCallback(ImDrawCallback_SetShaderState, MakeImGuiShaderState(EImGuiShaderState::Default));
		}
		else
		{
			ImGui::Image(DefaultClassIcon.Id, ImVec2(IconSize, IconSize), DefaultClassIcon.UV0, DefaultClassIcon.UV1);
		}

		if (!InSoftAssetPtr.IsNull() && ImGui::IsItemHovered())
		{
			const ImVec2 BorderRectSize = ImVec2(IconSize, IconSize);
			const ImVec2 CursorPosition = ImGui::GetCursorScreenPos();

			const ImVec2 p0 = ImVec2(CursorPosition.x, CursorPosition.y - BorderRectSize.y - 4.f);
			const ImVec2 p1 = ImVec2(CursorPosition.x + BorderRectSize.x, CursorPosition.y - 4.f);

			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			DrawList->AddRect(p0, p1, 0x80FFFFFF, 0.f, ImDrawFlags_None, 1.f);

			ImGui::SetTooltip("%s", TCHAR_TO_UTF8_PATH(*InSoftAssetPtr.GetLongPackageName()));

			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				AssetPickerUtils::OpenEditorForAsset(InSoftAssetPtr);
			}
		}
	};

	auto Add_UseSelectedAssetButton = [&](FSoftObjectPtr& InOutSoftAssetPtr, float IconSize)
	{
		const FImGuiImageBindingParams UseSelectedAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.UseSelectedAsset"), IconSize);

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
				FSoftObjectPtr AssetPtr = (AssetPickerUtils::GetSelectedAssetOfType(AssetClass));
				if (!AssetPtr.IsNull())
				{
					InOutSoftAssetPtr = AssetPtr;
				}
			}
			ImGui::SetItemTooltip("Use Selected Asset from Content Browser");
		}
	};

	auto Add_BrowseToAssetButton = [&](const FSoftObjectPtr& InSoftAssetPtr, float IconSize)
	{
		const FImGuiImageBindingParams BrowseToAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.BrowseToAsset"), IconSize);

		if (InSoftAssetPtr.IsNull() || (WITH_EDITOR == 0))
		{
			ImGui::BeginDisabled();
			FImGui::TransparentImageButton("BrowseToAsset", BrowseToAssetIcon);
			ImGui::EndDisabled();
		}
		else
		{
			if (FImGui::TransparentImageButton("BrowseToAsset", BrowseToAssetIcon))
			{
				AssetPickerUtils::SyncContentBrowserToAsset(FAssetData(InSoftAssetPtr.ToSoftObjectPath().GetLongPackageName(), InSoftAssetPtr.ToSoftObjectPath().GetAssetPathString(), AssetClassPath.GetAssetPath()));
			}
			ImGui::SetItemTooltip("Browse to '%s' in Content Browser", TCHAR_TO_UTF8(*InSoftAssetPtr.GetAssetName()));
		}
	};

	auto Add_ResetSelectionButton = [&](FSoftObjectPtr& InOutSoftAssetPtr, float IconSize)
	{
		const FImGuiImageBindingParams ResetToDefaultIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.ResetToDefault"), IconSize);

		if (FImGui::TransparentImageButton("ResetToDefault", ResetToDefaultIcon))
		{
			InOutSoftAssetPtr.Reset();
		}
		if (!InOutSoftAssetPtr.IsNull())
		{
			ImGui::SetItemTooltip("Reset");
		}
	};

	auto Add_AssetViewer = [&](FSoftObjectPtr& InOutSoftAssetPtr) -> ImVec2
	{
		// configuration
		const float AssetViewerWidth = 400.f * GlobalScale;
		const float AssetViewerMaxRowsVisible = 8;
		const float AssetViewerRowHeight = ImGui::GetFontSize() * 2.f + ImGui::GetStyle().ItemSpacing.y * 1.5f * GlobalScale;
		const char* AssetViewerPopupName = "AssetViewerPopup";

		const float AssetViewerRowHeightWithSpacing = (AssetViewerRowHeight + ImGui::GetStyle().ItemSpacing.y * GlobalScale);
		const float AssetViewerDesiredHeight = FMath::Min(AssetViewerMaxRowsVisible, FilteredAssetIndices.Num()) * AssetViewerRowHeightWithSpacing + AssetViewerRowHeightWithSpacing * 1.5f;
		const float AssetViewerComboxBoxWidth = FMath::Clamp(256.f * GlobalScale, 70.f * GlobalScale, ImGui::GetContentRegionAvail().x - (bDrawCompactWidget ? 75.f : 32.f) * GlobalScale);
		const int32 PreviewTextMaxLength = FMath::Clamp(FMath::CeilToInt(1.25f * AssetViewerComboxBoxWidth / ImGui::GetFontSize()) - 1, 4, 32);

		const float AssetViewerPopupPosX = ImGui::GetCursorScreenPos().x;
		const float AvailableSpaceAbove = ImGui::GetCursorScreenPos().y;
		const float MonitorDisplaySize = ImGui::GetPlatformIO().Monitors.empty() ? ImGui::GetWindowHeight() : ImGui::GetPlatformIO().Monitors[0].WorkSize.y;
		const float AvailableSpaceBelow = (MonitorDisplaySize - ImGui::GetCursorScreenPos().y);
		float AssetViewerPopupHeight = ((AvailableSpaceBelow > AssetViewerDesiredHeight) ? AvailableSpaceBelow : AvailableSpaceAbove) * 0.8f;
		AssetViewerPopupHeight = FMath::Min(AssetViewerPopupHeight, AssetViewerDesiredHeight);
		float AssetViewerPopupHeightMin = ((float)AssetViewerMinSizeFactor * AssetViewerRowHeightWithSpacing) + ImGui::GetStyle().WindowPadding.y * 2.f;

		// TODO: `ImGui::RenderTextEllipsis` does something similar
		FString PreviewText = !InOutSoftAssetPtr.IsNull() ? InOutSoftAssetPtr.GetAssetName() : FString(TEXT("None"));
		if (PreviewText.Len() >= PreviewTextMaxLength)
		{
			PreviewText.MidInline(0, PreviewTextMaxLength);

			// PreviewTextMaxLen...
			PreviewText[PreviewTextMaxLength - 1] = TCHAR('.');
			PreviewText[PreviewTextMaxLength - 2] = TCHAR('.');
			PreviewText[PreviewTextMaxLength - 3] = TCHAR('.');
		}

		ImVec2 ComboBoxSize;
		bool bIsComboBoxVisible;
		ImGui::BeginGroup();
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.f, 0.5f));
			if (ImGui::Button(TCHAR_TO_UTF8(*PreviewText), ImVec2(AssetViewerComboxBoxWidth, 0.f)))
			{
				ImGui::OpenPopup(AssetViewerPopupName);
			}
			if (!SelectedSoftAssetPtr.IsNull() && ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_NoSharedDelay | ImGuiHoveredFlags_DelayNormal))
			{
				ImGui::SetItemTooltip("%s", TCHAR_TO_UTF8_PATH(*SelectedSoftAssetPtr.GetLongPackageName()));
			}
			ComboBoxSize = ImGui::GetItemRectSize();
			bIsComboBoxVisible = ImGui::IsItemVisible();

			ImGui::SameLine();
			
			const FImGuiImageBindingParams DropDownArrowIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.DropDownArrow"), ComboBoxSize.y * 0.9f);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() - DropDownArrowIcon.Size.x * 2.f);
			FImGui::Image(DropDownArrowIcon);
			ImGui::PopStyleVar(1);
		}
		ImGui::EndGroup();

		if ((AvailableSpaceBelow > AssetViewerPopupHeight) || (AvailableSpaceBelow > AvailableSpaceAbove))
		{
			ImGui::SetNextWindowPos(ImVec2(AssetViewerPopupPosX, ImGui::GetCursorScreenPos().y), ImGuiCond_Always, ImVec2(0.f, 0.f));
		}
		else
		{
			const float OffsetY = ComboBoxSize.y + ImGui::GetStyle().ItemSpacing.y * 2.f * GlobalScale;
			ImGui::SetNextWindowPos(ImVec2(AssetViewerPopupPosX, ImGui::GetCursorScreenPos().y - OffsetY), ImGuiCond_Always, ImVec2(0.f, 1.f));
		}

		int32 NewSelectedIndex = INDEX_NONE;
		ImGui::SetNextWindowSize(ImVec2(0.f, FMath::Max(AssetViewerPopupHeight, AssetViewerPopupHeightMin)), ImGuiCond_Always);
		if (ImGui::BeginPopup(AssetViewerPopupName, ImGuiWindowFlags_NoScrollbar))
		{
			bool bFilterAvailableAssets = false;

			// reset filter text and set focus when asset viewer is triggered
			if (ImGui::IsWindowAppearing())
			{
				if (TextFilter.IsActive())
				{
					bFilterAvailableAssets = true;
				}
				TextFilter.Reset();
			}

			ImGui::BeginGroup();
			{
				if (TextFilter.Draw(Context, "Filter", "Search Assets", AssetViewerWidth, /*bSetFocus=*/ImGui::IsWindowAppearing()))
				{
					bFilterAvailableAssets = true;
				}

				if (ImGui::BeginListBox("##AssetList", ImVec2(AssetViewerWidth, ImGui::GetContentRegionAvail().y)))
				{
					auto Add_AssetListEntry = [&](int32 AssetIndex, int32 RowIndex)
						{
							FNameBuilder AssetName{ AvailableAssets[AssetIndex].AssetName };
							FNameBuilder AssetPath{ AvailableAssets[AssetIndex].PackagePath };
							const bool bWasSelected = (AssetIndex == LastSelectedAssetIndex);
							{
								FImGuiNamedScope Scope{ AssetIndex };

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
							Add_AssetThumbnail(PreviewTexture, AssetViewerRowHeight, FSoftObjectPtr());

							ImGui::SameLine();
							{
								ImGui::BeginGroup();
								ImGui::TextUnformatted(TCHAR_TO_UTF8(*AssetName));
								ImGui::TextUnformatted(TCHAR_TO_UTF8_PATH(*AssetPath));
								ImGui::EndGroup();
							}

							// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
							if (bWasSelected || ((RowIndex == 0) && (LastSelectedAssetIndex == INDEX_NONE)))
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
					Clipper.Begin(FilteredAssetIndices.Num());
					if (LastSelectedAssetIndexInFilteredList != INDEX_NONE)
					{
						Clipper.IncludeItemByIndex(LastSelectedAssetIndexInFilteredList);
					}

					while (Clipper.Step())
					{
						for (int32 Index = Clipper.DisplayStart; Index < Clipper.DisplayEnd; Index++)
						{
							Add_AssetListEntry(FilteredAssetIndices[Index], Index);
						}
					}

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
						ImGui::SetItemTooltip("%s", ToolTip);

						ImGui::PopStyleVar(2);
						ImGui::PopStyleColor(3);

						return bWasActive != bInOutState;
					};

				const FImGuiImageBindingParams ProjectContentIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.ProjectFolder"), 16.f * GlobalScale);
				const FImGuiImageBindingParams EngineContentIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.EngineFolder"), 16.f * GlobalScale);
				const FImGuiImageBindingParams PluginContentIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.PluginFolder"), 16.f * GlobalScale);
				const FImGuiImageBindingParams DeveloperContentIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.DeveloperFolder"), 16.f * GlobalScale);
				const FImGuiImageBindingParams AssetCollectionsIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.AssetCollection"), 16.f * GlobalScale);
				const FImGuiImageBindingParams LocalizedContentIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.LocalizedFolder"), 16.f * GlobalScale);

				bool bFilterSettingsChanged = false;
				bFilterSettingsChanged |= AddButton("ToggleProjectContent", AssetPickerUtils::bShowProjectContent, ProjectContentIcon, "Show Project Content?");
				bFilterSettingsChanged |= AddButton("ToggleEngineContent", AssetPickerUtils::bShowEngineContent, EngineContentIcon, "Show Engine Content?");
				bFilterSettingsChanged |= AddButton("TogglePluginContent", AssetPickerUtils::bShowPluginContent, PluginContentIcon, "Show Plugin Content?");
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
				bFilterSettingsChanged |= AddButton("ToggleDeveloperContent", AssetPickerUtils::bShowDeveloperContent, DeveloperContentIcon, "Show Developer Folder Content?");
#if WITH_EDITOR
				bFilterSettingsChanged |= AddButton("SearchCollectionNames", AssetPickerUtils::bSearchAssetCollections, AssetCollectionsIcon, "Search Collection Names?");
#endif
				bFilterSettingsChanged |= AddButton("ToggleLocaizedContent", AssetPickerUtils::bShowLocalizedContent, LocalizedContentIcon, "Show Localized Content?");

				bFilterAvailableAssets |= bFilterSettingsChanged;

				if (bFilterSettingsChanged)
				{
					AssetPickerUtils::SaveSettings();
				}
			}
			ImGui::EndGroup();
			AssetViewerMinSizeFactor = FMath::DivideAndRoundUp(ImGui::GetItemRectSize().y, AssetViewerRowHeightWithSpacing);

			if (bFilterAvailableAssets)
			{
				FilterAvailableAssets();
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
			InOutSoftAssetPtr = FSoftObjectPtr(AvailableAssets[NewSelectedIndex].ToSoftObjectPath());
		}

		return ComboBoxSize;
	};

	ImRect AssetDragDropArea;
	{
		ImGui::BeginGroup();
		if (bDrawCompactWidget)
		{
			if (strstr(Label, "##") == nullptr)
			{
				ImGui::BeginGroup();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(Label);
				ImGui::EndGroup();

				ImGui::SameLine();
			}

			float IconSize;
			float IconPaddingTop;
			ImGui::BeginGroup();
			{
				const ImVec2 AssetViewerComboBoxSize = Add_AssetViewer(SelectedSoftAssetPtr);

				IconSize = AssetViewerComboBoxSize.y * 0.9f;
				IconPaddingTop = AssetViewerComboBoxSize.y * 0.05f;

				ImGui::PushStyleColor(ImGuiCol_Button, 0xBFFFFFFF);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFFFFFFFF);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFFFFFFFF);
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4 * GlobalScale, 0));
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * GlobalScale, 0));

				ImGui::SameLine();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + IconPaddingTop);
				Add_UseSelectedAssetButton(SelectedSoftAssetPtr, IconSize);

				ImGui::SameLine();
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + IconPaddingTop);
				Add_BrowseToAssetButton(SelectedSoftAssetPtr, IconSize);

				ImGui::PopStyleVar(2);
				ImGui::PopStyleColor(3);
			}
			ImGui::EndGroup();
			AssetDragDropArea = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			// reset icon
			if (!SelectedSoftAssetPtr.IsNull())
			{
				ImGui::PushStyleColor(ImGuiCol_Button, 0xBFFFFFFF);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFFFFFFFF);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFFFFFFFF);
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4 * GlobalScale, 0));
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * GlobalScale, 0));

				if (!SelectedSoftAssetPtr.IsNull())
				{
					ImGui::SameLine();
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + IconPaddingTop);
					Add_ResetSelectionButton(SelectedSoftAssetPtr, IconSize);
				}

				ImGui::PopStyleVar(2);
				ImGui::PopStyleColor(3);
			}
		}
		else
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
				Add_AssetThumbnail(SelectedAssetTexture, 50.f * GlobalScale, SelectedSoftAssetPtr);
				ImGui::SameLine();

				ImGui::BeginGroup();
				{
					// combo box
					Add_AssetViewer(SelectedSoftAssetPtr);

					// icons
					{
						ImGui::PushStyleColor(ImGuiCol_Button, 0xBFFFFFFF);
						ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFFFFFFFF);
						ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFFFFFFFF);
						ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4 * GlobalScale, 0));
						ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * GlobalScale, 0));

						Add_UseSelectedAssetButton(SelectedSoftAssetPtr, 18.f * GlobalScale);
						ImGui::SameLine();
						Add_BrowseToAssetButton(SelectedSoftAssetPtr, 18.f * GlobalScale);

						ImGui::PopStyleVar(2);
						ImGui::PopStyleColor(3);
					}
				}
				ImGui::EndGroup();
			}
			ImGui::EndGroup();
			const ImVec2 GroupSize = ImGui::GetItemRectSize();
			AssetDragDropArea = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			// reset icon
			if (!SelectedSoftAssetPtr.IsNull())
			{
				ImGui::PushStyleColor(ImGuiCol_Button, 0xBFFFFFFF);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFFFFFFFF);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFFFFFFFF);

				ImGui::SameLine(); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + GroupSize.y * 0.5f - (18.f * GlobalScale));
				Add_ResetSelectionButton(SelectedSoftAssetPtr, 18.f * GlobalScale);

				ImGui::PopStyleColor(3);
			}
		}
		ImGui::EndGroup();
	}

#if WITH_EDITOR
	if (FImGui::DrawDragDropArea<FAssetDragDropOp>(Context, "AssetDragDrop", AssetDragDropArea,
		[&](TSharedPtr<FAssetDragDropOp> DragDropOp) { return DragDropOp->GetAssets().Num() == 1 && DragDropOp->GetAssets()[0].GetClass()->IsChildOf(AssetClass); },
		[&](TSharedPtr<FAssetDragDropOp> DragDropOp) { SelectedSoftAssetPtr = DragDropOp->GetAssets()[0].ToSoftObjectPath(); }))
	{
	}
#endif

	const bool bSelectionChanged = (SelectedSoftAssetPtr != InOutSelectedAsset);
	if (bSelectionChanged)
	{
		InOutSelectedAsset = SelectedSoftAssetPtr;
		LastSelectedAssetPtr = SelectedSoftAssetPtr;
		LastSelectedAssetIndex = AvailableAssets.IndexOfByPredicate(
			[SoftObjectPath=SelectedSoftAssetPtr.ToSoftObjectPath()](const auto& InAssetData) { return InAssetData.GetSoftObjectPath() == SoftObjectPath; });
		LastSelectedAssetIndexInFilteredList = FilteredAssetIndices.IndexOfByKey(LastSelectedAssetIndex);
	}
	return bSelectionChanged;
}

void FImGuiAssetPicker::FilterAvailableAssets()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("AssetPicker::FilterAssets"), STAT_ImGuiAssetPicker_FilterAssets, STATGROUP_ImGui);
	LLM_SCOPE_BYTAG(ImGuiAssetPicker);

	const UClass* AssetClass = GetAssetClass();

	FilteredAssetIndices.Reset();
	LastSelectedAssetIndexInFilteredList = INDEX_NONE;
	PackedAssetPathFilter = AssetPickerUtils::GetPackedAssetPathFilter();

#if WITH_EDITOR
	const ICollectionManager* CollectionManager = AssetPickerUtils::bSearchAssetCollections ? &FCollectionManagerModule::GetModule().Get() : nullptr;
#endif

	TArray<UClass*> ResolvedFilterClasses;
	ResolvedFilterClasses.Reserve(OptionalFilters.Num());
	for (const FFilter& Filter : OptionalFilters)
	{
		if (Filter.IsType<FImGuiAllowedClassFilter>())
		{
			ResolvedFilterClasses.Add(Cast<UClass>(Filter.Get<FImGuiAllowedClassFilter>().ClassPath.TryLoad()));
		}
		else if (Filter.IsType<FImGuiDisallowedClassFilter>())
		{
			ResolvedFilterClasses.Add(Cast<UClass>(Filter.Get<FImGuiDisallowedClassFilter>().ClassPath.TryLoad()));
		}
		else
		{
			ResolvedFilterClasses.Add(nullptr);
		}
	}

	const auto& AssetContainer = AssetPickerUtils::GetAssetContainer(AssetClassPath);
	const auto& AvailableAssets = AssetContainer.GetAvailableAssets();
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

		bool bOptionalFiltersPassed = true;
		if (!OptionalFilters.IsEmpty())
		{
			for (int32 FilterIndex = 0; FilterIndex < OptionalFilters.Num(); ++FilterIndex)
			{
				const FFilter& Filter = OptionalFilters[FilterIndex];
				if (Filter.IsType<FImGuiAssetTagFilter>())
				{
					const FImGuiAssetTagFilter& AssetTagFilter = Filter.Get<FImGuiAssetTagFilter>();
					FAssetDataTagMapSharedView::FFindTagResult TagResult = AvailableAssets[AssetIndex].TagsAndValues.FindTag(AssetTagFilter.TagName);
					bOptionalFiltersPassed &= (TagResult.IsSet() && TagResult.Equals(AssetTagFilter.ExpectedValue));
				}
				else if (Filter.IsType<FImGuiAllowedClassFilter>())
				{
					bOptionalFiltersPassed &= AvailableAssets[AssetIndex].GetClass()->IsChildOf(ResolvedFilterClasses[FilterIndex]);
				}
				else if (Filter.IsType<FImGuiDisallowedClassFilter>())
				{
					bOptionalFiltersPassed &= !AvailableAssets[AssetIndex].GetClass()->IsChildOf(ResolvedFilterClasses[FilterIndex]);
				}
				else
				{
					checkNoEntry();
				}
			}
		}
		if (!bOptionalFiltersPassed)
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

#undef TCHAR_TO_UTF8_PATH
