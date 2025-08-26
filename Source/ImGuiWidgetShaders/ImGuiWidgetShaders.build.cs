// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class ImGuiWidgetShaders : ModuleRules
	{
		public ImGuiWidgetShaders(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Projects",
					"Engine",
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"RHI",
					"RenderCore",
					"Renderer",
				}
			);
		}
	}
}
