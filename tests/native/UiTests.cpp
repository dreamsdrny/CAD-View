#include "ui/MainWindow.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QPainter>
#include "io/DxfAdapter.h"
#include <QPdfWriter>
#include <QFile>
#include <QListWidget>
#include <QMessageBox>
#include <QTimer>
#include <QInputDialog>
#include <QAction>
#include "branding/AppBrand.h"
#include "GenesisFixture.h"

using namespace cad;
using namespace cad::ui;
class UiTests : public QObject {
    Q_OBJECT
private slots:
    void applicationBrand() {
        branding::AppBrand::apply(*qobject_cast<QApplication*>(QCoreApplication::instance()));
        QCOMPARE(QCoreApplication::applicationName(),QString("CAD"));
        QCOMPARE(branding::AppBrand::iconLabel(),QString("Qiao"));
        QVERIFY(!branding::AppBrand::icon().isNull());
        for(int size:{16,32,48,256})QVERIFY(!branding::AppBrand::icon().pixmap(size,size).isNull());
        MainWindow window(false);
        QVERIFY(window.windowTitle().startsWith("CAD — "));
        QVERIFY(!window.windowIcon().isNull());
    }
    void commandsMoveCopyUndo() {
        MainWindow w(false);w.show();QTest::qWait(20);w.executeCommand("LINE");w.executeCommand("0,0");w.executeCommand("@20,0");
        auto* c=w.canvas();QCOMPARE(c->document->entities.size(),1);auto e=c->document->entities.begin().value();QCOMPARE(e.points[1],QPointF(20,0));
        c->setTool(Tool::Select);c->selectIds({e.id});w.executeCommand("COPY");w.executeCommand("0,0");w.executeCommand("0,10");QCOMPARE(c->document->entities.size(),2);
        w.executeCommand("UNDO");QCOMPARE(c->document->entities.size(),1);w.executeCommand("REDO");QCOMPARE(c->document->entities.size(),2);
        QVERIFY(!w.grab().isNull());c->document->markSaved();
    }
    void mouseDrawingAndLayers() {
        CadCanvas c(std::make_shared<Document>());c.resize(800,600);c.show();c.fitDrawing();c.snap=false;c.setTool(Tool::Circle);
        QTest::mouseClick(&c,Qt::LeftButton,Qt::NoModifier,QPoint(400,300));QTest::mouseClick(&c,Qt::LeftButton,Qt::NoModifier,QPoint(450,300));QCOMPARE(c.document->entities.size(),1);
        auto e=c.document->entities.begin().value();QCOMPARE(e.kind,Kind::Circle);QVERIFY(e.radius>0);c.selectIds({e.id});c.deleteSelection();QVERIFY(c.document->entities.isEmpty());
        QVERIFY(c.document->undo());c.synchronize();QCOMPARE(c.document->entities.size(),1);
        auto layers=c.document->layers;layers["0"].locked=true;c.document->changeLayers("锁定",layers);QVERIFY(!c.document->editable(e.id));
        QImage image(800,600,QImage::Format_ARGB32);image.fill(Qt::white);QPainter painter(&image);c.renderDocument(painter,QRectF(0,0,800,600));painter.end();QVERIFY(!image.isNull());
    }
    void asyncOpen() {
        QTemporaryDir temp;QString path=temp.filePath("demo.dxf");io::DxfAdapter::write(Document::demo(),path);
        MainWindow w(false);w.openPath(path);QTRY_COMPARE_WITH_TIMEOUT(w.canvas()->document->entities.size(),26,5000);QCOMPARE(w.canvas()->document->path,path);
        w.canvas()->document->markSaved();
    }
    void genesisPadsRender() {
        auto document=std::make_shared<Document>(io::DxfAdapter::parse(genesisFixture()));
        CadCanvas canvas(document);canvas.resize(900,600);canvas.grid=false;canvas.show();canvas.fitDrawing();QTest::qWait(20);
        auto image=canvas.grab().toImage();
        // Probe inside the actual pad width, away from its centerline.
        for(auto p:{QPointF(20.75,20),QPointF(30.75,20)}) {
            auto color=image.pixelColor(canvas.screenAt(p).toPoint());QVERIFY(color.red()>180);QVERIFY(color.green()<150);
        }
        QImage exported(900,600,QImage::Format_ARGB32);exported.fill(Qt::white);QPainter painter(&exported);canvas.renderDocument(painter,QRectF(0,0,900,600));painter.end();
        auto box=document->bounds();double scale=std::min(900/(box.width()+4),600/(box.height()+4));
        auto delta=QPointF(20.75,20)-box.center();auto probe=QPointF(450+delta.x()*scale,300-delta.y()*scale).toPoint();
        QVERIFY(exported.pixelColor(probe).red()<100);
    }
    void localGenesisPanel() {
        auto path=qEnvironmentVariable("CAD_TEST_DXF");if(path.isEmpty())QSKIP("Set CAD_TEST_DXF to verify a local production drawing.");
        MainWindow window(false);window.resize(1400,900);window.show();window.openPath(path);
        QTRY_COMPARE_WITH_TIMEOUT(window.canvas()->document->path,path,10000);
        auto d=window.canvas()->document;QVERIFY(!d->entities.isEmpty());
        for(const auto& warning:d->warnings)QVERIFY2(!warning.contains("暂不显示"),qPrintable(warning));
        window.canvas()->fitDrawing();QTest::qWait(100);
        auto screenshot=qEnvironmentVariable("CAD_TEST_SCREENSHOT");if(!screenshot.isEmpty())QVERIFY(window.grab().save(screenshot));
        qInfo()<<"Production DXF:"<<d->entities.size()<<"entities, bounds"<<d->bounds();
        d->markSaved();
    }
    void pdfExport() {
        QTemporaryDir temp;auto path=temp.filePath("drawing.pdf");CadCanvas canvas(std::make_shared<Document>(Document::demo()));
        QPdfWriter writer(path);QPainter painter(&writer);QVERIFY(painter.isActive());canvas.renderDocument(painter,QRectF(0,0,writer.width(),writer.height()));painter.end();
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QVERIFY(file.read(4)=="%PDF");
    }
    void closedOutlinesDoNotFill() {
        auto document=std::make_shared<Document>();Entity rectangle;rectangle.kind=Kind::Rectangle;rectangle.closed=true;rectangle.points={{0,0},{10,0},{10,10},{0,10}};document->load(rectangle);
        CadCanvas canvas(document);canvas.resize(800,600);canvas.grid=false;canvas.show();canvas.fitDrawing();QTest::qWait(10);
        auto image=canvas.grab().toImage();auto pos=canvas.screenAt({5,5}).toPoint();QCOMPARE(image.pixelColor(pos),QColor("#101922"));
    }
    void staleResultsAndInvalidTrim() {
        MainWindow w(false);w.show();auto* c=w.canvas();
        Entity circle;circle.kind=Kind::Circle;circle.points={{0,0}};circle.radius=1;
        Entity line;line.points={{0,0},{10,0}};c->document->commit("draw",{},{circle,line});c->synchronize();c->document->markSaved();w.addDocument(*c->document);c=w.canvas();
        QSet<EntityId> ids;for(const auto& e:c->document->entities)ids.insert(e.id);c->selectIds(ids);
        bool shown=false;QTimer::singleShot(0,[&]{auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());if(box){shown=true;box->accept();}});
        w.executeCommand("TRIM");QVERIFY(shown);
        auto* list=w.findChild<QListWidget*>();QVERIFY(list);list->addItem("stale");w.executeCommand("UNDO");QCOMPARE(list->count(),0);
        list->addItem("stale");w.executeCommand("REDO");QCOMPARE(list->count(),0);c->document->markSaved();
    }
    void sameLayerEdgeCheck() {
        MainWindow w(false);w.show();auto* c=w.canvas();Entity board;board.kind=Kind::Rectangle;board.closed=true;board.points={{0,0},{10,0},{10,10},{0,10}};
        Entity outside;outside.points={{20,20},{21,20}};c->document->commit("draw",{},{board,outside});c->synchronize();
        EntityId boardId=0;for(const auto& e:c->document->entities)if(e.kind==Kind::Rectangle)boardId=e.id;c->selectIds({boardId});
        QAction* edge=nullptr;for(auto* action:w.findChildren<QAction*>())if(action->text()=="工艺边"){edge=action;break;}QVERIFY(edge);
        QTimer::singleShot(0,[]{auto* input=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());if(input)input->accept();});edge->trigger();
        auto* list=w.findChild<QListWidget*>();QTRY_VERIFY_WITH_TIMEOUT(list&&list->count()>0,5000);QVERIFY(list->item(0)->text().contains("板框外"));c->document->markSaved();
    }
};
QTEST_MAIN(UiTests)
#include "UiTests.moc"
