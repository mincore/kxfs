/* ===================================================
 * Copyright (C) 2018 chenshuangping All Right Reserved.
 *      Author: mincore@163.com
 *    Filename: fuse_main.cpp
 *     Created: 2018-06-27 14:16
 * Description:
 * ===================================================
 */
#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>

#include "kxfs.pb.h"
#include "log.h"
#include "client.h"
#include "net.h"

#define C ((Client*)(fuse_get_context()->private_data))
#define UID (fuse_get_context()->uid)
#define GID (fuse_get_context()->gid)

static pthread_t FUSE_LOOP_PID;

bool Client::start(const char *host, uint16_t port) {
    sock_ = netconnect(1, host, port);
    if (sock_ == -1)
        return false;

    id_ = 0;
    auto send_thread = std::thread([this] { send_reqs(); });
    auto recv_thread = std::thread([this] { recv_resps(); });
    send_thread.detach();
    recv_thread.detach();
    return true;
}

Req Client::make_req(
        uint32_t id,
        MSG_TYPE type,
        const std::vector<const char *> &paths,
        const std::vector<int64_t> &args,
        const void *buffer,
        int size) {
    kxfs::Req req;

    for (auto &path: paths)
        req.add_paths(path);

    for (auto &arg: args)
        req.add_args(arg);

    Msg msg;
    msg.head.type = type;
    msg.head.id = id;
    msg.set_proto(&req, buffer, size);

    return Req{msg, make_future(msg.head.id)};
}

void Client::queue_req(const Msg &req) {
    std::unique_lock<std::mutex> lk(reqs_mutex_);
    reqs_.push_back(req);
    reqs_cond_.notify_one();
}

void Client::send_reqs() {
    bool error = false;
    while (!error) {
        std::list<Msg> reqs;
        {
            std::unique_lock<std::mutex> lk(reqs_mutex_);
            reqs_cond_.wait(lk, [this] { return quit_ || !reqs_.empty(); });
            if (quit_)
                break;
            reqs_.swap(reqs);
        }

        for (auto &req: reqs) {
            if (-1 == Msg::write(sock_, req)) {
                error = true;
                break;
            }
        }
    }
    pthread_kill(FUSE_LOOP_PID, SIGINT);
}

void Client::recv_resps() {
    while (1) {
        Msg msg;
        if (-1 == Msg::read(sock_, msg)) {
            break;
        }

        std::unique_lock<std::mutex> lk(future_map_mutex_);
        auto it = future_map_.find(msg.head.id);
        if (it == future_map_.end()) {
            LOG_ERROR("bad resp id:%d\n", msg.head.id);
            continue;
        }
        it->second.set(msg);
        if (msg.head.type != MSG_READ) {
            future_map_.erase(it);
        }
    }
    pthread_kill(FUSE_LOOP_PID, SIGINT);
}

void Client::exit() {
    LOG_DEBUG("exiting\n");
    std::unique_lock<std::mutex> lk(reqs_mutex_);
    quit_ = true;
    reqs_cond_.notify_all();
    close(sock_);
}

Future<Msg> Client::make_future(uint32_t id) {
    auto future = Future<Msg>();
    std::unique_lock<std::mutex> lk(future_map_mutex_);
    future_map_.insert({id, future});
    return future;
}

void Client::del_future(uint32_t id) {
    std::unique_lock<std::mutex> lk(future_map_mutex_);
    future_map_.erase(id);
}

static Node node_from_attr(const kxfs::Attr &attr) {
    Node node = make_node();
    node->attr.mode = attr.mode();
    node->attr.size = attr.size();
    node->attr.mtime = attr.mtime();
    if (attr.has_link())
        node->attr.link = attr.link();
    return node;
}

Node Client::get_node(const char *path) {
    Node node = cache_.get_node(path);
    if (node)
        return node;

    auto msg = C->send(MSG_GETATTR, {path}).wait();

    LOG_DEBUG("%s %s id:%d ret:%d\n", strmsg(msg.head.type), path, msg.head.id, msg.head.ret);

    if (msg.head.ret < 0) {
        errno = -msg.head.ret;
        LOG_ERROR("%s, err:%d %s\n", path, errno, strerror(errno));
        return node;
    }

    auto resp = msg.get_proto<kxfs::Resp>();
    auto attr = resp.file().attr();
    node = node_from_attr(resp.file().attr());

    for (int i=0; i<resp.entries_size(); i++) {
        auto &file = resp.entries(i);
        Entry entry;
        entry.name = file.path();
        entry.mode = file.attr().mode();
        node->entries.emplace_back(entry);

        if ((file.attr().mode() & S_IFMT) != S_IFDIR) {
            cache_.add_node(make_path(path, entry.name), node_from_attr(file.attr()));
        }
    }

    cache_.add_node(path, node);

    return node;
}

void Client::del_node(const char *path, bool parent_too) {
    cache_.del_node(path);
    if (parent_too)
        cache_.del_node(make_dirname(path));
}

static inline int kxfs_cmd(MSG_TYPE type,
        const std::vector<const char *> &paths,
        const std::vector<int64_t> &args = {}) {
    auto ret = C->send(type, paths, args).wait().head.ret;
    if (ret == 0) {
        for (auto &path: paths) {
            C->del_node(path);
        }
    }
    return ret;
}

static int kxfs_getattr(const char *path, struct stat *stbuf)
{
    LOG_DEBUG("%s\n", path);
    Node node = C->get_node(path);
    if (!node)
        return -errno;

    stbuf->st_mode  = node->attr.mode;
    stbuf->st_size  = node->attr.size;
    stbuf->st_mtime = node->attr.mtime;
    stbuf->st_uid = UID;
    stbuf->st_gid = GID;

    return 0;
}

