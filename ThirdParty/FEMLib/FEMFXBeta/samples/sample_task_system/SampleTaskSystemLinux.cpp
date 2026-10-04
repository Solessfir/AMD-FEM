// Linux implementation of the AMD sample task system interface. See LICENSE.txt.

#include "SampleTaskSystem.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace AMD
{
    namespace
    {
        struct Task
        {
            FmTaskFuncCallback func;
            void* data;
            int32_t begin;
            int32_t end;
        };

        struct SyncEvent
        {
            std::mutex mutex;
            std::condition_variable changed;
            bool triggered = false;
        };

        std::mutex taskMutex;
        std::mutex lifecycleMutex;
        std::condition_variable tasksReady;
        std::condition_variable workersStarted;
        std::deque<Task> tasks;
        std::vector<std::thread> workers;
        bool stopping = false;
        int owners = 0;
        int startedWorkers = 0;
        std::atomic<int> numWorkers{0};
        thread_local int workerIndex = -1;
    }

    int SampleGetTaskSystemDefaultNumThreads()
    {
        return std::max(1u, std::thread::hardware_concurrency());
    }

    void SampleInitTaskSystem(int numThreads)
    {
        std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex);
        if (owners++ > 0)
        {
            return;
        }
        stopping = false;
        startedWorkers = 0;
        numThreads = numThreads > 0 ? numThreads : SampleGetTaskSystemDefaultNumThreads();
        numWorkers = numThreads;
        for (int index = 0; index < numThreads; ++index)
        {
            workers.emplace_back([index]
            {
                workerIndex = index;
                {
                    std::lock_guard<std::mutex> lock(taskMutex);
                    ++startedWorkers;
                    workersStarted.notify_all();
                }
                for (;;)
                {
                    Task task;
                    {
                        std::unique_lock<std::mutex> lock(taskMutex);
                        tasksReady.wait(lock, [] { return stopping || !tasks.empty(); });
                        if (tasks.empty())
                        {
                            return;
                        }
                        task = tasks.front();
                        tasks.pop_front();
                    }
                    task.func(task.data, task.begin, task.end);
                }
            });
        }
    }

    void SampleDestroyTaskSystem()
    {
        std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex);
        if (owners == 0 || --owners > 0)
        {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(taskMutex);
            stopping = true;
        }
        tasksReady.notify_all();
        for (std::thread& worker : workers)
        {
            worker.join();
        }
        workers.clear();
        numWorkers = 0;
    }

    int SampleGetTaskSystemNumThreads()
    {
        return numWorkers;
    }

    void SampleWaitForAllThreadsToStart()
    {
        std::unique_lock<std::mutex> lock(taskMutex);
        workersStarted.wait(lock, [] { return startedWorkers == numWorkers; });
    }

    int SampleGetTaskSystemWorkerIndex()
    {
        return workerIndex;
    }

    void SampleAsyncTask(const char*, FmTaskFuncCallback func, void* data, int32_t begin, int32_t end)
    {
        {
            std::lock_guard<std::mutex> lock(taskMutex);
            tasks.push_back({func, data, begin, end});
        }
        tasksReady.notify_one();
    }

    FmSyncEvent* SampleCreateSyncEvent()
    {
        return new SyncEvent();
    }

    void SampleDestroySyncEvent(FmSyncEvent* event)
    {
        delete static_cast<SyncEvent*>(event);
    }

    void SampleWaitForSyncEvent(FmSyncEvent* event)
    {
        SyncEvent& sync = *static_cast<SyncEvent*>(event);
        std::unique_lock<std::mutex> lock(sync.mutex);
        sync.changed.wait(lock, [&sync] { return sync.triggered; });
    }

    void SampleTriggerSyncEvent(FmSyncEvent* event)
    {
        SyncEvent& sync = *static_cast<SyncEvent*>(event);
        std::lock_guard<std::mutex> lock(sync.mutex);
        sync.triggered = true;
        sync.changed.notify_all();
    }
}
