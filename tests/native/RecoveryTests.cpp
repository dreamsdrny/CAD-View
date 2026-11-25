#include "ui/RecoveryManager.h"
#include "io/DxfAdapter.h"
#include "TextDimensionFixture.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDirIterator>
#include <QProcess>
#include <QSignalSpy>

using namespace cad;
using namespace cad::ui;
namespace {
std::shared_ptr<Document> drawing(double length=10) {
    auto document=std::make_shared<Document>();Entity line;line.points={{0,0},{length,0}};
    document->commit("draw",{},{line});return document;
}
QStringList snapshots(const QString& root) {
    QStringList files;QDirIterator it(root,{"*.cad-recovery"},QDir::Files,QDirIterator::Subdirectories);
    while(it.hasNext())files.append(it.next());
    return files;
}
QByteArray readBytes(const QString& path) {QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return file.readAll();}
}
class RecoveryTests : public QObject {
    Q_OBJECT
private slots:
    void deletedExternalDimensionsStayDeletedAfterRecovery() {
        QTemporaryDir temp;auto document=std::make_shared<Document>(io::DxfAdapter::parse(editableDimensionFixture()));Entity label;
        for(const auto& e:document->entities)if(!e.dimensionTextKey.isEmpty()){label=e;break;}
        QVERIFY(document->deleteEntities({label.id}));auto count=document->entities.size();
        {RecoveryManager manager(temp.path());QSignalSpy saved(&manager,&RecoveryManager::snapshotSaved);QSignalSpy errors(&manager,&RecoveryManager::failure);manager.track(document);manager.captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);QVERIFY(errors.isEmpty());}
        RecoveryManager manager(temp.path());auto entries=manager.available();QCOMPARE(entries.size(),1);auto restored=manager.restore(entries.first());QCOMPARE(restored.entities.size(),count);
        QCOMPARE(io::DxfAdapter::serialize(restored).count("0\nDIMENSION\n"),1);
    }
    void revisionTracksBranchesAndRestoredState() {
        Document document;QCOMPARE(document.revision(),quint64(0));QVERIFY(!document.modified());
        document.markUnsaved();QVERIFY(document.modified());QVERIFY(!document.canUndo());
        document.markSaved();QVERIFY(!document.modified());
        Entity line;line.points={{0,0},{10,0}};document.commit("draw",{},{line});auto revision=document.revision();QVERIFY(revision>0);
        document.markSaved();QVERIFY(document.undo());QVERIFY(document.revision()>revision);revision=document.revision();
        QVERIFY(document.redo());QVERIFY(document.revision()>revision);QVERIFY(!document.modified());
        document.setUnits(1);revision=document.revision();document.setUnits(1);QCOMPARE(document.revision(),revision);
        QVERIFY(document.undo());document.setUnits(5);QVERIFY(document.modified());QVERIFY(!document.canRedo());
    }
    void untitledRecoveryIsUnsavedAndOriginalUntouched() {
        QTemporaryDir temp;QVERIFY(temp.isValid());auto root=temp.filePath("recovery");
        auto original=temp.filePath("原图[01].dwg");QFile source(original);QVERIFY(source.open(QIODevice::WriteOnly));source.write("AC1032 original");source.close();
        auto before=readBytes(original);auto document=drawing();document->title="中文图纸";document->path=original;
        auto layers=document->layers;layers.insert("编辑层",Layer{"编辑层"});document->changeLayers("layer",layers);document->currentLayer="编辑层";
        {
            RecoveryManager manager(root);QSignalSpy saved(&manager,&RecoveryManager::snapshotSaved);QSignalSpy errors(&manager,&RecoveryManager::failure);
            manager.track(document);manager.captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);QVERIFY(errors.isEmpty());
            QCOMPARE(snapshots(root).size(),1);QVERIFY(manager.available().isEmpty());
            RecoveryManager second(root);QVERIFY(second.available().isEmpty());
            // Capture a no-op only once, even though the document remains dirty.
            manager.captureNow();QTest::qWait(30);QCOMPARE(saved.size(),1);
        }
        RecoveryManager manager(root);auto entries=manager.available();QCOMPARE(entries.size(),1);
        QCOMPARE(entries[0].title,QString("中文图纸"));QCOMPARE(entries[0].originalPath,original);QVERIFY(entries[0].error.isEmpty());
        auto restored=manager.restore(entries[0]);QVERIFY(restored.modified());QVERIFY(restored.path.isEmpty());QVERIFY(!restored.canUndo());
        QCOMPARE(restored.title,QString("中文图纸（已恢复）"));QCOMPARE(restored.currentLayer,QString("编辑层"));QCOMPARE(restored.entities.size(),1);
        QCOMPARE(restored.entities.begin()->points.last(),QPointF(10,0));QCOMPARE(readBytes(original),before);
        QVERIFY(QFile::exists(entries[0].filePath));QVERIFY(manager.discard(entries[0]));QVERIFY(manager.available().isEmpty());QCOMPARE(readBytes(original),before);
    }
    void multipleDocumentsAndLatestRevision() {
        QTemporaryDir temp;auto first=drawing(10),second=drawing(20);first->title="first";second->title="second";
        {
            RecoveryManager manager(temp.path());QSignalSpy saved(&manager,&RecoveryManager::snapshotSaved);
            manager.track(first);manager.track(second);manager.captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),2,5000);
            first->setUnits(1);manager.captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),3,5000);
            second->markSaved();manager.cleanSavedDocuments();QCOMPARE(snapshots(temp.path()).size(),1);
            first->title="renamed";manager.captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),4,5000);
        }
        RecoveryManager manager(temp.path());auto entries=manager.available();QCOMPARE(entries.size(),1);QCOMPARE(entries.first().title,QString("renamed"));
        auto restored=manager.restore(entries.first());QCOMPARE(restored.units,1);QCOMPARE(restored.entities.begin()->points.last(),QPointF(10,0));
    }
    void editDuringSnapshotRemainsDirty() {
        QTemporaryDir temp;auto document=drawing();
        {
            RecoveryManager manager(temp.path());QSignalSpy saved(&manager,&RecoveryManager::snapshotSaved);manager.track(document);manager.captureNow();
            document->setUnits(1); // The in-flight value snapshot must not acquire this edit.
            QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);QVERIFY(document->modified());
            manager.captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),2,5000);QVERIFY(document->modified());
        }
        RecoveryManager manager(temp.path());auto entries=manager.available();QCOMPARE(entries.size(),1);QCOMPARE(manager.restore(entries.first()).units,1);
    }
    void explicitCloseWhileWritingDoesNotResurrectSnapshot() {
        QTemporaryDir temp;auto document=drawing();
        {
            RecoveryManager manager(temp.path());auto id=manager.track(document);manager.captureNow();manager.forget(id);
            QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),5000);QVERIFY(snapshots(temp.path()).isEmpty());
        }
        RecoveryManager manager(temp.path());QVERIFY(manager.available().isEmpty());
        {
            RecoveryManager other(temp.path());auto id=other.track(drawing());other.captureNow();other.forget(id);
            // Exercise destructor wait, without a delivered finished signal.
        }
        QVERIFY(snapshots(temp.path()).isEmpty());
    }
    void saveDuringSnapshotCleansLateResult() {
        QTemporaryDir temp;auto document=drawing();
        RecoveryManager manager(temp.path());manager.track(document);manager.captureNow();document->markSaved();manager.cleanSavedDocuments();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),5000);QVERIFY(snapshots(temp.path()).isEmpty());QVERIFY(!document->modified());
    }
    void savedDocumentDestroyedDuringWriteDoesNotLeaveRecovery() {
        QTemporaryDir temp;auto document=drawing();
        {
            RecoveryManager manager(temp.path());manager.track(document);manager.captureNow();document->markSaved();
            // No event loop delivery: destructor must still remove a late result.
        }
        QVERIFY(snapshots(temp.path()).isEmpty());
    }
    void revertingToSavedRevisionRemovesObsoleteSnapshot() {
        QTemporaryDir temp;auto document=drawing();RecoveryManager manager(temp.path());auto id=manager.track(document);
        QSignalSpy saved(&manager,&RecoveryManager::snapshotSaved);manager.captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);
        QVERIFY(document->undo());QVERIFY(!document->modified());manager.cleanSavedDocuments();QVERIFY(snapshots(temp.path()).isEmpty());
        QVERIFY(document->redo());manager.captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),2,5000);
        manager.forget(id);QVERIFY(snapshots(temp.path()).isEmpty());
    }
    void largeSnapshotRunsWithoutBlockingEventLoop() {
        QTemporaryDir temp;auto document=std::make_shared<Document>();
        for(int i=0;i<50000;++i){Entity line;line.points={{double(i),0},{double(i),10}};document->load(line);}
        document->markUnsaved();
        {
            RecoveryManager manager(temp.path());QSignalSpy saved(&manager,&RecoveryManager::snapshotSaved);manager.track(document);
            bool heartbeat=false;QTimer::singleShot(0,&manager,[&]{heartbeat=true;});manager.captureNow();
            QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,15000);QVERIFY(heartbeat);QVERIFY(document->modified());
        }
        RecoveryManager manager(temp.path());auto entries=manager.available();QCOMPARE(entries.size(),1);QCOMPARE(manager.restore(entries.first()).entities.size(),50000);
    }
    void corruptFilesAreIsolatedAndDoNotPreventOtherRecovery() {
        QTemporaryDir temp;
        {
            RecoveryManager manager(temp.path());QSignalSpy saved(&manager,&RecoveryManager::snapshotSaved);
            manager.track(drawing(10));manager.track(drawing(20));manager.captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),2,5000);
        }
        auto files=snapshots(temp.path());QCOMPARE(files.size(),2);
        QFile corrupt(files.first());QVERIFY(corrupt.open(QIODevice::WriteOnly|QIODevice::Truncate));corrupt.write("broken");corrupt.close();
        RecoveryManager manager(temp.path());auto entries=manager.available();QCOMPARE(entries.size(),2);int restored=0,broken=0;
        for(const auto& entry:entries) {
            if(entry.error.isEmpty()){QVERIFY(manager.restore(entry).modified());++restored;}
            else {QVERIFY_EXCEPTION_THROWN(manager.restore(entry),std::runtime_error);++broken;}
        }
        QCOMPARE(restored,1);QCOMPARE(broken,1);
        for(const auto& entry:entries)QVERIFY(manager.discard(entry));
        QVERIFY(manager.available().isEmpty());
    }
    void payloadChecksumRejectsTruncation() {
        QTemporaryDir temp;
        {
            RecoveryManager manager(temp.path());QSignalSpy saved(&manager,&RecoveryManager::snapshotSaved);manager.track(drawing());manager.captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);
        }
        RecoveryManager manager(temp.path());auto entries=manager.available();QCOMPARE(entries.size(),1);
        QFile corrupt(entries.first().filePath);QVERIFY(corrupt.open(QIODevice::ReadWrite));QVERIFY(corrupt.resize(corrupt.size()-10));corrupt.close();
        entries=manager.available();QVERIFY(entries.first().error.isEmpty());QVERIFY_EXCEPTION_THROWN(manager.restore(entries.first()),std::runtime_error);
        QVERIFY(QFile::exists(entries.first().filePath));
    }
    void failedWriteRetainsPreviousDurableSnapshot() {
        QTemporaryDir temp;auto document=drawing();QString id;
        {
            RecoveryManager manager(temp.path());QSignalSpy saved(&manager,&RecoveryManager::snapshotSaved),errors(&manager,&RecoveryManager::failure);
            id=manager.track(document);manager.captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);
            auto files=snapshots(temp.path());QCOMPARE(files.size(),1);auto before=readBytes(files.first());
            Entity text;text.kind=Kind::Text;text.points={{0,0}};text.text="invalid\nTEXT";
            document->commit("invalid",{},{text});manager.captureNow();QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(),5000);
            QCOMPARE(saved.size(),1);QCOMPARE(readBytes(files.first()),before);QVERIFY(document->modified());
        }
        RecoveryManager manager(temp.path());auto entries=manager.available();QCOMPARE(entries.size(),1);QCOMPARE(manager.restore(entries.first()).entities.size(),1);
    }
    void sourceRecordsAndDimensionEditsSurviveRecovery() {
        QTemporaryDir temp;auto document=std::make_shared<Document>(io::DxfAdapter::parse(editableDimensionFixture()));EntityId target=0;
        for(const auto& entity:document->entities)if(!entity.dimensionTextKey.isEmpty()){target=entity.id;break;}
        QVERIFY(target);QVERIFY(document->editText(target,"<>%%P0.05"));auto expected=io::DxfAdapter::serialize(*document);
        {
            RecoveryManager manager(temp.path());QSignalSpy saved(&manager,&RecoveryManager::snapshotSaved);manager.track(document);manager.captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);
        }
        RecoveryManager manager(temp.path());auto entries=manager.available();QCOMPARE(entries.size(),1);auto restored=manager.restore(entries.first());
        // Serialized DXF retains unsupported POINT, original shared block and DSTYLE XDATA.
        QVERIFY(restored.sourceDxf.contains("KEEP-DSTYLE"));QVERIFY(restored.sourceDxf.contains("0\nPOINT\n"));
        int edited=0,other=0;for(const auto& entity:restored.entities)if(!entity.dimensionTextKey.isEmpty()) {
            if(entity.dimensionTextOverride=="<>%%P0.05")++edited;
            if(entity.dimensionTextOverride=="<>%%P0.2")++other;
        }
        QCOMPARE(edited,1);QCOMPARE(other,1);QCOMPARE(restored.sourceDxf,expected);
    }
    void recoveredSourceIsRetainedUntilNewSnapshotCommits() {
        QTemporaryDir temp;
        {
            RecoveryManager manager(temp.path());QSignalSpy saved(&manager,&RecoveryManager::snapshotSaved);manager.track(drawing());manager.captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);
        }
        RecoveryManager manager(temp.path());auto entries=manager.available();QCOMPARE(entries.size(),1);
        auto recovered=std::make_shared<Document>(manager.restore(entries.first()));auto id=manager.track(recovered,entries.first().filePath);
        QVERIFY(QFile::exists(entries.first().filePath));QSignalSpy saved(&manager,&RecoveryManager::snapshotSaved);manager.captureNow();
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);QVERIFY(!QFile::exists(entries.first().filePath));QCOMPARE(snapshots(temp.path()).size(),1);
        manager.forget(id);QVERIFY(snapshots(temp.path()).isEmpty());
    }
    void invalidDirectoryReportsFailureWithoutChangingDocument() {
        QTemporaryDir temp;auto path=temp.filePath("not-a-directory");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("keep");file.close();
        auto document=drawing();RecoveryManager manager(path);QSignalSpy errors(&manager,&RecoveryManager::failure);manager.track(document);manager.captureNow();
        QVERIFY(!errors.isEmpty());QVERIFY(document->modified());QCOMPARE(readBytes(path),QByteArray("keep"));
    }
    void outsidePathsCannotBeDeletedOrRestored() {
        QTemporaryDir temp;auto path=temp.filePath("original.dxf");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("keep");file.close();
        RecoveryManager manager(temp.filePath("recovery"));RecoveryEntry entry;entry.filePath=path;QString error;
        QVERIFY(!manager.discard(entry,&error));QVERIFY(!error.isEmpty());QVERIFY_EXCEPTION_THROWN(manager.restore(entry),std::runtime_error);QCOMPARE(readBytes(path),QByteArray("keep"));
    }
    void activeInstanceSnapshotsCannotBeRestoredOrDiscarded() {
        QTemporaryDir temp;RecoveryManager active(temp.path());QSignalSpy saved(&active,&RecoveryManager::snapshotSaved);
        active.track(drawing());active.captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);
        auto files=snapshots(temp.path());QCOMPARE(files.size(),1);RecoveryEntry entry;entry.filePath=files.first();
        RecoveryManager other(temp.path());QString error;
        QVERIFY(other.available().isEmpty());QVERIFY_EXCEPTION_THROWN(other.restore(entry),std::runtime_error);
        QVERIFY(!other.discard(entry,&error));QVERIFY(error.contains("另一个"));QVERIFY(QFile::exists(entry.filePath));
    }
    void crashedProcessLockIsRecoverable() {
        QTemporaryDir temp;QProcess process;
        process.start(QCoreApplication::applicationDirPath()+"/fake_recovery_crash.exe",{temp.path()});
        QVERIFY(process.waitForFinished(10000));QCOMPARE(process.exitCode(),0);QCOMPARE(snapshots(temp.path()).size(),1);
        RecoveryManager manager(temp.path());auto entries=manager.available();QCOMPARE(entries.size(),1);
        auto document=manager.restore(entries.first());QVERIFY(document.modified());QCOMPARE(document.entities.size(),1);
        QVERIFY(manager.discard(entries.first()));
    }
};
QTEST_GUILESS_MAIN(RecoveryTests)
#include "RecoveryTests.moc"
