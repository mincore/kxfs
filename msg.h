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

#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <string.h>
#include <assert.h>

#include "log.h"
#include "utils.h"
#include "crc16.h"

#define CRC_SEED 0
const char *strmsg(int type);

class Msg {
public:
    enum Type {
        HELLO,
        KEEPALIVE,
        GETATTR,
        MKDIR,
        SYMLINK,
        UNLINK,
        RMDIR,
        RENAME,
        CHMOD,
        TRUNCATE,
        UTIMENS,
        READ,
        WRITE,
        CREATE,
        RELEASE,
    };

    #pragma pack(1)
    struct Head {
        uint32_t id;
        uint32_t type:8;
        uint32_t size:24;
        uint32_t crc;
        int32_t ret;
    };

    struct Attr {
        uint32_t mode = 0;
        uint64_t size = 0;
        uint64_t mtime = 0;
        uint64_t ctime = 0;
    };
    #pragma pack() 

    Head head = {0};
    std::vector<uint8_t> data;
    uint32_t offset = 0;

    void resize(uint32_t size) {
        data.resize(size);
        head.size = size;
        offset = 0;
    }

    void add_buf(const void *buf, int size) {
        data.insert(data.end(), (uint8_t*)buf, (uint8_t*)buf+size);
        head.size += size;
    }

    void add_string(const std::string &in) {
        uint32_t size = in.size();
        add_buf(&size, 4);
        add_buf(&in[0], size);
    }

    bool get_buf(void *buf, int size) {
        if (offset + size > head.size) {
            return false;
        }
        memcpy(buf, &data[0] + offset, size);
        offset += size;
        return true;
    }

    bool get_string(std::string &out) {
        uint32_t size;
        if (!get_buf(&size, 4)) {
            return false;
        }
        out.resize(size);
        if (!get_buf(&out[0], size)) {
            return false;
        }
        return true;
    }

    uint32_t get_rest(uint8_t **pdata) {
        if (pdata)
            *pdata = &data[0] + offset;
        return head.size - offset;
    }

    static int from_fd(int fd, Msg &msg) {
        if (-1 == read_buffer(fd, &msg.head, sizeof(Head)))
            return -1;
        
        //LOG_DEBUG("id:%d type:%d size:%d crc:%d ret:%d\n", 
        //    msg.head.id, msg.head.type, msg.head.size,
        //    msg.head.crc, msg.head.ret);

        msg.resize(msg.head.size);

        if (-1 == read_buffer(fd, &msg.data[0], msg.head.size)) {
            return -1;
        }

        //assert(msg.head.crc == crc16(CRC_SEED, &msg.data[0], msg.head.size));
        
        return 0;
    }

    static int to_fd(int fd, Msg &msg) {
        //assert(msg.head.size == msg.data.size());
        //msg.head.crc = crc16(CRC_SEED, &msg.data[0], msg.head.size);
        
        ///LOG_DEBUG("id:%d type:%d size:%d crc:%d ret:%d\n", 
        ///    msg.head.id, msg.head.type, msg.head.size,
        ///    msg.head.crc, msg.head.ret);

        if (-1 == write_buffer(fd, &msg.head, sizeof(Head)))
            return -1;

        if (msg.head.size > 0 && -1 == write_buffer(fd, &msg.data[0], msg.head.size))
            return -1;

        return 0;
    }
};

typedef std::shared_ptr<Msg> PMsg;

#endif
