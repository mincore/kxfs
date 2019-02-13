/* ===================================================
 * Copyright (C) 2018 chenshuangping All Right Reserved.
 *      Author: mincore@163.com
 *    Filename: server.cpp
 *     Created: 2018-06-29 11:29
 * Description:
 * ===================================================
 */
#include <unistd.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <list>
#include "msg.h"
#include "net.h"
#include "log.h"
#include "backend.h"

using namespace std::placeholders;

class Session {
public:
    Session(int fd, Backend *b): sock_(fd), backend_(b) {}

    ~Session() {
        send_thread_.join();
        recv_thread_.join();
        close(sock_);
    }

    void start();
    void reply(PMsg resp);

private:
    void recv_reqs();
    void send_resps();

private:
    std::thread send_thread_;
    std::thread recv_thread_;
    int sock_ = -1;

    std::mutex resps_mutex_;
    std::condition_variable resps_cond_;
    std::list<PMsg> resps_;

    Backend *backend_;
};

void Session::start() {
    recv_thread_ = std::thread([this] { recv_reqs(); });
    send_thread_ = std::thread([this] { send_resps(); });
}

void Session::reply(PMsg resp) {
    std::unique_lock<std::mutex> lk(resps_mutex_);
    resps_.push_back(resp);
    resps_cond_.notify_one();
}

void Session::recv_reqs() {
    while (1) {
        PMsg pmsg = std::make_shared<Msg>();
        if (-1 == Msg::from_fd(sock_, *pmsg))
            break;

        switch (pmsg->head.type) {
        case Msg::HELLO:
            break;
        case Msg::KEEPALIVE:
            break;
        default:
            backend_->process(pmsg, std::bind(&Session::reply, this, _1));
        }
    }
}

void Session::send_resps() {
    bool error = false;
    while (!error) {
        std::list<PMsg> resps;
        {
            std::unique_lock<std::mutex> lk(resps_mutex_);
            resps_cond_.wait(lk, [this] { return !resps_.empty(); });
            resps.swap(resps_);
        }

        for (auto &resp : resps) {
            if (-1 == Msg::to_fd(sock_, *resp)) {
                error = true;
                break;
            }
        }
    }
}

class Server {
public:
    ~Server() {
        close(sock_);
    }
    bool start(const char *host, uint16_t port);
    void set_root(const char *root) {
        backend_.set_root(root);
    }

private:
    Backend backend_;
    std::list<std::shared_ptr<Session> > sesses_;
    int sock_;
};

bool Server::start(const char *host, uint16_t port) {
    sock_ = netlisten(1, host, port);
    if (sock_ == -1)
        return false;

    char cip[32];
    int cport;
    int fd;

    while (-1 != (fd = netaccept(sock_, cip, &cport))) {
        LOG_INFO("%s:%d connnected\n", cip, cport);
        auto sess = std::make_shared<Session>(fd, &backend_);
        sess->start();
        sesses_.push_back(sess);
    }

    return true;
}

int main(int argc, char *argv[])
{
    const char *host = "0.0.0.0";
    uint16_t port = 8991;

    if (argc < 2) {
        printf("usage: %s dir\n", argv[0]);
        return -1;
    }

    log_init(NULL);

    Server s;
    s.set_root(argv[1]);
    if (!s.start(host, port)) {
        LOG_ERROR("listen on %s:%d failed\n", host, port);
        return -1;
    }

    log_exit();

    return 0;
}
