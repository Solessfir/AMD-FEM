// Copyright Epic Games, Inc. All Rights Reserved.

#include "FEM.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(FEMLog);

void FFEMModule::StartupModule()
{
	const FString PluginDir = IPluginManager::Get().FindPlugin(TEXT("FEM"))->GetBaseDir();
	const FString PluginShaderDir = FPaths::Combine(PluginDir, TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/FEM"), PluginShaderDir);
}

void FFEMModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FFEMModule, FEM)
