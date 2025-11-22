// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "IFEM.h"


DEFINE_LOG_CATEGORY(FEMLog);


class FFEM : public IFEM
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override
	{

	}

	virtual void ShutdownModule() override
	{

	}

private:


};

IMPLEMENT_MODULE( FFEM, FEM)
