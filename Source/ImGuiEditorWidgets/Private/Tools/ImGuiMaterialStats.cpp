// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

#include "ShaderCompiler.h"
#include "ImGuiSubsystem.h"
#include "UObject/Package.h"
#include "ImGuiAssetPicker.h"
#include "Materials/Material.h"
#include "MaterialStatsCommon.h"
#include "ShaderCompilerCommon.h"
#include "Materials/MaterialInstance.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Runtime/Launch/Resources/Version.h"

#if 0
namespace ImGuiShaderAnalyzer
{
	extern bool CanAnalyzeShader(EShaderFrequency ShaderType);
	extern void ShowShaderStats(EShaderFrequency ShaderType, const FString& EntryName, const FString& ShaderFilePath);
}
#endif

namespace ImGuiMaterialStats
{
	struct FMaterialStatsData
	{
		struct FShaderStatsData
		{
			FString			 ShaderClassName;
			FString			 ShaderEntryName;
			FString			 ShaderFilePath;
			int32			 NumInstructions;
			EShaderFrequency ShaderType;
		};

		void Reset()
		{
			if (DuplicatedMaterial && DuplicatedMaterial->IsRooted())
			{
				DuplicatedMaterial->RemoveFromRoot();
			}
			DuplicatedMaterial = nullptr;
			Resource = nullptr;
			Shaders.Empty();
			ShaderVFLookup.Empty();
			ActiveShaderIndices.Empty();
			ShaderOutputBaseDir.Empty();
		}

