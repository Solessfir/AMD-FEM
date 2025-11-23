//---------------------------------------------------------------------------------------
//
// Copyright (c) 2019 Advanced Micro Devices, Inc. All rights reserved.
//
//---------------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FEMFXMeshComponent.h"
#include "UObject/UnrealType.h"
#include "FEMCommon.h"
#include "FEMFXScene.generated.h"

class AFEMFXRigidBodyScene;
class AFEMActor;

UCLASS(BlueprintType, Blueprintable, config = Engine, meta = (ShortTooltip = "FEMScene is required to create FEM Meshes. Manages the Buffer data."))
class AFEMFXScene : public AActor
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	bool bIsMultiThreaded;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	int32 MaxTetMeshBuffers;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	int32 MaxTetMeshes;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	int32 MaxRigidBodies;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	int32 MaxDistanceContacts;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	int32 MaxFractureContacts;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	int32 MaxVolumeContacts;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	int32 MaxVolumeContactVerts;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	int32 MaxObjectPairTriPairs;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	int32 MaxObjectPairVolContactVerts;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	int32 MaxGlueConstraints;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	int32 MaxPlaneConstraints;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	int32 MaxRigidBodyAngleConstraints;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	int32 MaxDeformationConstraints;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	int32 MaxBroadPhasePairs;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	int32 MaxUserBroadPhasePairs;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	int32 MaxVerts;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup Parameters")
	int32 MaxJacobianSubmats;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setup Parameters")
	int32 NumWorkerThreads;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEM")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEM")
	bool bAllowTick;

	/** BEGIN AACTOR INTERFACE */
	virtual void Destroyed();

	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Called every frame
	virtual void Tick(float DeltaSeconds) override;
	/** END AACTOR INTERFACE */ 

	bool IsInitialized() { return bIsInitialized; }

	UFUNCTION(BlueprintCallable, Category = "FEM")
	void Initialize();

	UFUNCTION(BlueprintCallable, Category = "FEM")
	bool AllocateNewMesh(UFEMFXMeshComponent* meshComponent);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEM")
	TArray<UFEMFXMeshComponent*> m_ComponentsAllocated;

	AMD::FmScene* GetSceneBuffer();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEM")
	AFEMFXRigidBodyScene* RigidBodyScene;

    //UFUNCTION(BlueprintCallable, Category = "FEM")
    void AddRigidBodyToScene(AMD::FmRigidBody* inRigidBody);

	UFUNCTION(BlueprintCallable, Category = "FEM")
	void AddToResetList(AActor* actor);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEM")
	TArray<AFEMActor*> FEMActors;

	UFUNCTION(BlueprintCallable, Category = "FEM")
	void RemoveActor(AFEMActor* actor);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEM")
	float MaxSteps;
	
	void FreeComponent(UFEMFXMeshComponent* comp);

    UFUNCTION(BlueprintCallable, Category = "FEM")
    void SetAllSleeping();

    UFUNCTION(BlueprintCallable, Category = "FEM")
    void CreateSleepingGroup(const TArray<AActor*>& Actors);

    UFUNCTION(BlueprintCallable, Category = "FEM")
    void SetGroupsCanCollide(int32 i, int32 j, bool canCollide);

    UFUNCTION(BlueprintNativeEvent, CallInEditor, BlueprintCallable, Category = "FEM")
	void ResetScene();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEM")
	FVector minPlaneConstraint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEM")
	FVector maxPlaneConstraint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEMCollision")
	float MaxCollisionDistanceContacts;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEMCollision")
	float MaxDistanceContactsPerObjectPair;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEMCollision")
	float MaxCollisionVolumeContacts;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEMCollision")
	float MaxVolumeContactsPerObjectPair;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FEMCollision")
	float MinContactRelativeVelocity;


	/*bool IsProcessed(FString name);*/

	/*PreProcessedMesh& AddPreProcessedMesh(FString name, PreProcessedMesh mesh);

	PreProcessedMesh& GetPreProcessedMesh(FString name);

	TMap<FString, PreProcessedMesh> FEMPreProcessedMeshes;*/

    TSet<FString> ConditionCheckedMeshes;

    void AddConditionCheckedMesh(FString name);

    bool IsConditionChecked(FString name);

private:
	bool bIsInitialized;

	size_t SceneBufferNumBytes;
	
	uint8_t* SceneBufferMemory;

	AMD::FmScene* AMDFXSceneBuffer;

	/** Brought Over from Eric's FEMFXScene Actor */
	float frameTime;
	float timeElapsed;
    float lastTimestep;

	unsigned int maxTriangles;
	
	void FreeScene();
	/*void UpdateDebugTetMesh();*/
    void UpdateRenderingDataFromFracture();
	void UpdateSimData();
	/** END Brought over from Eric's FEMFXScene Actor */
};
