// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Diagnostics;

namespace UnrealBuildTool.Rules
{
	public class FEM : ModuleRules
	{
		public FEM(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			Definitions.Add("NOMINMAX");
			
			string PluginPath = Path.Combine(ModuleDirectory, "../../../");
			string FEMLibDir = Path.Combine(PluginPath, "FEM/ThirdParty/FEMLib/FEMFXBeta/amd_femfx/");
			//string MetisDir = Path.Combine(PluginPath, "FEM/ThirdParty/FEMLib/FEMFXBeta/external/metis-5.1.0/");
			
			PublicSystemIncludePaths.AddRange(
				new string[] {
					FEMLibDir + "inc/",
					FEMLibDir + "src/",
					FEMLibDir + "inc/Vectormath/",
					//MetisDir + "include/",
					//MetisDir + "libmetis/",
					//MetisDir + "GKlib/",
					//MetisDir + "programs/",
                    "Developer/FEM/Classes/"
				}
			);
			
			string FEMLibPath = FEMLibDir + "lib/";
			PublicLibraryPaths.Add(FEMLibPath);
			PublicAdditionalLibraries.Add("AMD_FEMFX.lib");
            PublicAdditionalLibraries.Add("sample_task_system.lib");
            //PublicAdditionalLibraries.Add("tbb.lib");
            
			//PublicLibraryPaths.Add(MetisDir + "libmetis/Release");
			//PublicAdditionalLibraries.Add("metis.lib");

			PublicIncludePaths.AddRange(
				new string[] {
					"Developer/FEM/Classes"
				});

			PrivateIncludePaths.AddRange(
				new string[] {
					"Developer/FEM/Private",
					"MainFrame",
				});

			//AddEngineThirdPartyPrivateStaticDependencies(Target, "FEMLib");
			//
			//RuntimeDependencies.Add(new RuntimeDependency(TBBDir + "tbb.lib"));
			//RuntimeDependencies.Add(new RuntimeDependency(FEMLibPath + "AMD_FEMFX.lib"));
			//RuntimeDependencies.Add(new RuntimeDependency(MetisDir + "libmetis/Release/metis.lib"));

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"FEMEditor",
					// ... add private dependencies that you statically link with here ...
				}
				);
			}


			PrivateIncludePaths.Add(Path.Combine(FEMLibDir, "inc/"));
			PrivateIncludePaths.Add(Path.Combine(FEMLibDir, "inc/Vectormath"));

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"ProceduralMeshComponent",
					"RHI",
					"RenderCore",
					"ShaderCore"
				});
		}
	}
}
