#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace cat::movie {
// One decoder for both actors. Owners can release or replace an unfinished
// request without waiting on the UI thread. Queued abandoned work is skipped;
// simultaneous requests for one resource share both decode and immutable data.
template<class Value> class DecodeQueue {
public:
    struct Job {
        std::atomic<bool> ready{false};
        std::shared_ptr<Value> value;
    };
    using Request=std::shared_ptr<Job>;
    DecodeQueue():worker_([this]{Run();}){}
    DecodeQueue(const DecodeQueue&)=delete;
    DecodeQueue& operator=(const DecodeQueue&)=delete;
    ~DecodeQueue(){
        {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;tasks_.clear();}
        signal_.notify_one();worker_.join();
    }
    Request Prepare(int key,std::function<std::shared_ptr<Value>()> decode) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(auto previous=cache_[key].lock())return previous;
        auto job=std::make_shared<Job>();cache_[key]=job;
        tasks_.push_back({job,std::move(decode)});signal_.notify_one();return job;
    }
private:
    struct Task {std::weak_ptr<Job> job;std::function<std::shared_ptr<Value>()> decode;};
    std::mutex mutex_;std::condition_variable signal_;
    std::map<int,std::weak_ptr<Job>> cache_;
    std::deque<Task> tasks_;bool stopping_=false;std::thread worker_;
    void Run() {
        for(;;){Task task;
            {std::unique_lock<std::mutex> lock(mutex_);signal_.wait(lock,[this]{return stopping_||!tasks_.empty();});
                if(stopping_)return;task=std::move(tasks_.front());tasks_.pop_front();}
            if(auto job=task.job.lock()) {
                try {job->value=task.decode();}catch(...){job->value.reset();}
                job->ready.store(true,std::memory_order_release);
            }
        }
    }
};
}
