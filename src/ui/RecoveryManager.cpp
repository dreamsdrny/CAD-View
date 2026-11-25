#include "RecoveryManager.h"
#include "io/DxfAdapter.h"
#include <QtConcurrent>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <stdexcept>

namespace cad::ui {
namespace {
const QByteArray magic("QiaoRecovery1\n");
constexpr qint64 maxPayload=256LL*1024*1024;
constexpr quint32 maxHeader=64*1024;
QString token() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
void fail(const QString& text) { throw std::runtime_error(text.toUtf8().constData()); }
QJsonObject readHeader(QFile& file) {
    if(!file.open(QIODevice::ReadOnly))fail("无法读取恢复快照："+file.errorString());
    if(file.size()>maxPayload+maxHeader+magic.size()+4)fail("恢复快照超过大小限制。");
    if(file.read(magic.size())!=magic)fail("恢复快照文件头无效。");
    QDataStream stream(&file);quint32 size=0;stream>>size;
    if(stream.status()!=QDataStream::Ok||size==0||size>maxHeader)fail("恢复快照元数据长度无效。");
    auto bytes=file.read(size);QJsonParseError error;
    auto json=QJsonDocument::fromJson(bytes,&error);
    if(bytes.size()!=size||error.error!=QJsonParseError::NoError||!json.isObject())fail("恢复快照元数据损坏。");
    auto header=json.object();auto checksum=header.value("sha256").toString();
    if(header.value("version").toInt()!=1||!header.value("title").isString()||!header.value("originalPath").isString()
       ||!QDateTime::fromString(header.value("savedAt").toString(),Qt::ISODateWithMs).isValid()
       ||checksum.size()!=64||QByteArray::fromHex(checksum.toLatin1()).size()!=32)
        fail("恢复快照元数据不完整或版本不受支持。");
    return header;
}
RecoveryEntry entryFor(const QString& path) {
    RecoveryEntry entry;entry.filePath=path;
    try {
        QFile file(path);auto header=readHeader(file);
        entry.title=header.value("title").toString();entry.originalPath=header.value("originalPath").toString();
        entry.savedAt=QDateTime::fromString(header.value("savedAt").toString(),Qt::ISODateWithMs);
    } catch(const std::exception& e) { entry.error=QString::fromUtf8(e.what());entry.title=QFileInfo(path).fileName(); }
    return entry;
}
void writeSnapshot(const Document& document,const QString& path) {
    auto bytes=io::DxfAdapter::serialize(document);
    if(bytes.size()>maxPayload)fail("图纸超过恢复快照的 256 MiB 限制，请手动保存。");
    QJsonObject metadata{{"version",1},{"title",document.title},{"originalPath",document.path},
        {"currentLayer",document.currentLayer},{"savedAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"sha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())}};
    auto header=QJsonDocument(metadata).toJson(QJsonDocument::Compact);
    if(header.size()>maxHeader)fail("恢复快照元数据超过限制。");
    QSaveFile file(path);file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly))fail("无法创建恢复快照："+file.errorString());
    if(file.write(magic)!=magic.size())fail("无法写入恢复快照文件头。");
    QDataStream stream(&file);stream<<quint32(header.size());
    if(stream.status()!=QDataStream::Ok||file.write(header)!=header.size()||file.write(bytes)!=bytes.size()||!file.commit())
        fail("无法原子提交恢复快照："+file.errorString());
}
}
RecoveryManager::RecoveryManager(QString directory,QObject* parent) : QObject(parent) {
    if(directory.isEmpty())directory=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/recovery";
    directory_=QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    connect(&timer_,&QTimer::timeout,this,&RecoveryManager::captureNow);
    connect(&watcher_,&QFutureWatcher<WriteResult>::finished,this,&RecoveryManager::finishWrite);
    timer_.setTimerType(Qt::CoarseTimer);timer_.start(60000);
}
RecoveryManager::~RecoveryManager() {
    timer_.stop();disconnect(&watcher_,nullptr,this,nullptr);watcher_.waitForFinished();
    // Never erase unsaved snapshots merely because a window object was destroyed.
    // Explicitly closed documents can no longer leave a late worker snapshot.
    if(!inFlight_.isEmpty()&&(!tracked_.contains(inFlight_)||!tracked_[inFlight_].document->modified()))removeSnapshot(inFlight_);
    lock_.reset();
    if(!sessionDirectory_.isEmpty())QDir().rmdir(sessionDirectory_);
}
QString RecoveryManager::track(std::shared_ptr<Document> document,QString recoveredFrom) {
    auto id=token();tracked_.insert(id,{std::move(document),std::nullopt,{},{},std::move(recoveredFrom)});return id;
}
void RecoveryManager::removeSnapshot(const QString& id) {
    if(sessionDirectory_.isEmpty())return;
    auto path=QDir(sessionDirectory_).filePath(id+".cad-recovery");
    if(QFile::exists(path)&&!QFile::remove(path))emit failure("无法清理恢复快照："+path);
}
void RecoveryManager::forget(const QString& id) {
    auto it=tracked_.find(id);if(it==tracked_.end())return;
    if(!it->recoveredFrom.isEmpty()) {
        QString error;if(!discard(entryFor(it->recoveredFrom),&error))emit failure(error);
    }
    tracked_.erase(it);queue_.removeAll(id);
    if(inFlight_!=id)removeSnapshot(id);
}
void RecoveryManager::cleanSavedDocuments() {
    for(auto it=tracked_.begin();it!=tracked_.end();++it)if(!it->document->modified()&&it.key()!=inFlight_) {
        removeSnapshot(it.key());it->savedRevision.reset();
        if(!it->recoveredFrom.isEmpty()) {
            QString error;if(discard(entryFor(it->recoveredFrom),&error))it->recoveredFrom.clear();else emit failure(error);
        }
    }
}
bool RecoveryManager::ensureSession() {
    if(lock_)return true;
    auto session=QDir(directory_).filePath(token());
    if(!QDir().mkpath(session)){emit failure("无法创建自动恢复目录："+directory_);return false;}
    auto lock=std::make_unique<QLockFile>(QDir(session).filePath("session.lock"));
    lock->setStaleLockTime(0);
    if(!lock->tryLock(0)){emit failure("无法锁定自动恢复目录："+session);QDir().rmdir(session);return false;}
    sessionDirectory_=session;lock_=std::move(lock);return true;
}
void RecoveryManager::captureNow() {
    cleanSavedDocuments();
    for(auto it=tracked_.cbegin();it!=tracked_.cend();++it) {
        if(it->document->modified()&&(!it->savedRevision||*it->savedRevision!=it->document->revision()
           ||it->savedTitle!=it->document->title||it->savedPath!=it->document->path))
            if(!queue_.contains(it.key()))queue_.append(it.key());
    }
    if(!busy())writeNext();
}
void RecoveryManager::writeNext() {
    while(!queue_.isEmpty()) {
        auto id=queue_.takeFirst();auto it=tracked_.find(id);
        if(it==tracked_.end()||!it->document->modified())continue;
        if(it->savedRevision&&*it->savedRevision==it->document->revision()&&it->savedTitle==it->document->title&&it->savedPath==it->document->path)continue;
        if(!ensureSession()){queue_.clear();return;}
        Document snapshot=*it->document;inFlight_=id;writingRevision_=snapshot.revision();writingTitle_=snapshot.title;writingPath_=snapshot.path;
        auto path=QDir(sessionDirectory_).filePath(id+".cad-recovery");
        watcher_.setFuture(QtConcurrent::run([snapshot=std::move(snapshot),path]{
            WriteResult result;try{writeSnapshot(snapshot,path);}catch(const std::exception& e){result.error=QString::fromUtf8(e.what());}return result;
        }));return;
    }
}
void RecoveryManager::finishWrite() {
    auto result=watcher_.result();auto id=inFlight_;inFlight_.clear();auto it=tracked_.find(id);
    if(it==tracked_.end()||!it->document->modified())removeSnapshot(id);
    else if(result.error.isEmpty()) {
        it->savedRevision=writingRevision_;it->savedTitle=writingTitle_;it->savedPath=writingPath_;
        // Keep the old snapshot until the recovered document has its own durable copy.
        if(!it->recoveredFrom.isEmpty()) {
            QString error;if(discard(entryFor(it->recoveredFrom),&error))it->recoveredFrom.clear();else emit failure(error);
        }
        emit snapshotSaved(id);
    }
    if(!result.error.isEmpty())emit failure("自动恢复失败（原图未改动）："+result.error);
    cleanSavedDocuments();writeNext();
}
bool RecoveryManager::validEntryPath(const QString& path) const {
    QFileInfo base(directory_);
    QString root=base.canonicalFilePath();
    QFileInfo file(path);QFileInfo session(file.absolutePath());
    QString sessionCanonical=session.canonicalFilePath();
    if(root.isEmpty()||sessionCanonical.isEmpty())return false;
    // The session directory must resolve directly under the recovery root, and
    // an existing snapshot must resolve inside that same session directory.
    if(QFileInfo(sessionCanonical).absolutePath()!=root)return false;
    if(file.exists()) {
        QString fileCanonical=file.canonicalFilePath();
        if(fileCanonical.isEmpty()||QFileInfo(fileCanonical).absolutePath()!=sessionCanonical)return false;
    }
    QFileInfo canonicalSession(sessionCanonical);
    return !session.isSymLink()&&!canonicalSession.isSymLink()
        &&!QUuid(canonicalSession.fileName()).isNull()&&file.fileName().endsWith(".cad-recovery")
        &&!QUuid(file.completeBaseName()).isNull();
}
QVector<RecoveryEntry> RecoveryManager::available() const {
    QVector<RecoveryEntry> entries;QDir root(directory_);
    for(const auto& session:root.entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks)) {
        if(QUuid(session.fileName()).isNull())continue;
        QLockFile lock(QDir(session.absoluteFilePath()).filePath("session.lock"));lock.setStaleLockTime(0);
        if(!lock.tryLock(0))continue;
        for(const auto& file:QDir(session.absoluteFilePath()).entryInfoList({"*.cad-recovery"},QDir::Files|QDir::NoSymLinks))
            if(validEntryPath(file.absoluteFilePath()))entries.append(entryFor(file.absoluteFilePath()));
    }
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.savedAt>b.savedAt;});return entries;
}
Document RecoveryManager::restore(const RecoveryEntry& entry) const {
    if(!validEntryPath(entry.filePath))fail("恢复快照路径无效。");
    QString path=QFileInfo(entry.filePath).canonicalFilePath();
    if(path.isEmpty()||!validEntryPath(path))fail("恢复快照路径无效。");
    QLockFile lock(QDir(QFileInfo(path).absolutePath()).filePath("session.lock"));lock.setStaleLockTime(0);
    if(!lock.tryLock(0))fail("该恢复快照仍被另一个 CAD 实例使用。");
    QFile file(path);auto header=readHeader(file);auto bytes=file.read(maxPayload+1);
    if(bytes.isEmpty()||bytes.size()>maxPayload||QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!=header.value("sha256").toString().toLatin1())
        fail("恢复快照内容损坏或不完整；原图未改动。");
    auto document=io::DxfAdapter::parse(bytes);
    document.title=header.value("title").toString()+"（已恢复）";
    // Never turn Ctrl+S into an accidental overwrite of the original DWG/DXF.
    document.path.clear();auto layer=header.value("currentLayer").toString();
    if(document.layers.contains(layer))document.currentLayer=layer;
    document.markUnsaved();return document;
}
bool RecoveryManager::discard(const RecoveryEntry& entry,QString* error) const {
    auto reject=[&](QString text){if(error)*error=std::move(text);return false;};
    if(!validEntryPath(entry.filePath))return reject("恢复快照路径无效。");
    if(!QFile::exists(entry.filePath))return true;
    QString path=QFileInfo(entry.filePath).canonicalFilePath();
    if(path.isEmpty()||!validEntryPath(path))return reject("恢复快照路径无效。");
    auto session=QFileInfo(path).absolutePath();
    QLockFile lock(QDir(session).filePath("session.lock"));lock.setStaleLockTime(0);
    if(!lock.tryLock(0))return reject("该恢复快照仍被另一个 CAD 实例使用。");
    if(!QFile::remove(path))return reject("无法删除恢复快照："+path);
    lock.unlock();QDir().rmdir(session);return true;
}
}
