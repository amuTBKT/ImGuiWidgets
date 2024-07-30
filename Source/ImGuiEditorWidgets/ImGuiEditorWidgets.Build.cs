// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class ImGuiEditorWidgets : ModuleRules
{
	public ImGuiEditorWidgets(ReadOnlyTargetRules Target) : base(Target)
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
		PublicDependencyModuleNames.AddRange(new string[] { "ImGuiWidgets" });

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
					"AssetRegistry",
					"MaterialEditor",
					"Blutility",
				}
			);
		}
	}
}
