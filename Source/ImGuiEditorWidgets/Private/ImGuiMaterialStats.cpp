// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#include "ImGuiStaticWidget.h"

#if WITH_IMGUI

#include "ImGuiEditorWidgets.h"

#include "ShaderCompiler.h"
#include "Materials/Material.h"
#include "MaterialStatsCommon.h"
#include "Materials/MaterialInstance.h"
#include "DataDrivenShaderPlatformInfo.h"

UE_DISABLE_OPTIMIZATION

namespace ImGuiMaterialStats
{
	static const EShaderPlatform PreviewShaderPlatform = EShaderPlatform::SP_PCD3D_SM6;

	struct FShaderStatsData
	{
		FString EntryName;
		FString ShaderName;
		FString ShaderDumpFilePath;
		int32	NumInstructions;
	};

	struct FMaterialStatsData
	{
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
		static FString MakeShaderName(FString VFName, FString ShaderEntryName)
		{
			static IConsoleVariable* DumpShaderDebugInfoShortCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DumpShaderDebugShortNames"));			
			if (DumpShaderDebugInfoShortCVar && (DumpShaderDebugInfoShortCVar->GetInt() == 1))
			{
				// shorten name based on: GlobalBeginCompileShader()

				if (VFName[0] == TCHAR('F') || VFName[0] == TCHAR('T'))
				{
					VFName.RemoveAt(0);
				}
				VFName.ReplaceInline(TEXT("VertexFactory"), TEXT("VF"));
				VFName.ReplaceInline(TEXT("GPUSkinAPEXCloth"), TEXT("APEX"));
				VFName.ReplaceInline(TEXT("true"), TEXT("_1"));
				VFName.ReplaceInline(TEXT("false"), TEXT("_0"));

				if (ShaderEntryName[0] == TCHAR('F') || ShaderEntryName[0] == TCHAR('T'))
				{
					ShaderEntryName.RemoveAt(0);
				}
			}
			FString ShaderName = VFName / ShaderEntryName;
			return ShaderName;
		}
		static FString SanitizeShaderDumpFilepath(const FString& FilePath)
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

