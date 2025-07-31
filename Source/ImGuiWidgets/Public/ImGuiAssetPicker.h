// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#if WITH_IMGUI

#include "Algo/BinarySearch.h"
#include "ImGuiCommonWidgets.h"
#include "AssetRegistry/AssetData.h"
#include "Blueprint/BlueprintSupport.h"
#include "AssetRegistry/AssetRegistryModule.h"

namespace FImGuiContentBrowserUtils
{
	IMGUIWIDGETS_API extern bool bShowProjectConent;
	IMGUIWIDGETS_API extern bool bShowEngineConent;
	IMGUIWIDGETS_API extern bool bShowPluginConent;
	IMGUIWIDGETS_API extern bool bShowDeveloperConent;

	IMGUIWIDGETS_API extern const FSlateBrush* GetIconForClass(UClass* AssetClass);
	IMGUIWIDGETS_API extern FSlateShaderResource* GetAssetThumbnail(const FAssetData& AssetData);
	IMGUIWIDGETS_API extern bool FilterAsset(FName AssetPath);
	IMGUIWIDGETS_API extern UObject* GetSelectedAsset(UClass* AssetClass);
	IMGUIWIDGETS_API extern void OpenEditorForAsset(UObject* Asset);
	IMGUIWIDGETS_API extern void SyncContentBrowserToAsset(UObject* Asset);
};

#ifdef TCHAR_TO_ANSI_PATH
#error TCHAR_TO_ANSI_PATH already defined
#endif
#define TCHAR_TO_ANSI_PATH(path) (ANSICHAR*)StringCast<ANSICHAR, FName::StringBufferSize>(static_cast<const TCHAR*>(path)).Get()

template <typename... Types>
class FImGuiAssetPicker : FNoncopyable
{
	// validation for generic asset types
	static_assert(sizeof...(Types) > 0, "Asset type missing, expected FImGuiAssetPicker<UAssetType>");
	using TAssetType = std::tuple_element<0, std::tuple<Types...>>::type;
	
	// blueprints support optional SubClass argument
	static constexpr bool bUsedWithBlueprintAsset = std::is_same<TAssetType, UBlueprint>::value;
	static constexpr bool bUseBlueprintSubClass = bUsedWithBlueprintAsset && (sizeof...(Types) > 1);
	
	class FAssetContainer
	{
	public:
		static FAssetContainer& GetInstance()
		{
			static FAssetContainer ContainerInstance = {};
			return ContainerInstance;
		}

		~FAssetContainer()
		{
			if (auto AssetRegistryModulePtr = FModuleManager::GetModulePtr<FAssetRegistryModule>(FName("AssetRegistry")))
			{
				IAssetRegistry& AssetRegistry = AssetRegistryModulePtr->Get();
				AssetRegistry.OnAssetAdded().RemoveAll(this);
				AssetRegistry.OnAssetRemoved().RemoveAll(this);
			}
		}

		uint32 GetRevisionId() const { return RevisionId; }
		const FSlateBrush* GetClassIconBrush() const { return ClassIconBrush; }
		const TArray<FAssetData>& GetAvailableAssets() const { return AvailableAssets; }

	private:
		FAssetContainer()
		{
			ClassIconBrush = FImGuiContentBrowserUtils::GetIconForClass(TAssetType::StaticClass());

			if (auto AssetRegistryModulePtr = FModuleManager::GetModulePtr<FAssetRegistryModule>(FName("AssetRegistry")))
			{
				IAssetRegistry& AssetRegistry = AssetRegistryModulePtr->Get();
				GatherAssets(AssetRegistry);
				AssetRegistry.OnAssetAdded().AddRaw(this, &FAssetContainer::OnAssetAdded);
				AssetRegistry.OnAssetRemoved().AddRaw(this, &FAssetContainer::OnAssetRemoved);
			}
		}

		static FORCEINLINE bool CompareAssetData(const FAssetData& LHS, const FAssetData& RHS)
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

			FARFilter Filter;
			Filter.ClassPaths.Add(TAssetType::StaticClass()->GetClassPathName());
			Filter.bRecursiveClasses = true; // TODO: expose filter settings

