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
#include <signal.h>
#include <pthread.h>

#include "cache.h"
#include "log.h"
#include "client.h"
#include "net.h"

pthread_t FUSE_TID;

#define C ((Client*)(fuse_get_context()->private_data))
#define UID (fuse_get_context()->uid)
#define GID (fuse_get_context()->gid)

bool Client::connect(const char *host, uint16_t port) {
    sock_ = netconnect(1, host, port);
    if (sock_ == -1) {
        LOG_ERROR("connect to %s:%d faild\n", host, port);
        return false;
    }

    LOG_DEBUG("connected to %s:%d\n", host, port);
    return true;
}

void Client::start() {
    id_ = 0;
    send_thread_ = std::thread([this] { send_reqs(); });
    recv_thread_ = std::thread([this] { recv_resps(); });
}

void Client::stop() {
    LOG_DEBUG("stopping\n");
    {
        std::unique_lock<std::mutex> lk(reqs_mutex_);
        quit_ = true;
        reqs_cond_.notify_all();
    }
    close(sock_);
    send_thread_.join();
    recv_thread_.join();
}

Req Client::make_req(
        uint32_t id,
        Msg::Type type,
        const std::vector<const char *> &paths,
        const std::vector<int64_t> &args,
        const void *buffer,
        int size) {
    Msg msg;
    msg.head.type = type;
    msg.head.id = id;

    for (auto &path: paths)
        msg.add_string(path);

    for (auto &arg: args)
        msg.add_buf(&arg, sizeof(arg));

    if (buffer && size > 0) {
        msg.add_buf(buffer, size);
    }

    return Req{msg, make_future(msg.head.id)};
}

void Client::queue_req(const Msg &req) {
    std::unique_lock<std::mutex> lk(reqs_mutex_);
    reqs_.push_back(req);
    reqs_cond_.notify_one();
}

void Client::send_reqs() {
    LOG_DEBUG("running send thread\n");
    bool error = false;

    while (!error) {
        std::list<Msg> reqs;
        {
            std::unique_lock<std::mutex> lk(reqs_mutex_);
            reqs_cond_.wait(lk, [this] { return quit_ || !reqs_.empty(); });
            if (quit_) {
                LOG_DEBUG("waked up, quit: %d\n", quit_);
                break;
            }
            reqs_.swap(reqs);
        }

        for (auto &req: reqs) {
            if (-1 == Msg::to_fd(sock_, req)) {
                pthread_kill(FUSE_TID, SIGTERM);
                error = true;
                break;
            }
        }
    }
    LOG_DEBUG("send thread exit\n");
}

void Client::recv_resps() {
    LOG_DEBUG("running recv thread\n");

    while (1) {
        fd_set rfds;
        FD_SET(sock_, &rfds);
        struct timeval tv = {1, 0};

        int ret = select(sock_+1, &rfds, NULL, NULL, &tv);
        if (ret < 0) {
            break;
        }
        if (ret == 0) {
            continue;
        }

        PMsg pmsg = std::make_shared<Msg>();
        if (-1 == Msg::from_fd(sock_, *pmsg)) {
            pthread_kill(FUSE_TID, SIGTERM);
            break;
        }

        std::unique_lock<std::mutex> lk(future_map_mutex_);
        auto it = future_map_.find(pmsg->head.id);
        if (it == future_map_.end()) {
            LOG_ERROR("bad resp id:%d\n", pmsg->head.id);
            continue;
        }
        it->second.set(pmsg);
        future_map_.erase(it);
    }
    LOG_DEBUG("recv thread exit\n");
}

Future<PMsg> Client::make_future(uint32_t id) {
    auto future = Future<PMsg>();
    std::unique_lock<std::mutex> lk(future_map_mutex_);
    future_map_.insert({id, future});
    return future;
}

