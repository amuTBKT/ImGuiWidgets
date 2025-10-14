// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

#include "Misc/Build.h"

#if STATS

#include "ImGuiWidgets.h"
#include "GPUProfiler.h"
#include "Engine/Engine.h"
#include "ImGuiSubsystem.h"
#include "Stats/StatsData.h"
#include "ImGuiStaticWidget.h"
#include "Misc/ConfigCacheIni.h"

#if WITH_EDITOR
#include "Editor.h"
#include "TimerManager.h"
#include "AssetRegistry/AssetData.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#endif //#if WITH_EDITOR

namespace ImGuiStatsVizualizer
{
	struct FStatGroupData
	{
		FAnsiString DisplayName; // used for ImGui widgets
		FString NameForCommand;	 // used for `stat` command
		FName Name;				 // "STATGROUP_XXXX"
		bool bIsActive = false;

		bool operator==(const FName& InName) const
		{
			return Name == InName;
		}
	};
	static TArray<FStatGroupData> StatGroups;

	static FImGuiTextFilter StatFilter = FImGuiTextFilter::MakeWidget(64u);
	static FImGuiImageBindingParams EditAssetIcon;
	static FImGuiImageBindingParams BrowseAssetIcon;

	FORCEINLINE static const char* GetStatDescription(const FComplexStatMessage& Stat)
	{
		static TMap<FName, FAnsiString> CachedDescriptions;

		const FName RawStatName = Stat.NameAndInfo.GetRawName();
		const FAnsiString* CachedDescription = CachedDescriptions.Find(RawStatName);
		if (!CachedDescription)
		{
			const FString StatDesc = Stat.GetDescription();
			const FString StatDisplay = StatDesc.Len() == 0 ? Stat.GetShortName().GetPlainNameString() : StatDesc;

			CachedDescription = &CachedDescriptions.Add(RawStatName, FAnsiString(*StatDisplay));
		}
		return *(*CachedDescription);
	}

	FORCEINLINE static bool AddSelectableRow(const char* Label, FName ItemName)
	{
		static TSet<FName> SelectedItems;

		const bool bWasSelected = SelectedItems.Contains(ItemName);
		const bool bIsSelected = ImGui::Selectable(Label, bWasSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);	
		if (bIsSelected)
		{
			const int32 NumItemsSelected = SelectedItems.Num();
			if (!ImGui::GetIO().KeyCtrl)
			{
				SelectedItems.Reset();
			}

			if (bWasSelected && NumItemsSelected == 1)
			{
				SelectedItems.Remove(ItemName);
			}
			else
			{
				SelectedItems.Add(ItemName);
			}
		}
		return bIsSelected;
	}

	// headings
	FORCEINLINE static int32 GetCycleStatsColumnCount()
	{
		return 7;
	}
	FORCEINLINE static int32 GetMemoryStatsColumnCount()
	{
		return 5;
	}
	FORCEINLINE static int32 GetCounterStatsColumnCount()
	{
		return 4;
	}

