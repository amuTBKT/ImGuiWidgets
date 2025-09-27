// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

#include "NiagaraGPUProfilerInterface.h"

#if WITH_IMGUI && WITH_NIAGARA_GPU_PROFILER

#if WITH_EDITOR
#include "Editor.h"
#endif

#include "Engine/World.h"
#include "NiagaraSystem.h"
#include "ImGuiSubsystem.h"
#include "ImGuiStaticWidget.h"
#include "ImGuiCommonWidgets.h"
#include "Textures/SlateIcon.h"
#include "NiagaraSimulationStageBase.h"

namespace ImGuiNiagaraProfiler
{
	struct FSimStageStatData
	{
		FName StageName = NAME_None;
		int32 CallCount = 0;
		float TotalTime = 0.f;

		bool operator==(const FName& OtherName) const
		{
			return StageName == OtherName;
		}
	};
	struct FEmitterStatData
	{
		TWeakObjectPtr<UNiagaraEmitter> Emitter;
		TArray<FSimStageStatData> SimStageStats;
		float TotalTime = 0.f;

		void Initialize(const FVersionedNiagaraEmitterWeakPtr& VersionedEmitter)
		{
			Emitter = VersionedEmitter.Emitter;

			// add particle update stage
			SimStageStats.AddDefaulted_GetRef().StageName = UNiagaraSimulationStageBase::ParticleSpawnUpdateName;

			const TArray<UNiagaraSimulationStageBase*>& SimulationStages = Emitter->GetEmitterData(VersionedEmitter.Version)->GetSimulationStages();
			for (const UNiagaraSimulationStageBase* SimStage : SimulationStages)
			{
				if (IsValid(SimStage))
				{
					SimStageStats.AddDefaulted_GetRef().StageName = SimStage->SimulationStageName;
				}
			}
		}

		void AddSimStageStat(const FName& StageName, float Duration)
		{
			FSimStageStatData* StageData = SimStageStats.FindByKey(StageName);
			if (!ensure(StageData != nullptr))
			{
				return;
			}

			StageData->CallCount += 1;
			StageData->TotalTime += Duration;

			TotalTime += Duration;
		}
	};
	struct FSystemStatData
	{
		TWeakObjectPtr<UNiagaraSystem> System;
		TArray<FEmitterStatData> EmitterStats;
		float TotalTime = 0.f;
	};
	struct FWorldStatData
	{
		TMap<FObjectKey, FSystemStatData> SystemStats;

		void AddStat(const FVersionedNiagaraEmitterWeakPtr& VersionedEmitter, const FName& InSimStageName, float InDuration)
		{
			UNiagaraEmitter* InEmitter = VersionedEmitter.Emitter.IsExplicitlyNull() ? nullptr : VersionedEmitter.Emitter.Get();
			UNiagaraSystem* InSystem = InEmitter ? InEmitter->GetTypedOuter<UNiagaraSystem>() : nullptr;
			if (!InSystem)
			{
				return;
			}

			FSystemStatData* SystemStat = SystemStats.Find(InSystem);
			if (!SystemStat)
			{
				SystemStat = &SystemStats.Add(FObjectKey(InSystem));
				SystemStat->System = InSystem;
				SystemStat->TotalTime = 0.f;
			}

			FEmitterStatData* EmitterStat = SystemStat->EmitterStats.FindByPredicate([InEmitter](const auto& Stat) { return Stat.Emitter.Get() == InEmitter; });
			if (!EmitterStat)
			{
				EmitterStat = &SystemStat->EmitterStats.Emplace_GetRef();
				EmitterStat->Initialize(VersionedEmitter);
			}
			EmitterStat->AddSimStageStat(InSimStageName, InDuration);

			SystemStat->TotalTime += InDuration;
		}
	};

	static TMap<TWeakObjectPtr<UWorld>, FWorldStatData> WorldStatData;
	static TUniquePtr<FNiagaraGpuProfilerListener> NiagaraGPUProfilerListener;
	static FImGuiTextFilter SimStageFilter = FImGuiTextFilter::MakeWidget(32u);
	static bool bIsCapturing = false;

	static TOptional<bool> bExpandAll;

