//---------------------------------------------------------------------------------------
//
// Copyright (c) 2019 Advanced Micro Devices, Inc. All rights reserved.
//
//---------------------------------------------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "FEMActor.h"
#include "FEMBlockActor.generated.h"

UCLASS(BlueprintType, Category="FEM")
class AFEMBlockActor : public AFEMActor
{
	GENERATED_BODY()

public:
	AFEMBlockActor();
};