	FORCEINLINE static void RenderGroupedHeadings(const bool bIsHierarchy)
	{
		// The heading looks like:
		// Stat [32chars]	CallCount [8chars]	IncAvg [8chars]	IncMax [8chars]	ExcAvg [8chars]	ExcMax [8chars]

		static const char* CaptionFlat = "Cycle counters (flat)";
		static const char* CaptionHier = "Cycle counters (hierarchy)";

		ImGui::TableSetupColumn(bIsHierarchy ? CaptionHier : CaptionFlat, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoHide);
		ImGui::TableSetupColumn("CallCount", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("InclusiveAvg", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("InclusiveMax", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("ExclusiveAvg", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("ExclusiveMax", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible
		ImGui::TableHeadersRow();
	}

	FORCEINLINE static void RenderMemoryHeadings()
	{
		// The heading looks like:
		// Stat [32chars]	MemUsed [8chars]	PhysMem [8chars]

		ImGui::TableSetupColumn("Memory Counters", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoHide);
		ImGui::TableSetupColumn("UsedMax", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Mem%", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("MemPool", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Pool Capacity", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible
		ImGui::TableHeadersRow();
	}

	FORCEINLINE static void RenderCounterHeadings()
	{
		// The heading looks like:
		// Stat [32chars]	Value [8chars]	Average [8chars]

		ImGui::TableSetupColumn("Counters", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoHide);
		ImGui::TableSetupColumn("Average", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible
		ImGui::TableHeadersRow();
	}

	FORCEINLINE static void RenderGpuStatHeadings()
	{
		// The heading looks like:
		// Stat [32chars]	Value [8chars]	Average [8chars]

		ImGui::TableSetupColumn("Counters", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoHide);
		ImGui::TableSetupColumn("Average", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible
		ImGui::TableHeadersRow();
	}

	// body
	static void RenderCycle(const FComplexStatMessage& Item, const bool bStackStat)
	{
		check(Item.NameAndInfo.GetFlag(EStatMetaFlags::IsCycle));

		const bool bIsInitialized = Item.NameAndInfo.GetField<EStatDataType>() == EStatDataType::ST_int64;

		//if (bIsInitialized) -> no need to worry about checking, default times will be 0ms (selectively disabling section breaks ImGui::Clipper)
		{        
			const char* StatDescription = GetStatDescription(Item);

			const FName RawStatName = Item.NameAndInfo.GetRawName();

			if (!StatFilter.PassFilter(StatDescription))
			{
				return;
			}

			ImGui::TableNextRow(ImGuiTableRowFlags_None);
		
			ImGui::TableSetColumnIndex(0);
			{
				AddSelectableRow(StatDescription, RawStatName);
			}

			if (bStackStat)
			{
				ImGui::TableSetColumnIndex(1);
				if (Item.NameAndInfo.GetFlag(EStatMetaFlags::IsPackedCCAndDuration))
				{
					ImGui::Text("%u", Item.GetValue_CallCount(EComplexStatField::IncAve));
				}
				else
				{
					//ImGui::Text(""); leave the column empty?
				}

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%1.2f ms", FPlatformTime::ToMilliseconds(Item.GetValue_Duration(EComplexStatField::IncAve)));

				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%1.2f ms", FPlatformTime::ToMilliseconds(Item.GetValue_Duration(EComplexStatField::IncMax)));

				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%1.2f ms", FPlatformTime::ToMilliseconds(Item.GetValue_Duration(EComplexStatField::ExcAve)));

				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%1.2f ms", FPlatformTime::ToMilliseconds(Item.GetValue_Duration(EComplexStatField::ExcMax)));
			}
			else
			{
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%1.2f ms", FPlatformTime::ToMilliseconds(Item.GetValue_Duration(EComplexStatField::IncAve)));

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%1.2f ms", FPlatformTime::ToMilliseconds(Item.GetValue_Duration(EComplexStatField::IncMax)));

				ImGui::TableSetColumnIndex(3);
				//ImGui::Text(""); leave the column empty?
			}

			if (bStackStat)
			{
				ImGui::TableSetColumnIndex(6);

#if WITH_EDITOR
				static TMap<FName, TWeakObjectPtr<UObject>> LinkedAsset;
									
				if (!LinkedAsset.Find(RawStatName))
				{
					FString AssetPath = Item.GetShortName().GetPlainNameString();
					AssetPath.RemoveFromEnd(" [GT_CNC]", ESearchCase::IgnoreCase);
					AssetPath.RemoveFromEnd(" [RT_CNC]", ESearchCase::IgnoreCase);
					AssetPath.RemoveFromEnd(" [RT]", ESearchCase::IgnoreCase);
					AssetPath.RemoveFromEnd(" [GT]", ESearchCase::IgnoreCase);

					FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry"));
					IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
					FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
					if (AssetData.IsValid())
					{
						LinkedAsset.Add(RawStatName) = AssetData.GetAsset();
					}
					else
					{
						LinkedAsset.Add(RawStatName) = nullptr;
					}
				}

				if (UObject* Asset = LinkedAsset.Find(RawStatName)->Get())
				{
					FImGuiNamedWidgetScope Scope{ GetTypeHash(StatDescription) };

					ImGui::PushStyleColor(ImGuiCol_Button, 0);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFF404040);
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFF404040);
					ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2);
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1, 1));

					if (FImGui::ImageButtonWithTint("BrowseToAsset", BrowseAssetIcon, 0x8FFFFFFF, 0xFFFFFFFF))
					{
						TArray<UObject*> Objects = { Asset };
						GEditor->SyncBrowserToObjects(Objects);
					}
					ImGui::SetItemTooltip("Browse to asset");
					
					ImGui::SameLine();

					if (FImGui::ImageButtonWithTint("EditAsset", EditAssetIcon, 0x8FFFFFFF, 0xFFFFFFFF))
					{
						// NOTE: executing this immediately causes crashes when quering stats next frame
						FFunctionGraphTask::CreateAndDispatchWhenReady(
							[AssetPtr = TWeakObjectPtr<UObject>(Asset)]()
							{
								if (!AssetPtr.IsValid())
								{
									return;
								}

								UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
								if (AssetEditorSubsystem)
								{
									AssetEditorSubsystem->OpenEditorForAsset(AssetPtr.Get());
								}
							}, TStatId(), NULL, ENamedThreads::GameThread);
					}
					ImGui::SetItemTooltip("Edit asset");

					ImGui::PopStyleVar(2);
					ImGui::PopStyleColor(3);
				}
#else
				//ImGui::Text("");
#endif //#if WITH_EDITOR
			}
		}
	}

	static void RenderMemoryCounter(const FGameThreadStatsData& ViewData, const FComplexStatMessage& Item)
	{
		const char* StatDescription = GetStatDescription(Item);

		if (!StatFilter.PassFilter(StatDescription))
		{
			return;
		}

		ImGui::TableNextRow(ImGuiTableRowFlags_None);

		FPlatformMemory::EMemoryCounterRegion Region = FPlatformMemory::EMemoryCounterRegion(Item.NameAndInfo.GetField<EMemoryRegion>());
		// At this moment we only have memory stats that are marked as non frame stats, so can't be cleared every frame.
		//const bool bDisplayAll = All.NameAndInfo.GetFlag(EStatMetaFlags::ShouldClearEveryFrame);
		const float MaxMemUsed = Item.GetValue_double(EComplexStatField::IncMax);

		// Draw the label
		ImGui::TableSetColumnIndex(0);
		{
			AddSelectableRow(StatDescription, Item.NameAndInfo.GetRawName());
		}

		// always use MB for easier comparisons
		const bool bAutoType = false;

		// Now append the max value of the stat
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%.2f MB", float(MaxMemUsed / (1024.0 * 1024.0)));
	
		ImGui::TableSetColumnIndex(2);
		if (ViewData.PoolCapacity.Contains(Region))
		{
			ImGui::Text("%.0f%%", float(100.0 * (double)MaxMemUsed / double(ViewData.PoolCapacity[Region])));
		}
		else
		{
			//ImGui::Text(""); leave the column empty?
		}
	
		ImGui::TableSetColumnIndex(3);
		if (ViewData.PoolAbbreviation.Contains(Region))
		{
			ImGui::TextUnformatted(TCHAR_TO_ANSI(*ViewData.PoolAbbreviation[Region]));
		}
		else
		{
			//ImGui::Text(""); leave the column empty?
		}
	
		ImGui::TableSetColumnIndex(4);
		if (ViewData.PoolCapacity.Contains(Region))
		{
			ImGui::Text("%.2f MB", float(double(ViewData.PoolCapacity[Region]) / (1024. * 1024.)));
		}
		else
		{
			//ImGui::Text(""); leave the column empty?
		}
	}

	static void RenderCounter(const FGameThreadStatsData& ViewData, const FComplexStatMessage& Item)
	{
		// If this is a cycle, render it as a cycle. This is a special case for manually set cycle counters.
		const bool bIsCycle = Item.NameAndInfo.GetFlag(EStatMetaFlags::IsCycle);
		if (bIsCycle)
		{
			RenderCycle(Item, false);
			return;
		}

		const char* StatDescription = GetStatDescription(Item);

		if (!StatFilter.PassFilter(StatDescription))
		{
			return;
		}

		ImGui::TableNextRow(ImGuiTableRowFlags_None);

		const bool bDisplayAll = Item.NameAndInfo.GetFlag(EStatMetaFlags::ShouldClearEveryFrame);
	
		ImGui::TableSetColumnIndex(0);
		{
			AddSelectableRow(StatDescription, Item.NameAndInfo.GetRawName());
		}

		ImGui::TableSetColumnIndex(1);
		if (bDisplayAll)
		{
			// Append the average.
			if (Item.NameAndInfo.GetField<EStatDataType>() == EStatDataType::ST_double)
			{
				ImGui::Text("%0.2f", Item.GetValue_double(EComplexStatField::IncAve));
			}
			else if (Item.NameAndInfo.GetField<EStatDataType>() == EStatDataType::ST_int64)
			{
				ImGui::Text("%lld", Item.GetValue_int64(EComplexStatField::IncAve));
			}
			else
			{
				//ImGui::Text(""); leave the column empty?
			}
		}
		else
		{
			//ImGui::Text(""); leave the column empty?
		}
	
		// Append the maximum.
		ImGui::TableSetColumnIndex(2);
		if (Item.NameAndInfo.GetField<EStatDataType>() == EStatDataType::ST_double)
		{
			ImGui::Text("%0.2f", Item.GetValue_double(EComplexStatField::IncMax));
		}
		else if (Item.NameAndInfo.GetField<EStatDataType>() == EStatDataType::ST_int64)
		{
			ImGui::Text("%lld", Item.GetValue_int64(EComplexStatField::IncMax));
		}
		else
		{
			//ImGui::Text(""); leave the column empty?
		}

		// Append the minimum.
		ImGui::TableSetColumnIndex(3);
		if (Item.NameAndInfo.GetField<EStatDataType>() == EStatDataType::ST_double)
		{
			ImGui::Text("%0.2f", Item.GetValue_double(EComplexStatField::IncMin));
		}
		else if (Item.NameAndInfo.GetField<EStatDataType>() == EStatDataType::ST_int64)
		{
			ImGui::Text("%lld", Item.GetValue_int64(EComplexStatField::IncMin));
		}
		else
		{
			//ImGui::Text(""); leave the column empty?
		}
	}

	template <typename T>
	static int32 RenderArrayOfStats(const TArray<FComplexStatMessage>& Aggregates, const FGameThreadStatsData& ViewData, const T& FunctionToCall)
	{
		int32 RowIndex = 0;

		// Render all counters.
		if (!StatFilter.IsActive()) // TODO: clipping doesn't work with JIT filtering
		{        
			ImGuiListClipper clipper;
			clipper.Begin(Aggregates.Num());
			while (clipper.Step())
			{
				for (RowIndex = clipper.DisplayStart; RowIndex < clipper.DisplayEnd; RowIndex++)
				{
					FunctionToCall(ViewData, Aggregates[RowIndex]);
				}
			}
		}
		else
		{
			for (RowIndex = 0; RowIndex < Aggregates.Num(); ++RowIndex)
			{
				FunctionToCall(ViewData, Aggregates[RowIndex]);
			}
		}

		return RowIndex;
	}

#if RHI_NEW_GPU_PROFILER
	template <typename T>
	static int32 RenderArrayOfGpuStats(TArray<const FComplexStatMessage*>&& StatMessages, const FGameThreadStatsData& ViewData, const T& FunctionToCall)
	{
		using EType = UE::RHI::GPUProfiler::FGPUStat::EType;

		int32 RowIndex = 0;

		// Render all counters.
		if (!StatFilter.IsActive()) // TODO: clipping doesn't work with JIT filtering
		{
			ImGuiListClipper clipper;
			clipper.Begin(StatMessages.Num());
			while (clipper.Step())
			{
				for (RowIndex = clipper.DisplayStart; RowIndex < clipper.DisplayEnd; RowIndex++)
				{
					FunctionToCall(ViewData, *StatMessages[RowIndex]);
				}
			}
		}
		else
		{
			for (RowIndex = 0; RowIndex < StatMessages.Num(); ++RowIndex)
			{
				FunctionToCall(ViewData, *StatMessages[RowIndex]);
			}
		}

		return RowIndex;
	}
#endif //#if RHI_NEW_GPU_PROFILER

	FORCEINLINE static void RenderFlatCycle(const FGameThreadStatsData& ViewData, const FComplexStatMessage& Item)
	{
		RenderCycle(Item, true);
	}

	static void RenderGroupedWithHierarchy(const FGameThreadStatsData& ViewData)
	{
		// coarse culling for sections that are not visible
		bool bCullNextSection = false;

		// Render all groups.
		for (int32 GroupIndex = 0; GroupIndex < ViewData.ActiveStatGroups.Num(); ++GroupIndex)
		{
			const FActiveStatGroupInfo& StatGroup = ViewData.ActiveStatGroups[GroupIndex];

			// If the stat isn't enabled for this particular viewport, skip
			const FName& StatGroupFName = ViewData.GroupNames[GroupIndex];
			const FName& GroupName = ViewData.GroupNames[GroupIndex];
		
			FStatGroupData* StatGroupData = StatGroups.FindByKey(StatGroupFName);
			if (!StatGroupData)
			{
				const FString& GroupDesc = ViewData.GroupDescriptions[GroupIndex];
			
				FString StatName = GroupName.ToString();
				StatName.RemoveFromStart(TEXT("STATGROUP_"), ESearchCase::CaseSensitive);

				StatGroupData = &StatGroups.Emplace_GetRef(FAnsiString(GroupDesc), StatName, StatGroupFName, true);
			};
			StatGroupData->bIsActive = true;

			if (!bCullNextSection && ImGui::CollapsingHeader(*StatGroupData->DisplayName, ImGuiTreeNodeFlags_DefaultOpen))
			{
				constexpr ImGuiTableFlags TableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;
				char ScratchTableIdBuffer[128];

				// Render cycles.
				{
					const bool bHasHierarchy = !!StatGroup.HierAggregate.Num();
					const bool bHasFlat = !!StatGroup.FlatAggregate.Num();

					if (bHasHierarchy || bHasFlat)
					{
						sprintf_s(ScratchTableIdBuffer, sizeof(ScratchTableIdBuffer), "%s_CycleStats", *StatGroupData->DisplayName);
						if (ImGui::BeginTable(ScratchTableIdBuffer, GetCycleStatsColumnCount(), TableFlags))
						{
							RenderGroupedHeadings(bHasHierarchy);

							if (bHasHierarchy)
							{
								const int32 LastRowDisplayed = RenderArrayOfStats(StatGroup.HierAggregate, ViewData, RenderFlatCycle);							
								bCullNextSection = LastRowDisplayed < StatGroup.HierAggregate.Num();
							}
						
							if (!bCullNextSection && bHasFlat)
							{
								const int32 LastRowDisplayed = RenderArrayOfStats(StatGroup.FlatAggregate, ViewData, RenderFlatCycle);
								bCullNextSection = LastRowDisplayed < StatGroup.FlatAggregate.Num();
							}

							ImGui::EndTable();
						}
					}
				}

				// Render memory counters.
				if (!bCullNextSection && StatGroup.MemoryAggregate.Num())
				{
					sprintf_s(ScratchTableIdBuffer, sizeof(ScratchTableIdBuffer), "%s_MemoryStats", *StatGroupData->DisplayName);
					if (ImGui::BeginTable(ScratchTableIdBuffer, GetMemoryStatsColumnCount(), TableFlags))
					{
						RenderMemoryHeadings();

						int32 LastRowDisplayed = RenderArrayOfStats(StatGroup.MemoryAggregate, ViewData, RenderMemoryCounter);
						bCullNextSection = LastRowDisplayed < StatGroup.MemoryAggregate.Num();

						ImGui::EndTable();
					}
				}

				// Render remaining counters.
				if (!bCullNextSection && StatGroup.CountersAggregate.Num())
				{
					sprintf_s(ScratchTableIdBuffer, sizeof(ScratchTableIdBuffer), "%s_CounterStats", *StatGroupData->DisplayName);
					if (ImGui::BeginTable(ScratchTableIdBuffer, GetCounterStatsColumnCount(), TableFlags))
					{
						RenderCounterHeadings();

						int32 LastRowDisplayed = RenderArrayOfStats(StatGroup.CountersAggregate, ViewData, RenderCounter);
						bCullNextSection = LastRowDisplayed < StatGroup.CountersAggregate.Num();
					
						ImGui::EndTable();
					}
				}

#if RHI_NEW_GPU_PROFILER
				if (!bCullNextSection && StatGroup.GpuStatsAggregate.Num())
				{
					TArray<const FComplexStatMessage*> FilteredStats;
					FilteredStats.Reserve(StatGroup.GpuStatsAggregate.Num());
					for (const FComplexStatMessage& Message : StatGroup.GpuStatsAggregate)
					{
						using EType = UE::RHI::GPUProfiler::FGPUStat::EType;
						const FName ShortName = Message.GetShortName();
						const int32 Number = ShortName.GetNumber();
						if ((EType)Number == EType::Busy)
						{
							FilteredStats.Add(&Message);
						}
					}

					sprintf_s(ScratchTableIdBuffer, sizeof(ScratchTableIdBuffer), "%s_GpuStats", *StatGroupData->DisplayName);
					if (ImGui::BeginTable(ScratchTableIdBuffer, GetCounterStatsColumnCount(), TableFlags))
					{
						RenderGpuStatHeadings();

						int32 LastRowDisplayed = RenderArrayOfGpuStats(MoveTemp(FilteredStats), ViewData, RenderCounter);
						bCullNextSection = LastRowDisplayed < StatGroup.CountersAggregate.Num();

						ImGui::EndTable();
					}
				}
#endif //#if RHI_NEW_GPU_PROFILER
			}
		}
	}

	struct FSerializedData
	{
		// serializing array is easier than structures
		TArray<FString> StatNames;
		TArray<FString> DisplayNames;
		TArray<FString> StatGroupNames;

		bool IsStatGroupDataValid() const
		{
			return DisplayNames.Num() == StatNames.Num() && DisplayNames.Num() == StatGroupNames.Num();
		}
	};
	static void LoadWidgetSettings()
	{
		FConfigFile* WidgetSettings = FImGuiSettings::GetConfigFile();
		if (!WidgetSettings)
		{
			return;
		}

		FSerializedData SerializedData;
		WidgetSettings->GetArray(TEXT("ImGuiStatsVisualizer"), TEXT("StatGroup_StatNames"), SerializedData.StatNames);
		WidgetSettings->GetArray(TEXT("ImGuiStatsVisualizer"), TEXT("StatGroup_DisplayNames"), SerializedData.DisplayNames);
		WidgetSettings->GetArray(TEXT("ImGuiStatsVisualizer"), TEXT("StatGroup_StatGroupNames"), SerializedData.StatGroupNames);

		if (SerializedData.IsStatGroupDataValid())
		{
			for (int32 GroupIndex = 0; GroupIndex < SerializedData.DisplayNames.Num(); ++GroupIndex)
			{
				const FName GroupName = FName(SerializedData.StatGroupNames[GroupIndex]);

				FStatGroupData* GroupData = StatGroups.FindByKey(GroupName);
				if (!GroupData)
				{
					GroupData = &StatGroups.AddDefaulted_GetRef();
				}
				GroupData->NameForCommand = SerializedData.StatNames[GroupIndex];
				GroupData->DisplayName = TCHAR_TO_ANSI(*SerializedData.DisplayNames[GroupIndex]);
				GroupData->Name = FName(SerializedData.StatGroupNames[GroupIndex]);
				GroupData->bIsActive = false;
			}
		}
	}
	static void SaveWidgetSettings()
	{
		FConfigFile* WidgetSettings = FImGuiSettings::GetConfigFile();
		if (!WidgetSettings)
		{
			return;
		}

		FSerializedData SerializedData;
		for (const auto& GroupData : StatGroups)
		{
			SerializedData.StatNames.Add(GroupData.NameForCommand);
			SerializedData.StatGroupNames.Add(GroupData.Name.ToString());
			SerializedData.DisplayNames.Add(ANSI_TO_TCHAR(*GroupData.DisplayName));
		}

		WidgetSettings->SetArray(TEXT("ImGuiStatsVisualizer"), TEXT("StatGroup_StatNames"), SerializedData.StatNames);
		WidgetSettings->SetArray(TEXT("ImGuiStatsVisualizer"), TEXT("StatGroup_DisplayNames"), SerializedData.DisplayNames);
		WidgetSettings->SetArray(TEXT("ImGuiStatsVisualizer"), TEXT("StatGroup_StatGroupNames"), SerializedData.StatGroupNames);
		
		FImGuiSettings::SaveConfigFile();
	}

	static void RenderStatsHeader(FImGuiTickContext* Context, int32& GroupIndexToRemove)
	{
		bool bDeleteIconHovered = false;
		if (Context->ImGuiContext->DragDropActive)
		{
			UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
			const FImGuiImageBindingParams DeleteIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("GenericCommands.Delete"), FVector2D(ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.f));
			
			ImGui::PushStyleColor(ImGuiCol_Button, 0xBFFFFFFF);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFFFFFFFF);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFFFFFFFF);
			ImGui::PushStyleColor(ImGuiCol_DragDropTarget, 0xFF0000FF);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
			
			FImGui::TransparentImageButton("DeleteIcon", DeleteIcon);
			bDeleteIconHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly);
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("DND_STATGROUP_BUTTON"))
				{
					GroupIndexToRemove = *(const int32*)Payload->Data;
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::PopStyleVar();
			ImGui::PopStyleColor(4);
		}
		else
		{
			UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
			const FImGuiImageBindingParams SaveIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.Save"), FVector2D(ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.f));

			ImGui::PushStyleColor(ImGuiCol_Button, 0xBFFFFFFF);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFFFFFFFF);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFFFFFFFF);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

			if (FImGui::TransparentImageButton("SaveSettings", SaveIcon))
			{
				SaveWidgetSettings();
			}
			ImGui::SetItemTooltip("Save Settings");

			ImGui::PopStyleVar();
			ImGui::PopStyleColor(3);
		}

		ImGui::SameLine();

		// add new stat
		{
			ImGui::SetNextItemWidth(128);

			static char StatSourceBuffer[64] = { 0 };
			if (ImGui::InputTextWithHint("##NewStat", "Add Stat", StatSourceBuffer, sizeof(StatSourceBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
			{
				// execute command
				const FString StatCommand = FString::Printf(TEXT("stat %s -nodisplay"), ANSI_TO_TCHAR(StatSourceBuffer));
				GEngine->Exec(nullptr, *StatCommand);

				// reset and keep focus
				StatSourceBuffer[0] = 0;
				ImGui::SetKeyboardFocusHere(-1);
			}

			if (ImGui::IsItemActive())
			{
				ImDrawList* DrawList = ImGui::GetWindowDrawList();
				DrawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_FrameBgActive), 0.f, ImDrawFlags_None, 1.f);
			}
		}

		TOptional<TTuple<int32, int32>> PendingButtonMoveOp;
		for (int32 GroupIndex = 0; GroupIndex < StatGroups.Num(); ++GroupIndex)
		{
			FStatGroupData& GroupData = StatGroups[GroupIndex];
			const char* GroupName = *GroupData.DisplayName;

			FImGuiNamedWidgetScope StatGroupScope{ GroupName };

			const bool bApplyStyle = GroupData.bIsActive;
			if (bApplyStyle)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(2.f / 7.0f, 0.6f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(2.f / 7.0f, 0.7f, 0.7f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(2.f / 7.0f, 0.8f, 0.8f));
			}

			ImGui::SameLine();
			if ((ImGui::GetCursorPosX() + ImGui::CalcTextSize(GroupName).x + ImGui::GetStyle().FramePadding.x * 2.f) >= ImGui::GetWindowWidth())
			{
				ImGui::NewLine(); //<br>
			}

			if (ImGui::Button(GroupName))
			{
				const FString StatCommand = FString::Printf(TEXT("stat %s -nodisplay"), *GroupData.NameForCommand);
				GEngine->Exec(nullptr, *StatCommand);
			}
			if (ImGui::BeginPopupContextItem("Customization"))
			{
				static char NameBuffer[256] = { '0' };
				if (ImGui::IsWindowAppearing())
				{
					FMemory::Memcpy(NameBuffer, *GroupData.DisplayName, FMath::Min((size_t)GroupData.DisplayName.Len() + 1, sizeof(NameBuffer) - 1));
				}

				ImGui::Text("Display Name:");
				if (ImGui::InputText("##Name", NameBuffer, sizeof(NameBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
				{
					if (FCStringAnsi::Strlen(NameBuffer) > 2)
					{
						GroupData.DisplayName = NameBuffer;
					}
				}

				if (ImGui::Button("Remove Group"))
				{
					GroupIndexToRemove = GroupIndex;
				}

				ImGui::EndPopup();
			}

			if (bApplyStyle)
			{
				ImGui::PopStyleColor(3);
			}

			// disable at the end, we'll re-enable when stat group is encountered when displaying..
			GroupData.bIsActive = false;

			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
			{
				ImGui::SetDragDropPayload("DND_STATGROUP_BUTTON", &GroupIndex, sizeof(int32));

				if (bDeleteIconHovered)
				{
					ImGui::Text("Remove '%s'", GroupName);
				}
				else
				{
					ImGui::Text("Move '%s'", GroupName);
				}

				ImGui::EndDragDropSource();
			}

			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("DND_STATGROUP_BUTTON"))
				{
					int32 SourceButtonIndex = *(const int32*)Payload->Data;
					if (SourceButtonIndex != GroupIndex)
					{
						PendingButtonMoveOp = TTuple<int32, int32>{ SourceButtonIndex, GroupIndex };
					}
				}
				ImGui::EndDragDropTarget();
			}
		}

		if (PendingButtonMoveOp.IsSet())
		{
			const int32 SourceIndex = PendingButtonMoveOp.GetValue().Key;
			const int32 DestIndex = PendingButtonMoveOp.GetValue().Value;
			FStatGroupData Source = StatGroups[SourceIndex];
			
			StatGroups.RemoveAt(SourceIndex);
			StatGroups.Insert(Source, DestIndex);
		}

		ImGui::Separator();

		StatFilter.Draw(Context, "FilterLabel", "Filter Stats", ImGui::GetWindowWidth() * 0.75f);
	}

	static void Initialize()
	{
		LoadWidgetSettings();
	}

	static void RegisterOneFrameResources()
	{
		UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
		EditAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icons.Edit"), FVector2D(ImGui::GetFontSize()));
		BrowseAssetIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icons.Search"), FVector2D(ImGui::GetFontSize()));
	}

	static void Tick(FImGuiTickContext* Context)
	{
		FImGuiTickScope Scope{ Context };

		// TODO: Find a better/reliable way to check if docknode is already active
#if WITH_EDITOR //dockspace already created if not using standalone widgets
		ImGuiDockNodeFlags DockingFlags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoTabBar;
		const ImGuiID MainDockSpaceID = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), DockingFlags);
		ImGui::SetNextWindowDockID(MainDockSpaceID, ImGuiCond_Always);