	static void Initialize()
	{
		NiagaraGPUProfilerListener = MakeUnique<FNiagaraGpuProfilerListener>();
		NiagaraGPUProfilerListener->SetEnabled(bIsCapturing);
		NiagaraGPUProfilerListener->SetHandler(
			[](const FNiagaraGpuFrameResultsPtr& FrameResults)
			{
				if (!bIsCapturing)
				{
					return;
				}

				TArray<UWorld*> WorldsUpdatedThisFrame;
				for (const auto& DispatchResult : FrameResults->DispatchResults)
				{
					UWorld* OwnerWorld = DispatchResult.OwnerComponent.IsValid() ? DispatchResult.OwnerComponent->GetWorld() : nullptr;
					if (OwnerWorld)
					{
						// NOTE: it's possible to receive stats for worlds separately, this ensures we don't reset the wrong world stats.
						if (!WorldsUpdatedThisFrame.Contains(OwnerWorld))
						{
							if (auto* WorldStatsToReset = WorldStatData.Find(OwnerWorld))
							{
								WorldStatsToReset->SystemStats.Reset();
							}
							WorldsUpdatedThisFrame.Add(OwnerWorld);
						}

						WorldStatData.FindOrAdd(OwnerWorld)
								.AddStat(DispatchResult.OwnerEmitter, DispatchResult.StageName, (float)DispatchResult.DurationMicroseconds / 1000.f);
					}
				}
			}
		);
	}

	static bool ShouldDisplayStat(const FSimStageStatData& SimStageStat)
	{
		FNameBuilder SimStageName{ SimStageStat.StageName };
		return SimStageFilter.PassFilter(SimStageName.ToView());
	}
	static bool ShouldDisplayStat(const FEmitterStatData& EmitterStat)
	{
		UNiagaraEmitter* Emitter = EmitterStat.Emitter.Get();
		if (!Emitter)
		{
			return false;
		}

		for (const auto& Stat : EmitterStat.SimStageStats)
		{
			if (ShouldDisplayStat(Stat))
			{
				return true;
			}
		}

		return false;
	}
	static bool ShouldDisplayStat(const FSystemStatData& SystemStat)
	{
		if (!SystemStat.System.IsValid() || SystemStat.EmitterStats.IsEmpty())
		{
			return false;
		}

		for (const auto& EmitterStat : SystemStat.EmitterStats)
		{
			if (ShouldDisplayStat(EmitterStat))
			{
				return true;
			}
		}

		return false;
	}

	static void DisplayEmitterStats(const FEmitterStatData& EmitterStat)
	{
		UNiagaraEmitter* Emitter = EmitterStat.Emitter.Get();		
		const FString& EmitterName = Emitter->GetUniqueEmitterName();

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::TreeNode(TCHAR_TO_ANSI(*EmitterName), "%s - %f", TCHAR_TO_ANSI(*EmitterName), EmitterStat.TotalTime))
		{
			FImGuiNamedWidgetScope Scope{ *EmitterName };

			static constexpr ImGuiTableFlags TableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;
			if (ImGui::BeginTable("SimStages", 3, TableFlags))
			{
				ImGui::TableSetupColumn("Stage Name", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoHide);
				ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Duration(ms)", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();

				for (const auto& Stat : EmitterStat.SimStageStats)
				{
					if (!ShouldDisplayStat(Stat))
					{
						continue;
					}

					ImGui::TableNextRow(ImGuiTableRowFlags_None);

					ImGui::TableSetColumnIndex(0);
					{
						FNameBuilder SimStageName{ Stat.StageName };
						ImGui::TextUnformatted(TCHAR_TO_ANSI(*SimStageName));
					}

					ImGui::TableSetColumnIndex(1);
					{
						ImGui::Text("%i", Stat.CallCount);
					}

					ImGui::TableSetColumnIndex(2);
					{
						ImGui::Text("%f", Stat.TotalTime);
					}
				}

				ImGui::EndTable();
			}

			ImGui::TreePop();
		}
	}

	static void DisplaySystemStats(const FSystemStatData& SystemStat)
	{
		FNameBuilder SystemName{ SystemStat.System->GetFName() };

		if (bExpandAll.IsSet())
		{
			ImGui::SetNextItemOpen(bExpandAll.GetValue(), ImGuiCond_Always);
		}
		else
		{
			ImGui::SetNextItemOpen(false, ImGuiCond_Once);
		}

		if (ImGui::CollapsingHeader(TCHAR_TO_ANSI(*SystemName)))
		{
			ImGui::SameLine(); ImGui::Text(" - %fms", SystemStat.TotalTime);
			for (const auto& EmitterStat : SystemStat.EmitterStats)
			{
				if (!ShouldDisplayStat(EmitterStat))
				{
					continue;
				}

				DisplayEmitterStats(EmitterStat);
			}
		}
		else
		{
			ImGui::SameLine(); ImGui::Text(" - %fms", SystemStat.TotalTime);
		}
	}

	static void DisplayWorldStats(const UWorld* World, const FWorldStatData& WorldStats)
	{
		if (!World || WorldStats.SystemStats.IsEmpty())
		{
			return;
		}

#if WITH_EDITOR
		// disable duplicates from editor world
		if (World->WorldType == EWorldType::Editor && (GEditor && GEditor->PlayWorld != nullptr))
		{
			return;
		}
#endif

		if (ImGui::BeginTabItem(TCHAR_TO_ANSI(*World->GetName())))
		{
			if (ImGui::BeginChild("ScrollingArea"))
			{
				for (const auto& SystemStatItr : WorldStats.SystemStats)
				{
					if (!ShouldDisplayStat(SystemStatItr.Value))
					{
						continue;
					}

					FImGuiNamedWidgetScope Scope{ GetTypeHash(SystemStatItr.Key) };

					DisplaySystemStats(SystemStatItr.Value);
				}
			}
			ImGui::EndChild();

			ImGui::EndTabItem();
		}
	}

	static void Tick(FImGuiTickContext* Context)
	{
		FImGuiTickScope Scope{ Context };

		// TODO: Find a better/reliable way to check if docknode is already active
#if WITH_EDITOR //dockspace already created if not using standlone widgets
		ImGuiDockNodeFlags DockingFlags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoTabBar;
		const ImGuiID MainDockSpaceID = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), DockingFlags);
		ImGui::SetNextWindowDockID(MainDockSpaceID, ImGuiCond_Always);
