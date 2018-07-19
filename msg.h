/* ===================================================
 * Copyright (C) 2018 chenshuangping All Right Reserved.
 *      Author: mincore@163.com
 *    Filename: msg.h
 *     Created: 2018-06-29 11:30
 * Description:
 * ===================================================
 */
#ifndef _MSG_H
#define _MSG_H

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <memory>
#include <atomic>
#include "kxfs.pb.h"
#include "log.h"
#include "utils.h"

enum MSG_TYPE {
    MSG_KEEPALVE,
    MSG_GETATTR,
    MSG_MKDIR,
    MSG_SYMLINK,
    MSG_UNLINK,
    MSG_RMDIR,
    MSG_RENAME,
    MSG_CHMOD,
    MSG_TRUNCATE,
    MSG_UTIMENS,
    MSG_CREATE,
    MSG_READ,
    MSG_WRITE,
};

#ifdef DEBUG
static inline const char *strmsg(int type) {
    switch (type) {
        case MSG_KEEPALVE: return "MSG_KEEPALVE";
        case MSG_GETATTR: return "MSG_GETATTR";
        case MSG_MKDIR: return "MSG_MKDIR";
        case MSG_SYMLINK: return "MSG_SYMLINK";
        case MSG_UNLINK: return "MSG_UNLINK";
        case MSG_RMDIR: return "MSG_RMDIR";
        case MSG_RENAME: return "MSG_RENAME";
        case MSG_CHMOD: return "MSG_CHMOD";
        case MSG_TRUNCATE: return "MSG_TRUNCATE";
        case MSG_UTIMENS: return "MSG_UTIMENS";
        case MSG_CREATE: return "MSG_CREATE";
        case MSG_READ: return "MSG_READ";
        case MSG_WRITE: return "MSG_WRITE";
    }
    return "UNKNOWN";
}
#endif

struct MsgHead {
    uint32_t is_boardcast:1;
    uint32_t has_more:1;
    uint32_t type:6;
    uint32_t size:24;
    uint32_t id;
    int32_t ret;
};

struct Msg {
public:
    MsgHead head;
    std::shared_ptr<uint8_t> data;

    Msg() {
        memset(&head, 0, sizeof(head));
    }

    template<class T>
    T get_proto() const {
        T v;
        v.ParseFromArray(data.get(), head.size - head.ret);
        return v;
    }

    void set_proto(const ::google::protobuf::Message *src, const void *buffer = NULL, int size = 0) {
        int proto_size = src ? src->ByteSize() : 0;
        head.size = proto_size + size;
        data.reset(new uint8_t[head.size]);
        if (src)
            src->SerializeToArray(data.get(), proto_size);
        if (buffer) {
            memcpy(data.get() + proto_size, buffer, size);
            head.ret = size;
        }
    }

    static int read(int fd, Msg &msg) {
        if (-1 == read_buffer(fd, &msg.head, sizeof(MsgHead)))
            return -1;

        //LOG_DEBUG("recv type:%s id:%d ...\n", strmsg(msg.head.type), msg.head.id);

        if (msg.head.size >= (1<<23)) {
            LOG_ERROR("bad size: %d\n", msg.head.size);
            abort();
        }

        msg.data.reset(new uint8_t[msg.head.size]);
        if (-1 == read_buffer(fd, msg.data.get(), msg.head.size)) {
            return -1;
        }

        //LOG_DEBUG("recv type:%s id:%d ret: %d, done\n", strmsg(msg.head.type), msg.head.id, msg.head.ret);

        return 0;
    }

    static int write(int fd, const Msg &msg) {
        //LOG_DEBUG("send type:%s id:%d ...\n", strmsg(msg.head.type), msg.head.id);

        if (-1 == write_buffer(fd, &msg.head, sizeof(MsgHead)))
            return -1;

        if (msg.head.size > 0 && -1 == write_buffer(fd, msg.data.get(), msg.head.size))
            return -1;

        //LOG_DEBUG("send type:%s id:%d done\n", strmsg(msg.head.type), msg.head.id);

        return 0;
    }
};

#endif
