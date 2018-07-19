/* ===================================================
 * Copyright (C) 2018 chenshuangping All Right Reserved.
 *      Author: mincore@163.com
 *    Filename: task.h
 *     Created: 2018-07-13 14:29
 * Description:
 * ===================================================
 */
#ifndef _TASK_H
#define _TASK_H

#include <vector>
#include <deque>
#include <set>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

typedef std::unique_lock<std::mutex> Lock;
typedef std::function<void(void)> Task;

template<class T>
class Queue {
public:
    void push(const T& t) {
        Lock lk(mutex_);
        list_.push_back(t);
        cond_.notify_one();
    }

    T pop() {
        Lock lk(mutex_);
        cond_.wait(lk, [this]{ return !list_.empty(); });
        T t = list_.front();
        list_.pop_front();
        return t;
    }

private:
    std::mutex mutex_;
    std::condition_variable cond_;
    std::deque<T> list_;
};

class ThreadPool;
class TaskQueue {
public:
    TaskQueue(ThreadPool *pool): pool_(pool) {}
    void push(const Task& task);

private:
    void run(const Task& task);

private:
    ThreadPool *pool_;
    bool sched_ = false;
    std::mutex mutex_;
    std::deque<Task> list_;
};

class ThreadPool {
public:
    ThreadPool(int nthread = 1) {
        for (int i=0; i<nthread; i++) {
            threads_.push_back(std::thread([this] {
                while (1) { queue_.pop()(); }
            }));
        }
    }

    ~ThreadPool() {
        for (auto &thread : threads_)
            thread.join();
    }

    void push(const Task& task) {
        queue_.push(task);
    }

    TaskQueue* make_taskq() {
        TaskQueue *q = new TaskQueue(this);
        Lock lk(taskq_mutex_);
        taskq_.insert(q);
        return q;
    }

    void remove_taskq(TaskQueue* q) {
        Lock lk(taskq_mutex_);
        taskq_.erase(q);
    }

private:
    std::vector<std::thread> threads_;
    Queue<Task> queue_;

    std::mutex taskq_mutex_;
    std::set<TaskQueue*> taskq_;
};

#endif
