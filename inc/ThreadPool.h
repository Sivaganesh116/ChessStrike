#ifndef __THREAD_POOL_H__
#define __THREAD_POOL_H__

#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <functional>
#include <cassert>

#include "concurrentqueue.h"

class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::atomic<bool> running;
    moodycamel::ConcurrentQueue<uWS::MoveOnlyFunction<void()>> taskQueue;
    
public:
    ThreadPool(size_t numThreads) : running(true) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this]() {
                while (running.load(std::memory_order_acquire)) {
                    uWS::MoveOnlyFunction<void()> task;

                    if (taskQueue.try_dequeue(task)) {
                        task();
                    } else {
                        std::this_thread::yield(); // avoid tight spin
                    }
                }
            });
        }
    }

    void submit(uWS::MoveOnlyFunction<void()> task) {
        assert(running && "ThreadPool not running");
        taskQueue.enqueue(std::move(task));
    }

    void shutdown() {
        running.store(false, std::memory_order_release);
        for (auto& thread : workers) {
            if (thread.joinable())
                thread.join();
        }
    }

    ~ThreadPool() {
        if (running.load()) shutdown();
    }
};

#endif
