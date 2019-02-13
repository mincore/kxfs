#include "msg.h"

const char *strmsg(int type) {
    switch (type) {
        case Msg::HELLO: return "MSG_HELLO";
        case Msg::KEEPALIVE: return "MSG_KEEPALIVE";
        case Msg::GETATTR: return "MSG_GETATTR";
        case Msg::MKDIR: return "MSG_MKDIR";
        case Msg::SYMLINK: return "MSG_SYMLINK";
        case Msg::UNLINK: return "MSG_UNLINK";
        case Msg::RMDIR: return "MSG_RMDIR";
        case Msg::RENAME: return "MSG_RENAME";
        case Msg::CHMOD: return "MSG_CHMOD";
        case Msg::TRUNCATE: return "MSG_TRUNCATE";
        case Msg::UTIMENS: return "MSG_UTIMENS";
        case Msg::READ: return "MSG_READ";
        case Msg::WRITE: return "MSG_WRITE";
        case Msg::CREATE: return "MSG_CREATE";
    }
    return "UNKNOWN";
}