		static FString ShortenShaderDebugName(const FString& DebugName)
		{
			static IConsoleVariable* DumpShaderDebugInfoShortCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DumpShaderDebugShortNames"));
			FString ShortName = DebugName;
			if (DumpShaderDebugInfoShortCVar && (DumpShaderDebugInfoShortCVar->GetInt() == 1))
			{
				// shorten name based on: GlobalBeginCompileShader()
				ShortName.ReplaceInline(TEXT("BasePass"), TEXT("BP"));
				ShortName.ReplaceInline(TEXT("ForForward"), TEXT("Fwd"));
				ShortName.ReplaceInline(TEXT("Shadow"), TEXT("Shdw"));
				ShortName.ReplaceInline(TEXT("LightMap"), TEXT("LM"));
				ShortName.ReplaceInline(TEXT("EHeightFogFeature==E_"), TEXT(""));
				ShortName.ReplaceInline(TEXT("Capsule"), TEXT("Caps"));
				ShortName.ReplaceInline(TEXT("Movable"), TEXT("Mov"));
				ShortName.ReplaceInline(TEXT("Culling"), TEXT("Cull"));
				ShortName.ReplaceInline(TEXT("Atmospheric"), TEXT("Atm"));
				ShortName.ReplaceInline(TEXT("Atmosphere"), TEXT("Atm"));
				ShortName.ReplaceInline(TEXT("Exponential"), TEXT("Exp"));
				ShortName.ReplaceInline(TEXT("Ambient"), TEXT("Amb"));
				ShortName.ReplaceInline(TEXT("Perspective"), TEXT("Persp"));
				ShortName.ReplaceInline(TEXT("Occlusion"), TEXT("Occ"));
				ShortName.ReplaceInline(TEXT("Position"), TEXT("Pos"));
				ShortName.ReplaceInline(TEXT("Skylight"), TEXT("Sky"));
				ShortName.ReplaceInline(TEXT("LightingPolicy"), TEXT("LP"));
				ShortName.ReplaceInline(TEXT("TranslucentLighting"), TEXT("TranslLight"));
				ShortName.ReplaceInline(TEXT("Translucency"), TEXT("Transl"));
				ShortName.ReplaceInline(TEXT("DistanceField"), TEXT("DistFiel"));
				ShortName.ReplaceInline(TEXT("Indirect"), TEXT("Ind"));
				ShortName.ReplaceInline(TEXT("Cached"), TEXT("Cach"));
				ShortName.ReplaceInline(TEXT("Inject"), TEXT("Inj"));
				ShortName.ReplaceInline(TEXT("Visualization"), TEXT("Viz"));
				ShortName.ReplaceInline(TEXT("Instanced"), TEXT("Inst"));
				ShortName.ReplaceInline(TEXT("Evaluate"), TEXT("Eval"));
				ShortName.ReplaceInline(TEXT("Landscape"), TEXT("Land"));
				ShortName.ReplaceInline(TEXT("Dynamic"), TEXT("Dyn"));
				ShortName.ReplaceInline(TEXT("Vertex"), TEXT("Vtx"));
				ShortName.ReplaceInline(TEXT("Output"), TEXT("Out"));
				ShortName.ReplaceInline(TEXT("Directional"), TEXT("Dir"));
				ShortName.ReplaceInline(TEXT("Irradiance"), TEXT("Irr"));
				ShortName.ReplaceInline(TEXT("Deferred"), TEXT("Def"));
				ShortName.ReplaceInline(TEXT("true"), TEXT("_1"));
				ShortName.ReplaceInline(TEXT("false"), TEXT("_0"));
				ShortName.ReplaceInline(TEXT("PROPAGATE_AO"), TEXT("AO"));
				ShortName.ReplaceInline(TEXT("PROPAGATE_SECONDARY_OCCLUSION"), TEXT("SEC_OCC"));
				ShortName.ReplaceInline(TEXT("PROPAGATE_MULTIPLE_BOUNCES"), TEXT("MULT_BOUNC"));
				ShortName.ReplaceInline(TEXT("LOCAL_LIGHTS_DISABLED"), TEXT("NoLL"));
				ShortName.ReplaceInline(TEXT("LOCAL_LIGHTS_ENABLED"), TEXT("LL"));
				ShortName.ReplaceInline(TEXT("LOCAL_LIGHTS_PREPASS_ENABLED"), TEXT("LLPP"));
				ShortName.ReplaceInline(TEXT("PostProcess"), TEXT("Post"));
				ShortName.ReplaceInline(TEXT("AntiAliasing"), TEXT("AA"));
				ShortName.ReplaceInline(TEXT("Mobile"), TEXT("Mob"));
				ShortName.ReplaceInline(TEXT("Linear"), TEXT("Lin"));
				ShortName.ReplaceInline(TEXT("INT32_MAX"), TEXT("IMAX"));
				ShortName.ReplaceInline(TEXT("Policy"), TEXT("Pol"));
				ShortName.ReplaceInline(TEXT("EAtmRenderFlag==E_"), TEXT(""));
			}
			return ShortName;
		}
		static FString MakeShaderName(FString VFName, FString ShaderClassName)
		{
			static IConsoleVariable* DumpShaderDebugInfoShortCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DumpShaderDebugShortNames"));			
			if (DumpShaderDebugInfoShortCVar && (DumpShaderDebugInfoShortCVar->GetInt() == 1))
			{
				// shorten name based on: GlobalBeginCompileShader()
				if (VFName.Len() > 0)
				{
					if (VFName[0] == TCHAR('F') || VFName[0] == TCHAR('T'))
					{
						VFName.RemoveAt(0);
					}
					VFName.ReplaceInline(TEXT("VertexFactory"), TEXT("VF"));
					VFName.ReplaceInline(TEXT("GPUSkinAPEXCloth"), TEXT("APEX"));
					VFName.ReplaceInline(TEXT("true"), TEXT("_1"));
					VFName.ReplaceInline(TEXT("false"), TEXT("_0"));
				}

				if (ShaderClassName[0] == TCHAR('F') || ShaderClassName[0] == TCHAR('T'))
				{
					ShaderClassName.RemoveAt(0);
				}
			}
			return (VFName.Len() == 0) ? ShaderClassName : (VFName / ShaderClassName);
		}
		static FString SanitizeShaderDumpFilePath(const FString& FilePath)
		{
			FString SanitizedPath = FilePath;
			// sanitize based on: FShaderCompilingManager::CreateShaderDebugInfoPath
			SanitizedPath.ReplaceInline(TEXT("<"), TEXT("("));
			SanitizedPath.ReplaceInline(TEXT(">"), TEXT(")"));
			SanitizedPath.ReplaceInline(TEXT("::"), TEXT("=="));
			SanitizedPath.ReplaceInline(TEXT("|"), TEXT("_"));
			SanitizedPath.ReplaceInline(TEXT("*"), TEXT("-"));
			SanitizedPath.ReplaceInline(TEXT("?"), TEXT("!"));
			SanitizedPath.ReplaceInline(TEXT("\""), TEXT("\'"));
			return SanitizedPath;
		}

