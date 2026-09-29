#include "seadrive-conn.h"

#include <KOverlayIconPlugin>

#include <QFileInfo>
#include <QHash>
#include <QUrl>

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

    QStringList getOverlays(const QUrl &url) override
    {
        const QString path = url.toLocalFile();
        if (!inSeaDrive(path))
            return {};

        const QStringList overlays = emblemsFor(conn_.send(QStringLiteral("get-file-status"), path).value_or(QString()));
        std::lock_guard<std::mutex> lock(mutex_);
        watched_.insert(path, overlays);
        return overlays;
    }

private:
    void poll()
    {
        // Own connection: SeaDriveConn is per-thread, conn_ belongs to the UI thread.
        SeaDriveConn conn;
        std::unique_lock<std::mutex> lock(mutex_);
        while (!wake_.wait_for(lock, std::chrono::seconds(2), [this] { return stop_; })) {
            const QStringList paths = watched_.keys();
            lock.unlock();

            // ponytail: re-polls every file seen this session, one request each;
            // fine for normal browsing, add a batch command to SeaDrive if huge
            // folders make this slow.
            QHash<QString, QStringList> fresh;
            QStringList gone;
            for (const QString &path : paths) {
                if (!QFileInfo::exists(path)) {
                    gone << path;
                    continue;
                }
                const auto status = conn.send(QStringLiteral("get-file-status"), path);
                if (!status)
                    break; // SeaDrive not running; retry next tick
                fresh.insert(path, emblemsFor(*status));
            }

            lock.lock();
            for (const QString &path : gone)
                watched_.remove(path);
            for (auto it = fresh.cbegin(); it != fresh.cend(); ++it) {
                auto old = watched_.find(it.key());
                if (old != watched_.end() && *old != it.value()) {
                    *old = it.value();
                    // Queued to Dolphin's UI thread by Qt (receiver lives there).
                    Q_EMIT overlaysChanged(QUrl::fromLocalFile(it.key()), it.value());
                }
            }
        }
    }

    SeaDriveConn conn_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    QHash<QString, QStringList> watched_;
    std::thread poller_; // last: starts after the members it uses exist
};

#include "seadrive-overlay-plugin.moc"
