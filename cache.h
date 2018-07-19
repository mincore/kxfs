/* ===================================================
 * Copyright (C) 2018 chenshuangping All Right Reserved.
 *      Author: mincore@163.com
 *    Filename: cache.h
 *     Created: 2018-06-28 12:21
 * Description:
 * ===================================================
 */
#ifndef _CACHE_H
#define _CACHE_H

#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <mutex>
#include "utils.h"

#define TIMEOUT 60

struct Attr {
    int32_t mode;
    int32_t mtime;
    int64_t size;
    std::string link;
};

struct Entry {
    std::string name;
    int32_t mode;
};

struct _Node {
    Attr attr;
    std::vector<Entry> entries;
    uint32_t vaild_time;
};
typedef std::shared_ptr<_Node> Node;

static inline Node make_node() {
    return std::make_shared<_Node>();
}

class Cache {
public:
    void add_node(const std::string &path, const Node &node) {
        std::unique_lock<std::mutex> lk(node_map_mutex_);
        node_map_[path] = node;
        node_map_[path]->vaild_time = now_sec() + TIMEOUT;
    }

    void del_node(const std::string &path) {
        std::unique_lock<std::mutex> lk(node_map_mutex_);
        node_map_.erase(path);
    }

    Node get_node(const std::string &path) {
        std::unique_lock<std::mutex> lk(node_map_mutex_);
        auto it = node_map_.find(path);
        if (it == node_map_.end() || now_sec() > it->second->vaild_time) {
            return Node();
        }
        return it->second;
    }

private:
    std::mutex node_map_mutex_;
    std::unordered_map<std::string, Node> node_map_;
};

#endif