		void PopulateStats(EShaderPlatform ShaderPlatform)
		{
			check(Shaders.IsEmpty());

			const FMaterialShaderMap* MaterialShaderMap = Resource->GetGameThreadShaderMap();
			if (MaterialShaderMap != nullptr)
			{
				TMap<FShaderId, TShaderRef<FShader>> ShaderMap;
				MaterialShaderMap->GetShaderList(ShaderMap);

#if ((ENGINE_MAJOR_VERSION * 100u + ENGINE_MINOR_VERSION) > 506) //(Version > 5.6)
				const FString ResourceUniqueName = ShortenShaderDebugName(Resource->GetUniqueAssetName(MaterialShaderMap->GetShaderMapId()));
#else
				const FString ResourceUniqueName = ShortenShaderDebugName(Resource->GetUniqueAssetName(ShaderPlatform, MaterialShaderMap->GetShaderMapId()));
#endif
				const FString ShaderPlatformName = FDataDrivenShaderPlatformInfo::GetName(ShaderPlatform).ToString();
				ShaderOutputBaseDir = GShaderCompilingManager->GetAbsoluteShaderDebugInfoDirectory() / ShaderPlatformName / ResourceUniqueName;

				Shaders.Reserve(ShaderMap.Num());
				for (const auto& Entry : ShaderMap)
				{
					const FShaderType* EntryShader = Entry.Value.GetType();
					const FVertexFactoryType* VertexFactory = Entry.Value.GetVertexFactoryType();
					const FString VertexFactoryName = VertexFactory ? VertexFactory->GetName() : TEXT("Global");
					const FString ShaderClassName = EntryShader->GetName();
					const FString ShaderName = VertexFactory ? MakeShaderName(VertexFactoryName, ShaderClassName) : MakeShaderName(TEXT(""), ShaderClassName);

					FString ShaderFileName = EntryShader->GetShaderFilename();
					int32 SlashIndex;
					if (ShaderFileName.FindLastChar(TEXT('/'), SlashIndex))
					{
						ShaderFileName = ShaderFileName.RightChop(SlashIndex + 1);
					}

					const FString DebugExtensionStr = FString::Printf(TEXT("_%08x%08x"), MaterialShaderMap->GetShaderMapId().BaseMaterialId.A, MaterialShaderMap->GetShaderMapId().BaseMaterialId.B);
#if ((ENGINE_MAJOR_VERSION * 100u + ENGINE_MINOR_VERSION) > 506) //(Version > 5.6)
					FString DebugGroupName = Resource->GetUniqueAssetName(MaterialShaderMap->GetShaderMapId()) / LexToString(Resource->GetQualityLevel()) / Entry.Key.ShaderPipelineName.GetDebugString().String.Get() / ShaderName;
#else
					FString DebugGroupName = Resource->GetUniqueAssetName(ShaderPlatform, MaterialShaderMap->GetShaderMapId()) / LexToString(Resource->GetQualityLevel()) / Entry.Key.ShaderPipelineName.GetDebugString().String.Get() / ShaderName;
#endif
					DebugGroupName = ShortenShaderDebugName(DebugGroupName);

					FString ShaderFilePath = GShaderCompilingManager->GetAbsoluteShaderDebugInfoDirectory() / ShaderPlatformName / DebugGroupName / FString::Printf(TEXT("%i%s"), Entry.Key.PermutationId, *DebugExtensionStr) / ShaderFileName;
					ShaderFilePath = SanitizeShaderDumpFilePath(ShaderFilePath);

					const int32 ShaderIndex = Shaders.AddDefaulted();
					Shaders[ShaderIndex].ShaderClassName = ShaderClassName;
					Shaders[ShaderIndex].ShaderEntryName = EntryShader->GetFunctionName();
					Shaders[ShaderIndex].ShaderFilePath = ShaderFilePath;
					Shaders[ShaderIndex].NumInstructions = Entry.Value.GetShader()->GetNumInstructions();
					Shaders[ShaderIndex].ShaderType = EntryShader->GetFrequency();

					ShaderVFLookup.FindOrAdd(VertexFactoryName).Add(ShaderIndex);

					IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
					if (PlatformFile.FileExists(*ShaderFilePath))
					{
						ActiveShaderIndices.AddUnique(ShaderIndex);
					}
				}
			}
		}

