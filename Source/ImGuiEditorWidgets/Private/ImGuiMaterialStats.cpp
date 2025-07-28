// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#if WITH_IMGUI

#include "ShaderCompiler.h"
#include "ImGuiAssetPicker.h"
#include "ImGuiStaticWidget.h"
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

		UMaterial* DuplicatedMaterial = nullptr;
		FMaterialResourceStats* Resource = nullptr;
		TArray<FShaderStatsData> Shaders;
		TMap<FString, TArray<int32>> ShaderVFLookup;
		TArray<int32> ActiveShaderIndices;
		FString ShaderOutputBaseDir;
	};

	static FMaterialStatsData MaterialStats = {};
	static EShaderPlatform PreviewShaderPlatform = EShaderPlatform::SP_PCD3D_SM6;
	static EMaterialQualityLevel::Type PreviewQualityLevel = EMaterialQualityLevel::Num;

	static void Reset()
	{
		const FString& DirectoryToDelete = MaterialStats.ShaderOutputBaseDir;
		if (!DirectoryToDelete.IsEmpty() &&
			DirectoryToDelete.StartsWith(FPaths::ProjectDir()) &&
			DirectoryToDelete.StartsWith(GShaderCompilingManager->GetAbsoluteShaderDebugInfoDirectory()))
		{
			IFileManager::Get().DeleteDirectory(*DirectoryToDelete, false, true);
		}

		TArray<TRefCountPtr<FMaterial>> MaterialsToDeleteOnRenderThread;
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
		FMaterial::DeleteMaterialsOnRenderThread(MaterialsToDeleteOnRenderThread);
	}

	static void Initialize()
	{
		FCoreDelegates::OnEnginePreExit.AddLambda(
			[]()
			{
				Reset();
			});
	}

	static void Tick(ImGuiContext* Context)
	{
		FImGuiTickScope Scope{ Context };

		if (ImGui::Begin("Material Stats", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			static IConsoleVariable* DumpShaderInfoCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DumpShaderDebugInfo"));
			static IConsoleVariable* DumpShaderShortNamesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DumpShaderDebugShortNames"));
			static int32 DumpShaderInfoCVarRestoreValue = INDEX_NONE;
			static bool bIsCompilingPermutations = false;

			static FImGuiAssetPicker<UMaterial> MaterialPicker;
			static TWeakObjectPtr<UMaterial> SelectedMaterial;
			static FImGuiTextFilter<128> ShaderFilter;
			static uint32 EnabledShaderTypes = (1u << SF_Vertex) | (1u << SF_Pixel);

			UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
			const FImGuiImageBindingParams WarningIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_FNAME("Icons.Warning"), FVector2D(ImGui::GetFontSize()), 1.f);
			const FImGuiImageBindingParams BrowseIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_FNAME("Icons.Search"), FVector2D(ImGui::GetFontSize()), 1.f);
			const FImGuiImageBindingParams EditIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_FNAME("Icons.Edit"), FVector2D(ImGui::GetFontSize()), 1.f);
			const FImGuiImageBindingParams AnalyzeIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_FNAME("DerivedData.Cache.Statistics"), FVector2D(ImGui::GetFontSize()), 1.f);

			// warning messages
			{
				bool bAddSeparator = false;
				
				if (DumpShaderInfoCVar)
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
						ImGui::TextUnformatted("Browse/Edit functions maybe not work properly without 'r.DumpShaderDebugShortNames' enabled");

						bAddSeparator = true;
					}
				}

				if (bAddSeparator)
				{
					ImGui::Separator();
				}
			}
			
			if (MaterialPicker.Draw("Selected Material", SelectedMaterial))
			{
				Reset();
			}

			// stats collection
			{
				bool bButtonDisabled = (bIsCompilingPermutations || !SelectedMaterial.IsValid());
				
				ImGui::BeginDisabled(bButtonDisabled);
				const char* ButtonText = bIsCompilingPermutations ? "Compiling..." : (MaterialStats.ShaderVFLookup.IsEmpty() ? "Gather Stats" : "Refresh Stats");
				if (ImGui::Button(ButtonText))
				{
					Reset();

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
					MaterialStats.DuplicatedMaterial = (UMaterial*)StaticDuplicateObject(SelectedMaterial.Get(), GetTransientPackage(), Name);
					UMaterial* MaterialToUse = MaterialStats.DuplicatedMaterial;
					if (!MaterialToUse->IsRooted())
					{
						MaterialToUse->AddToRoot();
					}
					auto& Resource = MaterialStats.Resource;
					Resource = new FMaterialResourceStats();
#if ((ENGINE_MAJOR_VERSION * 100u + ENGINE_MINOR_VERSION) > 506) //(Version > 5.6)
					Resource->SetMaterial(MaterialToUse, nullptr, PreviewShaderPlatform, PreviewQualityLevel);
					Resource->CacheShaders(EMaterialShaderPrecompileMode::Default);
#else
					Resource->SetMaterial(MaterialToUse, nullptr, ERHIFeatureLevel::SM6, PreviewQualityLevel);
					Resource->CacheShaders(PreviewShaderPlatform, EMaterialShaderPrecompileMode::Default);
#endif
				}
				ImGui::EndDisabled();
			}

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
							if (ImGui::IsItemHovered())
							{
								ImGui::SetTooltip(ToolTip);
							}

							if (bApplyStyle)
							{
								ImGui::PopStyleColor(3);
							}
						};
					
					ShaderFilter.Draw("FilterShaders", "Filter Shaders");

					ImGui::SameLine(); AddCheckboxButton("VS", "Toggle Vertex Shaders", EnabledShaderTypes, (1u << SF_Vertex));
					ImGui::SameLine(); AddCheckboxButton("PS", "Toggle Pixel Shaders", EnabledShaderTypes, (1u << SF_Pixel));
					ImGui::SameLine(); AddCheckboxButton("CS", "Toggle Compute Shaders", EnabledShaderTypes, (1u << SF_Compute));
					ImGui::SameLine(); AddCheckboxButton("RTX", "Toggle RayTracing Shaders", EnabledShaderTypes, ((1u << SF_RayMiss) | (1u << SF_RayHitGroup)));
					//ImGui::SameLine(); AddCheckboxButton("MS", "Toggle Mesh/Amplification Shaders",  EnabledShaderTypes, ((1u << SF_Mesh) | (1u << SF_Amplification)));
				}
				ImGui::Separator();

				if (ImGui::BeginTabBar("Shaders"))
				{
					for (const auto& [VFName, ShaderIndices] : MaterialStats.ShaderVFLookup)
					{
						if (ImGui::BeginTabItem(TCHAR_TO_ANSI(*VFName)))
						{
							FImGuiNamedWidgetScope VF_Scope{ *VFName };

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

									FImGuiNamedWidgetScope Shader_Scope{ *Shader.ShaderFilePath };

									ImGui::TableNextRow(ImGuiTableRowFlags_None);

									ImGui::TableSetColumnIndex(0);
									{
										ImGui::TextUnformatted(TCHAR_TO_ANSI(*Shader.ShaderClassName));
									}

									ImGui::TableSetColumnIndex(1);
									{
										ImGui::TextUnformatted(TCHAR_TO_ANSI(CrossCompiler::GetFrequencyName(Shader.ShaderType)));
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
											if (ImGui::IsItemHovered())
											{
												ImGui::SetTooltip("Browse to shader file");
											}

											ImGui::SameLine();

											if (FImGui::ImageButtonWithTint("EditFile", EditIcon, 0x8FFFFFFF, 0xFFFFFFFF))
											{
												FPlatformProcess::LaunchFileInDefaultExternalApplication(*Shader.ShaderFilePath);
											}
											if (ImGui::IsItemHovered())
											{
												ImGui::SetTooltip("Edit shader file");
											}

											ImGui::SameLine();

#if 0
											ImGui::BeginDisabled(ImGuiShaderAnalyzer::CanAnalyzeShader(Shader.ShaderType) == false);
											if (FImGui::ImageButtonWithTint("Analyze", AnalyzeIcon, 0x8FFFFFFF, 0xFFFFFFFF))
											{
												ImGuiShaderAnalyzer::ShowShaderStats(Shader.ShaderType, Shader.ShaderEntryName, Shader.ShaderFilePath);
											}
											if (ImGui::IsItemHovered())
											{
												ImGui::SetTooltip("Analyze shader");
											}
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
	}

	IMGUI_REGISTER_STATIC_WIDGET(Initialize, Tick);
}

#endif //#if WITH_IMGUI