Node Client::node_from_msg(const char* path, Msg &msg) {
    Node node = make_node();
    Msg::Attr &attr = node->attr;

    if (!msg.get_buf(&attr, sizeof(Msg::Attr))) {
        LOG_ERROR("get attr failed\n");
        return node;
    }

    if ((attr.mode & S_IFMT) == S_IFLNK) {
        msg.get_string(node->link);
        return node;
    }
    
    if ((attr.mode & S_IFMT) == S_IFDIR) {
        std::string name;
        while (msg.get_string(name)) {
            Node subnode = make_node();

            if (!msg.get_buf(&subnode->attr, sizeof(Msg::Attr))) {
                LOG_DEBUG("get attr failed\n");
                break;
            }

            node->entries.push_back({name, subnode->attr.mode});

            if ((subnode->attr.mode & S_IFMT) == S_IFLNK) {
                msg.get_string(subnode->link);
            }

            cache_.add_node(make_path(path, name), subnode);
        }
    }
    
    cache_.add_node(path, node);

    return node;
}

Node Client::get_node(const char *path) {
    Node node = cache_.get_node(path);
    if (node) {
        return node;
    }

    PMsg pmsg;
    if (!C->send(Msg::GETATTR, {path}).wait(pmsg, 1000)) {
        LOG_ERROR("wait for msg failed\n");
        return Node();
    }
    auto msg = *pmsg;

    if (msg.head.ret < 0) {
        errno = -msg.head.ret;
        LOG_ERROR("%s, err:%d %s\n", path, errno, strerror(errno));
        return node;
    }

    return node_from_msg(path, msg);
}

void Client::del_node(const char *path, bool parent_too) {
    cache_.del_node(path);
    if (parent_too) {
        cache_.del_node(make_dirname(path));
    }
}

static inline int kxfs_cmd(Msg::Type type,
        const std::vector<const char *> &paths,
        const std::vector<int64_t> &args = {}) {
    PMsg pmsg;
    if (!C->send(type, paths, args).wait(pmsg, 1000)) {
        LOG_ERROR("wait for msg failed\n");
        return -1;
    }
    auto ret = pmsg->head.ret;
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

    int n = std::min(size-1, node->link.length());
    memcpy(buf, node->link.c_str(), n);
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
        LOG_DEBUG("readdir sub: %s\n", entry.name.c_str());
        st.st_mode = entry.mode;
        if (filler(buf, entry.name.c_str(), &st, 0))
            break;
    }

    return 0;
}

static int kxfs_mkdir(const char *path, mode_t mode)
{
    LOG_DEBUG("%s\n", path);
    return kxfs_cmd(Msg::MKDIR, {path}, {mode});
}

static int kxfs_unlink(const char *path)
{
    LOG_DEBUG("%s\n", path);
    return kxfs_cmd(Msg::UNLINK, {path});
}

static int kxfs_rmdir(const char *path)
{
    LOG_DEBUG("%s\n", path);
    return kxfs_cmd(Msg::RMDIR, {path});
}

static int kxfs_symlink(const char *from, const char *to)
{
    LOG_DEBUG("%s %s\n", from, to);
    return kxfs_cmd(Msg::SYMLINK, {from, to});
}

static int kxfs_rename(const char *from, const char *to)
{
    LOG_DEBUG("%s %s\n", from, to);
    return kxfs_cmd(Msg::RENAME, {from, to});
}

static int kxfs_chmod(const char *path, mode_t mode)
{
    LOG_DEBUG("%s\n", path);
    return kxfs_cmd(Msg::CHMOD, {path}, {mode});
}

static int kxfs_truncate(const char *path, off_t size)
{
    LOG_DEBUG("%s\n", path);
    return kxfs_cmd(Msg::TRUNCATE, {path}, {size});
}

static int kxfs_utimens(const char *path, const struct timespec ts[2])
{
#define TS(t) ((((int64_t)(t).tv_sec) << 32) | (uint32_t)((t).tv_nsec))
    LOG_DEBUG("%s\n", path);
    return kxfs_cmd(Msg::UTIMENS, {path}, {TS(ts[0]), TS(ts[1])});
}