#endif

		if (ImGui::Begin("Stats", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			RegisterOneFrameResources();
			
			int32 GroupIndexToRemove = INDEX_NONE;
			if (ImGui::BeginChild("Header", ImVec2(0.f, 0.f), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
			{
				RenderStatsHeader(Context, GroupIndexToRemove);
			}
			ImGui::EndChild();

			ImGui::Separator();

			if (ImGui::BeginChild("Body"))
			{
				FGameThreadStatsData* ViewData = FLatestGameThreadStatsData::Get().Latest;
				if (ViewData/* || !ViewData->bRenderStats*/)
				{
					if (!ViewData->RootFilter.IsEmpty())
					{
						ImGui::Text("Root filter is active. ROOT=%s", TCHAR_TO_ANSI(*ViewData->RootFilter));
						
						ImGui::Separator();
					}

					if (!ViewData->bDrawOnlyRawStats)
					{
						RenderGroupedWithHierarchy(*ViewData);
					}
				}
				else
				{
					ImGui::TextUnformatted("Not recording stats...");
				}
			}
			ImGui::EndChild();

			// need to defer removal as showing current stats can readd the group
			if (StatGroups.IsValidIndex(GroupIndexToRemove))
			{
				if (StatGroups[GroupIndexToRemove].bIsActive)
				{
					const FString StatCommand = FString::Printf(TEXT("stat %s -nodisplay"), *StatGroups[GroupIndexToRemove].NameForCommand);
					GEngine->Exec(nullptr, *StatCommand);
				}
				StatGroups.RemoveAt(GroupIndexToRemove);
			}
		}
		ImGui::End();
	}
	
	static FStaticWidgetRegisterParams RegisterParams =
	{
		.InitFunction		= &Initialize,
		.TickFunction		= &Tick,
		.WidgetIcon			= FSlateIcon(FName("EditorStyle"), FName("Profiler.Tab")),
		.WidgetName			= "Stats Visualizer",
		.WidgetDescription	= "Widget for displaying stat groups."
	};
	IMGUI_REGISTER_STANDALONE_WIDGET(RegisterParams);
}

#endif //#if STATS
