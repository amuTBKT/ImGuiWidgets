// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class ImGuiWidgets : ModuleRules
{
	public ImGuiWidgets(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"RHI",
				"Core",
				"Engine",
				"SlateCore",
				"RenderCore",
				"CoreUObject",
            }
		);

		// for using ImGui
        PrivateDependencyModuleNames.AddRange(new string[] { "ImGui", "ImGuiRuntime" });

		// for using Niagara
		PrivateDependencyModuleNames.Add("Niagara");

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "UnrealEd",
                    "AssetRegistry",
                }
            );
        }
    }
}
