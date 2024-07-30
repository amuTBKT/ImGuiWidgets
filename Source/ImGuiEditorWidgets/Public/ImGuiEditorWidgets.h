// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "ImGuiPluginTypes.h"

#if WITH_IMGUI

#include "ImGuiCommonWidgets.h"

#include "Editor.h"
#include "AssetThumbnail.h"
#include "ClassIconFinder.h"
#include "EditorUtilityLibrary.h"
#include "Styling/SlateIconFinder.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ThumbnailRendering/ThumbnailManager.h"

UE_DISABLE_OPTIMIZATION

template <typename TAssetType>
struct FImGuiAssetPicker : FNoncopyable
{
	FImGuiAssetPicker() = default;

	~FImGuiAssetPicker()
	{
		if (UObjectInitialized())
		{
			IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			AssetRegistry.OnAssetAdded().RemoveAll(this);
			AssetRegistry.OnAssetAdded().RemoveAll(this);
		}
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
		AssetRegistry.GetAssetsByClass(TAssetType::StaticClass()->GetClassPathName(), AvailableAssets);
		AssetRegistry.OnAssetAdded().AddRaw(this, &FImGuiAssetPicker::OnAssetAdded);
		AssetRegistry.OnAssetAdded().AddRaw(this, &FImGuiAssetPicker::OnAssetRemoved);
	}

	void OnAssetAdded(const FAssetData& AssetData)
	{
		if (AssetData.GetClass() && AssetData.GetClass()->IsChildOf(TAssetType::StaticClass()))
		{
			bAssetListChanged = true;
			AvailableAssets.Add(AssetData);
		}
	}

