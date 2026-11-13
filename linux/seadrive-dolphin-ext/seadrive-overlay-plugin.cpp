#include "seadrive-conn.h"

#include <KOverlayIconPlugin>

#include <QFileInfo>
#include <QHash>
#include <QUrl>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

static QStringList emblemsFor(const QString &status)
{
    static const QHash<QString, QString> emblems{
        {QStringLiteral("locked"), QStringLiteral("emblem-seadrive-locked-by-others")},
        {QStringLiteral("locked_by_me"), QStringLiteral("emblem-seadrive-locked-by-me")},
        {QStringLiteral("syncing"), QStringLiteral("emblem-seadrive-syncing")},
        {QStringLiteral("synced"), QStringLiteral("emblem-seadrive-done")},
        {QStringLiteral("cloud"), QStringLiteral("emblem-seadrive-cloud")},
    };
    const QString emblem = emblems.value(status);
    return emblem.isEmpty() ? QStringList() : QStringList{emblem};
}

// Dolphin asks for overlays once per item as it is shown, then only redraws
// when overlaysChanged() is emitted. A background thread re-polls every file
// Dolphin has asked about and emits overlaysChanged() when its status changes.
class SeaDriveOverlayPlugin : public KOverlayIconPlugin
{
    Q_PLUGIN_METADATA(IID "com.seafile.seadrive.overlayiconplugin" FILE "seadrive-overlay.json")
    Q_OBJECT

public:
    SeaDriveOverlayPlugin()
        : poller_([this] { poll(); })
    {
    }

    ~SeaDriveOverlayPlugin() override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        poller_.join();
    }

    // Runs on Dolphin's UI thread, so it never touches the socket: a path seen
    // for the first time gets no emblem until the poller fetches it.
    // Keyed by the path Dolphin asked for, which may run through a symlink
    // (~/Pictures/...); SeaDrive is asked about the resolved path.
    QStringList getOverlays(const QUrl &url) override
    {
        const QString path = url.toLocalFile();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = watched_.constFind(path);
            if (it != watched_.cend())
                return it->emblems;
        }
        const QString real = resolve(path);
        if (real.isEmpty())
            return {};

        std::lock_guard<std::mutex> lock(mutex_);
        watched_.insert(path, {real, {}});
        unfetched_ << path;
        wake_.notify_one();
        return {};
    }

private:
    // The item's folder is resolved once and cached, so browsing a slow
    // network mount resolves each folder once, not each file. Runs on the UI
    // thread only. A link changed while Dolphin runs is picked up on restart.
    QString resolve(const QString &path)
    {
        if (inSeaDrive(path))
            return path;
        const int slash = path.lastIndexOf(QLatin1Char('/'));
        const QString dir = path.left(slash);
        auto it = dirs_.constFind(dir);
        if (it == dirs_.cend())
            it = dirs_.insert(dir, seadrivePath(dir));
        return it->isEmpty() ? QString() : *it + path.mid(slash);
    }

    void poll()
    {
        using clock = std::chrono::steady_clock;
        SeaDriveConn conn;
        auto nextFullPoll = clock::now() + kPollInterval;
        std::unique_lock<std::mutex> lock(mutex_);
        while (true) {
            wake_.wait_until(lock, nextFullPoll, [this] { return stop_ || !unfetched_.isEmpty(); });
            if (stop_)
                return;

            // New paths at once, every watched path each kPollInterval.
            QStringList paths = unfetched_;
            if (clock::now() >= nextFullPoll) {
                paths = watched_.keys();
                nextFullPoll = clock::now() + kPollInterval;
            }
            unfetched_.clear();
            lock.unlock();

            // ponytail: re-polls every file seen this session, one request each;
            // fine for normal browsing, add a batch command to SeaDrive if huge
            // folders make this slow.
            QHash<QString, QStringList> fresh;
            QStringList gone;
            for (const QString &path : paths) {
                if (stop_)
                    break;
                lock.lock();
                const QString real = watched_.value(path).real;
                lock.unlock();
                if (real.isEmpty() || !QFileInfo::exists(real)) {
                    gone << path;
                    continue;
                }
                const auto status = conn.send(QStringLiteral("get-file-status"), real);
                if (!status)
                    break; // SeaDrive not running; retry next tick
                fresh.insert(path, emblemsFor(*status));
            }

            QHash<QString, QStringList> changed;
            lock.lock();
            for (const QString &path : gone)
                watched_.remove(path);
            for (auto it = fresh.cbegin(); it != fresh.cend(); ++it) {
                auto old = watched_.find(it.key());
                if (old != watched_.end() && old->emblems != it.value()) {
                    old->emblems = it.value();
                    changed.insert(it.key(), it.value());
                }
            }
            lock.unlock();

            // Emitted unlocked so a receiver may call getOverlays(). Qt queues
            // it to Dolphin's UI thread (the receiver lives there).
            for (auto it = changed.cbegin(); it != changed.cend(); ++it)
                Q_EMIT overlaysChanged(QUrl::fromLocalFile(it.key()), it.value());
            lock.lock();
        }
    }

    static constexpr std::chrono::seconds kPollInterval{2};

    std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> stop_{false}; // also read unlocked between requests
    struct Watched {
        QString real; // as SeaDrive knows it
        QStringList emblems;
    };
    QHash<QString, Watched> watched_; // by the path Dolphin asked for
    QHash<QString, QString> dirs_; // folder -> its path in SeaDrive, or empty; UI thread only
    QStringList unfetched_; // in watched_, waiting for their first fetch
    std::thread poller_; // last: starts after the members it uses exist
};

#include "seadrive-overlay-plugin.moc"
