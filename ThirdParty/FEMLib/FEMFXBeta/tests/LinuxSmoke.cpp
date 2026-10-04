#include "AMD_FEMFX.h"
#include "RenderTetAssignment.h"
#include "SampleTaskSystem.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>

void* FmAlignedMalloc(size_t size, size_t alignment)
{
    void* pointer = nullptr;
    return posix_memalign(&pointer, std::max(alignment, sizeof(void*)), size) == 0 ? pointer : nullptr;
}

void FmAlignedFree(void* pointer)
{
    free(pointer);
}

static void Check(bool condition, const char* message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main()
{
    using namespace AMD;
    static_assert(sizeof(FmVector3) == 12, "Scalar vector ABI");
    static_assert(alignof(FmAtomicUint) == 64, "Atomic alignment ABI");

    for (int threadCount : {1, 4})
    {
        SampleInitTaskSystem(threadCount);

        SampleInitTaskSystem(threadCount + 1);
        Check(SampleGetTaskSystemNumThreads() == threadCount, "Active scenes keep their worker count");
        SampleDestroyTaskSystem();
        Check(SampleGetTaskSystemNumThreads() == threadCount, "Second scene release keeps the first scene active");
        Check(SampleGetTaskSystemWorkerIndex() == -1, "Game thread is not a worker");
        FmSyncEvent* nestedEvent = SampleCreateSyncEvent();
        SampleAsyncTask("Parent", [](void* event, int32_t, int32_t)
        {
            SampleAsyncTask("Child", [](void* event, int32_t, int32_t)
            {
                SampleTriggerSyncEvent(event);
            }, event, 0, 1);
        }, nestedEvent, 0, 1);
        SampleWaitForSyncEvent(nestedEvent);
        SampleDestroySyncEvent(nestedEvent);
        SampleWaitForAllThreadsToStart();
        std::atomic<int> finished{0};
        for (int task = 0; task < 64; ++task)
        {
            SampleAsyncTask("Smoke", [](void* data, int32_t, int32_t)
            {
                Check(SampleGetTaskSystemWorkerIndex() >= 0, "Worker index");
                ++*static_cast<std::atomic<int>*>(data);
            }, &finished, 0, 1);
        }
        // Destroy drains queued work and joins all worker threads.
        SampleDestroyTaskSystem();
        Check(SampleGetTaskSystemNumThreads() == 0, "Last scene release stops workers");
        Check(finished == 64, "All submitted tasks completed");
        SampleInitTaskSystem(threadCount);

        FmSceneSetupParams sceneParams;
        sceneParams.maxTetMeshBuffers = 1;
        sceneParams.maxTetMeshes = 1;
        sceneParams.maxDistanceContacts = 64;
        sceneParams.maxVolumeContacts = 8;
        sceneParams.maxVolumeContactVerts = 64;
        sceneParams.maxDeformationConstraints = 8;
        sceneParams.maxBroadPhasePairs = 8;
        sceneParams.maxRigidBodyBroadPhasePairs = 8;
        sceneParams.maxSceneVerts = 4;
        sceneParams.maxTetMeshBufferFeatures = 4;
        sceneParams.numWorkerThreads = threadCount;
        sceneParams.maxConstraintSolverDataSize = FmEstimateSceneConstraintSolverDataSize(sceneParams);
        FmScene* scene = FmCreateScene(sceneParams);
        Check(scene != nullptr, "Scene allocation");
        FmTaskSystemCallbacks callbacks;
        callbacks.GetTaskSystemNumThreads = SampleGetTaskSystemNumThreads;
        callbacks.GetTaskSystemWorkerIndex = SampleGetTaskSystemWorkerIndex;
        callbacks.SubmitAsyncTask = SampleAsyncTask;
        callbacks.CreateSyncEvent = SampleCreateSyncEvent;
        callbacks.DestroySyncEvent = SampleDestroySyncEvent;
        callbacks.WaitForSyncEvent = SampleWaitForSyncEvent;
        callbacks.TriggerSyncEvent = SampleTriggerSyncEvent;
        FmSetSceneTaskSystemCallbacks(scene, callbacks);
        FmSceneControlParams control = FmGetSceneControlParams(*scene);
        control.numThreads = threadCount;
        control.gravityVector = FmVector3(0.0f, -9.81f, 0.0f);
        FmSetSceneControlParams(scene, control);

        FmVector3 positions[] = {FmVector3(0, 0, 0), FmVector3(1, 0, 0), FmVector3(0, 1, 0), FmVector3(0, 0, 1)};
        FmTetVertIds tetIds = {{0, 1, 2, 3}};
        FmArray<uint> incident[4];
        for (auto& vertex : incident)
        {
            FmAddIncidentTetToSet(vertex, 0);
        }
        FmTetMeshBufferBounds bounds;
        FmComputeTetMeshBufferBounds(&bounds, nullptr, nullptr, incident, &tetIds, nullptr, 4, 1, false);
        FmTetMeshBufferSetupParams meshParams;
        meshParams.numVerts = bounds.numVerts;
        meshParams.numTets = bounds.numTets;
        meshParams.numVertIncidentTets = bounds.numVertIncidentTets;
        meshParams.maxVertAdjacentVerts = bounds.maxVertAdjacentVerts;
        meshParams.maxVerts = bounds.maxVerts;
        meshParams.maxExteriorFaces = bounds.maxExteriorFaces;
        meshParams.maxTetMeshes = bounds.maxTetMeshes;
        FmTetMesh* mesh = nullptr;
        FmTetMeshBuffer* buffer = FmCreateTetMeshBuffer(meshParams, nullptr, nullptr, &mesh);
        Check(buffer && mesh, "Tet mesh allocation");
        FmInitVertState(mesh, positions, FmMatrix3::identity(), FmVector3(0, 10, 0));
        FmInitTetState(mesh, &tetIds, FmTetMaterialParams());
        Check(FmComputeMeshConstantMatrices(mesh) > 0.0f, "Positive tet volume");
        Check(FmInitConnectivity(mesh, incident), "Tet connectivity");
        FmSetMassesFromRestDensities(mesh);
        FmSetVertFlags(mesh, 0, FM_VERT_FLAG_KINEMATIC);

        FmBvh* bvh = FmCreateBvh(1);
        FmBuildRestMeshTetBvh(bvh, positions, &tetIds, 1);
        RenderVertTetAssignment assignment;
        ComputeRenderVertTetAssignment(&assignment, positions, &tetIds, *bvh, FmVector3(0.25f));
        Check(assignment.inside && assignment.tetId == 0, "Render vertex tet assignment");
        for (float coordinate : assignment.barycentricCoords)
        {
            Check(std::fabs(coordinate - 0.25f) < 0.001f, "Render barycentric coordinates");
        }
        FmDestroyBvh(bvh);
        Check(FmAddTetMeshBufferToScene(scene, buffer) != FM_INVALID_ID, "Add mesh to scene");
        const float initialY = FmGetVertPosition(*mesh, 2).y;
        for (int step = 0; step < 10; ++step)
        {
            FmUpdateScene(scene, 1.0f / 60.0f);
        }
        const float finalY = FmGetVertPosition(*mesh, 2).y;
        Check(std::isfinite(finalY) && finalY < initialY, "Simulation advances under gravity");
        Check(std::fabs(FmGetVertPosition(*mesh, 0).y - 10.0f) < 0.001f, "Kinematic vertex remains fixed");
        FmRemoveTetMeshBufferFromScene(scene, 0);
        FmDestroyTetMeshBuffer(buffer);
        FmDestroyScene(scene);
        SampleDestroyTaskSystem();
    }
    puts("FEMFX Linux smoke passed: 1/4 worker tasks, render assignments, mesh simulation, teardown");
}
