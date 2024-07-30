// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#include "ImGuiStaticWidget.h"

#if WITH_IMGUI && STATS

#include <string>
#include "Engine/World.h"
#include "NiagaraSystem.h"
#include "Styling/AppStyle.h"
#include "ImGuiCommonWidgets.h"
#include "NiagaraGPUProfilerInterface.h"

//UE_DISABLE_OPTIMIZATION

namespace ImGuiNiagaraProfiler
{
	struct FSimStageStatData
	{
		FName StageName;
		float Time;
	};
	struct FWorldStatData
	{
		TMap<TWeakObjectPtr<UNiagaraSystem>, TMap<FVersionedNiagaraEmitterWeakPtr, TArray<FSimStageStatData>>> CapturedStats;
	};

	static TMap<TWeakObjectPtr<UWorld>, FWorldStatData> WorldStatData;
	static TUniquePtr<FNiagaraGpuProfilerListener> NiagaraGPUProfilerListener;
	static FImGuiTextFilter<128> StatFilter;
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
					UNiagaraEmitter* OwnerEmitter = DispatchResult.OwnerEmitter.Emitter.IsExplicitlyNull() ? nullptr : DispatchResult.OwnerEmitter.Emitter.Get();
					UNiagaraSystem* OwnerSystem = OwnerEmitter ? OwnerEmitter->GetTypedOuter<UNiagaraSystem>() : nullptr;
						
					if (OwnerWorld && OwnerSystem)
					{
						auto& CapturedStats = WorldStatData.FindOrAdd(OwnerWorld).CapturedStats;
						auto& EmitterStats = CapturedStats.FindOrAdd(OwnerSystem).FindOrAdd(DispatchResult.OwnerEmitter);
						EmitterStats.Add({ DispatchResult.StageName, (float)DispatchResult.DurationMicroseconds / 1000.f });
					}
				}
			}
		);
	}

	static void Tick(ImGuiContext* Context)
	{
		FImGuiTickScope Scope{ Context };

		if (ImGui::Begin("Niagara Profiler", nullptr, ImGuiWindowFlags_None))
		{
			ImGui::Checkbox("Enable profiling", &bIsCapturing);
			NiagaraGPUProfilerListener->SetEnabled(bIsCapturing);
			if (bIsCapturing)
			{
				StatFilter.Draw("NiagaraStats");

				ImGui::Separator();

				if (WorldStatData.Num() > 0)
				{
					if (ImGui::BeginTabBar("Stats"))
					{
						for (const auto& [World, WorldData] : WorldStatData)
						{
							if (World.IsValid() && ImGui::BeginTabItem(TCHAR_TO_ANSI(*World->GetName())))
							{
								for (auto& [System, EmitterStats] : WorldData.CapturedStats)
								{
									if (!System.IsValid())
									{
										continue;
									}

									const FString SystemName = System->GetFName().ToString();
									ImGui::SetNextItemOpen(true, ImGuiCond_Once);
									if (ImGui::CollapsingHeader(TCHAR_TO_ANSI(*SystemName)))
									{
										for (auto& [VerEmitterWeakPtr, Stats] : EmitterStats)
										{
											UNiagaraEmitter* OwnerEmitter = !VerEmitterWeakPtr.Emitter.IsExplicitlyNull() ? VerEmitterWeakPtr.Emitter.Get() : nullptr;
											if (!OwnerEmitter)
											{
												continue;
											}

											const FString EmitterName = OwnerEmitter->GetUniqueEmitterName();
											float EmitterSimTime = 0.f;

											struct FAccumulatedStat
											{
												FName StageName;
												float Time = 0.f;
												int32 Count = 0;
											};
											TArray<FAccumulatedStat, TInlineAllocator<16>> AccumulatedStats;
											AccumulatedStats.Reserve(Stats.Num());
											for (auto& Stat : Stats)
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

											ImGui::SetNextItemOpen(true, ImGuiCond_Once);
											if (ImGui::TreeNode(TCHAR_TO_ANSI(*FString::Printf(TEXT("%s-%s"), *SystemName, *EmitterName)), "%s - %f", TCHAR_TO_ANSI(*EmitterName), EmitterSimTime))
											{
												static constexpr ImGuiTableFlags TableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;
												char ScratchTableIdBuffer[128];
												sprintf_s(ScratchTableIdBuffer, sizeof(ScratchTableIdBuffer), "%s_SimStages", TCHAR_TO_ANSI(*EmitterName));
												if (ImGui::BeginTable(ScratchTableIdBuffer, 3, TableFlags))
												{
													ImGui::TableSetupColumn("Stage Name", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoHide);
													ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed);
													ImGui::TableSetupColumn("Duration(ms)", ImGuiTableColumnFlags_WidthFixed);
													ImGui::TableSetupScrollFreeze(0, 1); // Make row always visible

													static constexpr float TableRowHeight = 0.f; //autosize, having issues setting center alignment for text
													ImGui::TableNextRow(ImGuiTableRowFlags_Headers, TableRowHeight);
													for (int32 column = 0; column < 3; column++)
													{
														ImGui::TableSetColumnIndex(column);
														ImGui::TableHeader(ImGui::TableGetColumnName(column));
													}

													for (auto& Stat : AccumulatedStats)
													{
														const FString N = Stat.StageName.ToString();
														std::string StageName = std::string(TCHAR_TO_ANSI(*N));

														if (StatFilter.PassFilter(StageName.c_str()))
														{
															ImGui::TableNextRow(ImGuiTableRowFlags_None, TableRowHeight);

															ImGui::TableSetColumnIndex(0);
															{
																ImGui::TextUnformatted(StageName.c_str());
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
										}
									}
								}
								ImGui::EndTabItem();
							}
						}
						ImGui::EndTabBar();
					}
				}
			}			
		}
		ImGui::End();
	}

	IMGUI_REGISTER_STATIC_WIDGET(Initialize, Tick);
}

//UE_ENABLE_OPTIMIZATION

#endif //#if STATS
