// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class ImGuiEditorWidgets : ModuleRules
{
	public ImGuiEditorWidgets(ReadOnlyTargetRules Target) : base(Target)
	{
		//PCHUsage = PCHUsageMode.NoPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"RHI",
				"Core",
				"Engine",
				"SlateCore",
				"RenderCore",
				"CoreUObject",

                "UnrealEd",
                "Blutility",
                "AssetRegistry",
                "MaterialEditor",
                "ShaderCompilerCommon",
            }
		);

        // for using ImGui
        PublicDependencyModuleNames.AddRange(new string[] { "ImGui", "ImGuiRuntime", "ImGuiWidgets" });
	}
}
