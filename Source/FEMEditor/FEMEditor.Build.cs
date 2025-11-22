//---------------------------------------------------------------------------------------
// Copyright (c) 2019 Advanced Micro Devices, Inc.
//---------------------------------------------------------------------------------------

using UnrealBuildTool;

public class FEMEditor : ModuleRules
{
	public FEMEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Dependencies
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"FEM",
			"Core",
			"CoreUObject",
			"Engine",
			"ProceduralMeshComponent",
			"UnrealEd",
			"Slate",
			"SlateCore",
			"AssetTools",
			"Json",
			"JsonUtilities",
			"EditorStyle",
			"MainFrame",
			"InputCore"
		});
	}
}