		UMaterialInterface* DuplicatedMaterial = nullptr;
		FMaterialResourceStats* Resource = nullptr;
		TArray<FShaderStatsData> Shaders;
		TMap<FString, TArray<int32>> ShaderVFLookup;
		TArray<int32> ActiveShaderIndices;
		FString ShaderOutputBaseDir;
	};

	// state for each window as we can have multiple stats window at a time
	struct FMaterialWindowState
	{
		FImGuiAssetPicker MaterialPicker = FImGuiAssetPicker::MakeWidget(UMaterialInterface::StaticClass());
		FImGuiTextFilter ShaderFilter = FImGuiTextFilter::MakeWidget(32u);

		TWeakObjectPtr<UMaterialInterface> SelectedMaterial;
		FMaterialStatsData MaterialStats = {};

		EMaterialQualityLevel::Type PreviewQualityLevel = EMaterialQualityLevel::Num;
		EShaderPlatform PreviewShaderPlatform = EShaderPlatform::SP_PCD3D_SM6;
		uint32 EnabledShaderTypes = (1u << SF_Vertex) | (1u << SF_Pixel);
		bool bIsCompilingPermutations = false;

		static uint32 GetUniqueWindowId()
		{
			static uint32 WindowId = 0;
			return WindowId++;
		}
		uint32 WindowId = GetUniqueWindowId();
		ImGuiID DockNodeId = 0;
	};
	static TArray<FMaterialWindowState> MaterialWindowStates = {};

	static void ResetWindowState(FMaterialWindowState* WindowToReset)
	{
		TArray<FMaterialWindowState*> WindowsToReset;
		if (WindowToReset)
		{
			WindowsToReset.Add(WindowToReset);
		}
		else
		{
			for (auto& MaterialWindowState : MaterialWindowStates)
			{
				WindowsToReset.Add(&MaterialWindowState);
			}
		}

		TArray<TRefCountPtr<FMaterial>> MaterialsToDeleteOnRenderThread;
		for (auto* MaterialWindowState : WindowsToReset)
		{
			FMaterialStatsData& MaterialStats = MaterialWindowState->MaterialStats;
			const FString& DirectoryToDelete = MaterialStats.ShaderOutputBaseDir;
			if (!DirectoryToDelete.IsEmpty() &&
				DirectoryToDelete.StartsWith(FPaths::ProjectDir()) &&
				DirectoryToDelete.StartsWith(GShaderCompilingManager->GetAbsoluteShaderDebugInfoDirectory()))
			{
				IFileManager::Get().DeleteDirectory(*DirectoryToDelete, false, true);
			}

			if (auto Resource = MaterialStats.Resource)
			{
				if (Resource->PrepareDestroy_GameThread())
				{
					MaterialsToDeleteOnRenderThread.Add(Resource);
				}
				else
				{
					delete Resource;
				}
			}
			MaterialStats.Reset();
		}
		FMaterial::DeleteMaterialsOnRenderThread(MaterialsToDeleteOnRenderThread);
	}