			if constexpr (bUseBlueprintSubClass)
			{
				using TBlueprintClassType = std::tuple_element<1, std::tuple<Types...>>::type;
				Filter.TagsAndValues.Add(FBlueprintTags::NativeParentClassPath, FObjectPropertyBase::GetExportPath(TBlueprintClassType::StaticClass()));
			}

			AssetRegistry.GetAssets(Filter, AvailableAssets);

			AvailableAssets.Sort([](const auto& A, const auto& B){ return CompareAssetData(A, B); });
		}

		FORCEINLINE bool FilterAsset(const FAssetData& AssetData) const
		{
			if constexpr (bUseBlueprintSubClass)
			{
				using TBlueprintClassType = std::tuple_element<1, std::tuple<Types...>>::type;
				FAssetDataTagMapSharedView::FFindTagResult NativeClassPathTag = AssetData.TagsAndValues.FindTag(FBlueprintTags::NativeParentClassPath);
				if (!NativeClassPathTag.IsSet() || (!NativeClassPathTag.Equals(FObjectPropertyBase::GetExportPath(TBlueprintClassType::StaticClass()))))
				{
					return false;
				}
				return true;
			}
			else
			{
				return AssetData.GetClass() && AssetData.GetClass()->IsChildOf(TAssetType::StaticClass());
			}
		}

		void OnAssetAdded(const FAssetData& AssetData)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("AssetPicker::AddAsset"), STAT_ImGuiAssetPicker_AddAsset, STATGROUP_ImGui);

			if (FilterAsset(AssetData))
			{
				RevisionId++;

				int32 InsertIndex = Algo::LowerBound(AvailableAssets, AssetData, [](const auto& A, const auto& B){ return CompareAssetData(A, B); });
				bool bExists = (AvailableAssets.IsValidIndex(InsertIndex) && AvailableAssets[InsertIndex] == AssetData);
				if (!bExists && InsertIndex >= 0)
				{
					AvailableAssets.Insert(AssetData, InsertIndex);
				}
			}
		}

		void OnAssetRemoved(const FAssetData& AssetData)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("AssetPicker::RemoveAsset"), STAT_ImGuiAssetPicker_AddAsset, STATGROUP_ImGui);

			if (FilterAsset(AssetData))
			{
				RevisionId++;
				AvailableAssets.Remove(AssetData);
			}
		}

	private:
		TArray<FAssetData> AvailableAssets;
		const FSlateBrush* ClassIconBrush = nullptr;
		uint32 RevisionId = 0;
	};

