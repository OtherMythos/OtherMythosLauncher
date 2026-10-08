#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace OML{
    //One background thread running posted tasks in order.
    class TaskQueue{
    public:
        TaskQueue();
        //Waits for the running task to finish; tasks not yet started are dropped.
        ~TaskQueue();
        TaskQueue(const TaskQueue&) = delete;
        TaskQueue& operator=(const TaskQueue&) = delete;

        void post(std::function<void()> task);
        //Whether a task is running or waiting.
        bool busy() const;

    private:
        void run();

        std::mutex mMutex;
        std::condition_variable mCondition;
        std::deque<std::function<void()>> mTasks;
        std::atomic<int> mPending;
        bool mStopping;
        std::thread mThread;
    };
}
