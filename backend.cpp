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

static int readattr(const std::string &path, Msg::Attr &attr, PMsg out) {
    struct stat stbuf = {0};
    if (-1 == lstat(path.c_str(), &stbuf)) {
        LOG_ERROR("path:%s failed, err:%d %s\n", path.c_str(), errno, strerror(errno));
        return -1;
    }

    memset(&attr, 0, sizeof(attr));
    attr.mode = stbuf.st_mode;
    attr.size = stbuf.st_size;
    attr.mtime = stbuf.st_mtime;
    out->add_buf(&attr, sizeof(attr));

    if ((attr.mode & S_IFMT) ==  S_IFLNK) {
        char link[1024] = {0};
        readlink(path.c_str(), link, sizeof(link)-1);
        out->add_string(link);
    }

    return 0;
}

static void readdir(const std::string &path, PMsg out) {
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

        out->add_string(name);
        Msg::Attr attr;
        readattr(make_path(path, name), attr, out);

        free(namelist[i]);
    }

    free(namelist);
}

int Backend::impl_getattr(PMsg in, PMsg out) {
    std::string path;
    if (!in->get_string(path)) {
        LOG_INFO("get string failed\n");
        return -1;
    }
    path = make_path(root_, path);

    Msg::Attr attr;
    if (-1 == readattr(path, attr, out)) {
        LOG_ERROR("path:%s failed\n", path.c_str());
        return -errno;
    }
    
    if ((attr.mode & S_IFMT) == S_IFDIR) {
        readdir(path, out);
    }

    return 0;
}

int Backend::impl_mkdir (PMsg in, PMsg out) {
    std::string path;
    if (!in->get_string(path)) {
        return -1;
    }
    path = make_path(root_, path);

    int64_t mode;
    if (!in->get_buf(&mode, sizeof(mode))) {
        return -1;
    }

    if (-1 == mkdir(path.c_str(), mode)) {
        LOG_ERROR("path:%s failed\n", path.c_str());
        return -1;
    }

    return 0;
}

int Backend::impl_symlink (PMsg in, PMsg out) {
    std::string from;
    if (!in->get_string(from)) {
        return -1;
    }
    from = make_path(root_, from);
    
    std::string to;
    if (!in->get_string(to)) {
        return -1;
    }
    to = make_path(root_, to);

    auto dname = make_dirname(to);
    auto bname = make_basename(to);

    int fd = open(dname.c_str(), O_DIRECTORY);
    if (fd == -1) {
        LOG_ERROR("open %s failed\n", dname.c_str());
        return -errno;
    }

    if (-1 == symlinkat(from.c_str(), fd, bname.c_str())) {
        LOG_ERROR("from:%s to:%s failed\n", from.c_str(), to.c_str());
        int err = errno;
        close(fd);
        return -err;
    }

    close(fd);
    return 0;
}

int Backend::impl_unlink (PMsg in, PMsg out) {
    std::string path;
    if (!in->get_string(path)) {
        return -1;
    }
    path = make_path(root_, path);

    if (-1 == unlink(path.c_str())) {
        LOG_ERROR("path:%s failed\n", path.c_str());
        return -errno;
    }

    return 0;
}

int Backend::impl_rmdir (PMsg in, PMsg out) {
    std::string path;
    if (!in->get_string(path)) {
        return -1;
    }
    path = make_path(root_, path);

    if (-1 == rmdir(path.c_str())) {
        LOG_ERROR("path:%s failed\n", path.c_str());
        return -errno;
    }

    return 0;
}

int Backend::impl_rename (PMsg in, PMsg out) {
    std::string from;
    if (!in->get_string(from)) {
        return -1;
    }
    from = make_path(root_, from);

    std::string to;
    if (!in->get_string(to)) {
        return -1;
    }
    to = make_path(root_, to);

    if (-1 == rename(from.c_str(), to.c_str())) {
        LOG_ERROR("path:%s %s failed\n", from.c_str(), to.c_str());
        return -errno;
    }

    return 0;
}

int Backend::impl_chmod (PMsg in, PMsg out) {
    std::string path;
    if (!in->get_string(path)) {
        return -1;
    }
    path = make_path(root_, path);
    
    int64_t mode;
    if (!in->get_buf(&mode, sizeof(mode))) {
        return -1;
    }

    if (-1 == chmod(path.c_str(), mode)) {
        LOG_ERROR("path:%s failed\n", path.c_str());
        return -errno;
    }

    return 0;
}

int Backend::impl_truncate (PMsg in, PMsg out) {
    std::string path;
    if (!in->get_string(path)) {
        return -1;
    }
    path = make_path(root_, path);
    
    uint64_t size;
    if (!in->get_buf(&size, sizeof(size))) {
        return -1;
    }

    if (-1 == truncate(path.c_str(), size)) {
        LOG_ERROR("path:%s size:%lld failed\n", path.c_str(), (long long)size);
        return -errno;
    }

    return 0;
}

int Backend::impl_utimens (PMsg in, PMsg out) {
    std::string path;
    if (!in->get_string(path)) {
        return -1;
    }
    path = make_path(root_, path);
    
    uint64_t t1;
    if (!in->get_buf(&t1, sizeof(t1))) {
        return -1;
    }

    uint64_t t2;
    if (!in->get_buf(&t2, sizeof(t2))) {
        return -1;
    }

    int fd = open(path.c_str(), O_RDWR);
    if (-1 == fd) {
        LOG_ERROR("open %s failed\n", path.c_str());
        return -errno;
    }

    struct timespec ts[2] = {
        {(long)(t1>>32), (int)(t1 & 0xFFFFFFFF)},
        {(long)(t2>>32), (int)(t2 & 0xFFFFFFFF)}
    };

    if (-1 == futimens(fd, ts)) {
        LOG_ERROR("path:%s t1:%lld, t2:%lld failed\n", path.c_str(), (long long)t1, (long long)t2);
        int err = errno;
        close(fd);
        return -err;
    }

    close(fd);
    return 0;
}

