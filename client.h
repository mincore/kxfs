/* ===================================================
 * Copyright (C) 2018 chenshuangping All Right Reserved.
 *      Author: mincore@163.com
 *    Filename: agent.h
 *     Created: 2018-06-27 13:45
 * Description:
 * ===================================================
 */
#ifndef _AGENT_H
#define _AGENT_H

#include <thread>
#include <atomic>
#include <map>
#include <list>
#include "utils.h"
#include "msg.h"
#include "cache.h"

struct fuse;
class Cache;

struct Req {
    Msg req;
    Future<PMsg> resp;
};

class Client {
public:
    bool connect(const char *host, uint16_t port);
    void start();
    void stop();

    Future<PMsg> send(
        Msg::Type type,
        const std::vector<const char *> &paths,
        const std::vector<int64_t> &args = {},
        const void *buffer = NULL,
        int size = 0) {
        auto req = make_req(new_id(), type, paths, args, buffer, size);
        queue_req(req.req);
        return req.resp;
    }

    void del_node(const char *path, bool parent_too = true);
    Node get_node(const char *path);

    uint32_t new_id() {
        return id_++;
    }

private:
    void send_reqs();
    void recv_resps();
    void queue_req(const Msg &req);
    Future<PMsg> make_future(uint32_t id);
    Node node_from_msg(const char* path, Msg &msg);

    Req make_req(
        uint32_t id,
        Msg::Type type,
        const std::vector<const char *> &paths,
        const std::vector<int64_t> &args = {},
        const void *buffer = NULL,
        int size = 0);

private:
    int sock_ = -1;

    std::thread send_thread_;
    std::thread recv_thread_;

    std::mutex reqs_mutex_;
    std::condition_variable reqs_cond_;
    std::list<Msg> reqs_;

    std::mutex future_map_mutex_;
    std::multimap<int, Future<PMsg> > future_map_;

    bool quit_ = false;
    Cache cache_;

    std::atomic<uint32_t> id_;
};

#endif
