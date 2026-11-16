#include "../src/cat_decode_queue.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <vector>

void Check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
int main(){
    using Queue=cat::movie::DecodeQueue<int>;
    std::mutex mutex;std::condition_variable condition;bool started=false,release=false;
    std::atomic<unsigned> decodes{0},abandonedDecodes{0};
    Queue queue;
    auto first=queue.Prepare(1,[&]{
        ++decodes;std::unique_lock<std::mutex> lock(mutex);started=true;condition.notify_all();
        condition.wait(lock,[&]{return release;});return std::make_shared<int>(42);
    });
    {std::unique_lock<std::mutex> lock(mutex);Check(condition.wait_for(lock,std::chrono::seconds(3),[&]{return started;}),"decoder did not start");}
    auto second=queue.Prepare(1,[&]{++decodes;return std::make_shared<int>(0);});
    Check(first==second,"two cats did not share pending decode");
    auto abandoned=queue.Prepare(2,[&]{++abandonedDecodes;return std::make_shared<int>(9);});
    // This is deliberately before releasing the blocked decoder. A future's
    // blocking destructor would deadlock here, as the old UI reset did.
    first.reset();abandoned.reset();
    Check(!second->ready.load(),"unfinished source was exposed as ready");
    auto following=queue.Prepare(3,[]{return std::make_shared<int>(7);});
    {std::lock_guard<std::mutex> lock(mutex);release=true;}condition.notify_all();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!following->ready.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    Check(following->ready.load(std::memory_order_acquire),"queued replacement was not decoded");
    Check(second->ready.load(std::memory_order_acquire)&&*second->value==42,"shared source was corrupted");
    Check(*following->value==7&&decodes==1&&abandonedDecodes==0,"cancelled queue work ran or duplicate decoded");
    auto cached=queue.Prepare(1,[]{return std::make_shared<int>(0);});
    Check(cached==second&&*cached->value==42,"completed source was not reused");
    std::weak_ptr<Queue::Job> released=second;cached.reset();second.reset();
    // The worker has advanced to the following job, so cache ownership must be weak.
    Check(released.expired(),"completed movies were retained by cache");
    std::cout<<"decode queue: shared in-flight/cache, nonblocking cancel, skipped abandoned work, weak release passed\n";
}
