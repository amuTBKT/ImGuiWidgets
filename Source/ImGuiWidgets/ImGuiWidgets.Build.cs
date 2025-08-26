// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class ImGuiWidgets : ModuleRules
{
	public ImGuiWidgets(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine",
				"Projects",
				"SlateCore",
				"CoreUObject",
				"AssetRegistry",

				"RHI",
				"Renderer",
				"RenderCore",
				"ImGuiWidgetShaders",
			}
		);

		// for using ImGui
		PublicDependencyModuleNames.AddRange(new string[] { "ImGui", "ImGuiRuntime" });

		// for using Niagara
		PrivateDependencyModuleNames.Add("Niagara");

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
					"ContentBrowser",
					"CollectionManager",
				}
			);
		}
	}
}
