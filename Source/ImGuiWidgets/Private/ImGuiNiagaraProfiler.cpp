// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#include "NiagaraGPUProfilerInterface.h"

#if WITH_IMGUI && WITH_NIAGARA_GPU_PROFILER

#include "Engine/World.h"
#include "NiagaraSystem.h"
#include "ImGuiStaticWidget.h"
#include "ImGuiCommonWidgets.h"
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
	static FImGuiTextFilter<32> SimStageFilter;
	static bool bIsCapturing = false;

	static void Initialize()
	{
		NiagaraGPUProfilerListener = MakeUnique<FNiagaraGpuProfilerListener>();
		NiagaraGPUProfilerListener->SetEnabled(bIsCapturing);
		NiagaraGPUProfilerListener->SetHandler(
			[](const FNiagaraGpuFrameResultsPtr& FrameResults)
			{
				WorldStatData.Reset();

				if (!bIsCapturing)
				{
					return;
				}

				for (const auto& DispatchResult : FrameResults->DispatchResults)
				{
					UWorld* OwnerWorld = DispatchResult.OwnerComponent.IsValid() ? DispatchResult.OwnerComponent->GetWorld() : nullptr;						
					if (/*ensure*/(OwnerWorld))
					{
						WorldStatData.FindOrAdd(OwnerWorld)
								.AddStat(DispatchResult.OwnerEmitter, DispatchResult.StageName, (float)DispatchResult.DurationMicroseconds / 1000.f);
					}
				}
			}
		);
	}

	static void DisplayEmitterStats(const FEmitterStatData& EmitterStat)
	{
		UNiagaraEmitter* Emitter = EmitterStat.Emitter.Get();
		if (!Emitter)
		{
			return;
		}

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
					FNameBuilder SimStageName{ Stat.StageName };
					if (SimStageFilter.PassFilter(SimStageName.ToView()))
					{
						ImGui::TableNextRow(ImGuiTableRowFlags_None);

						ImGui::TableSetColumnIndex(0);
						{
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
				}

				ImGui::EndTable();
			}

			ImGui::TreePop();
		}
	}


	static void DisplaySystemStats(const FSystemStatData& SystemStat)
	{
		if (!SystemStat.System.IsValid() || SystemStat.EmitterStats.IsEmpty())
		{
			return;
		}

		FNameBuilder SystemName{ SystemStat.System->GetFName() };

		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader(TCHAR_TO_ANSI(*SystemName)))
		{
			ImGui::SameLine(); ImGui::Text(" - %fms", SystemStat.TotalTime);
			for (const auto& EmitterStat : SystemStat.EmitterStats)
			{
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

		if (ImGui::BeginTabItem(TCHAR_TO_ANSI(*World->GetName())))
		{
			if (ImGui::BeginChild("ScrollingArea"))
			{
				for (const auto& SystemStatItr : WorldStats.SystemStats)
				{
					FImGuiNamedWidgetScope Scope{ GetTypeHash(SystemStatItr.Key) };

					DisplaySystemStats(SystemStatItr.Value);
				}
			}
			ImGui::EndChild();

			ImGui::EndTabItem();
		}
	}

	static void Tick(ImGuiContext* Context)
	{
		FImGuiTickScope Scope{ Context };

		if (ImGui::Begin("Niagara Profiler", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			if (ImGui::BeginChild("Header", ImVec2(0.f, 0.f), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
			{
				ImGui::Checkbox("Enable profiling", &bIsCapturing);
				
				ImGui::Separator();

				ImGui::BeginDisabled(!bIsCapturing);
				SimStageFilter.Draw("SimStageFilter", "Filter Simulation Stages", false, ImGui::GetWindowWidth() * 0.75f);
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
		}
		ImGui::End();
	}

	IMGUI_REGISTER_STATIC_WIDGET(Initialize, Tick);
}

#endif //#if STATS
