// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#if WITH_IMGUI

#include "Editor.h"
#include "AssetThumbnail.h"
#include "ClassIconFinder.h"
#include "ImGuiCommonWidgets.h"
#include "EditorUtilityLibrary.h"
#include "Styling/SlateIconFinder.h"
#include "AssetRegistry/AssetData.h"
#include "Blueprint/BlueprintSupport.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ThumbnailRendering/ThumbnailManager.h"

template <typename... Types>
class FImGuiAssetPicker : FNoncopyable
{
	// validation for generic asset types
	static_assert(sizeof...(Types) > 0, "Asset type missing, expected FImGuiAssetPicker<UAssetType>");
	using TAssetType = std::tuple_element<0, std::tuple<Types...>>::type;
	
	// blueprints support optional SubClass argument
	static constexpr bool bUsedWithBlueprintAsset = std::is_same<TAssetType, UBlueprint>::value;
	static constexpr bool bUseBlueprintSubClass = bUsedWithBlueprintAsset && (sizeof...(Types) > 1);
	
public:
	FImGuiAssetPicker() = default;

	~FImGuiAssetPicker()
	{
		if (UObjectInitialized())
		{
			IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			AssetRegistry.OnAssetAdded().RemoveAll(this);
			AssetRegistry.OnAssetRemoved().RemoveAll(this);
		}
	}

