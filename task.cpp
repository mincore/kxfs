/* ===================================================
 * Copyright (C) 2018 chenshuangping All Right Reserved.
 *      Author: mincore@163.com
 *    Filename: task.cpp
 *     Created: 2018-07-13 14:51
 * Description:
 * ===================================================
 */
#include "task.h"

void TaskQueue::push(const Task& task) {
    Lock lk(mutex_);
    if (sched_) {
        list_.push_back(task);
        return;
    }
    sched_ = true;
    pool_->push([task, this] {
        run(task);
    });
}

void TaskQueue::run(const Task& task) {
    task();
    Lock lk(mutex_);
    if (list_.empty()) {
        sched_ = false;
        return;
    }

    Task next = list_.front();
    list_.pop_front();
    pool_->push([next, this] {
        run(next);
    });
}