	void OnAssetRemoved(const FAssetData& AssetData)
	{
		bAssetListChanged = true;
		AvailableAssets.Remove(AssetData);
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
		const FImGuiImageBindingParams UseSelectedAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(FAppStyle::GetBrush(FName(TEXT("Icons.Use"))), FVector2D(18.) * GlobalScale, 1.f);
		const FImGuiImageBindingParams BrowseToAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(FAppStyle::GetBrush(FName(TEXT("Icons.BrowseContent"))), FVector2D(18.) * GlobalScale, 1.f);
		const FImGuiImageBindingParams ResetToDefaultIcon = ImGuiSubsystem->RegisterOneFrameResource(FAppStyle::GetBrush(FName(TEXT("PropertyWindow.DiffersFromDefault"))), FVector2D(18.) * GlobalScale, 1.f);
		const FImGuiImageBindingParams ComboboxDownArrowIcon = ImGuiSubsystem->RegisterOneFrameResource(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("ComboButton").DownArrowImage, FVector2D(18.) * GlobalScale, 1.f);
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
				DrawList->AddRect(p0, p1, ImColor(ImVec4(0.26f, 0.59f, 0.98f, 0.67f)), 0.f, ImDrawFlags_None, 1.f);

				ImGui::SetTooltip("%s", TCHAR_TO_ANSI(*Asset->GetPathName()));

				if (Asset && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					GEditor->EditObject(Asset);
				}
			}
		};

		auto Add_UseSelectedAssetButton = [&](TAssetType*& InOutAsset)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_WindowBg));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImGuiCol_WindowBg));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetColorU32(ImGuiCol_WindowBg));

			if (ImGui::ImageButton("UseSelectedAsset", UseSelectedAssetIcon.Id, UseSelectedAssetIcon.Size, UseSelectedAssetIcon.UV0, UseSelectedAssetIcon.UV1, ImVec4(0, 0, 0, 0), ImVec4(UseSelectedAssetIconTint, UseSelectedAssetIconTint, UseSelectedAssetIconTint, 1.f)))
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
			UseSelectedAssetIconTint = ImGui::IsItemHovered() ? 1.f : 0.75f;
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Use Selected Asset from Content Browser");
			}

			ImGui::PopStyleColor(3);
		};

		auto Add_BrowseToAssetButton = [&](TAssetType* InAsset)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_WindowBg));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImGuiCol_WindowBg));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetColorU32(ImGuiCol_WindowBg));

			if (!InAsset)
			{
				ImGui::BeginDisabled();
				ImGui::ImageButton("BrowseToAsset", BrowseToAssetIcon.Id, BrowseToAssetIcon.Size, BrowseToAssetIcon.UV0, BrowseToAssetIcon.UV1, ImVec4(0, 0, 0, 0), ImVec4(BrowseIconTint, BrowseIconTint, BrowseIconTint, 1.f));
				ImGui::EndDisabled();
			}
			else
			{
				if (ImGui::ImageButton("BrowseToAsset", BrowseToAssetIcon.Id, BrowseToAssetIcon.Size, BrowseToAssetIcon.UV0, BrowseToAssetIcon.UV1, ImVec4(0, 0, 0, 0), ImVec4(BrowseIconTint, BrowseIconTint, BrowseIconTint, 1.f)))
				{
					TArray<UObject*> Objects;
					Objects.Add(InAsset);
					GEditor->SyncBrowserToObjects(Objects);
				}
				BrowseIconTint = ImGui::IsItemHovered() ? 1.f : 0.75f;
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Browse to '%s' in Content Browser", TCHAR_TO_ANSI(*InAsset->GetName()));
				}
			}

			ImGui::PopStyleColor(3);
		};

		auto Add_ResetSelectionButton = [&](TAssetType*& InOutAsset)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_WindowBg));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImGuiCol_WindowBg));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetColorU32(ImGuiCol_WindowBg));

			if (ImGui::ImageButton("ResetToDefault", ResetToDefaultIcon.Id, ResetToDefaultIcon.Size, ResetToDefaultIcon.UV0, ResetToDefaultIcon.UV1, ImVec4(0, 0, 0, 0), ImVec4(ResetIconTint, ResetIconTint, ResetIconTint, 1.f)))
			{
				InOutAsset = nullptr;
			}
			ResetIconTint = ImGui::IsItemHovered() ? 1.f : 0.5f;
			if (InOutAsset && ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Reset this property to its default value");
			}

			ImGui::PopStyleColor(3);
		};

		auto Add_AssetViewer = [&](TAssetType*& InOutAsset)
		{
			int32 NewSelectedIndex = INDEX_NONE;
			const FString PreviewText = InOutAsset ? InOutAsset->GetName() : FString(TEXT("None"));

			const float AssetViewerWidth = 400.f * GlobalScale;
			const char* AssetViewerPopupName = "AssetViewerPopup";
			const float AssetViewerPopupPosX = ImGui::GetCursorPosX();
			const float AssetViewerRowHeight = 36.f * GlobalScale;
			const bool bWasAssetViewerVisible = ImGui::IsPopupOpen(AssetViewerPopupName);

			ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.f, 0.5f));
			if (ImGui::Button(TCHAR_TO_ANSI(*PreviewText), ImVec2(256.f, 0.f)))
			{
				ImGui::OpenPopup(AssetViewerPopupName);
			}
			const float ComboBoxHeight = ImGui::GetItemRectSize().y;
			ImGui::SameLine();
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() - ComboboxDownArrowIcon.Size.x * 2.f);
			ImGui::Image(ComboboxDownArrowIcon.Id, ComboboxDownArrowIcon.Size, ComboboxDownArrowIcon.UV0, ComboboxDownArrowIcon.UV1);
			ImGui::PopStyleVar(1);

			const float AvailableSpaceAbove = ImGui::GetCursorScreenPos().y * 0.65f;			
			const float AvailableSpaceBelow = (ImGui::GetWindowHeight() - ImGui::GetCursorScreenPos().y) * 0.75f;
			const float PopupHeight = std::min(std::max(AvailableSpaceBelow, AvailableSpaceAbove), AssetViewerRowHeight * (AvailableAssets.Num() + 1));
			if (AvailableSpaceBelow > AvailableSpaceAbove)
			{
				ImGui::SetNextWindowPos(ImVec2(AssetViewerPopupPosX, ImGui::GetCursorScreenPos().y), ImGuiCond_Always, ImVec2(0.f, 0.f));
			}
			else
			{
				const float OffsetY = ComboBoxHeight + ImGui::GetStyle().ItemSpacing.y * 2.f * GlobalScale;
				ImGui::SetNextWindowPos(ImVec2(AssetViewerPopupPosX, ImGui::GetCursorScreenPos().y - OffsetY), ImGuiCond_Always, ImVec2(0.f, 1.f));
			}
			if (ImGui::BeginPopup(AssetViewerPopupName, ImGuiWindowFlags_NoScrollbar))
			{
				// reset filter text and set focus when asset viewer is triggered
				if (!bWasAssetViewerVisible)
				{
					TextFilter.Reset();
				}
				TextFilter.Draw("Filter", "Search Assets", /*bSetFocus*/!bWasAssetViewerVisible, AssetViewerWidth);

				if (ImGui::BeginListBox("###AssetList", ImVec2(AssetViewerWidth, PopupHeight - ImGui::GetItemRectSize().y)))
				{
					auto DisplayAsset = [&](int32 AssetIndex)
					{						
						const FString AssetName = AvailableAssets[AssetIndex].AssetName.ToString();
						const bool bWasSelected = (AssetIndex == LastSelectedAssetIndex);
						{
							ImGui::PushID(GetTypeHash(AvailableAssets[AssetIndex]));
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
								DisplayAsset(AssetIndex);
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
								DisplayAsset(AssetIndex);
							}
						}
					}

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
	// TODO: Move resources to a common type so that they can be shared
	TArray<FAssetData> AvailableAssets;
	TMap<FAssetData, TSharedPtr<FAssetThumbnail>> AssetThumnailIcons;

	const FSlateBrush* ClassIconBrush = nullptr;
	TSharedRef<FAssetThumbnail> SelectedAssetThumbnail = MakeShared<FAssetThumbnail>(nullptr, 50, 50, UThumbnailManager::Get().GetSharedThumbnailPool());

	FImGuiTextFilter<128> TextFilter = {};

	bool bInitialized = false;
	bool bAssetListChanged = false;
	int32 LastSelectedAssetIndex = INDEX_NONE;

	float ResetIconTint = 0.5f;
	float BrowseIconTint = 0.75f;
	float UseSelectedAssetIconTint = 0.75f;
};

UE_ENABLE_OPTIMIZATION

#endif //#if WITH_IMGUI