#endif

		if (ImGui::Begin("Niagara Profiler", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			if (ImGui::BeginChild("Header", ImVec2(0.f, 0.f), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
			{
				ImGui::Checkbox("Enable profiling", &bIsCapturing);
				
				ImGui::Separator();

				ImGui::BeginDisabled(!bIsCapturing);
				SimStageFilter.Draw(Context, "SimStageFilter", "Filter Simulation Stages", ImGui::GetWindowWidth() * 0.75f);
				ImGui::SameLine();

				// collapse/expand buttons
				{
					UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
					const FImGuiImageBindingParams CollapseAllIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.CollapseAll"), FVector2D(16.) * ImGui::GetStyle().FontScaleMain);
					const FImGuiImageBindingParams ExpandAllIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icon.ExpandAll"), FVector2D(16.) * ImGui::GetStyle().FontScaleMain);

					ImGui::PushStyleColor(ImGuiCol_Button, 0);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFF404040);
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFF404040);
					ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2);
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1, 1));

					if (FImGui::ImageButtonWithTint("CollapseAll", CollapseAllIcon, 0x8FFFFFFF, 0xFFFFFFFF))
					{
						bExpandAll = false;
					}
					ImGui::SetItemTooltip("Collapse All");

					ImGui::SameLine();

					if (FImGui::ImageButtonWithTint("ExpandAll", ExpandAllIcon, 0x8FFFFFFF, 0xFFFFFFFF))
					{
						bExpandAll = true;
					}
					ImGui::SetItemTooltip("Expand All");

					ImGui::PopStyleVar(2);
					ImGui::PopStyleColor(3);
				}
				ImGui::EndDisabled();
			}
			ImGui::EndChild();

			ImGui::Separator();

			NiagaraGPUProfilerListener->SetEnabled(bIsCapturing);
			if (bIsCapturing)
			{
				if ((WorldStatData.Num() > 0) && ImGui::BeginTabBar("Stats"))
				{
					for (const auto& [World, WorldStats] : WorldStatData)
					{
						DisplayWorldStats(World.Get(), WorldStats);
					}
					ImGui::EndTabBar();
				}
			}

			bExpandAll.Reset();
		}
		ImGui::End();
	}

	static FStaticWidgetRegisterParams RegisterParams =
	{
		.InitFunction		= &Initialize,
		.TickFunction		= &Tick,
		.WidgetIcon			= FSlateIcon(FName("NiagaraEditorStyle"), FName("Tab.Debugger")),
		.WidgetName			= "Niagara Profiler",
		.WidgetDescription	= "Widget for displaying Niagara gpu stats."
	};
	IMGUI_REGISTER_STANDALONE_WIDGET(RegisterParams);
}

#endif //#if STATS
