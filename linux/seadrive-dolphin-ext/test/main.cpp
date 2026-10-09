#include "seadrive-conn.h"
#include <KOverlayIconPlugin>
#include <KFileItemActions>
#include <KFileItemListProperties>
#include <KFileItem>
#include <QApplication>
#include <QMenu>
#include <QPluginLoader>
#include <QElapsedTimer>
#include <QThread>
#include <QHash>
#include <QFile>
#include <QDir>
#include <cstdio>
#include <type_traits>
#include <sys/stat.h>
// SeaDriveConn owns a socket fd: a copy would close it twice.
static_assert(!std::is_copy_constructible_v<SeaDriveConn> && !std::is_copy_assignable_v<SeaDriveConn>);
static void setStatus(const char *s, const char *c, double delay = 0) {
    QFile f(QDir::homePath() + "/state.json"); f.open(QIODevice::WriteOnly);
    f.write(QByteArray("{\"status\":\"") + s + "\",\"cached\":\"" + c + "\",\"delay\":" + QByteArray::number(delay) + "}");
}
static int fails = 0;
#define CHECK(x) do { if (!(x)) { printf("FAIL: %s (line %d)\n", #x, __LINE__); fails++; } else printf("ok: %s\n", #x); } while (0)
template <class Pred> static bool waitFor(Pred done, int ms) {
    QElapsedTimer t; t.start();
    while (!done() && t.elapsed() < ms) { QCoreApplication::processEvents(); QThread::msleep(5); }
    return done();
}
static void walk(QMenu *m, QStringList &out, QString indent = "") {
    for (QAction *a : m->actions()) { out << indent + a->text(); if (a->menu()) walk(a->menu(), out, indent + "  "); }
}
static QStringList menuFor(const QString &path, bool dir) {
    KFileItem item(QUrl::fromLocalFile(path), dir ? "inode/directory" : "text/plain", dir ? S_IFDIR : S_IFREG);
    KFileItemActions actions; actions.setItemListProperties(KFileItemListProperties(KFileItemList{item}));
    QMenu menu; actions.addActionsTo(&menu);
    QStringList out; walk(&menu, out);
    return out;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QString mount = QDir::homePath() + "/SeaDrive";
    const QString lib = mount + "/lib";
    const QString file = lib + "/a.txt";
    const QStringList done{"emblem-seadrive-done"}, lockedOthers{"emblem-seadrive-locked-by-others"}, cloud{"emblem-seadrive-cloud"};
    setStatus("synced", "cached");

    QPluginLoader loader(argv[1]);
    auto *ov = qobject_cast<KOverlayIconPlugin *>(loader.instance());
    CHECK(ov);
    if (!ov) return 1;
    QHash<QString, QStringList> latest;
    QObject::connect(ov, &KOverlayIconPlugin::overlaysChanged, [&](const QUrl &u, const QStringList &o) { latest[u.toLocalFile()] = o; });

    // New paths are fetched in the background right away, not on the next 2s tick.
    ov->getOverlays(QUrl::fromLocalFile(file));
    ov->getOverlays(QUrl::fromLocalFile(lib));
    CHECK(waitFor([&] { return latest.value(file) == done && latest.value(lib) == done; }, 1000));
    CHECK(ov->getOverlays(QUrl::fromLocalFile(file)) == done);
    CHECK(ov->getOverlays(QUrl::fromLocalFile("/etc/passwd")).isEmpty());
    CHECK(ov->getOverlays(QUrl::fromLocalFile(mount + "X/a")).isEmpty());

    setStatus("locked", "cached");
    CHECK(waitFor([&] { return latest.value(file) == lockedOthers && latest.value(lib) == lockedOthers; }, 3500));
    setStatus("cloud", "uncached");
    CHECK(waitFor([&] { return latest.value(file) == cloud; }, 3500));
    CHECK(ov->getOverlays(QUrl::fromLocalFile(file)) == cloud);
    setStatus("locked", "cached");

    QStringList m = menuFor(file, false);
    printf("  file menu (locked): %s\n", qPrintable(m.join(" / ")));
    CHECK(m.contains("SeaDrive") && !m.contains("  Lock File") && m.contains("  Remove cache") && m.contains("  Show File History"));
    setStatus("locked_by_me", "cloud");
    m = menuFor(file, false);
    CHECK(m.contains("  Unlock File") && m.contains("  Download"));
    setStatus("synced", "cloud");
    m = menuFor(file, false);
    CHECK(m.contains("  Lock File"));
    m = menuFor(lib, true);
    printf("  dir menu: %s\n", qPrintable(m.join(" / ")));
    CHECK(m.contains("  Get Upload Link"));
    m = menuFor(mount, true);
    printf("  mount menu: %s\n", qPrintable(m.join(" / ")));
    CHECK(m.contains("    me@x") && m.contains("      Resync") && m.contains("  Quit"));
    CHECK(!menuFor("/etc/passwd", false).contains("SeaDrive"));

    // trigger an action
    KFileItem item(QUrl::fromLocalFile(file), "text/plain", S_IFREG);
    KFileItemActions actions; actions.setItemListProperties(KFileItemListProperties(KFileItemList{item}));
    QMenu menu; actions.addActionsTo(&menu);
    for (QAction *a : menu.actions()) if (a->menu()) for (QAction *s : a->menu()->actions()) if (s->text() == "Get Share Link") s->trigger();
    QFile log(QDir::homePath() + "/requests.log"); log.open(QIODevice::ReadOnly);
    CHECK(log.readAll().contains("get-share-link\t" + file.toUtf8()));

    // A slow SeaDrive must not stall Dolphin's UI thread.
    setStatus("synced", "cached", 2);
    QElapsedTimer t; t.start();
    ov->getOverlays(QUrl::fromLocalFile(lib + "/slow.txt"));
    CHECK(t.elapsed() < 200);

    // Closing Dolphin must not wait for a whole poll pass (100 files x 50ms).
    setStatus("synced", "cached", 0.05);
    for (int i = 0; i < 100; ++i) {
        const QString p = lib + "/f" + QString::number(i);
        QFile(p).open(QIODevice::WriteOnly);
        ov->getOverlays(QUrl::fromLocalFile(p));
    }
    waitFor([] { return false; }, 2100);  // let requests made at the 2s delay finish
    t.restart();
    delete ov;  // must join poller thread cleanly
    CHECK(t.elapsed() < 1000);
    printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
    return fails != 0;
}
