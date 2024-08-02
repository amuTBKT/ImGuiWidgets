// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#include "ImGuiStaticWidget.h"

#if WITH_IMGUI && STATS

#include "Engine/World.h"
#include "NiagaraSystem.h"
#include "ImGuiCommonWidgets.h"
#include "NiagaraGPUProfilerInterface.h"

namespace ImGuiNiagaraProfiler
{
	struct FSimStageStatData
	{
		FName StageName = NAME_None;
		float Time = 0.f;
	};
	struct FEmitterStatData
	{
		TWeakObjectPtr<UNiagaraEmitter> Emitter;
		TArray<FSimStageStatData> SimStageStats;
	};
	struct FSystemStatData
	{
		TWeakObjectPtr<UNiagaraSystem> System;
		TArray<FEmitterStatData> EmitterStats;
	};
	struct FWorldStatData
	{
		TArray<FSystemStatData> SystemStats;

		void AddStat(UNiagaraSystem* InSystem, UNiagaraEmitter* InEmitter, FSimStageStatData&& InSimStageData)
		{
			FSystemStatData* SystemStat = SystemStats.FindByPredicate([InSystem](const auto& Stat) { return Stat.System.Get() == InSystem; });
			if (!SystemStat)
			{
				SystemStat = &SystemStats.Emplace_GetRef();
				SystemStat->System = InSystem;
			}

			FEmitterStatData* EmitterStat = SystemStat->EmitterStats.FindByPredicate([InEmitter](const auto& Stat) { return Stat.Emitter.Get() == InEmitter; });
			if (!EmitterStat)
			{
				EmitterStat = &SystemStat->EmitterStats.Emplace_GetRef();
				EmitterStat->Emitter = InEmitter;
			}

			EmitterStat->SimStageStats.Add(MoveTemp(InSimStageData));
		}
	};