	static void Initialize()
	{
		// spawn with one window active
		MaterialWindowStates.AddDefaulted();

		FCoreDelegates::OnEnginePreExit.AddLambda(
			[]()
			{
				ResetWindowState(nullptr);
			});
	}

	static void Tick(FImGuiTickContext* Context)
	{
		FImGuiTickScope Scope{ Context };
		
		static IConsoleVariable* DumpShaderInfoCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DumpShaderDebugInfo"));
		static IConsoleVariable* DumpShaderShortNamesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DumpShaderDebugShortNames"));
		static int32 DumpShaderInfoCVarRestoreValue = INDEX_NONE;
		
		UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
		const FImGuiImageBindingParams WarningIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.Warning"), ImGui::GetFontSize());
		const FImGuiImageBindingParams BrowseIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.Search"), ImGui::GetFontSize());
		const FImGuiImageBindingParams EditIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.Edit"), ImGui::GetFontSize());
		const FImGuiImageBindingParams AnalyzeIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.Statistics"), ImGui::GetFontSize());

		bool bIsAnyWindowCompilingMaterial = false;
		for (FMaterialWindowState& MaterialWindowState : MaterialWindowStates)
		{
			bool& bIsCompilingPermutations = MaterialWindowState.bIsCompilingPermutations;
			FMaterialStatsData& MaterialStats = MaterialWindowState.MaterialStats;
			const EShaderPlatform PreviewShaderPlatform = MaterialWindowState.PreviewShaderPlatform;

			if (bIsCompilingPermutations)
			{
				bool bAllPermutationsCompiled = true;
				if (auto Resource = MaterialStats.Resource)
				{
					const bool bCompilationFinished = Resource->IsCompilationFinished();
					bAllPermutationsCompiled &= bCompilationFinished;
				}

				bIsCompilingPermutations = !bAllPermutationsCompiled;

				if (bAllPermutationsCompiled)
				{
					if (DumpShaderInfoCVar && (DumpShaderInfoCVarRestoreValue != DumpShaderInfoCVar->GetInt()))
					{
						DumpShaderInfoCVar->Set(DumpShaderInfoCVarRestoreValue);
						DumpShaderInfoCVarRestoreValue = INDEX_NONE;
					}

					MaterialStats.PopulateStats(PreviewShaderPlatform);
				}
			}

			bIsAnyWindowCompilingMaterial |= bIsCompilingPermutations;
		}

		ImGuiDockNodeFlags DockingFlags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoCloseButton;
		const ImGuiID MainDockSpaceID = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), DockingFlags);

		for (int32 WindowIndex = 0; WindowIndex < MaterialWindowStates.Num(); ++WindowIndex)
		{
			FMaterialWindowState& MaterialWindowState = MaterialWindowStates[WindowIndex];
			FAnsiString TabName = MaterialWindowState.SelectedMaterial.IsValid() ? TCHAR_TO_UTF8(*MaterialWindowState.SelectedMaterial->GetName()) : "Untitled";

			if (MaterialWindowState.DockNodeId > 0)
			{
				// dock in the appropriate tab when creating new window
				ImGui::SetNextWindowDockID(MaterialWindowState.DockNodeId, ImGuiCond_Once);
				MaterialWindowState.DockNodeId = 0u;
			}
			else if (WindowIndex == 0)
			{
				// keep the first window pinned to the dock
				ImGui::SetNextWindowDockID(MainDockSpaceID, ImGuiCond_Always);
			}

			bool bKeepItem = true;
			bool* bKeepItemPtr = (WindowIndex > 0) ? &bKeepItem : nullptr;
			if (ImGui::Begin(*FAnsiString::Printf("%s###MaterialWindow%i", *TabName, MaterialWindowState.WindowId), bKeepItemPtr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
			{
				FImGuiAssetPicker& MaterialPicker = MaterialWindowState.MaterialPicker;
				FImGuiTextFilter& ShaderFilter = MaterialWindowState.ShaderFilter;
				
				TWeakObjectPtr<UMaterialInterface>& SelectedMaterial = MaterialWindowState.SelectedMaterial;
				FMaterialStatsData& MaterialStats = MaterialWindowState.MaterialStats;
				
				EMaterialQualityLevel::Type PreviewQualityLevel = MaterialWindowState.PreviewQualityLevel;
				EShaderPlatform& PreviewShaderPlatform = MaterialWindowState.PreviewShaderPlatform;
				bool& bIsCompilingPermutations = MaterialWindowState.bIsCompilingPermutations;
				uint32& EnabledShaderTypes = MaterialWindowState.EnabledShaderTypes;

				// warning messages
				{
					bool bAddSeparator = false;

					if (bIsCompilingPermutations && DumpShaderInfoCVar)
					{
						if (DumpShaderInfoCVar->GetInt() == 1)
						{
							FImGui::Image(WarningIcon, ImVec4(1.f, 0.721568627f, 0.f, 1.f));
							ImGui::SameLine();
							ImGui::TextUnformatted("Shader debug data enabled...");

							bAddSeparator = true;
						}
					}

					if (DumpShaderShortNamesCVar)
					{
						if (DumpShaderShortNamesCVar->GetInt() == 0)
						{
							FImGui::Image(WarningIcon, ImVec4(1.f, 0.721568627f, 0.f, 1.f));
							ImGui::SameLine();
							ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
							ImGui::TextUnformatted("Browse/Edit functions may not work properly without 'r.DumpShaderDebugShortNames' enabled.");
							ImGui::PopTextWrapPos();

							bAddSeparator = true;
						}
					}

					if (bAddSeparator)
					{
						ImGui::Separator();
					}
				}

				if (MaterialPicker.Draw(Context, "Selected Material", SelectedMaterial))
				{
					ResetWindowState(&MaterialWindowState);
				}

				// stats collection
				{
					bool bIsButtonDisabled = (bIsAnyWindowCompilingMaterial || !SelectedMaterial.IsValid());

					ImGui::BeginDisabled(bIsButtonDisabled);
					const char* ButtonText = bIsCompilingPermutations ? "Compiling..." : (MaterialStats.ShaderVFLookup.IsEmpty() ? "Gather Stats" : "Refresh Stats");
					if (ImGui::Button(ButtonText))
					{
						ResetWindowState(&MaterialWindowState);

						bIsCompilingPermutations = true;
						if (DumpShaderInfoCVar)
						{
							DumpShaderInfoCVarRestoreValue = DumpShaderInfoCVar->GetInt();
							if (DumpShaderInfoCVarRestoreValue != 1)
							{
								DumpShaderInfoCVar->Set(1);
							}
						}

						const FString UniqueName = FString::Printf(TEXT("%s_%s"), *SelectedMaterial->GetName(), *FGuid::NewGuid().ToString());
						const FName Name = FName(*UniqueName);
						MaterialStats.DuplicatedMaterial = (UMaterialInterface*)StaticDuplicateObject(SelectedMaterial.Get(), GetTransientPackage(), Name);
						UMaterialInterface* MaterialToUse = MaterialStats.DuplicatedMaterial;
						if (!MaterialToUse->IsRooted())
						{
							MaterialToUse->AddToRoot();
						}
						auto& Resource = MaterialStats.Resource;
						Resource = new FMaterialResourceStats();
#if ((ENGINE_MAJOR_VERSION * 100u + ENGINE_MINOR_VERSION) > 506) //(Version > 5.6)
						Resource->SetMaterial(MaterialToUse->GetMaterial(), Cast<UMaterialInstance>(MaterialToUse), PreviewShaderPlatform, PreviewQualityLevel);
						Resource->CacheShaders(EMaterialShaderPrecompileMode::Default);
#else
						Resource->SetMaterial(MaterialToUse->GetMaterial(), Cast<UMaterialInstance>(MaterialToUse), ERHIFeatureLevel::SM6, PreviewQualityLevel);
						Resource->CacheShaders(PreviewShaderPlatform, EMaterialShaderPrecompileMode::Default);
#endif
					}
					ImGui::EndDisabled();
				}

				if (!bIsCompilingPermutations && !MaterialStats.ShaderVFLookup.IsEmpty())
				{
					ImGui::SameLine();
					// filtering
					{
						auto AddCheckboxButton = [](const char* Label, const char* ToolTip, uint32& InOutState, uint32 BitsToToggle)
							{
								const bool bApplyStyle = (InOutState & BitsToToggle) > 0u;
								if (bApplyStyle)
								{
									ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(2.f / 7.0f, 0.6f, 0.6f));
									ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(2.f / 7.0f, 0.7f, 0.7f));
									ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(2.f / 7.0f, 0.8f, 0.8f));
								}

								if (ImGui::Button(Label))
								{
									InOutState ^= BitsToToggle;
								}
								ImGui::SetItemTooltip("%s", ToolTip);

								if (bApplyStyle)
								{
									ImGui::PopStyleColor(3);
								}
							};

						ShaderFilter.Draw(Context, "FilterShaders", "Filter Shaders", ImGui::GetContentRegionAvail().x * 0.5f);

						ImGui::SameLine(); AddCheckboxButton("VS", "Toggle Vertex Shaders", EnabledShaderTypes, (1u << SF_Vertex));
						ImGui::SameLine(); AddCheckboxButton("PS", "Toggle Pixel Shaders", EnabledShaderTypes, (1u << SF_Pixel));
						ImGui::SameLine(); AddCheckboxButton("CS", "Toggle Compute Shaders", EnabledShaderTypes, (1u << SF_Compute));
						ImGui::SameLine(); AddCheckboxButton("RTX", "Toggle RayTracing Shaders", EnabledShaderTypes, ((1u << SF_RayMiss) | (1u << SF_RayHitGroup)));
						//ImGui::SameLine(); AddCheckboxButton("MS", "Toggle Mesh/Amplification Shaders", EnabledShaderTypes, ((1u << SF_Mesh) | (1u << SF_Amplification)));
					}
					ImGui::Separator();

					if (ImGui::BeginTabBar("Shaders"))
					{
						for (const auto& [VFName, ShaderIndices] : MaterialStats.ShaderVFLookup)
						{
							if (ImGui::BeginTabItem(TCHAR_TO_UTF8(*VFName)))
							{
								FImGuiNamedScope VF_Scope{ *VFName };

								static constexpr ImGuiTableFlags TableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY;
								if (ImGui::BeginTable("ShaderTable", 4, TableFlags))
								{
									ImGui::TableSetupColumn("Shader Name", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoHide);
									ImGui::TableSetupColumn("Shader Type", ImGuiTableColumnFlags_WidthFixed);
									ImGui::TableSetupColumn("Instruction Count", ImGuiTableColumnFlags_WidthFixed);
									ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch);
									ImGui::TableSetupScrollFreeze(0, 1);
									ImGui::TableHeadersRow();

									for (int32 ShaderIndex : ShaderIndices)
									{
										const auto& Shader = MaterialStats.Shaders[ShaderIndex];

										if (((EnabledShaderTypes & (1u << Shader.ShaderType)) == 0u) ||
											!ShaderFilter.PassFilter(Shader.ShaderClassName))
										{
											continue;
										}

										FImGuiNamedScope Shader_Scope{ *Shader.ShaderFilePath };

										ImGui::TableNextRow(ImGuiTableRowFlags_None);

										ImGui::TableSetColumnIndex(0);
										{
											ImGui::TextUnformatted(TCHAR_TO_UTF8(*Shader.ShaderClassName));
										}

										ImGui::TableSetColumnIndex(1);
										{
											ImGui::TextUnformatted(TCHAR_TO_UTF8(CrossCompiler::GetFrequencyName(Shader.ShaderType)));
										}

										ImGui::TableSetColumnIndex(2);
										{
											ImGui::Text("%i", Shader.NumInstructions);
										}

										ImGui::TableSetColumnIndex(3);
										{
											if (MaterialStats.ActiveShaderIndices.Contains(ShaderIndex))
											{
												ImGui::PushStyleColor(ImGuiCol_Button, 0);
												ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFF404040);
												ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFF404040);
												ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2);
												ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1, 1));

												if (FImGui::ImageButtonWithTint("BrowseToDir", BrowseIcon, 0x8FFFFFFF, 0xFFFFFFFF))
												{
													FPlatformProcess::ExploreFolder(*Shader.ShaderFilePath);
												}
												ImGui::SetItemTooltip("Browse to shader file");

												ImGui::SameLine();

												if (FImGui::ImageButtonWithTint("EditFile", EditIcon, 0x8FFFFFFF, 0xFFFFFFFF))
												{
													FPlatformProcess::LaunchFileInDefaultExternalApplication(*Shader.ShaderFilePath);
												}
												ImGui::SetItemTooltip("Edit shader file");

												ImGui::SameLine();

#if 0
												ImGui::BeginDisabled(ImGuiShaderAnalyzer::CanAnalyzeShader(Shader.ShaderType) == false);
												if (FImGui::ImageButtonWithTint("Analyze", AnalyzeIcon, 0x8FFFFFFF, 0xFFFFFFFF))
												{
													ImGuiShaderAnalyzer::ShowShaderStats(Shader.ShaderType, Shader.ShaderEntryName, Shader.ShaderFilePath);
												}
												ImGui::SetItemTooltip("Analyze shader");
												ImGui::EndDisabled();
#endif

												ImGui::PopStyleVar(2);
												ImGui::PopStyleColor(3);
											}
										}
									}

									ImGui::EndTable();
								}

								ImGui::EndTabItem();
							}
						}
						ImGui::EndTabBar();
					}
				}
			}
			ImGui::End();

			if (!bKeepItem)
			{
				ResetWindowState(&MaterialWindowStates[WindowIndex]);
				MaterialWindowStates.RemoveAt(WindowIndex);
				WindowIndex = FMath::Max(0, WindowIndex - 1);
			}
		}

		// TabBar customization
		{
			/*
			TODO: how to find the ideal tab bar for customization?
			Just finding the first node with valid TabBar here, seems to pick the last window (ideally should be using the first?)
			*/
			TArray<ImGuiDockNode*> Stack;
			Stack.Add(ImGui::DockContextFindNodeByID(Context->ImguiContext, MainDockSpaceID));
			ImGuiDockNode* DockNode = nullptr;
			while (true)
			{
				DockNode = Stack.IsEmpty() ? nullptr : Stack.Pop();
				if (!DockNode || DockNode->TabBar)
				{
					break;
				}

				if (DockNode->ChildNodes[0]) Stack.Add(DockNode->ChildNodes[0]);
				if (DockNode->ChildNodes[1]) Stack.Add(DockNode->ChildNodes[1]);
			}

			if (DockNode && ImGui::DockNodeBeginAmendTabBar(DockNode))
			{
				ImGui::SameLine();
				if (ImGui::Button("+"))
				{
					FMaterialWindowState& NewMaterialWindowState = MaterialWindowStates.AddDefaulted_GetRef();
					NewMaterialWindowState.DockNodeId = DockNode->ID;
				}
				ImGui::DockNodeEndAmendTabBar();
			}
		}
	}

	static FStaticWidgetRegisterParams RegisterParams =
	{
		.InitFunction		= &Initialize,
		.TickFunction		= &Tick,
		.WidgetIcon			= FSlateIcon(FAppStyle::GetAppStyleSetName(), FName("MaterialEditor.ToggleMaterialStats.Tab")),
		.WidgetName			= "Material Stats",
		.WidgetDescription	= "Widget for inspecting compiled Material stats."
	};
	IMGUI_REGISTER_STANDALONE_WIDGET(RegisterParams);
}
