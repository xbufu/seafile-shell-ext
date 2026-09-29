#include "seadrive-conn.h"

#include <KAbstractFileItemActionPlugin>
#include <KFileItem>
#include <KFileItemListProperties>
#include <KPluginFactory>

#include <QAction>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>

// Right-click menus. Items that change a file's lock or cache state need no
// refresh here: the overlay plugin picks up the new status on its next poll.
class SeaDriveActionPlugin : public KAbstractFileItemActionPlugin
{
    Q_OBJECT

public:
    SeaDriveActionPlugin(QObject *parent, const QVariantList &)
        : KAbstractFileItemActionPlugin(parent)
    {
    }

    QList<QAction *> actions(const KFileItemListProperties &props, QWidget *parentWidget) override
    {
        const KFileItemList items = props.items();
        if (items.size() != 1 || !items.first().isLocalFile())
            return {};

        const QString path = items.first().localPath();
        auto *menu = new QMenu(parentWidget);
        // Dolphin passes the current folder for a right-click on empty space.
        const bool ok = path == seadriveMountDir() ? addMountMenu(menu)
                                                   : inSeaDrive(path) && addFileMenu(menu, path, items.first().isDir());
        if (!ok) {
            delete menu;
            return {};
        }

        auto *top = new QAction(QStringLiteral("SeaDrive"), parentWidget);
        top->setMenu(menu);
        return {top};
    }

private:
    static void add(QMenu *menu, const QString &label, const QString &cmd, const QString &arg = QString())
    {
        connect(menu->addAction(label), &QAction::triggered, menu, [cmd, arg] {
            if (!SeaDriveConn().send(cmd, arg))
                qWarning("SeaDrive: '%s' failed for '%s'", qPrintable(cmd), qPrintable(arg));
        });
    }

    static bool addFileMenu(QMenu *menu, const QString &path, bool isDir)
    {
        SeaDriveConn conn;
        if (conn.send(QStringLiteral("is-file-in-repo"), path) != QStringLiteral("true"))
            return false;

        if (isDir) {
            add(menu, QStringLiteral("Get Upload Link"), QStringLiteral("get-upload-link"), path);
            add(menu, QStringLiteral("Remove cache"), QStringLiteral("uncache"), path);
            add(menu, QStringLiteral("Download"), QStringLiteral("download"), path);
            return true;
        }

        const auto status = conn.send(QStringLiteral("get-file-status"), path);
        const auto cached = conn.send(QStringLiteral("is-file-cached"), path);
        if (!status || !cached)
            return false;

        if (*status == QStringLiteral("locked_by_me"))
            add(menu, QStringLiteral("Unlock File"), QStringLiteral("unlock-file"), path);
        else if (*status != QStringLiteral("locked"))
            add(menu, QStringLiteral("Lock File"), QStringLiteral("lock-file"), path);

        if (*cached == QStringLiteral("cached"))
            add(menu, QStringLiteral("Remove cache"), QStringLiteral("uncache"), path);
        else
            add(menu, QStringLiteral("Download"), QStringLiteral("download"), path);

        add(menu, QStringLiteral("Get Share Link"), QStringLiteral("get-share-link"), path);
        add(menu, QStringLiteral("Get Internal Link"), QStringLiteral("get-internal-link"), path);
        add(menu, QStringLiteral("Show File History"), QStringLiteral("show-history"), path);
        return true;
    }

    static bool addMountMenu(QMenu *menu)
    {
        add(menu, QStringLiteral("Transfer Progress"), QStringLiteral("show-transfer-progress"));
        add(menu, QStringLiteral("Show File Sync Errors"), QStringLiteral("show-file-sync-errors"));
        add(menu, QStringLiteral("Show Encrypted Libraries"), QStringLiteral("show-encrypted-libraries"));
        add(menu, QStringLiteral("Open Logs Folder"), QStringLiteral("open-logs-folder"));
        add(menu, QStringLiteral("Settings"), QStringLiteral("show-settings"));

        QMenu *accounts = menu->addMenu(QStringLiteral("Accounts"));
        const QString reply = SeaDriveConn().send(QStringLiteral("show-accounts")).value_or(QString());
        const QJsonArray list = QJsonDocument::fromJson(reply.toUtf8()).array();
        for (const QJsonValue &account : list) {
            const QString signature = account[QStringLiteral("signature")].toString();
            QMenu *sub = accounts->addMenu(account[QStringLiteral("name")].toString());
            add(sub, QStringLiteral("Resync"), QStringLiteral("resync-account"), signature);
            add(sub, QStringLiteral("Delete"), QStringLiteral("delete-account"), signature);
        }
        add(accounts, QStringLiteral("Add an account"), QStringLiteral("add-account"));

        add(menu, QStringLiteral("About"), QStringLiteral("show-about"));
        add(menu, QStringLiteral("Online Help"), QStringLiteral("show-help"));
        add(menu, QStringLiteral("Quit"), QStringLiteral("quit"));
        return true;
    }
};

K_PLUGIN_CLASS_WITH_JSON(SeaDriveActionPlugin, "seadrive-action.json")

#include "seadrive-action-plugin.moc"
