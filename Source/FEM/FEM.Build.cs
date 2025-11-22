// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class FEM : ModuleRules
{
	public FEM(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDefinitions.Add("NOMINMAX");

		string PluginPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../.."));
		string FEMLibDir  = Path.Combine(PluginPath, "FEM/ThirdParty/FEMLib/FEMFXBeta/amd_femfx/");

		// Public include paths
		PublicIncludePaths.AddRange(new string[]
		{
			Path.Combine(FEMLibDir, "inc"),
			Path.Combine(FEMLibDir, "inc/Vectormath")
		});

		// Private include paths
		PrivateIncludePaths.AddRange(new string[]
		{
			Path.Combine(FEMLibDir, "inc"),
			Path.Combine(FEMLibDir, "inc/Vectormath")
		});

		// Dependencies
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"ProceduralMeshComponent",
			"RHI",
			"RenderCore",
			"Projects"
		});

		// Link against FEMFX static libraries
		string FEMLibPath = Path.Combine(FEMLibDir, "lib");

		PublicAdditionalLibraries.Add(Path.Combine(FEMLibPath, "AMD_FEMFX.lib"));
		PublicAdditionalLibraries.Add(Path.Combine(FEMLibPath, "sample_task_system.lib"));
	}
}
