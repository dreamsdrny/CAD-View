#pragma once
#include "core/Document.h"
#include <QObject>
#include <QDateTime>
#include <QFutureWatcher>
#include <QLockFile>
#include <QTimer>
#include <memory>
#include <optional>

namespace cad::ui {
struct RecoveryEntry {
    QString filePath, title, originalPath, error;
    QDateTime savedAt;
};

// Recovery snapshots are independent of the original drawing. A per-session
// lock prevents another running CAD instance from offering live snapshots.
class RecoveryManager : public QObject {
    Q_OBJECT
public:
    explicit RecoveryManager(QString directory={}, QObject* parent=nullptr);
    ~RecoveryManager() override;
    QString track(std::shared_ptr<Document> document, QString recoveredFrom={});
    void forget(const QString& id);
    void captureNow();
    void cleanSavedDocuments();
    bool busy() const { return watcher_.isRunning() || !inFlight_.isEmpty(); }
    QString directory() const { return directory_; }
    QVector<RecoveryEntry> available() const;
    Document restore(const RecoveryEntry& entry) const;
    bool discard(const RecoveryEntry& entry, QString* error=nullptr) const;
signals:
    void snapshotSaved(QString id);
    void failure(QString message);
private:
    struct Tracked {
        std::shared_ptr<Document> document;
        std::optional<quint64> savedRevision;
        QString savedTitle, savedPath, recoveredFrom;
    };
    struct WriteResult { QString error; };
    QString directory_, sessionDirectory_, inFlight_;
    QHash<QString,Tracked> tracked_;
    std::unique_ptr<QLockFile> lock_;
    QTimer timer_;
    QFutureWatcher<WriteResult> watcher_;
    QStringList queue_;
    quint64 writingRevision_=0;
    QString writingTitle_, writingPath_;
    bool ensureSession();
    void writeNext();
    void finishWrite();
    void removeSnapshot(const QString& id);
    bool validEntryPath(const QString& path) const;
};
}
