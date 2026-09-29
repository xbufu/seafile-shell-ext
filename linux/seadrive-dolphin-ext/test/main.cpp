#include <KOverlayIconPlugin>
#include <KFileItemActions>
#include <KFileItemListProperties>
#include <KFileItem>
#include <QApplication>
#include <QMenu>
#include <QPluginLoader>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <cstdio>
static void setStatus(const char *s, const char *c) {
    QFile f(QDir::homePath() + "/state.json"); f.open(QIODevice::WriteOnly);
    f.write(QByteArray("{\"status\":\"") + s + "\",\"cached\":\"" + c + "\"}");
}
static int fails = 0;
#define CHECK(x) do { if (!(x)) { printf("FAIL: %s (line %d)\n", #x, __LINE__); fails++; } else printf("ok: %s\n", #x); } while (0)
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
    const QString file = mount + "/lib/a.txt";
    setStatus("synced", "cached");

    QPluginLoader loader(argv[1]);
    auto *ov = qobject_cast<KOverlayIconPlugin *>(loader.instance());
    CHECK(ov);
    CHECK(ov->getOverlays(QUrl::fromLocalFile(file)) == QStringList{"emblem-seadrive-done"});
    CHECK(ov->getOverlays(QUrl::fromLocalFile(mount + "/lib")) == QStringList{"emblem-seadrive-done"});
    CHECK(ov->getOverlays(QUrl::fromLocalFile("/etc/passwd")).isEmpty());
    CHECK(ov->getOverlays(QUrl::fromLocalFile(mount + "X/a")).isEmpty());

    QStringList got;
    QObject::connect(ov, &KOverlayIconPlugin::overlaysChanged, [&](const QUrl &u, const QStringList &o) {
        got << u.toLocalFile() + "=" + o.join(",");
    });
    setStatus("locked", "cached");
    QTimer::singleShot(3500, &app, &QApplication::quit); app.exec();
    got.sort();
    CHECK(got == QStringList({file + "=emblem-seadrive-locked-by-others", mount + "/lib=emblem-seadrive-locked-by-others"}));
    printf("  signals: %s\n", qPrintable(got.join(" | ")));

    setStatus("cloud", "uncached");
    CHECK(ov->getOverlays(QUrl::fromLocalFile(file)) == QStringList{"emblem-seadrive-cloud"});
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
    m = menuFor(mount + "/lib", true);
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

    delete ov;  // must join poller thread cleanly
    printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
    return fails != 0;
}
