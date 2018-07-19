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
#include <list>
#include "utils.h"
#include "msg.h"
#include "cache.h"

struct fuse;
class Cache;

struct Req {
    Msg req;
    Future<Msg> resp;
};

class Client {
public:
    ~Client() {
        if (-1 != sock_) {
            exit();
        }
    }

    bool start(const char *host, uint16_t port);

    Future<Msg> send(
        MSG_TYPE type,
        const std::vector<const char *> &paths,
        const std::vector<int64_t> &args = {},
        uint64_t *fh = NULL,
        const void *buffer = NULL,
        int size = 0) {
        auto req = make_req(fh ? (uint32_t)(*fh): new_id(), type, paths, args, buffer, size);
        queue_req(req.req);
        return req.resp;
    }
    void del_future(uint32_t id);

    void del_node(const char *path, bool parent_too = true);
    Node get_node(const char *path);

    uint32_t new_id() {
        return id_++;
    }

private:
    void send_reqs();
    void recv_resps();
    void queue_req(const Msg &req);
    Future<Msg> make_future(uint32_t id);

    Req make_req(
        uint32_t id,
        MSG_TYPE type,
        const std::vector<const char *> &paths,
        const std::vector<int64_t> &args = {},
        const void *buffer = NULL,
        int size = 0);

    void exit();

private:
    int sock_ = -1;

    std::mutex reqs_mutex_;
    std::condition_variable reqs_cond_;
    std::list<Msg> reqs_;

    std::mutex future_map_mutex_;
    std::multimap<int, Future<Msg> > future_map_;

    bool quit_ = false;
    Cache cache_;

    std::atomic<uint32_t> id_;
};

#endif