public:
	bool Draw(const char* Label, TWeakObjectPtr<TAssetType>& InOutSelectedAssetPtr)
	{
		TAssetType* SelectedAsset = InOutSelectedAssetPtr.Get();
		if (Draw(Label, SelectedAsset))
		{
			InOutSelectedAssetPtr = SelectedAsset;
			return true;
		}
		return false;
	}

	bool Draw(const char* Label, TAssetType*& InOutSelectedAsset)
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("AssetPicker::Draw"), STAT_ImGuiAssetPicker_Draw, STATGROUP_ImGui);

		FImGuiNamedWidgetScope WidgetScope{ Label };

		auto& AssetContainer = FAssetContainer::GetInstance();
		const TArray<FAssetData>& AvailableAssets = AssetContainer.GetAvailableAssets();

		FSlateShaderResource* SelectedAssetTexture = nullptr;
		TAssetType* SelectedAsset = InOutSelectedAsset;
		if (SelectedAsset)
		{
			if ((SelectedAsset != LastSelectedAssetPtr.Get()) || (ContainerRevisionId != AssetContainer.GetRevisionId()))
			{
				ContainerRevisionId = AssetContainer.GetRevisionId();
				
				LastSelectedAssetIndex = AvailableAssets.IndexOfByKey(FAssetData(SelectedAsset, FAssetData::ECreationFlags::None));
				
				FilterAvailableAssets();
			}
			SelectedAssetTexture = FImGuiContentBrowserUtils::GetAssetThumbnail(FAssetData(SelectedAsset, FAssetData::ECreationFlags::None));
		}
		if (PackedAssetPathFilter != GetGlobalAssetPathFilter())
		{
			FilterAvailableAssets();
		}

		const float GlobalScale = ImGui::GetIO().FontGlobalScale;

		UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
		const FImGuiImageBindingParams DefaultClassIcon = ImGuiSubsystem->RegisterOneFrameResource(AssetContainer.GetClassIconBrush(), FVector2D(50.) * GlobalScale, 1.f);		
		const FImGuiImageBindingParams UseSelectedAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.UseSelectedAsset"), FVector2D(18.) * GlobalScale, 1.f);
		const FImGuiImageBindingParams BrowseToAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.BrowseToAsset"), FVector2D(18.) * GlobalScale, 1.f);
		const FImGuiImageBindingParams ResetToDefaultIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.ResetToDefault"), FVector2D(18.) * GlobalScale, 1.f);
		const FImGuiImageBindingParams DropDownArrowIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.DropDownArrow"), FVector2D(18.) * GlobalScale, 1.f);
		const FImGuiImageBindingParams ProjectContentIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.ProjectFolder"), FVector2D(16.) * GlobalScale, 1.f);
		const FImGuiImageBindingParams EngineContentIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.EngineFolder"), FVector2D(16.) * GlobalScale, 1.f);
		const FImGuiImageBindingParams PluginContentIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.PluginFolder"), FVector2D(16.) * GlobalScale, 1.f);
		const FImGuiImageBindingParams DeveloperContentIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.DeveloperFolder"), FVector2D(16.) * GlobalScale, 1.f);

		auto Add_AssetThumbnail = [&](FSlateShaderResource* AssetThumbnail, float IconSize, TAssetType* Asset)
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
					FImGuiContentBrowserUtils::OpenEditorForAsset(Asset);
				}
			}
		};

		auto Add_UseSelectedAssetButton = [&](TAssetType*& InOutAsset)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, 0);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFF404040);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFF404040);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));

			if (FImGui::ImageButtonWithTint("UseSelectedAsset", UseSelectedAssetIcon, 0x8FFFFFFF, 0xFFFFFFFF))
			{
				if (auto SelectedAsset = Cast<TAssetType>(FImGuiContentBrowserUtils::GetSelectedAsset(TAssetType::StaticClass())))
				{
					InOutAsset = SelectedAsset;
				}
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Use Selected Asset from Content Browser");
			}

			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(3);
		};

		auto Add_BrowseToAssetButton = [&](TAssetType* InAsset)
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
					FImGuiContentBrowserUtils::SyncContentBrowserToAsset(InAsset);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Browse to '%s' in Content Browser", TCHAR_TO_ANSI(*InAsset->GetName()));
				}
			}

			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(3);
		};

		auto Add_ResetSelectionButton = [&](TAssetType*& InOutAsset)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, 0xBFFFFFFF);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFFFFFFFF);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFFFFFFFF);

			if (FImGui::TransparentImageButton("ResetToDefault", ResetToDefaultIcon))
			{
				InOutAsset = nullptr;
			}
			if (InOutAsset && ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Reset this property to its default value");
			}

			ImGui::PopStyleColor(3);
		};

		auto Add_AssetViewer = [&](TAssetType*& InOutAsset)
		{
			// configuration
			const float AssetViewerWidth = 400.f * GlobalScale;
			const char* AssetViewerPopupName = "AssetViewerPopup";
			const float AssetViewerRowHeight = 36.f * GlobalScale;
			const int32 PreviewTextMaxLength = 32;

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
			const float PopupHeight = FMath::Min(FMath::Max(AvailableSpaceBelow, AvailableSpaceAbove), AssetViewerRowHeight * (AvailableAssets.Num() + 1));
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
					TextFilter.Reset();
					bFilterAvailableAssets = true;
				}
				
				ImGui::BeginGroup();
				{
					if (TextFilter.Draw("Filter", "Search Assets", /*bSetFocus*/!bIsAssetViewerVisible, AssetViewerWidth))
					{
						bFilterAvailableAssets = true;
					}

					if (ImGui::BeginListBox("##AssetList", ImVec2(AssetViewerWidth, PopupHeight - ImGui::GetItemRectSize().y)))
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

								FSlateShaderResource* PreviewTexture = FImGuiContentBrowserUtils::GetAssetThumbnail(AvailableAssets[AssetIndex]);
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
						Clipper.Begin(FilteredAssets.Num());
						if (LastSelectedAssetIndexInFilteredList != INDEX_NONE)
						{
							Clipper.IncludeItemByIndex(LastSelectedAssetIndexInFilteredList);
						}

						int32 RowIndex = 0;
						while (Clipper.Step() && (NewSelectedIndex == INDEX_NONE))
						{
							for (int32 Index = Clipper.DisplayStart; Index < Clipper.DisplayEnd; Index++)
							{
								Add_AssetListEntry(FilteredAssets[Index], RowIndex++);
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
							if (ImGui::IsItemHovered())
							{
								ImGui::SetItemTooltip(ToolTip);
							}

							ImGui::PopStyleVar(2);
							ImGui::PopStyleColor(3);

							return bWasActive != bInOutState;
						};
					
					bFilterAvailableAssets |= AddButton("ToggleProjectContent", FImGuiContentBrowserUtils::bShowProjectConent, ProjectContentIcon, "Show Project Content?");
					bFilterAvailableAssets |= AddButton("ToggleEngineContent", FImGuiContentBrowserUtils::bShowEngineConent, EngineContentIcon, "Show Engine Content?");
					bFilterAvailableAssets |= AddButton("TogglePluginContent", FImGuiContentBrowserUtils::bShowPluginConent, PluginContentIcon, "Show Plugin Content?");
					bFilterAvailableAssets |= AddButton("ToggleDeveloperContent", FImGuiContentBrowserUtils::bShowDeveloperConent, DeveloperContentIcon, "Show Developer Folder Content?");
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
				InOutAsset = Cast<TAssetType>(AvailableAssets[NewSelectedIndex].GetAsset());
			}
		};

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

		const bool bSelectionChanged = (SelectedAsset != InOutSelectedAsset);
		if (bSelectionChanged)
		{
			InOutSelectedAsset = SelectedAsset;
			LastSelectedAssetPtr = SelectedAsset;
			LastSelectedAssetIndex = AvailableAssets.IndexOfByKey(FAssetData(SelectedAsset, FAssetData::ECreationFlags::None));
			LastSelectedAssetIndexInFilteredList = FilteredAssets.IndexOfByKey(LastSelectedAssetIndex);
		}
		return bSelectionChanged;
	}

