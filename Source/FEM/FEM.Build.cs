// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class FEM : ModuleRules
{
	public FEM(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDefinitions.Add("NOMINMAX");
		PublicDefinitions.Add("FEMFX_USE_UNREAL_MATH=1");

		string PluginDirectory = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
		string ThirdPartyDir = Path.Combine(PluginDirectory, "ThirdParty");
		string FEMFXDir = Path.Combine(ThirdPartyDir, "FEMLib", "FEMFXBeta", "amd_femfx");

		// Public include paths
		PublicIncludePaths.AddRange(new string[]
		{
			Path.Combine(FEMFXDir, "inc"),
			Path.Combine(FEMFXDir, "inc/Vectormath")
		});

		// Private include paths
		PrivateIncludePaths.AddRange(new string[]
		{
			Path.Combine(FEMFXDir, "inc"),
			Path.Combine(FEMFXDir, "inc/Vectormath")
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
		string FEMLibPath = Path.Combine(FEMFXDir, "lib");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(FEMLibPath, "AMD_FEMFX.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(FEMLibPath, "sample_task_system.lib"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			if (Target.Architecture != "x86_64-unknown-linux-gnu")
			{
				throw new BuildException("FEM supports Linux x86_64 only.");
			}
			string LinuxLibPath = Path.Combine(FEMLibPath, "Linux", Target.Architecture);
			PublicAdditionalLibraries.Add(Path.Combine(LinuxLibPath, "libAMD_FEMFX.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LinuxLibPath, "libsample_task_system.a"));
			PublicSystemLibraries.Add("pthread");
		}
	}
}