int Backend::impl_read (PMsg in, PMsg out) {
    std::string path;
    if (!in->get_string(path)) {
        return -1;
    }
    path = make_path(root_, path);
    
    int64_t size;
    if (!in->get_buf(&size, sizeof(size))) {
        return -1;
    }

    uint64_t offset;
    if (!in->get_buf(&offset, sizeof(offset))) {
        return -1;
    }

    LOG_DEBUG("%s path:%s size:%lld offset:%lld\n", strmsg(in->head.type), path.c_str(),
            (long long)size, (long long)offset);

    int fd = open(path.c_str(), O_RDONLY);
    if (-1 == fd) {
        LOG_ERROR("open %s failed\n", path.c_str());
        return -errno;
    }

    if (offset > 0) {
        if (-1 == lseek(fd, offset, 0)) {
            LOG_ERROR("seek %s offset:%lld failed\n", path.c_str(), (long long)offset);
            int err = errno;
            close(fd);
            return -err;
        }
    }

    out->resize(size);
    size = read(fd, &out->data[0], size);
    if (size < 0) {
         int err = errno;
         close(fd);
         return -err;
    }

    close(fd);
    out->resize(size);
    return size;
}

int Backend::impl_write (PMsg in, PMsg out) {
    std::string path;
    if (!in->get_string(path)) {
        return -1;
    }
    path = make_path(root_, path);
    
    int64_t size;
    if (!in->get_buf(&size, sizeof(size))) {
        return -1;
    }

    uint64_t offset;
    if (!in->get_buf(&offset, sizeof(offset))) {
        return -1;
    }

    uint64_t id;
    if (!in->get_buf(&id, sizeof(id))) {
        return -1;
    }
    LOG_INFO("fh id: %d\n", (int)id);

    uint8_t *data;
    in->get_rest(&data);
    
    LOG_DEBUG("id:%d, head.size:%d head.ret:%d, size:%lld\n",
            in->head.id, in->head.size, in->head.ret, (long long)size);

    bool find = false;
    int fd = -1;
    {
        std::unique_lock<std::mutex> lk(fdmaps_mutex_);
        auto it = fdmaps_.find(id);
        if (it != fdmaps_.end()) {
            fd = it->second;
            find = true;
            LOG_INFO("find id: %d\n", (int)id);
        }
    }

    if (!find) {
        fd = open(path.c_str(), O_CREAT | O_WRONLY);
        if (-1 == fd) {
            LOG_INFO("open %s failed\n", path.c_str());
            return -errno;
        }
    }

    if (offset > 0) {
        if (-1 == lseek(fd, offset, SEEK_SET)) {
            int err = errno;
            close(fd);
            return -err;
        }
    }

    size = write(fd, data, size);
    if (size < 0) {
        int err = errno;
        if (!find) close(fd);
        return -err;
    }

    if (!find) close(fd);
    out->resize(size);
    return size;
}

int Backend::impl_create (PMsg in, PMsg out) {
    std::string path;
    if (!in->get_string(path)) {
        return -1;
    }
    path = make_path(root_, path);
    
    uint64_t mode;
    if (!in->get_buf(&mode, sizeof(mode))) {
        return -1;
    }

    LOG_DEBUG("id:%d, head.size:%d head.type:%d\n",
            in->head.id, in->head.size, in->head.type);

    int fd = creat(path.c_str(), mode);
    if (fd == -1) {
        return -errno;
    }

    int id;
    {
        std::unique_lock<std::mutex> lk(fdmaps_mutex_);
        id = fd_index_++;
        fdmaps_[id] = fd;
    }

    LOG_INFO("created id: %d\n", id);
    out->add_buf(&id, sizeof(id));
    return 0;
}

int Backend::impl_release (PMsg in, PMsg out) {
    uint64_t id;
    if (!in->get_buf(&id, sizeof(id))) {
        return -1;
    }

    std::unique_lock<std::mutex> lk(fdmaps_mutex_);
    auto it = fdmaps_.find((int)id);
    if (it != fdmaps_.end()) {
        int fd = it->second;
        fdmaps_.erase(it);
        close(fd);
        LOG_INFO("release id: %d\n", (int)id);
    }

    return 0;
}

int Backend::impl_hello(PMsg in, PMsg out) {
    return 0;
}

int Backend::impl_keepalive(PMsg in, PMsg out) {
    return 0;
}

int Backend::process_msg(PMsg in, PMsg out) {
    switch (in->head.type) {
    case Msg::HELLO:      return impl_hello(in, out);
    case Msg::KEEPALIVE:  return impl_keepalive(in, out);
    case Msg::GETATTR:    return impl_getattr(in, out); 
    case Msg::MKDIR:      return impl_mkdir(in, out); 
    case Msg::SYMLINK:    return impl_symlink(in, out); 
    case Msg::UNLINK:     return impl_unlink(in, out); 
    case Msg::RMDIR:      return impl_rmdir(in, out); 
    case Msg::RENAME:     return impl_rename(in, out); 
    case Msg::CHMOD:      return impl_chmod(in, out); 
    case Msg::TRUNCATE:   return impl_truncate(in, out); 
    case Msg::UTIMENS:    return impl_utimens(in, out); 
    case Msg::READ:       return impl_read(in, out); 
    case Msg::WRITE:      return impl_write(in, out); 
    case Msg::CREATE:     return impl_create(in, out); 
    case Msg::RELEASE:    return impl_release(in, out); 
    default:              return -1;
    }
}