	bool Draw(const char* Label, TWeakObjectPtr<TAssetType>& SelectedAssetPtr)
	{
		Initialize();

		FImGuiNamedWidgetScope WidgetScope{ Label };

		FSlateShaderResource* SelectedAssetTexture = nullptr;
		TAssetType* SelectedAsset = SelectedAssetPtr.Get();
		if (SelectedAsset)
		{
			if (SelectedAsset != SelectedAssetThumbnail->GetAsset() || bAssetListChanged)
			{
				bAssetListChanged = false;

				SelectedAssetThumbnail->SetAsset(SelectedAsset);
				LastSelectedAssetIndex = AvailableAssets.IndexOfByKey(SelectedAsset);
			}
			SelectedAssetTexture = SelectedAssetThumbnail->GetViewportRenderTargetTexture();
		}

		const float GlobalScale = ImGui::GetIO().FontGlobalScale;

		UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
		const FImGuiImageBindingParams UseSelectedAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_FNAME("Icons.Use"), FVector2D(18.) * GlobalScale, 1.f);
		const FImGuiImageBindingParams BrowseToAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_FNAME("Icons.BrowseContent"), FVector2D(18.) * GlobalScale, 1.f);
		const FImGuiImageBindingParams ResetToDefaultIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_FNAME("PropertyWindow.DiffersFromDefault"), FVector2D(18.) * GlobalScale, 1.f);
		const FImGuiImageBindingParams ComboboxDownArrowIcon = ImGuiSubsystem->RegisterOneFrameResource(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>(IMGUI_FNAME("ComboButton")).DownArrowImage, FVector2D(18.) * GlobalScale, 1.f);
		const FImGuiImageBindingParams DefaultClassIcon = ImGuiSubsystem->RegisterOneFrameResource(ClassIconBrush, FVector2D(50.)* GlobalScale, 1.f);

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

				ImGui::SetTooltip("%s", TCHAR_TO_ANSI(*Asset->GetPathName()));

				if (Asset && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					GEditor->EditObject(Asset);
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

			if (ImGui::ImageButtonWithTint("UseSelectedAsset", UseSelectedAssetIcon.Id, UseSelectedAssetIcon.Size, UseSelectedAssetIcon.UV0, UseSelectedAssetIcon.UV1, 0x8FFFFFFF, 0xFFFFFFFF))
			{
				for (auto Asset : UEditorUtilityLibrary::GetSelectedAssets())
				{
					if (Asset->IsA(TAssetType::StaticClass()) && (Asset != InOutAsset))
					{
						InOutAsset = Cast<TAssetType>(Asset);
						break;
					}
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
				ImGui::ImageButtonWithTint("BrowseToAsset", BrowseToAssetIcon.Id, BrowseToAssetIcon.Size, BrowseToAssetIcon.UV0, BrowseToAssetIcon.UV1, 0x8FFFFFFF, 0xFFFFFFFF);
				ImGui::EndDisabled();
			}
			else
			{
				if (ImGui::ImageButtonWithTint("BrowseToAsset", BrowseToAssetIcon.Id, BrowseToAssetIcon.Size, BrowseToAssetIcon.UV0, BrowseToAssetIcon.UV1, 0x8FFFFFFF, 0xFFFFFFFF))
				{
					TArray<UObject*> Objects;
					Objects.Add(InAsset);
					GEditor->SyncBrowserToObjects(Objects);
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

			if (ImGui::TransparentImageButton("ResetToDefault", ResetToDefaultIcon.Id, ResetToDefaultIcon.Size, ResetToDefaultIcon.UV0, ResetToDefaultIcon.UV1))
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
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() - ComboboxDownArrowIcon.Size.x * 2.f);
			ImGui::Image(ComboboxDownArrowIcon.Id, ComboboxDownArrowIcon.Size, ComboboxDownArrowIcon.UV0, ComboboxDownArrowIcon.UV1);
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
				// reset filter text and set focus when asset viewer is triggered
				if (!bIsAssetViewerVisible)
				{
					TextFilter.Reset();
				}
				TextFilter.Draw("Filter", "Search Assets", /*bSetFocus*/!bIsAssetViewerVisible, AssetViewerWidth);

				if (ImGui::BeginListBox("###AssetList", ImVec2(AssetViewerWidth, PopupHeight - ImGui::GetItemRectSize().y)))
				{
					auto Add_AssetListEntry = [&](int32 AssetIndex)
					{
						const FString AssetName = AvailableAssets[AssetIndex].AssetName.ToString();
						const bool bWasSelected = (AssetIndex == LastSelectedAssetIndex);
						{
							ImGui::PushID(GetTypeHash(AvailableAssets[AssetIndex].AssetName));
							if (ImGui::Selectable("", bWasSelected, ImGuiSelectableFlags_None, ImVec2(0, AssetViewerRowHeight)))
							{
								NewSelectedIndex = AssetIndex;
							}
							if (!bWasSelected)
							{
								ImGui::GetWindowDrawList()->AddRectFilled(
									ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
									ImGui::GetColorU32((AssetIndex & 0x1) ? ImGuiCol_TableRowBgAlt : ImGuiCol_TableRowBg));
							}
							ImGui::PopID();
						}
						ImGui::SameLine();

						if (!AssetThumnailIcons.Find(AvailableAssets[AssetIndex]))
						{
							AssetThumnailIcons.Add(AvailableAssets[AssetIndex]) = MakeShareable(new FAssetThumbnail(AvailableAssets[AssetIndex], 50.f, 50.f, UThumbnailManager::Get().GetSharedThumbnailPool()));
						}

						FSlateShaderResource* PreviewTexture = AssetThumnailIcons[AvailableAssets[AssetIndex]]->GetViewportRenderTargetTexture();
						Add_AssetThumbnail(PreviewTexture, AssetViewerRowHeight, nullptr);

						ImGui::SameLine();
						{
							ImGui::BeginGroup();
							ImGui::TextUnformatted(TCHAR_TO_ANSI(*AssetName));
							ImGui::TextUnformatted(TCHAR_TO_ANSI(*AvailableAssets[AssetIndex].PackagePath.ToString()));
							ImGui::EndGroup();
						}

						// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
						if (bWasSelected)
						{
							// scroll to item when popup is opened
							if (!bIsAssetViewerVisible)
							{
								ImGui::ScrollToItem();
							}
							ImGui::SetItemDefaultFocus();
						}
					};

					if (!TextFilter.IsActive()) // TODO: clipping doesn't work with JIT filtering
					{
						ImGuiListClipper Clipper;
						Clipper.Begin(AvailableAssets.Num());
						if (LastSelectedAssetIndex != INDEX_NONE)
						{
							Clipper.IncludeItemByIndex(LastSelectedAssetIndex);
						}

						while (Clipper.Step() && (NewSelectedIndex == INDEX_NONE))
						{
							for (int32 AssetIndex = Clipper.DisplayStart; AssetIndex < Clipper.DisplayEnd; AssetIndex++)
							{
								Add_AssetListEntry(AssetIndex);
							}
						}
					}
					else
					{
						for (int32 AssetIndex = 0; AssetIndex < AvailableAssets.Num(); ++AssetIndex)
						{
							const FString AssetName = AvailableAssets[AssetIndex].AssetName.ToString();
							if (TextFilter.PassFilter(AssetName))
							{
								Add_AssetListEntry(AssetIndex);
							}
						}
					}

					// NOTE: ImGui::BeginListBox can return false on first attempt (rect not visible in the popup window)
					// So this flag is set there, instead of using `ImGui::IsPopupOpen(AssetViewerPopupName)`
					bIsAssetViewerVisible = true;

					ImGui::EndListBox();
				}

				if (NewSelectedIndex != INDEX_NONE)
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			if (NewSelectedIndex != INDEX_NONE)
			{
				InOutAsset = Cast<TAssetType>(AvailableAssets[NewSelectedIndex].GetAsset());
			}
		};

		if (strstr(Label, "###") == nullptr)
		{
			ImGui::BeginGroup();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFontSize() * 1.5f);
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
				Add_UseSelectedAssetButton(SelectedAsset); ImGui::SameLine(); Add_BrowseToAssetButton(SelectedAsset);
			}
			ImGui::EndGroup();
		}
		ImGui::EndGroup();
		const ImVec2 GroupSize = ImGui::GetItemRectSize();

		// reset icon
		if (SelectedAsset)
		{
			ImGui::SameLine(); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + GroupSize.y / 2.f - ResetToDefaultIcon.Size.y);
			Add_ResetSelectionButton(SelectedAsset);
		}

		const bool bSelectionChanged = (SelectedAsset != SelectedAssetPtr.Get());
		if (bSelectionChanged)
		{
			SelectedAssetPtr = SelectedAsset;
			LastSelectedAssetIndex = AvailableAssets.IndexOfByKey(SelectedAsset);
		}
		return bSelectionChanged;
	}