		void PopulateStats()
		{
			check(Shaders.IsEmpty());

			const FMaterialShaderMap* MaterialShaderMap = Resource->GetGameThreadShaderMap();
			if (MaterialShaderMap != nullptr)
			{				
				TMap<FShaderId, TShaderRef<FShader>> ShaderMap;
				MaterialShaderMap->GetShaderList(ShaderMap);

				const FString ResourceUniqueName = ShortenShaderDebugName(Resource->GetUniqueAssetName(PreviewShaderPlatform, MaterialShaderMap->GetShaderMapId()));				
				const FString ShaderPlatformName = FDataDrivenShaderPlatformInfo::GetName(PreviewShaderPlatform).ToString();
				ShaderOutputBaseDir = GShaderCompilingManager->GetAbsoluteShaderDebugInfoDirectory() / ShaderPlatformName / ResourceUniqueName;

				Shaders.Reserve(ShaderMap.Num());
				for (const auto& Entry : ShaderMap)
				{
					FShaderType* EntryShader = Entry.Value.GetType();
					FVertexFactoryType* VertexFactory = Entry.Value.GetVertexFactoryType();
					if (VertexFactory)
					{
						FString VertexFactoryName = VertexFactory->GetName();
						FString ShaderEntryName = EntryShader->GetName();
						const FString ShaderName = MakeShaderName(VertexFactoryName, ShaderEntryName);

						FString ShaderFileName = EntryShader->GetShaderFilename();
						int32 SlashIndex;
						if (ShaderFileName.FindLastChar(TEXT('/'), SlashIndex))
						{
							ShaderFileName = ShaderFileName.RightChop(SlashIndex + 1);
						}
						
						const FString DebugExtensionStr = FString::Printf(TEXT("_%08x%08x"), MaterialShaderMap->GetShaderMapId().BaseMaterialId.A, MaterialShaderMap->GetShaderMapId().BaseMaterialId.B);
						FString DebugGroupName = Resource->GetUniqueAssetName(PreviewShaderPlatform, MaterialShaderMap->GetShaderMapId()) / LexToString(Resource->GetQualityLevel()) / Entry.Key.ShaderPipelineName.GetDebugString().String.Get() / ShaderName;
						DebugGroupName = ShortenShaderDebugName(DebugGroupName);
						
						FString ShaderDumpFilePath = GShaderCompilingManager->GetAbsoluteShaderDebugInfoDirectory() / ShaderPlatformName / DebugGroupName / FString::Printf(TEXT("%i%s"), Entry.Key.PermutationId, *DebugExtensionStr) / ShaderFileName;
						ShaderDumpFilePath = SanitizeShaderDumpFilepath(ShaderDumpFilePath);

						const int32 ShaderIndex = Shaders.AddDefaulted();
						Shaders[ShaderIndex].EntryName = ShaderEntryName;
						Shaders[ShaderIndex].ShaderName = ShaderName;
						Shaders[ShaderIndex].ShaderDumpFilePath = ShaderDumpFilePath;
						Shaders[ShaderIndex].NumInstructions = Entry.Value.GetShader()->GetNumInstructions();
						
						ShaderVFLookup.FindOrAdd(VertexFactoryName).Add(ShaderIndex);

						IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
						if (PlatformFile.FileExists(*ShaderDumpFilePath))
						{
							ActiveShaderIndices.AddUnique(ShaderIndex);
						}
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

		if (ImGui::Begin("Material Stats", nullptr, ImGuiWindowFlags_None))
		{
			static IConsoleVariable* DumpShaderInfoCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DumpShaderDebugInfo"));
			static IConsoleVariable* DumpShaderShortNamesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DumpShaderDebugShortNames"));
			static int32 DumpShaderInfoCVarRestoreValue = INDEX_NONE;
			static bool bIsCompilingPermutations = false;
			
			static FImGuiAssetPicker<UMaterial> MaterialPicker;
			static TWeakObjectPtr<UMaterial> SelectedMaterial;

			UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
			const FImGuiImageBindingParams WarningIcon = ImGuiSubsystem->RegisterOneFrameResource(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Warning").GetIcon(), FVector2D(ImGui::GetFontSize()), 1.f);
			const FImGuiImageBindingParams BrowseIcon = ImGuiSubsystem->RegisterOneFrameResource(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Search").GetIcon(), FVector2D(ImGui::GetFontSize()), 1.f);
			const FImGuiImageBindingParams EditIcon = ImGuiSubsystem->RegisterOneFrameResource(FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Edit").GetIcon(), FVector2D(ImGui::GetFontSize()), 1.f);

			if (DumpShaderInfoCVar)
			{
				if (DumpShaderInfoCVar->GetInt() == 1)
				{
					ImGui::Image(WarningIcon.Id, WarningIcon.Size, WarningIcon.UV0, WarningIcon.UV1, ImVec4(1.f, 0.721568627f, 0.f, 1.f));
					ImGui::SameLine();
					ImGui::TextUnformatted("Shader debug data enabled...");
				}
			}

			if (DumpShaderShortNamesCVar)
			{
				if (DumpShaderShortNamesCVar->GetInt() == 0)
				{
					ImGui::Image(WarningIcon.Id, WarningIcon.Size, WarningIcon.UV0, WarningIcon.UV1, ImVec4(1.f, 0.721568627f, 0.f, 1.f));
					ImGui::SameLine();
					ImGui::TextUnformatted("Browse/Edit functions maybe not work properly without 'r.DumpShaderDebugShortNames' enabled");
				}
			}
			
			ImGui::Separator();
			
			if (MaterialPicker.Draw("Selected Material", SelectedMaterial))
			{
				Reset();
			}

			ImGui::Separator();

			// stats collection
			{
				bool bButtonDisabled = (bIsCompilingPermutations || !SelectedMaterial.IsValid());
				if (bButtonDisabled)
				{
					ImGui::BeginDisabled();
				}

				if (ImGui::Button(bIsCompilingPermutations ? "Compiling..." : "Gather Stats"))
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

					const FString UniqueName = FString::Printf(TEXT("%s_%s"), *SelectedMaterial->GetName(), * FGuid::NewGuid().ToString());
					const FName Name = FName(*UniqueName);
					MaterialStats.DuplicatedMaterial = (UMaterial*)StaticDuplicateObject(SelectedMaterial.Get(), GetTransientPackage(), Name);
					UMaterial* MaterialToUse = MaterialStats.DuplicatedMaterial;
					if (!MaterialToUse->IsRooted())
					{
						MaterialToUse->AddToRoot();
					}
					auto& Resource = MaterialStats.Resource;
					Resource = new FMaterialResourceStats();
					Resource->SetMaterial(MaterialToUse, nullptr, ERHIFeatureLevel::SM6, EMaterialQualityLevel::Num);
					Resource->CacheShaders(PreviewShaderPlatform, EMaterialShaderPrecompileMode::Default);
				}

				if (bButtonDisabled)
				{
					ImGui::EndDisabled();
				}
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

					MaterialStats.PopulateStats();
				}
			}

			if (!bIsCompilingPermutations && !MaterialStats.ShaderVFLookup.IsEmpty())
			{
				if (ImGui::BeginTabBar("Shader"))
				{
					for (const auto& [VFName, ShaderIndices] : MaterialStats.ShaderVFLookup)
					{
						if (ImGui::BeginTabItem(TCHAR_TO_ANSI(*VFName)))
						{
							for (int32 ShaderIndex : ShaderIndices)
							{
								const auto& Shader = MaterialStats.Shaders[ShaderIndex];

								ImGui::TextUnformatted(TCHAR_TO_ANSI(*Shader.EntryName));

								IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
								if (MaterialStats.ActiveShaderIndices.Contains(ShaderIndex))
								{
									ImGui::SameLine();

									ImGui::PushID(HashCombine(GetTypeHash(Shader.ShaderName), PointerHash("Browse")));
									if (ImGui::ImageButton("BrowseToDir", BrowseIcon.Id, BrowseIcon.Size, BrowseIcon.UV0, BrowseIcon.UV1))
									{
										FPlatformProcess::ExploreFolder(*Shader.ShaderDumpFilePath);
									}
									if (ImGui::IsItemHovered())
									{
										ImGui::SetTooltip("Browse to shader file");
									}
									ImGui::PopID();

									ImGui::SameLine();

									ImGui::PushID(HashCombine(GetTypeHash(Shader.ShaderName), PointerHash("Edit")));
									if (ImGui::ImageButton("EditFile", EditIcon.Id, EditIcon.Size, EditIcon.UV0, EditIcon.UV1))
									{
										FPlatformProcess::LaunchFileInDefaultExternalApplication(*Shader.ShaderDumpFilePath);
									}
									if (ImGui::IsItemHovered())
									{
										ImGui::SetTooltip("Edit shader file");
									}
									ImGui::PopID();

									ImGui::SameLine();

									ImGui::Text("NumInstructions: %i", Shader.NumInstructions);
								}
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

UE_ENABLE_OPTIMIZATION

#endif //#if WITH_IMGUI
