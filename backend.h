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

typedef std::function<void(PMsg out)> Reply;
    
static PMsg OutMsg(PMsg in) {
    PMsg out = std::make_shared<Msg>();
    out->head.id = in->head.id;
    out->head.type = in->head.type;
    out->head.crc = 0;
    out->head.size = 0;
    out->head.ret = -1;
    return out;
}

class Backend {
public:
    Backend(): pool_(4) {}

    void set_root(const char *root) {
        root_ = root;
    }

    void process(PMsg in, std::function<void(PMsg out)> reply) {
        pool_.push([=]{
            PMsg out = OutMsg(in);
            out->head.ret = process_msg(in, out);
            out->head.size = out->data.size();
            reply(out);
        });
    }

private:
    int process_msg(PMsg in, PMsg out);
    int impl_hello(PMsg in, PMsg out);
    int impl_keepalive(PMsg in, PMsg out);
    int impl_getattr(PMsg in, PMsg out); 
    int impl_mkdir(PMsg in, PMsg out); 
    int impl_symlink(PMsg in, PMsg out); 
    int impl_unlink(PMsg in, PMsg out); 
    int impl_rmdir(PMsg in, PMsg out); 
    int impl_rename(PMsg in, PMsg out); 
    int impl_chmod(PMsg in, PMsg out); 
    int impl_truncate(PMsg in, PMsg out);
    int impl_utimens(PMsg in, PMsg out); 
    int impl_read(PMsg in, PMsg out); 
    int impl_write(PMsg in, PMsg out); 
    int impl_create(PMsg in, PMsg out); 
    int impl_release(PMsg in, PMsg out); 

private:
    ThreadPool pool_;
    std::string root_;
    std::mutex fdmaps_mutex_;
    std::unordered_map<int, int> fdmaps_;
    int fd_index_ = 1;
};

#endif
