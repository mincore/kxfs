/* ===================================================
 * Copyright (C) 2018 chenshuangping All Right Reserved.
 *      Author: mincore@163.com
 *    Filename: utils.h
 *     Created: 2018-07-03 16:10
 * Description:
 * ===================================================
 */
#ifndef _UTILS_H
#define _UTILS_H

#include <chrono>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <libgen.h>
#include <unistd.h>

static inline uint64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
            ).count();
}

static inline uint32_t now_sec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch()
            ).count();
}

static inline std::string no_end_slash(const std::string &src) {
    auto end = (*src.rbegin() == '/' ? src.end()-1 : src.end());
    return std::string(src.begin(), end);
}

static inline std::string make_path(const std::string &dir, const std::string &name) {
    std::string ret = no_end_slash(dir);
    if (*name.begin() != '/')
        ret += '/';
    ret += no_end_slash(name);
    return ret;
}

static inline std::string make_dirname(const std::string &src) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", src.c_str());
    return dirname(tmp);
}

static inline std::string make_basename(const std::string &src) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", src.c_str());
    return basename(tmp);
}

template<class T>
class Future {
private:
    class Impl {
    public:
        void set(T val) {
            std::unique_lock<std::mutex> lk(mutex_);
            val_ = val;
            has_val_ = true;
            cond_.notify_all();
        }

        void clear() {
            std::unique_lock<std::mutex> lk(mutex_);
            has_val_ = false;
        }

        T& wait() {
            std::unique_lock<std::mutex> lk(mutex_);
            cond_.wait(lk, [this]{ return has_val_; });
            return val_;
        }

    private:
        std::mutex mutex_;
        std::condition_variable cond_;
        T val_;
        bool has_val_ = false;
    };
    std::shared_ptr<Impl> impl_;

public:
    Future():impl_(std::make_shared<Impl>()) {}

    void set(T val) { impl_->set(val); }
    T& wait() { return impl_->wait(); }
};

static int read_buffer(int fd, void *buffer, int size) {
    uint8_t *p = (uint8_t*)buffer;
    int left = size;
    while (left) {
        int n = ::read(fd, p, left);
        if (n <=0 )
            return -1;
        p += n;
        left -= n;
    }
    return size;
}

static int write_buffer(int fd, const void *buffer, int size) {
    uint8_t *p = (uint8_t*)buffer;
    int left = size;
    while (left) {
        int n = ::write(fd, p, left);
        if (n <=0 )
            return -1;
        p += n;
        left -= n;
    }
    return size;
}

#endif