private:
	FORCEINLINE static uint8 GetGlobalAssetPathFilter()
	{
		uint8 Filter = 0u;
		if (FImGuiContentBrowserUtils::bShowProjectConent)
		{
			Filter |= 1u << 0;
		}
		if (FImGuiContentBrowserUtils::bShowEngineConent)
		{
			Filter |= 1u << 1;
		}
		if (FImGuiContentBrowserUtils::bShowPluginConent)
		{
			Filter |= 1u << 2;
		}
		if (FImGuiContentBrowserUtils::bShowDeveloperConent)
		{
			Filter |= 1u << 3;
		}
		return Filter;
	}

	void FilterAvailableAssets()
	{
		FilteredAssets.Reset();
		LastSelectedAssetIndexInFilteredList = INDEX_NONE;
		PackedAssetPathFilter = GetGlobalAssetPathFilter();

		const auto& AssetContainer = FAssetContainer::GetInstance();
		const TArray<FAssetData>& AvailableAssets = AssetContainer.GetAvailableAssets();
		for (int32 AssetIndex = 0; AssetIndex < AvailableAssets.Num(); ++AssetIndex)
		{
			FNameBuilder AssetName{ AvailableAssets[AssetIndex].AssetName };
			if (!TextFilter.PassFilter(AssetName.ToView()))
			{
				continue;
			}
			if (!FImGuiContentBrowserUtils::FilterAsset(AvailableAssets[AssetIndex].PackagePath))
			{
				continue;
			}

			if (LastSelectedAssetIndex == AssetIndex)
			{
				LastSelectedAssetIndexInFilteredList = FilteredAssets.Num();
			}
			FilteredAssets.Add(AssetIndex);
		}
	}

private:
	FImGuiTextFilter<64> TextFilter = {};
	TWeakObjectPtr<TAssetType> LastSelectedAssetPtr = nullptr;

	TArray<int32> FilteredAssets;
	uint8 PackedAssetPathFilter = 0;
	bool bIsAssetViewerVisible = false;
	uint32 ContainerRevisionId = UINT32_MAX;
	int32 LastSelectedAssetIndex = INDEX_NONE;
	int32 LastSelectedAssetIndexInFilteredList = INDEX_NONE;
};

#undef TCHAR_TO_ANSI_PATH

#endif //#if WITH_IMGUI
