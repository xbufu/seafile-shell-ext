#pragma once

#include <QDir>
#include <QFile>
#include <QString>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <optional>

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

inline QString seadriveMountDir()
{
    return QDir::homePath() + QStringLiteral("/SeaDrive");
}

inline bool inSeaDrive(const QString &path)
{
    return path.startsWith(seadriveMountDir() + QLatin1Char('/'));
}

// Client for the SeaDrive GUI socket (~/.seadrive/seadrive_ext.sock).
// Request and reply are both a native-endian uint32 length followed by UTF-8
// text; the request text is "command\targument".
// Not thread-safe: use one instance per thread.
class SeaDriveConn
{
public:
    SeaDriveConn() = default;
    SeaDriveConn(const SeaDriveConn &) = delete;
    SeaDriveConn &operator=(const SeaDriveConn &) = delete;
    ~SeaDriveConn() { disconnect(); }

    std::optional<QString> send(const QString &cmd, const QString &arg = QString())
    {
        const QByteArray body = (cmd + QLatin1Char('\t') + arg).toUtf8();
        const uint32_t len = body.size();

        // A pooled fd may be dead if SeaDrive restarted: retry once on a fresh one.
        for (int attempt = 0; attempt < 2; ++attempt) {
            const bool fresh = fd_ < 0;
            if (fresh && !connectSocket())
                return std::nullopt;

            uint32_t replyLen = 0;
            if (writeAll(&len, sizeof(len)) && writeAll(body.constData(), len)
                && readAll(&replyLen, sizeof(replyLen)) && replyLen < (16u << 20)) {
                QByteArray reply(replyLen, Qt::Uninitialized);
                if (readAll(reply.data(), replyLen))
                    return QString::fromUtf8(reply);
            }
            disconnect();
            if (fresh)
                break;
        }
        return std::nullopt;
    }

private:
    bool connectSocket()
    {
        fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd_ < 0)
            return false;

        // Called from Dolphin's UI thread: never hang it on a stuck SeaDrive.
        timeval tv{3, 0};
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        const QByteArray path = QFile::encodeName(QDir::homePath() + QStringLiteral("/.seadrive/seadrive_ext.sock"));
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path.constData(), sizeof(addr.sun_path) - 1);
        if (::connect(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            disconnect();
            return false;
        }
        return true;
    }

    void disconnect()
    {
        if (fd_ >= 0)
            close(fd_);
        fd_ = -1;
    }

    bool writeAll(const void *data, size_t n)
    {
        auto p = static_cast<const char *>(data);
        while (n > 0) {
            // MSG_NOSIGNAL: a closed socket must not SIGPIPE Dolphin.
            const ssize_t w = ::send(fd_, p, n, MSG_NOSIGNAL);
            if (w < 0 && errno == EINTR)
                continue;
            if (w <= 0)
                return false;
            p += w;
            n -= w;
        }
        return true;
    }

    bool readAll(void *data, size_t n)
    {
        auto p = static_cast<char *>(data);
        while (n > 0) {
            const ssize_t r = ::recv(fd_, p, n, 0);
            if (r < 0 && errno == EINTR)
                continue;
            if (r <= 0)
                return false;
            p += r;
            n -= r;
        }
        return true;
    }

    int fd_ = -1;
};