static int kxfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    LOG_DEBUG("%s\n", path);
    PMsg pmsg;
    if (!C->send(Msg::CREATE, {path}, {mode}).wait(pmsg, 1000)) {
        LOG_ERROR("wait for msg failed\n");
        return -1;
    }

    auto msg = *pmsg;
    if (msg.head.ret < 0) {
        return msg.head.ret;
    }

    int id;
    msg.get_buf(&id, sizeof(id));
    fi->fh = id;

    return 0;
}

static int kxfs_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    LOG_DEBUG("%s\n", path);

    PMsg pmsg;
    if (!C->send(Msg::READ, {path}, {(int64_t)size, (int64_t)offset}).wait(pmsg, 1000)) {
        LOG_ERROR("wait for msg failed\n");
        return -1;
    }
    auto msg = *pmsg;

    LOG_DEBUG("ret:%d size:%zd head.size:%d\n", msg.head.ret, msg.data.size(), msg.head.size);

    if (msg.head.ret < 0) {
        return msg.head.ret;
    }

    memcpy(buf, &msg.data[0], msg.head.size);

    return msg.head.ret;
}

static int kxfs_open(const char *path, struct fuse_file_info *fi)
{
    fi->fh = C->new_id();
    fi->keep_cache = 1;
    LOG_DEBUG("%s %d\n", path, (uint32_t)fi->fh);
    return 0;
}

static int kxfs_release(const char *path, struct fuse_file_info *fi)
{
    LOG_DEBUG("%s %d\n", path, (uint32_t)fi->fh);
    return kxfs_cmd(Msg::RELEASE, {}, {(uint32_t)fi->fh});
}

static int kxfs_write(const char *path, const char *buf, size_t size,
             off_t offset, struct fuse_file_info *fi)
{
    LOG_DEBUG("%s %d\n", path, (uint32_t)fi->fh);
    PMsg pmsg;
    if (!C->send(Msg::WRITE, {path}, 
            {(int64_t)size, (int64_t)offset, (int64_t)fi->fh},
            buf, size).wait(pmsg, 1000)) {
        LOG_ERROR("wait for msg failed\n");
        return -1;
    }
    auto ret = pmsg->head.ret;
    if (ret >= 0) {
        C->del_node(path, false);
    }
    return ret;
}

static void* kxfs_init(struct fuse_conn_info *conn) {
    conn->max_write = 128*1024;
    conn->max_readahead = 128*1024;
    return C;
}

static struct fuse_operations *init_ops(struct fuse_operations *ops) {
    memset(ops, 0, sizeof(*ops));
    ops->init     = kxfs_init;
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

static void kxfs_run(int argc, char **argv, 
    const char *mount_point, 
    struct fuse_operations *ops,
    Client *c
    ) {
    char *argv2[argc+2];
    for (int i=0; i<argc; i++) {
        argv2[i] = argv[i];
    }
    argv2[argc++] = (char*)"-o";
    argv2[argc++] = (char*)"big_writes";

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv2);
    struct fuse_chan *chan = fuse_mount(mount_point, &args);
    struct fuse *fuse = fuse_new(chan, &args, ops, sizeof(*ops), c);
    struct fuse_session *se = fuse_get_session(fuse);
	fuse_set_signal_handlers(se);
    fuse_daemonize(1);

    c->start();

    fuse_loop_mt(fuse);
    fuse_remove_signal_handlers(se);
    fuse_unmount(mount_point, chan);
	fuse_destroy(fuse);
    fuse_opt_free_args(&args);
    
    c->stop();
}

int main(int argc, char *argv[])
{
    char *str = getenv("KXFS_HOST");
    const char *host = str ? str : "127.0.0.1";
    uint16_t port = 8991;

    log_init(NULL);

    Client c;
    if (!c.connect(host, port)) {
        LOG_ERROR("connect to %s:%d failed\n", host, port);
        return -1;
    }

    FUSE_TID = pthread_self();

    struct fuse_operations ops;
    init_ops(&ops);

    kxfs_run(argc, argv, "/mnt", &ops, &c);

    log_exit();

    return 0;
}