	static TMap<TWeakObjectPtr<UWorld>, FWorldStatData> WorldStatData;
	static TUniquePtr<FNiagaraGpuProfilerListener> NiagaraGPUProfilerListener;
	static FImGuiTextFilter<128> SimStageFilter;
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
					if (OwnerWorld)
					{
						UNiagaraEmitter* OwnerEmitter = DispatchResult.OwnerEmitter.Emitter.IsExplicitlyNull() ? nullptr : DispatchResult.OwnerEmitter.Emitter.Get();
						UNiagaraSystem* OwnerSystem = OwnerEmitter ? OwnerEmitter->GetTypedOuter<UNiagaraSystem>() : nullptr;
						if (OwnerSystem)
						{
							FSimStageStatData SimStageData;
							SimStageData.StageName = DispatchResult.StageName;
							SimStageData.Time = (float)DispatchResult.DurationMicroseconds / 1000.f;
							WorldStatData.FindOrAdd(OwnerWorld).AddStat(OwnerSystem, OwnerEmitter, MoveTemp(SimStageData));
						}
					}
				}
			}
		);
	}

	static void Tick(ImGuiContext* Context)
	{
		FImGuiTickScope Scope{ Context };

		if (ImGui::Begin("Niagara Profiler", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			constexpr float HeaderSizeY = 50.f; // TODO: auto resize header
			if (ImGui::BeginChild("Header", ImVec2(0.f, HeaderSizeY * 0.5f + HeaderSizeY * ImGui::GetIO().FontGlobalScale * 0.5f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
			{
				ImGui::Checkbox("Enable profiling", &bIsCapturing);
				
				ImGui::BeginDisabled(!bIsCapturing);
				SimStageFilter.Draw("SimStageFilter", "Filter Simulation Stages");
				ImGui::EndDisabled();
				
				ImGui::Separator();
			}
			ImGui::EndChild();

			NiagaraGPUProfilerListener->SetEnabled(bIsCapturing);
			if (bIsCapturing)
			{
				auto AddEmitterStatsFunc = [](UNiagaraEmitter* Emitter, const TArray<FSimStageStatData>& SimStageStats)
				{
					if (!Emitter)
					{
						return;
					}

					const FString EmitterName = Emitter->GetUniqueEmitterName();

					struct FAccumulatedStat
					{
						FName StageName = NAME_None;
						float Time = 0.f;
						int32 Count = 0;
					};

					float EmitterSimTime = 0.f;
					TArray<FAccumulatedStat, TInlineAllocator<16>> AccumulatedStats;
					// accumlate stats and prepare data for displaying
					{						
						AccumulatedStats.Reserve(SimStageStats.Num());
						for (const auto& Stat : SimStageStats)
						{
							FAccumulatedStat* AccumStat = AccumulatedStats.FindByPredicate([StageName = Stat.StageName](auto& S) { return S.StageName == StageName; });
							if (!AccumStat)
							{
								AccumStat = &AccumulatedStats.Emplace_GetRef();
								AccumStat->StageName = Stat.StageName;
							}
							check(AccumStat);

							AccumStat->Time += Stat.Time;
							AccumStat->Count += 1;

							EmitterSimTime += Stat.Time;
						}
					}

					ImGui::SetNextItemOpen(true, ImGuiCond_Once);
					if (ImGui::TreeNode(TCHAR_TO_ANSI(*EmitterName), "%s - %f", TCHAR_TO_ANSI(*EmitterName), EmitterSimTime))
					{
						FImGuiNamedWidgetScope Scope{ TCHAR_TO_ANSI(*EmitterName) };

						static constexpr ImGuiTableFlags TableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;
						if (ImGui::BeginTable("SimStages", 3, TableFlags))
						{
							ImGui::TableSetupColumn("Stage Name", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoHide);
							ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed);
							ImGui::TableSetupColumn("Duration(ms)", ImGuiTableColumnFlags_WidthFixed);
							ImGui::TableSetupScrollFreeze(0, 1);

							ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
							for (int32 ColumnIndex = 0; ColumnIndex < 3; ColumnIndex++)
							{
								ImGui::TableSetColumnIndex(ColumnIndex);
								ImGui::TableHeader(ImGui::TableGetColumnName(ColumnIndex));
							}

							for (const auto& Stat : AccumulatedStats)
							{
								const FString SimStageName = Stat.StageName.ToString();

								if (SimStageFilter.PassFilter(SimStageName))
								{
									ImGui::TableNextRow(ImGuiTableRowFlags_None);

									ImGui::TableSetColumnIndex(0);
									{
										ImGui::TextUnformatted(TCHAR_TO_ANSI(*SimStageName));
									}

									ImGui::TableSetColumnIndex(1);
									{
										ImGui::Text("%i", Stat.Count);
									}

									ImGui::TableSetColumnIndex(2);
									{
										ImGui::Text("%f", Stat.Time);
									}
								}
							}

							ImGui::EndTable();
						}

						ImGui::TreePop();
					}
				};

				auto AddSystemStatsFunc = [&AddEmitterStatsFunc](UNiagaraSystem* System, const TArray<FEmitterStatData>& EmitterStats)
				{
					if (!System || EmitterStats.IsEmpty())
					{
						return;
					}

					const FString SystemName = System->GetFName().ToString();

					ImGui::SetNextItemOpen(true, ImGuiCond_Once);
					if (ImGui::CollapsingHeader(TCHAR_TO_ANSI(*SystemName)))
					{
						FImGuiNamedWidgetScope Scope{ TCHAR_TO_ANSI(*SystemName) };

						for (const auto& EmitterStat : EmitterStats)
						{
							AddEmitterStatsFunc(EmitterStat.Emitter.Get(), EmitterStat.SimStageStats);
						}
					}
				};

				auto AddWorldStatsFunc = [&AddSystemStatsFunc](UWorld* World, const FWorldStatData& WorldStats)
				{
					if (!World || WorldStats.SystemStats.IsEmpty())
					{
						return;
					}

					if (ImGui::BeginTabItem(TCHAR_TO_ANSI(*World->GetName())))
					{
						FImGuiNamedWidgetScope Scope{ TCHAR_TO_ANSI(*World->GetName()) };

						if (ImGui::BeginChild("ScrollingArea"))
						{
							for (const auto& SystemStat : WorldStats.SystemStats)
							{
								AddSystemStatsFunc(SystemStat.System.Get(), SystemStat.EmitterStats);
							}
						}
						ImGui::EndChild();

						ImGui::EndTabItem();
					}
				};

				if ((WorldStatData.Num() > 0) && ImGui::BeginTabBar("Stats"))
				{
					for (const auto& [World, WorldStats] : WorldStatData)
					{
						AddWorldStatsFunc(World.Get(), WorldStats);
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
