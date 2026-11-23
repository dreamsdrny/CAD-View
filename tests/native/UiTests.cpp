#include "ui/MainWindow.h"
#include "ui/CommandParser.h"
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
#include "MarkFixture.h"
#include "ConverterFixture.h"
#include <QTextEdit>
#include <QDockWidget>
#include <QLineEdit>
#include <QDialog>
#include <QTextBrowser>
#include <QCheckBox>
#include <QSettings>

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
    void compactWorkspaceAndCommands() {
        MainWindow window(false);window.show();QTest::qWait(20);
        auto* layers=window.findChild<QDockWidget*>("图层管理");auto* properties=window.findChild<QDockWidget*>("对象属性");
        auto* analysis=window.findChild<QDockWidget*>("PCB / FPC 工程分析");
        QVERIFY(layers);QVERIFY(properties);QVERIFY(analysis);QVERIFY(window.tabifiedDockWidgets(layers).contains(properties));
        QVERIFY(!analysis->isVisible());QVERIFY(window.findChild<QLineEdit*>("commandInput"));
        analysis->toggleViewAction()->trigger();QVERIFY(analysis->isVisible());
        auto a=parseCommandPoint(" 1,2 ",{10,20},{11,20});QVERIFY(a);QCOMPARE(*a,QPointF(1,2));
        a=parseCommandPoint("@1,2",{10,20},{11,20});QVERIFY(a);QCOMPARE(*a,QPointF(11,22));
        a=parseCommandPoint("2<90",{10,20},{11,20});QVERIFY(a);QVERIFY(geo::length(*a,{10,22})<1e-9);
        QVERIFY(!parseCommandPoint("nan,2",{},{1,0}));QVERIFY(!parseCommandPoint("1,2,3",{},{1,0}));
    }
    void lazyPathsRemainCorrectAfterEditAndExport() {
        auto document=std::make_shared<Document>();Entity line;line.points={{2,2},{8,2}};document->load(line);
        CadCanvas canvas(document);canvas.resize(800,600);canvas.grid=false;canvas.show();canvas.fitDrawing();QTest::qWait(20);
        auto probe=canvas.screenAt({5,2}).toPoint();QVERIFY(canvas.grab().toImage().pixelColor(probe)!=QColor("#101922"));
        auto before=document->entities.begin().value(),after=before;after.translate({0,4});document->commit("move",{before},{after});canvas.synchronize();
        auto old=canvas.screenAt({5,2}).toPoint(),moved=canvas.screenAt({5,6}).toPoint();auto image=canvas.grab().toImage();
        QCOMPARE(image.pixelColor(old),QColor("#101922"));QVERIFY(image.pixelColor(moved)!=QColor("#101922"));
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,moved);QVERIFY(canvas.selection.contains(after.id));
        QImage exported(800,600,QImage::Format_ARGB32);exported.fill(Qt::white);QPainter painter(&exported);canvas.renderDocument(painter,QRectF(0,0,800,600));painter.end();
        QVERIFY(exported.pixelColor(400,300)!=QColor(Qt::white));
    }
    void mouseDrawingAndLayers() {
        CadCanvas c(std::make_shared<Document>());c.resize(800,600);c.show();c.fitDrawing();c.snap=false;c.setTool(Tool::Circle);
        QTest::mouseClick(&c,Qt::LeftButton,Qt::NoModifier,QPoint(400,300));QTest::mouseClick(&c,Qt::LeftButton,Qt::NoModifier,QPoint(450,300));QCOMPARE(c.document->entities.size(),1);
        auto e=c.document->entities.begin().value();QCOMPARE(e.kind,Kind::Circle);QVERIFY(e.radius>0);c.selectIds({e.id});c.deleteSelection();QVERIFY(c.document->entities.isEmpty());
        QVERIFY(c.document->undo());c.synchronize();QCOMPARE(c.document->entities.size(),1);
        auto layers=c.document->layers;layers["0"].locked=true;c.document->changeLayers("锁定",layers);QVERIFY(!c.document->editable(e.id));
        QImage image(800,600,QImage::Format_ARGB32);image.fill(Qt::white);QPainter painter(&image);c.renderDocument(painter,QRectF(0,0,800,600));painter.end();QVERIFY(!image.isNull());
    }
    void gapToolCreatesDualUnitDimension() {
        auto document=std::make_shared<Document>();document->units=4;
        Entity top;top.points={{0,10},{30,10}};
        Entity bottom;bottom.points={{0,0},{30,0}};
        document->load(top);document->load(bottom);
        CadCanvas canvas(document);canvas.resize(800,600);canvas.show();canvas.snap=false;canvas.fitDrawing();QTest::qWait(20);
        canvas.setTool(Tool::Gap);
        QSignalSpy recorded(&canvas,&CadCanvas::measured);
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,canvas.screenAt({15,10}).toPoint());
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,canvas.screenAt({15,0}).toPoint());
        QTRY_COMPARE(recorded.count(),1);
        auto text=recorded.first().first().toString();
        QVERIFY(text.contains("中心距"));QVERIFY(text.contains("mm"));QVERIFY(text.contains("mil"));
        QCOMPARE(document->entities.size(),3);
        for(const auto& e:document->entities) if(e.kind==Kind::Dimension) {
            QCOMPARE(e.text,QString("<>"));
            QVERIFY(e.height>0&&e.height<2.);   // adaptive height, smaller than the old fixed 2
            auto dual=CadCanvas::pathFor(e,document.get()).boundingRect();
            auto plain=CadCanvas::pathFor(e).boundingRect();
            QVERIFY(dual.width()>plain.width()+4.);
        }
    }
    void measurePanelRecordsAndLocates() {
        MainWindow w(false);w.show();QTest::qWait(20);
        auto* panel=w.findChild<QListWidget*>("measureRecords");QVERIFY(panel);
        auto* dock=w.findChild<QDockWidget*>("标注测量");QVERIFY(dock);QVERIFY(dock->isVisible());
        auto* c=w.canvas();
        Entity a;a.points={{0,0},{10,0}};Entity b;b.points={{0,8},{10,8}};
        c->document->commit("draw",{},{a,b});c->synchronize();
        c->setTool(Tool::Gap);
        QTest::mouseClick(c,Qt::LeftButton,Qt::NoModifier,c->screenAt({5,8}).toPoint());
        QTest::mouseClick(c,Qt::LeftButton,Qt::NoModifier,c->screenAt({5,0}).toPoint());
        QTRY_COMPARE(panel->count(),1);
        QVERIFY(panel->item(0)->text().contains("mm"));
        QTest::mouseClick(panel->viewport(),Qt::LeftButton,Qt::NoModifier,panel->visualItemRect(panel->item(0)).center());
        QVERIFY(!c->selection.isEmpty());
    }
    void startupTutorialShowsAndPersists() {
        QSettings settings("EngineeringTools","CADWorkbenchNative");const QString key="ui/showStartupTutorial";
        auto saved=settings.value(key);
        settings.setValue(key,true);settings.sync();
        bool found=false,contentOk=false,shown=false;
        {
            MainWindow window(false);window.show();QTest::qWait(30);
            QTimer closer;int tries=0;
            QObject::connect(&closer,&QTimer::timeout,[&]{
                auto* d=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if(d&&d->windowTitle().contains("操作教程")){
                    found=true;
                    if(auto* browser=d->findChild<QTextBrowser*>())contentOk=browser->toHtml().contains("对象间距")&&browser->toHtml().contains("mil");
                    if(auto* cb=d->findChild<QCheckBox*>())cb->setChecked(false);
                    d->accept();closer.stop();
                } else if(++tries>400)closer.stop();
            });
            closer.start(5);
            window.showStartupTutorial(true);
            QVERIFY(found);QVERIFY(contentOk);
            QTimer probe;int probes=0;
            QObject::connect(&probe,&QTimer::timeout,[&]{if(QApplication::activeModalWidget())shown=true;if(++probes>20)probe.stop();});
            probe.start(5);
            window.showStartupTutorial(true);QTest::qWait(120);
            QVERIFY(!shown);   // persisted off -> suppressed at startup
        }
        if(saved.isValid())settings.setValue(key,saved);else settings.remove(key);
        settings.sync();
    }
    void gapOnDemoBoardSurvivesDxfRoundtrip() {
        auto document=std::make_shared<Document>(Document::demo());
        CadCanvas canvas(document);canvas.resize(800,600);canvas.show();canvas.snap=false;canvas.fitDrawing();QTest::qWait(20);
        canvas.setTool(Tool::Gap);
        QSignalSpy recorded(&canvas,&CadCanvas::measured);
        // Two drill circles at (5,5) and (75,5), radius 1.6 -> edge gap 66.8, center gap 70.
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,canvas.screenAt({5,5}).toPoint());
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,canvas.screenAt({75,5}).toPoint());
        QTRY_COMPARE(recorded.count(),1);
        auto text=recorded.first().first().toString();
        QVERIFY(text.contains("边到边"));QVERIFY(text.contains("66.800 mm"));QVERIFY(text.contains("mil"));
        Entity gapDim;int dims=0;
        for(const auto& e:document->entities)if(e.kind==Kind::Dimension){++dims;if(std::abs(geo::length(e.points[0],e.points[1])-66.8)<1e-6)gapDim=e;}
        QCOMPARE(dims,2);QCOMPARE(gapDim.text,QString("<>"));
        QVERIFY(std::abs(gapDim.points[0].x()-6.6)<1e-6);QVERIFY(std::abs(gapDim.points[1].x()-73.4)<1e-6);
        auto bytes=io::DxfAdapter::serialize(*document);auto loaded=io::DxfAdapter::parse(bytes);
        QCOMPARE(loaded.entities.size(),document->entities.size());
        bool found=false;
        for(const auto& e:loaded.entities)if(e.kind==Kind::Dimension&&std::abs(geo::length(e.points[0],e.points[1])-66.8)<1e-6){
            found=true;QCOMPARE(e.text,QString("<>"));
            auto label=CadCanvas::pathFor(e,&loaded).boundingRect();
            QVERIFY(label.width()>CadCanvas::pathFor(e).boundingRect().width()+8.);
        }
        QVERIFY(found);
    }
    void asyncOpen() {
        QTemporaryDir temp;QString path=temp.filePath("demo.dxf");io::DxfAdapter::write(Document::demo(),path);
        MainWindow w(false);w.openPath(path);QTRY_COMPARE_WITH_TIMEOUT(w.canvas()->document->entities.size(),26,5000);QCOMPARE(w.canvas()->document->path,path);
        w.canvas()->document->markSaved();
    }
    void asyncDwgReader() {
        ConverterEnvironment environment;QTemporaryDir temp;
        auto dwg=temp.filePath("中文图纸[版本02.dwg"),dxf=temp.filePath("中文图纸[版本02.dxf");
        qputenv("CAD_LIBREDWG_READER",(QCoreApplication::applicationDirPath()+"/fake_dwg_reader.exe").toUtf8());
        QFile source(dwg);QVERIFY(source.open(QIODevice::WriteOnly));source.write("AC1032\n"+io::DxfAdapter::serialize(Document::demo()));source.close();
        io::DxfAdapter::write(Document::demo(),dxf);
        MainWindow w(false);QSignalSpy opened(&w,&MainWindow::fileOpened);w.openPath(dwg);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(),1,5000);
        QCOMPARE(w.canvas()->document->path,dwg);QCOMPARE(w.canvas()->document->entities.size(),26);
        QVERIFY(!w.windowTitle().contains("DXF 副本"));
        QVERIFY(w.findChild<QTextEdit*>()->toPlainText().contains("LibreDWG"));
        w.canvas()->document->markSaved();
    }
    void checkedOpenFailure() {
        MainWindow w(false);w.setProperty("checkOpen",true);QSignalSpy failed(&w,&MainWindow::fileOpenFailed);
        QTemporaryDir temp;w.openPath(temp.filePath("missing.dxf"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,5000);QVERIFY(!failed.first().first().toString().isEmpty());
        QVERIFY(w.canvas()->document->entities.isEmpty());
    }
    void localDwgPanel() {
        auto path=qEnvironmentVariable("CAD_TEST_DWG");if(path.isEmpty())QSKIP("Set CAD_TEST_DWG to verify a local production DWG or companion.");
        MainWindow window(false);window.setProperty("checkOpen",true);window.resize(1400,900);window.show();
        QSignalSpy opened(&window,&MainWindow::fileOpened),failed(&window,&MainWindow::fileOpenFailed);window.openPath(path);
        QTRY_VERIFY_WITH_TIMEOUT(opened.count()+failed.count()>0,30000);
        QVERIFY2(failed.isEmpty(),failed.isEmpty()?"":qPrintable(failed.first().first().toString()));
        auto d=window.canvas()->document;QVERIFY(!d->entities.isEmpty());
        window.canvas()->fitDrawing();QTest::qWait(200);
        auto screenshot=qEnvironmentVariable("CAD_TEST_SCREENSHOT");if(!screenshot.isEmpty())QVERIFY(window.grab().save(screenshot));
        qInfo()<<"Production drawing:"<<d->path<<d->entities.size()<<"entities, bounds"<<d->bounds();
        for(const auto& warning:d->warnings)qInfo().noquote()<<warning;
        d->markSaved();
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
    void markSolidHatchRender() {
        auto document=std::make_shared<Document>(io::DxfAdapter::parse(markFixture()));
        CadCanvas canvas(document);canvas.resize(800,600);canvas.grid=false;canvas.show();canvas.fitDrawing();QTest::qWait(20);
        auto image=canvas.grab().toImage();
        auto filled=image.pixelColor(canvas.screenAt({18.2,33.2}).toPoint());
        auto outside=image.pixelColor(canvas.screenAt({18.7,33.2}).toPoint());
        QVERIFY(filled!=QColor("#101922"));QCOMPARE(outside,QColor("#101922"));
        QImage exported(800,600,QImage::Format_ARGB32);exported.fill(Qt::white);QPainter painter(&exported);canvas.renderDocument(painter,QRectF(0,0,800,600));painter.end();
        auto box=document->bounds();double scale=std::min(800/(box.width()+4),600/(box.height()+4));
        auto delta=QPointF(18.2,33.2)-box.center();auto probe=QPointF(400+delta.x()*scale,300-delta.y()*scale).toPoint();
        QVERIFY(exported.pixelColor(probe).red()<100);
    }
    void solidPreviewRender() {
        QByteArray source="0\nSECTION\n2\nENTITIES\n0\nSOLID\n10\n1\n20\n1\n11\n3\n21\n1\n12\n1\n22\n3\n13\n3\n23\n3\n0\nENDSEC\n0\nEOF\n";
        auto document=std::make_shared<Document>(io::DxfAdapter::parse(source));CadCanvas canvas(document);canvas.resize(800,600);canvas.grid=false;canvas.show();canvas.fitDrawing();QTest::qWait(20);
        QVERIFY(canvas.grab().toImage().pixelColor(canvas.screenAt({2,2}).toPoint())!=QColor("#101922"));
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
        auto* list=w.findChild<QListWidget*>("analysisResults");QVERIFY(list);list->addItem("stale");w.executeCommand("UNDO");QCOMPARE(list->count(),0);
        list->addItem("stale");w.executeCommand("REDO");QCOMPARE(list->count(),0);c->document->markSaved();
    }
    void sameLayerEdgeCheck() {
        MainWindow w(false);w.show();auto* c=w.canvas();Entity board;board.kind=Kind::Rectangle;board.closed=true;board.points={{0,0},{10,0},{10,10},{0,10}};
        Entity outside;outside.points={{20,20},{21,20}};c->document->commit("draw",{},{board,outside});c->synchronize();
        EntityId boardId=0;for(const auto& e:c->document->entities)if(e.kind==Kind::Rectangle)boardId=e.id;c->selectIds({boardId});
        QAction* edge=nullptr;for(auto* action:w.findChildren<QAction*>())if(action->text()=="工艺边"){edge=action;break;}QVERIFY(edge);
        QTimer::singleShot(0,[]{auto* input=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());if(input)input->accept();});edge->trigger();
        auto* list=w.findChild<QListWidget*>("analysisResults");QTRY_VERIFY_WITH_TIMEOUT(list&&list->count()>0,5000);QVERIFY(list->item(0)->text().contains("板框外"));c->document->markSaved();
    }
};
QTEST_MAIN(UiTests)
#include "UiTests.moc"