static int kxfs_readlink(const char *path, char *buf, size_t size)
{
    LOG_DEBUG("%s\n", path);
    Node node = C->get_node(path);
    if (!node)
        return -errno;

    int n = std::min(size-1, node->attr.link.length());
    memcpy(buf, node->attr.link.c_str(), n);
    buf[n] = 0;

    return 0;
}

static int kxfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
        off_t offset, struct fuse_file_info *fi)
{
    LOG_DEBUG("%s\n", path);
    Node node = C->get_node(path);
    if (!node)
        return -errno;

    struct stat st;
    memset(&st, 0, sizeof(st));
    st.st_mode = S_IFDIR;
    filler(buf, ".", &st, 0);
    filler(buf, "..", &st, 0);

    for (auto &entry: node->entries) {
        st.st_mode = entry.mode;
        if (filler(buf, entry.name.c_str(), &st, 0))
            break;
    }

    return 0;
}

static int kxfs_mkdir(const char *path, mode_t mode)
{
    LOG_DEBUG("%s\n", path);
    return kxfs_cmd(MSG_MKDIR, {path}, {mode});
}

static int kxfs_unlink(const char *path)
{
    LOG_DEBUG("%s\n", path);
    return kxfs_cmd(MSG_UNLINK, {path});
}

static int kxfs_rmdir(const char *path)
{
    LOG_DEBUG("%s\n", path);
    return kxfs_cmd(MSG_RMDIR, {path});
}

static int kxfs_symlink(const char *from, const char *to)
{
    LOG_DEBUG("%s %s\n", from, to);
    return kxfs_cmd(MSG_SYMLINK, {from, to});
}

static int kxfs_rename(const char *from, const char *to)
{
    LOG_DEBUG("%s %s\n", from, to);
    return kxfs_cmd(MSG_RENAME, {from, to});
}

static int kxfs_chmod(const char *path, mode_t mode)
{
    LOG_DEBUG("%s\n", path);
    return kxfs_cmd(MSG_CHMOD, {path}, {mode});
}

static int kxfs_truncate(const char *path, off_t size)
{
    LOG_DEBUG("%s\n", path);
    return kxfs_cmd(MSG_TRUNCATE, {path}, {size});
}

static int kxfs_utimens(const char *path, const struct timespec ts[2])
{
#define TS(t) ((((int64_t)(t).tv_sec) << 32) | (uint32_t)((t).tv_nsec))
    LOG_DEBUG("%s\n", path);
    return kxfs_cmd(MSG_UTIMENS, {path}, {TS(ts[0]), TS(ts[1])});
}

static int kxfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    LOG_DEBUG("%s\n", path);
    fi->fh = C->new_id();
    return kxfs_cmd(MSG_CREATE, {path}, {mode});
}

static int kxfs_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    auto future = C->send(MSG_READ, {path}, {(int64_t)size, (int64_t)offset});
    size_t ret = 0;

    LOG_DEBUG("%s\n", path);

    while (1) {
        auto msg = future.wait();
        if (msg.head.ret < 0) {
            C->del_future(msg.head.id);
            return msg.head.ret;
        }

        memcpy(buf, msg.data.get(), msg.head.size);
        buf += msg.head.size;
        ret += msg.head.size;
        if (!msg.head.has_more) {
            C->del_future(msg.head.id);
            break;
        }
    }

    return ret;
}

static int kxfs_open(const char *path, struct fuse_file_info *fi)
{
    fi->fh = C->new_id();
    LOG_DEBUG("%s %d\n", path, (uint32_t)fi->fh);
    return 0;
}

static int kxfs_release(const char *path, struct fuse_file_info *fi)
{
    LOG_DEBUG("%s %d\n", path, (uint32_t)fi->fh);
    return 0;
}

static int kxfs_write(const char *path, const char *buf, size_t size,
             off_t offset, struct fuse_file_info *fi)
{
    LOG_DEBUG("%s %d\n", path, (uint32_t)fi->fh);
    auto ret = C->send(MSG_WRITE, {path}, {(int64_t)size, (int64_t)offset},
            &fi->fh,
            buf, size).wait().head.ret;
    if (ret >= 0) {
        C->del_node(path, false);
    }
    return ret;
}

static struct fuse_operations *init_ops(struct fuse_operations *ops) {
    memset(ops, 0, sizeof(*ops));
    ops->getattr  = kxfs_getattr;
    ops->readlink = kxfs_readlink;
    ops->readdir  = kxfs_readdir;
    ops->mkdir    = kxfs_mkdir;
    ops->symlink  = kxfs_symlink;
    ops->unlink   = kxfs_unlink;
    ops->rmdir    = kxfs_rmdir;
    ops->rename   = kxfs_rename;
    ops->chmod    = kxfs_chmod;
    ops->truncate = kxfs_truncate;
    ops->utimens  = kxfs_utimens;
    ops->create   = kxfs_create;
    ops->read     = kxfs_read;
    ops->write    = kxfs_write;
    ops->open     = kxfs_open;
    ops->release  = kxfs_release;
    return ops;
}

int main(int argc, char *argv[])
{
    char *str = getenv("KXFS_HOST");
    const char *host = str ? str : "127.0.0.1";
    uint16_t port = 8991;

    Client c;
    if (!c.start(host, port)) {
        LOG_ERROR("connect to %s:%d failed\n", host, port);
        return -1;
    }

    FUSE_LOOP_PID = pthread_self();

    struct fuse_operations ops;
    return fuse_main(argc, argv, init_ops(&ops), &c);
}
