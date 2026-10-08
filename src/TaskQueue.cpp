#include "TaskQueue.h"

namespace OML{
    TaskQueue::TaskQueue()
        : mPending(0),
          mStopping(false),
          mThread(&TaskQueue::run, this) {
    }

    TaskQueue::~TaskQueue(){
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mStopping = true;
            mTasks.clear();
        }
        mCondition.notify_all();
        mThread.join();
    }

    void TaskQueue::post(std::function<void()> task){
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mTasks.push_back(std::move(task));
            mPending++;
        }
        mCondition.notify_one();
    }

    bool TaskQueue::busy() const{
        return mPending.load() > 0;
    }

    void TaskQueue::run(){
        while(true){
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mMutex);
                mCondition.wait(lock, [this]{ return mStopping || !mTasks.empty(); });
                if(mStopping) return;
                task = std::move(mTasks.front());
                mTasks.pop_front();
            }
            task();
            mPending--;
        }
    }
}
