/* ===================================================
 * Copyright (C) 2018 chenshuangping All Right Reserved.
 *      Author: mincore@163.com
 *    Filename: backend.cpp
 *     Created: 2018-06-29 14:48
 * Description:
 * ===================================================
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>

#include <unordered_map>
#include "backend.h"
#include "utils.h"
#include "log.h"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

extern const char *ROOT;

static int readattr(const std::string &path, kxfs::Attr *attr) {
    struct stat stbuf = {0};
    if (-1 == lstat(path.c_str(), &stbuf)) {
        LOG_ERROR("path:%s failed, err:%d %s\n", path.c_str(), errno, strerror(errno));
        return -1;
    }

    attr->set_mode(stbuf.st_mode);
    attr->set_size(stbuf.st_size);
    attr->set_mtime(stbuf.st_mtime);

    if ((stbuf.st_mode & S_IFMT) ==  S_IFLNK) {
        char link[1024] = {0};
        readlink(path.c_str(), link, sizeof(link)-1);
        attr->set_link(link);
    }

    return 0;
}

static void readdir(const std::string &path, kxfs::Resp &resp) {
    struct dirent **namelist;
    int n;

    n = scandir(path.c_str(), &namelist, NULL, NULL);
    if (n < 0) {
        LOG_ERROR("path:%s failed\n", path.c_str());
        return;
    }

    for (int i=0; i<n; i++) {
        const char *name = namelist[i]->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        auto file = resp.add_entries();
        auto attr = file->mutable_attr();
        file->set_path(name);
        readattr(make_path(path, name), attr);

        free(namelist[i]);
    }

    free(namelist);
}

static Msg OutMsg(const Msg &in) {
    Msg out;
    out.head.id = in.head.id;
    out.head.type = in.head.type;
    return out;
}

class ReplayHelper {
public:
    ReplayHelper(Reply *reply, Msg *out): reply_(reply), out_(out) {}
    ~ReplayHelper() { (*reply_)(*out_); }
private:
    Reply *reply_;
    Msg *out_;
};

static bool func_getattr(const Msg &in, Reply reply) {
    Msg out = OutMsg(in);
    auto req = in.get_proto<kxfs::Req>();
    kxfs::Resp resp;
    ReplayHelper helper(&reply, &out);

    auto path = make_path(ROOT, req.paths(0));
    auto file = resp.mutable_file();
    auto attr = file->mutable_attr();

    LOG_DEBUG("path:%s\n", path.c_str());

    if (-1 == readattr(path, attr)) {
        LOG_ERROR("path:%s failed\n", path.c_str());
        out.head.ret = -errno;
        return true;
    }

    if (attr->mode() & S_IFDIR) {
        readdir(path, resp);
    }

    out.set_proto(&resp);
    return true;
}

static bool func_mkdir (const Msg &in, Reply reply) {
    Msg out = OutMsg(in);
    auto req = in.get_proto<kxfs::Req>();
    ReplayHelper helper(&reply, &out);

    auto path = make_path(ROOT, req.paths(0));
    int mode = req.args(0);

    if (-1 == mkdir(path.c_str(), mode)) {
        LOG_ERROR("path:%s failed\n", path.c_str());
        out.head.ret = -errno;
    }

    return true;
}

static bool func_symlink (const Msg &in, Reply reply) {
    Msg out = OutMsg(in);
    auto req = in.get_proto<kxfs::Req>();
    ReplayHelper helper(&reply, &out);

    auto from = req.paths(0);
    auto to = make_path(ROOT, req.paths(1));
    auto dname = make_dirname(to);
    auto bname = make_basename(to);

    int fd = open(dname.c_str(), O_DIRECTORY);
    if (fd == -1) {
        LOG_ERROR("open %s failed\n", dname.c_str());
        out.head.ret = -errno;
        return true;
    }

    if (-1 == symlinkat(from.c_str(), fd, bname.c_str())) {
        LOG_ERROR("from:%s to:%s failed\n", from.c_str(), to.c_str());
        out.head.ret = -errno;
    }

    close(fd);
    return true;
}

static bool func_unlink (const Msg &in, Reply reply) {
    Msg out = OutMsg(in);
    auto req = in.get_proto<kxfs::Req>();
    ReplayHelper helper(&reply, &out);

    auto path = make_path(ROOT, req.paths(0));

    if (-1 == unlink(path.c_str())) {
        LOG_ERROR("path:%s failed\n", path.c_str());
        out.head.ret = -errno;
    }

    return true;
}

static bool func_rmdir (const Msg &in, Reply reply) {
    Msg out = OutMsg(in);
    auto req = in.get_proto<kxfs::Req>();
    ReplayHelper helper(&reply, &out);

    auto path = make_path(ROOT, req.paths(0));

    if (-1 == rmdir(path.c_str())) {
        LOG_ERROR("path:%s failed\n", path.c_str());
        out.head.ret = -errno;
    }

    return true;
}

static bool func_rename (const Msg &in, Reply reply) {
    Msg out = OutMsg(in);
    auto req = in.get_proto<kxfs::Req>();
    ReplayHelper helper(&reply, &out);

    auto from = make_path(ROOT, req.paths(0));
    auto to = make_path(ROOT, req.paths(1));

    if (-1 == rename(from.c_str(), to.c_str())) {
        LOG_ERROR("path:%s %s failed\n", from.c_str(), to.c_str());
        out.head.ret = -errno;
    }

    return true;
}

static bool func_chmod (const Msg &in, Reply reply) {
    Msg out = OutMsg(in);
    auto req = in.get_proto<kxfs::Req>();
    ReplayHelper helper(&reply, &out);

    auto path = make_path(ROOT, req.paths(0));
    int mode = req.args(0);

    if (-1 == chmod(path.c_str(), mode)) {
        LOG_ERROR("path:%s failed\n", path.c_str());
        out.head.ret = -errno;
    }

    return true;
}

static bool func_truncate (const Msg &in, Reply reply) {
    Msg out = OutMsg(in);
    auto req = in.get_proto<kxfs::Req>();
    ReplayHelper helper(&reply, &out);

    auto path = make_path(ROOT, req.paths(0));
    long long size = req.args(0);

    if (-1 == truncate(path.c_str(), size)) {
        LOG_ERROR("path:%s size:%lld failed\n", path.c_str(), size);
        out.head.ret = -errno;
    }

    return true;
}

static bool func_utimens (const Msg &in, Reply reply) {
    Msg out = OutMsg(in);
    auto req = in.get_proto<kxfs::Req>();
    ReplayHelper helper(&reply, &out);

    auto path = make_path(ROOT, req.paths(0));
    long long t1 = req.args(0);
    long long t2 = req.args(1);

    int fd = open(path.c_str(), O_RDWR);
    if (-1 == fd) {
        LOG_ERROR("open %s failed\n", path.c_str());
        out.head.ret = -errno;
        return true;
    }

    struct timespec ts[2] = {
        {t1>>32, t1 & 0xFFFFFFFF},
        {t2>>32, t2 & 0xFFFFFFFF}
    };

    if (-1 == futimens(fd, ts)) {
        LOG_ERROR("path:%s t1:%lld, t2:%lld failed\n", path.c_str(), t1, t2);
        out.head.ret = -errno;
    }

    close(fd);
    return true;
}

static bool func_create (const Msg &in, Reply reply) {
    Msg out = OutMsg(in);
    auto req = in.get_proto<kxfs::Req>();
    ReplayHelper helper(&reply, &out);

    auto path = make_path(ROOT, req.paths(0));
    int mode = req.args(0);

    int fd = creat(path.c_str(), mode);
    if (-1 == fd) {
        LOG_ERROR("path:%s mode:%d failed\n", path.c_str(), mode);
        out.head.ret = -errno;
    }

    close(fd);
    return true;
}

static bool func_read (const Msg &in, Reply reply) {
    auto req = in.get_proto<kxfs::Req>();

    auto path = make_path(ROOT, req.paths(0));
    size_t size = req.args(0);
    off_t offset = req.args(1);

    std::vector<char> buf(512*1024);

    LOG_DEBUG("%s path:%s size:%lld offset:%lld\n", strmsg(in.head.type), path.c_str(),
            (long long)size, (long long)offset);

    int fd = open(path.c_str(), O_RDONLY);
    if (-1 == lseek(fd, offset, 0)) {
        LOG_ERROR("seek %s offset:%lld failed\n", path.c_str(), (long long)offset);
        Msg out = OutMsg(in);
        ReplayHelper helper(&reply, &out);
        out.head.ret = -errno;
        return true;
    }

    while (size) {
        Msg out = OutMsg(in);
        ReplayHelper helper(&reply, &out);
        out.head.has_more = 1;

        int ntmp = std::min(size, buf.size());
        int n = read(fd, &buf[0], ntmp);
        if (n < 0) {
            out.head.ret = -errno;
            out.head.has_more = 0;
            break;
        }

        if (n < ntmp)
            size = 0;
        else
            size -= n;

        if (size == 0) {
            out.head.has_more = 0;
        }

        out.set_proto(NULL, &buf[0], n);
    }

    close(fd);
    return true;
}

static bool func_write (const Msg &in, Reply reply) {
    auto req = in.get_proto<kxfs::Req>();
    auto path = make_path(ROOT, req.paths(0));
    int size = (size_t)req.args(0);
    off_t offset = (off_t)req.args(1);
    uint8_t *data = in.data.get() + req.ByteSize();

    LOG_DEBUG("id:%d, head.size:%d head.ret:%d, size:%d\n",
            in.head.id, in.head.size, in.head.ret, size);

    assert(size == in.head.size - req.ByteSize());

    Msg out = OutMsg(in);
    ReplayHelper helper(&reply, &out);

    int fd = open(path.c_str(), O_CREAT | O_WRONLY);
    if (-1 == fd) {
        out.head.ret = -errno;
        return true;
    }

    if (offset > 0) {
        lseek(fd, offset, SEEK_SET);
    }

    int n = write(fd, data, size);
    out.head.ret = n < 0 ? -errno : n;

    close(fd);
    return true;
}

static cmd_func funcs[] = {
    func_getattr, func_mkdir, func_symlink, func_unlink,
    func_rmdir, func_rename, func_chmod, func_truncate,
    func_utimens, func_create, func_read, func_write,
};

extern cmd_func get_cmd_func(uint16_t type) {
    if (type >= ARRAY_SIZE(funcs)) {
        return NULL;
    }

    return funcs[type];
}
