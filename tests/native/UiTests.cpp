#include "ui/MainWindow.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QPainter>
#include "io/DxfAdapter.h"
#include <QPdfWriter>
#include <QFile>

using namespace cad;
using namespace cad::ui;
class UiTests : public QObject {
    Q_OBJECT
private slots:
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
};
QTEST_MAIN(UiTests)
#include "UiTests.moc"
