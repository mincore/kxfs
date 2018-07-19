/* ===================================================
 * Copyright (C) 2018 chenshuangping All Right Reserved.
 *      Author: mincore@163.com
 *    Filename: backend.h
 *     Created: 2018-06-29 14:48
 * Description:
 * ===================================================
 */
#ifndef _BACKEND_H
#define _BACKEND_H

#include <unordered_map>
#include "task.h"
#include "msg.h"
#include "log.h"

typedef std::function<void(const Msg &out)> Reply;
typedef bool (*cmd_func)(const Msg &in, Reply reply);
extern cmd_func get_cmd_func(uint16_t type);

class Backend {
public:
    Backend(): pool_(1) {}

    void process(const Msg &in, std::function<void(const Msg &out)> reply) {
        cmd_func func = get_cmd_func(in.head.type);
        if (!func) {
            LOG_ERROR("INVALIED TYPE:%d\n", in.head.type);
            return;
        }

        auto f = [=] { func(in, reply); };
        auto q = get_taskq(in);

        if (q)
            q->push(f);
        else
            pool_.push(f);
    }

private:
    TaskQueue *get_taskq(const Msg& in) {
        if (in.head.type != MSG_WRITE)
            return NULL;

        Lock lk(taskq_map_lock_);
        if (taskq_map_.find(in.head.id) == taskq_map_.end()) {
            taskq_map_[in.head.id] = pool_.make_taskq();
        }
        return taskq_map_[in.head.id];
    }

    void remove_taskq(uint32_t id) {
        TaskQueue *q = NULL;
        {
            Lock lk(taskq_map_lock_);
            auto it = taskq_map_.find(id);
            if (it != taskq_map_.end()) {
                q = it->second;
                taskq_map_.erase(it);
            }
        }
        if (q)
            pool_.remove_taskq(q);
    }

private:
    ThreadPool pool_;
    std::mutex taskq_map_lock_;
    std::unordered_map<uint32_t, TaskQueue*> taskq_map_;
};

#endif