private:
	FORCEINLINE void GatherAssets(IAssetRegistry& AssetRegistry)
	{
		FARFilter Filter;
		Filter.ClassPaths.Add(TAssetType::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true; // TODO: expose filter settings
		
		if constexpr (bUseBlueprintSubClass)
		{
			using TBlueprintClassType = std::tuple_element<1, std::tuple<Types...>>::type;
			Filter.TagsAndValues.Add(FBlueprintTags::NativeParentClassPath, FObjectPropertyBase::GetExportPath(TBlueprintClassType::StaticClass()));
		}

		AssetRegistry.GetAssets(Filter, AvailableAssets);
	}

	FORCEINLINE void Initialize()
	{
		if (bInitialized)
		{
			return;
		}
		bInitialized = true;

		ClassIconBrush = FClassIconFinder::FindThumbnailForClass(TAssetType::StaticClass(), NAME_None);

		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		GatherAssets(AssetRegistry);
		AssetRegistry.OnAssetAdded().AddRaw(this, &FImGuiAssetPicker::OnAssetAdded);
		AssetRegistry.OnAssetRemoved().AddRaw(this, &FImGuiAssetPicker::OnAssetRemoved);
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
		if (FilterAsset(AssetData))
		{
			bAssetListChanged = true;
			AvailableAssets.AddUnique(AssetData);
		}
	}

	void OnAssetRemoved(const FAssetData& AssetData)
	{
		if (FilterAsset(AssetData))
		{
			bAssetListChanged = true;
			AvailableAssets.Remove(AssetData);
		}
	}

private:
	// TODO: Move resources to a common type so that they can be shared
	TArray<FAssetData> AvailableAssets;
	TMap<FAssetData, TSharedPtr<FAssetThumbnail>> AssetThumnailIcons;

	const FSlateBrush* ClassIconBrush = nullptr;
	TSharedRef<FAssetThumbnail> SelectedAssetThumbnail = MakeShared<FAssetThumbnail>(nullptr, 50, 50, UThumbnailManager::Get().GetSharedThumbnailPool());

	FImGuiTextFilter<128> TextFilter = {};

	bool bInitialized = false;
	bool bAssetListChanged = false;
	bool bIsAssetViewerVisible = false;
	int32 LastSelectedAssetIndex = INDEX_NONE;
};

#endif //#if WITH_IMGUI
