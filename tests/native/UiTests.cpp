#include "ui/MainWindow.h"
#include "ui/CommandParser.h"
#include "ui/RecoveryManager.h"
#include "ui/PanelizationDialog.h"
#include "core/DimensionLayout.h"
#include "core/ContourOperations.h"
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
#include "TextDimensionFixture.h"
#include "core/TextLayout.h"
#include "ConverterFixture.h"
#include <QTextEdit>
#include <QDockWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QToolBar>
#include <QDialog>
#include <QTextBrowser>
#include <QCheckBox>
#include <QSettings>
#include <QFontDatabase>
#include <QScreen>
#include <QTabWidget>
#include <QDirIterator>
#include <QFileDialog>
#include <QStandardPaths>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QTableWidget>

using namespace cad;
using namespace cad::ui;
class UiTests : public QObject {
    Q_OBJECT
private slots:
    void mergedPanelDialogOwnsMaterialPlanningSection() {
        Document d;Entity source;source.kind=Kind::Polyline;source.closed=true;source.points={{0,0},{65.3,0},{65.3,14.4},{48,14.4},{48,22.9},{36,22.9},{36,12.1},{0,12.1}};
        d.load(source);d.markSaved();PanelizationDialog dialog(d,{},{});dialog.show();
        QTRY_VERIFY(dialog.findChild<QComboBox*>("panelContourCandidates")->count()>0);
        auto* modes=dialog.findChild<QTabWidget*>("panelModeTabs");QVERIFY(modes);QCOMPARE(modes->count(),2);QVERIFY(modes->tabText(1).contains("材料"));
        QVERIFY(!dialog.findChild<QDialog*>("fpcMaterialDialog"));QVERIFY(!dialog.findChild<QPushButton*>("panelMaterialPlanner"));
        modes->setCurrentIndex(1);auto* table=dialog.findChild<QTableWidget*>("panelMaterialSchemes");QVERIFY(table);QTRY_VERIFY(table->rowCount()>0);
        QVERIFY(dialog.findChild<QLabel*>("panelMaterialMetrics")->text().contains("预计"));
        auto* units=dialog.findChild<QComboBox*>("panelMaterialUnits");auto* width=dialog.findChild<QDoubleSpinBox*>("panelMaterialWidth");auto* height=dialog.findChild<QDoubleSpinBox*>("panelMaterialHeight");
        units->setCurrentIndex(1);QVERIFY(std::abs(width->value()-18.42)<1e-6);QVERIFY(std::abs(height->value()-24.42)<1e-6);
        auto* order=dialog.findChild<QSpinBox*>("panelMaterialOrder");order->setValue(1234);QVERIFY(!dialog.findChild<QPushButton*>("panelMaterialGenerate")->isEnabled());
        QTemporaryDir temp;QVERIFY_EXCEPTION_THROWN(dialog.saveMaterialReport(temp.filePath("stale.csv")),std::runtime_error);
        dialog.findChild<QPushButton*>("panelMaterialCompare")->click();QVERIFY(table->rowCount()>0);
        auto csv=temp.filePath("result.csv"),pdf=temp.filePath("result.pdf");dialog.saveMaterialReport(csv);dialog.saveMaterialReport(pdf,true);
        QFile file(csv);QVERIFY(file.open(QIODevice::ReadOnly));auto bytes=file.readAll();QVERIFY(bytes.startsWith("\xEF\xBB\xBF"));QVERIFY(bytes.contains("1234"));file.close();file.setFileName(pdf);QVERIFY(file.open(QIODevice::ReadOnly));QVERIFY(file.read(5).startsWith("%PDF"));
        units->setCurrentIndex(0);QVERIFY(std::abs(width->value()-467.868)<1e-5);
        QCOMPARE(d.entities.size(),1);QVERIFY(!d.modified());
    }
    void fpcBackgroundSearchApplyAndMaterialDrawing() {
        Document d;Entity source;source.kind=Kind::Polyline;source.closed=true;source.points={{0,0},{65.3,0},{65.3,14.4},{48,14.4},{48,22.9},{36,22.9},{36,12.1},{0,12.1}};
        d.load(source);d.markSaved();PanelizationDialog dialog(d,{},{});dialog.show();
        auto* modes=dialog.findChild<QTabWidget*>("panelModeTabs");QVERIFY(modes);QTRY_VERIFY(dialog.findChild<QComboBox*>("panelContourCandidates")->count()>0);
        modes->setCurrentIndex(1);auto* table=dialog.findChild<QTableWidget*>("panelMaterialSchemes");QTRY_VERIFY(table->rowCount()>0);
        dialog.findChild<QSpinBox*>("panelMaterialSearchRows")->setValue(2);dialog.findChild<QSpinBox*>("panelMaterialSearchColumns")->setValue(2);dialog.findChild<QSpinBox*>("panelMaterialSearchMaxPcs")->setValue(4);
        bool heartbeat=false;QTimer::singleShot(0,&dialog,[&]{heartbeat=true;});dialog.findChild<QPushButton*>("panelMaterialSearch")->click();
        auto* cancel=dialog.findChild<QPushButton*>("panelMaterialCancel");QTRY_VERIFY_WITH_TIMEOUT(!cancel->isEnabled(),60000);QVERIFY(heartbeat);
        QVERIFY(table->rowCount()>0);auto* apply=dialog.findChild<QPushButton*>("panelMaterialApply");QVERIFY(apply->isEnabled());
        bool generated=false;Document material;dialog.onDrawing=[&](Document doc){material=std::move(doc);generated=true;};dialog.findChild<QPushButton*>("panelMaterialGenerate")->click();QVERIFY(generated);QVERIFY(!material.entities.isEmpty());
        int sets=0;for(const auto& e:material.entities)if(e.layer=="FPC_SET")++sets;QVERIFY(sets>0);
        auto output=qEnvironmentVariable("CAD_TEST_FPC_OUTPUT");if(!output.isEmpty()){dialog.saveMaterialReport(output+"-ui.csv");dialog.saveMaterialReport(output+"-report.pdf",true);io::DxfAdapter::write(material,output+"-ui.dxf");QVERIFY(dialog.grab().save(output+"-dialog.png"));auto* tabs=dialog.findChild<QTabWidget*>("panelMaterialTabs");QVERIFY(tabs);tabs->setCurrentIndex(1);QTest::qWait(20);QVERIFY(dialog.grab().save(output+"-preview.png"));
            dialog.findChild<QCheckBox*>("panelMaterialDetailed")->setChecked(true);QTest::qWait(30);QVERIFY(dialog.grab().save(output+"-detailed.png"));auto* preview=dialog.findChild<CadCanvas*>("panelMaterialPreview");QVERIFY(preview);io::DxfAdapter::write(*preview->document,output+"-detailed.dxf");}
        int units=table->item(table->currentRow(),2)->text().toInt();QVERIFY(units>=1);apply->click();QCOMPARE(modes->currentIndex(),0);
        auto panel=dialog.outputDocument();int pcs=0;for(const auto& e:panel.entities)if(e.layer=="PANEL_PCS")++pcs;QCOMPARE(pcs,units);
        QCOMPARE(d.entities.size(),1);QVERIFY(!d.modified());
    }
    void fpcSearchCancellationLeavesNoExportableStaleResult() {
        Document d;Entity source;source.kind=Kind::Polyline;source.closed=true;source.points={{0,0},{10,0},{10,7},{0,7}};d.load(source);d.markSaved();
        PanelizationDialog dialog(d,{},{});dialog.show();auto* modes=dialog.findChild<QTabWidget*>("panelModeTabs");QVERIFY(modes);QTRY_VERIFY(dialog.findChild<QComboBox*>("panelContourCandidates")->count()>0);
        modes->setCurrentIndex(1);QTRY_VERIFY(dialog.findChild<QTableWidget*>("panelMaterialSchemes")->rowCount()>0);
        dialog.findChild<QPushButton*>("panelMaterialSearch")->click();dialog.findChild<QPushButton*>("panelMaterialCancel")->click();
        QTRY_VERIFY_WITH_TIMEOUT(!dialog.findChild<QPushButton*>("panelMaterialCancel")->isEnabled(),60000);QVERIFY(!dialog.findChild<QPushButton*>("panelMaterialGenerate")->isEnabled());
    }
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
#ifdef Q_OS_WIN
        // Qt's offscreen platform does not discover Windows system fonts.
        // Register the same font used by the real Windows canvas for layout tests.
        if(QGuiApplication::platformName()=="offscreen") {
            auto font=qEnvironmentVariable("SystemRoot","C:/Windows")+"/Fonts/msyh.ttc";
            QVERIFY2(QFontDatabase::addApplicationFont(font)>=0,qPrintable("Cannot load test font: "+font));
        }
#endif
    }
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
    void compactBlankStartupAndExplicitDemo() {
        MainWindow window(false);window.show();QTest::qWait(30);
        QVERIFY(window.canvas());QVERIFY(window.canvas()->document->entities.isEmpty());
        QCOMPARE(window.canvas()->document->title,QString("未命名"));QVERIFY(!window.windowTitle().contains("PCB"));
        auto* tabs=window.findChild<QTabWidget*>("documentTabs");QVERIFY(tabs);QCOMPARE(tabs->count(),1);
        QVERIFY(window.width()<=1180);QVERIFY(window.height()<=760);
        auto* screen=QGuiApplication::primaryScreen();QVERIFY(screen);
        QVERIFY(window.width()<=screen->availableGeometry().width());
        QVERIFY(window.height()<=screen->availableGeometry().height());
        QVERIFY(window.minimumWidth()<=760);QVERIFY(window.minimumHeight()<=520);
        QVERIFY(window.findChild<QDockWidget*>("对象属性")->minimumWidth()<=240);
        auto* measureDock=window.findChild<QDockWidget*>("标注测量");QVERIFY(measureDock);QVERIFY(measureDock->minimumWidth()<=210);
        QVERIFY(!measureDock->isVisible());window.canvas()->setTool(Tool::Gap);QVERIFY(measureDock->isVisible());
        window.canvas()->setTool(Tool::Select);
        QAction* demo=nullptr;for(auto* candidate:window.findChildren<QAction*>())if(candidate->text()=="打开演示板"){demo=candidate;break;}
        QVERIFY(demo);demo->trigger();QCOMPARE(window.canvas()->document->entities.size(),26);
        QVERIFY(window.canvas()->document->title.contains("PCB"));window.canvas()->document->markSaved();
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
    void keyboardPanZoomAndCommandFocus() {
        auto document=std::make_shared<Document>();Entity line;line.points={{0,0},{100,0}};document->load(line);
        CadCanvas canvas(document);canvas.resize(800,600);canvas.grid=false;canvas.snap=false;canvas.show();canvas.fitDrawing();QTest::qWait(20);
        QPointF probe=canvas.modelAt({400,300});
        auto panDelta=[&](Qt::KeyboardModifiers modifiers) {
            auto before=canvas.screenAt(probe);
            QTest::keyClick(&canvas,Qt::Key_Right,modifiers);
            auto delta=canvas.screenAt(probe)-before;
            QTest::keyClick(&canvas,Qt::Key_Left,modifiers);
            return delta;
        };
        QVERIFY(std::abs(panDelta(Qt::NoModifier).x()-48)<0.5);
        QVERIFY(std::abs(panDelta(Qt::ShiftModifier).x()-240)<0.5);
        QVERIFY(std::abs(panDelta(Qt::ControlModifier).x()-12)<0.5);

        auto beforeDown=canvas.screenAt(probe);QTest::keyClick(&canvas,Qt::Key_Down);
        QVERIFY(canvas.screenAt(probe).y()-beforeDown.y()>47.5);
        QTest::keyClick(&canvas,Qt::Key_Up);QVERIFY(std::abs(canvas.screenAt(probe).y()-beforeDown.y())<0.5);

        auto leftOfCentre=canvas.modelAt({300,300});QTest::keyClick(&canvas,Qt::Key_Plus);
        QVERIFY(canvas.screenAt(leftOfCentre).x()<300);
        QTest::keyClick(&canvas,Qt::Key_Minus);QVERIFY(std::abs(canvas.screenAt(leftOfCentre).x()-300)<0.5);

        MainWindow window(false);window.show();QTest::qWait(30);
        auto* c=window.canvas();auto* input=window.findChild<QLineEdit*>("commandInput");QVERIFY(input);
        input->setText("12");input->setCursorPosition(2);input->setFocus();
        auto centre=c->modelAt(QPointF(c->width()/2.,c->height()/2.));auto beforeTyping=c->screenAt(centre);
        QTest::keyClick(input,Qt::Key_Left);QTest::keyClicks(input,"3");
        QCOMPARE(input->text(),QString("132"));QCOMPARE(c->screenAt(centre),beforeTyping);

        QAction* panRight=nullptr;for(auto* a:window.findChildren<QAction*>())if(a->text()==QString::fromUtf8("向右移动图纸（→）")){panRight=a;break;}
        QVERIFY(panRight);panRight->trigger();QVERIFY(c->screenAt(centre).x()-beforeTyping.x()>47.5);
        c->document->markSaved();
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
        QVERIFY(!text.contains("标注未创建"));
        QCOMPARE(document->entities.size(),3);
        bool gapDimension=false;
        for(const auto& e:document->entities) if(e.kind==Kind::Dimension) {
            gapDimension=true;QVERIFY(e.id);
            QCOMPARE(e.text,QString("<>"));
            QVERIFY(e.height>0&&e.height<2.);   // adaptive height, smaller than the old fixed 2
            auto dual=CadCanvas::pathFor(e,document.get()).boundingRect();
            auto plain=CadCanvas::pathFor(e).boundingRect();
            QVERIFY(dual.width()>plain.width()+4.);
        }
        QVERIFY(gapDimension);
    }
    void measurePanelRecordsAndLocates() {
        MainWindow w(false);w.show();QTest::qWait(20);
        auto* panel=w.findChild<QListWidget*>("measureRecords");QVERIFY(panel);
        auto* dock=w.findChild<QDockWidget*>("标注测量");QVERIFY(dock);dock->show();QVERIFY(dock->isVisible());
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
            Entity dual;dual.kind=Kind::Text;dual.points={{0,0}};
            dual.height=e.height;dual.text=loaded.dualLength(66.8);
            auto single=dual;single.text=value(66.8,3);
            // Check label content rather than a fixed whole-dimension width delta:
            // system-font glyphs can fit inside the existing dimension lines.
            QVERIFY(CadCanvas::pathFor(dual).boundingRect().width()>CadCanvas::pathFor(single).boundingRect().width()*2);
            QVERIFY(label.width()>=CadCanvas::pathFor(e).boundingRect().width());
        }
        QVERIFY(found);
    }
    void asyncOpen() {
        QTemporaryDir temp;QString path=temp.filePath("demo.dxf");io::DxfAdapter::write(Document::demo(),path);
        MainWindow w(false);w.openPath(path);QTRY_COMPARE_WITH_TIMEOUT(w.canvas()->document->entities.size(),26,5000);QCOMPARE(w.canvas()->document->path,path);
        w.canvas()->document->markSaved();
    }
    void panelDialogSelectionRegenerateAndIsolation() {
        Document source;source.units=4;Entity e;e.kind=Kind::Polyline;e.closed=true;e.points={{0,0},{65.3,0},{65.3,14.4},{48,14.4},{48,22.9},{36,22.9},{36,12.1},{0,12.1}};source.load(e);source.markSaved();
        auto baseline=source.entities;PanelizationDialog dialog(source,{source.entities.begin().key()},{});dialog.show();
        auto* generate=dialog.findChild<QPushButton*>("panelGenerate");QVERIFY(generate);QTRY_VERIFY(dialog.findChild<QComboBox*>("panelContourCandidates")->count()>0);
        auto doc=dialog.outputDocument();int dims=0;for(const auto& entity:doc.entities)if(entity.kind==Kind::Dimension){++dims;QCOMPARE(entity.dimensionFormat,1);}QCOMPARE(dims,6);
        auto* gap=dialog.findChild<QDoubleSpinBox*>("panelGapX");QVERIFY(gap);gap->setValue(4);QVERIFY(generate->isEnabled());
        doc=dialog.outputDocument();bool found=false;for(const auto& entity:doc.entities)if(entity.layer=="PANEL_DIM_ARRAY"&&dimension::label(entity,4)=="ARRAY 134.600")found=true;QVERIFY(found);
        dialog.findChild<QComboBox*>("panelRotation")->setCurrentIndex(1);QVERIFY(generate->isEnabled());
        doc=dialog.outputDocument();found=false;for(const auto& entity:doc.entities)if(entity.layer=="PANEL_DIM_PCS"&&dimension::label(entity,4)=="PCS 22.900")found=true;QVERIFY(found);
        dialog.findChild<QPushButton*>("panelEditSuggestions")->click();QVERIFY(!dialog.findChild<QCheckBox*>("panelAutomaticBridges")->isChecked());QVERIFY(dialog.findChild<QTableWidget*>("panelBridgeTable")->rowCount()>0);QVERIFY(generate->isEnabled());
        dialog.findChild<QCheckBox*>("panelDimensions")->setChecked(false);doc=dialog.outputDocument();for(const auto& entity:doc.entities)QVERIFY(entity.kind!=Kind::Dimension);
        QCOMPARE(source.entities,baseline);QVERIFY(!source.modified());QVERIFY(doc.sourceDxf.isEmpty());
        auto* candidate=dialog.findChild<QComboBox*>("panelContourCandidates");QVERIFY(candidate);
        auto* sourceCanvas=dialog.findChild<CadCanvas*>("panelSourceCanvas");QVERIFY(sourceCanvas);sourceCanvas->selectIds({});dialog.findChild<QPushButton*>("panelConfirmContour")->click();
        QCOMPARE(candidate->count(),0);QVERIFY(generate->isEnabled());
        sourceCanvas->selectIds({source.entities.begin().key()});dialog.findChild<QPushButton*>("panelConfirmContour")->click();QVERIFY(generate->isEnabled());
        gap->setValue(0);QVERIFY_EXCEPTION_THROWN(dialog.outputDocument(),std::runtime_error);
    }
    void panelPdfAndScreenshot() {
        Entity e;e.kind=Kind::Polyline;e.closed=true;e.points={{0,0},{65.3,0},{65.3,14.4},{48,14.4},{48,22.9},{36,22.9},{36,12.1},{0,12.1}};
        engineering::panel::Options options;auto result=engineering::panel::generate(e,options);
        auto document=std::make_shared<Document>(engineering::panel::drawing(result,options));CadCanvas canvas(document);canvas.resize(1200,850);canvas.grid=false;canvas.show();canvas.fitDrawing();QTest::qWait(20);
        QTemporaryDir temp;auto output=qEnvironmentVariable("CAD_TEST_PANEL_UI_OUTPUT");auto pdf=output.isEmpty()?temp.filePath("panel.pdf"):output+".pdf";
        {QPdfWriter writer(pdf);writer.setPageSize(QPageSize(QPageSize::A4));QPainter painter(&writer);QVERIFY(painter.isActive());canvas.renderDocument(painter,{0,0,double(writer.width()),double(writer.height())});}
        QFile file(pdf);QVERIFY(file.open(QIODevice::ReadOnly));QVERIFY(file.read(5).startsWith("%PDF"));QVERIFY(file.size()>1000);
        auto screenshot=canvas.grab();QVERIFY(!screenshot.isNull());if(!output.isEmpty())QVERIFY(screenshot.save(output+".png"));
        Document source;source.load(e);source.markSaved();PanelizationDialog dialog(source,{source.entities.begin().key()},{});dialog.show();QTRY_VERIFY(dialog.findChild<QPushButton*>("panelGenerate")->isEnabled());
        auto* tabs=dialog.findChild<QTabWidget*>("panelTabs");QVERIFY(tabs);tabs->setCurrentIndex(1);QTest::qWait(30);
        if(!output.isEmpty())QVERIFY(dialog.grab().save(output+"-dialog.png"));
        dialog.resize(760,740);QTest::qWait(30);
        auto* preview=dialog.findChild<CadCanvas*>("panelPreviewCanvas");QVERIFY(preview);auto b=preview->document->bounds();
        auto a=preview->screenAt(b.topLeft()),z=preview->screenAt(b.bottomRight());QVERIFY(a.x()>=0&&z.x()<=preview->width());QVERIFY(z.y()>=0&&a.y()<=preview->height());
        if(!output.isEmpty())QVERIFY(dialog.grab().save(output+"-compact.png"));
        auto real=qEnvironmentVariable("CAD_TEST_PANEL_RENDER_INPUT");
        if(!real.isEmpty()) {
            auto production=std::make_shared<Document>(io::DxfAdapter::read(real));CadCanvas actual(production);actual.resize(1200,850);actual.grid=false;actual.show();actual.fitDrawing();QTest::qWait(20);
            if(!output.isEmpty()) {
                QVERIFY(actual.grab().save(output+"-E2.png"));
                QPdfWriter writer(output+"-E2.pdf");writer.setPageSize(QPageSize(QPageSize::A4));QPainter painter(&writer);QVERIFY(painter.isActive());actual.renderDocument(painter,{0,0,double(writer.width()),double(writer.height())});
            }
        }
    }
    void panelCommandGeneratesSeparateTab() {
        MainWindow window(false);Document source;Entity e;e.kind=Kind::Polyline;e.closed=true;e.points={{0,0},{20,0},{20,10},{0,10}};source.load(e);source.markSaved();window.addDocument(source);
        auto original=window.canvas()->document;window.canvas()->selectIds({original->entities.begin().key()});bool accepted=false;
        QTimer timer;timer.setInterval(10);connect(&timer,&QTimer::timeout,&window,[&]{auto* dialog=window.findChild<QDialog*>("panelizationDialog");if(!dialog)return;auto* button=dialog->findChild<QPushButton*>("panelGenerate");if(button&&button->isEnabled()){accepted=true;button->click();}});timer.start();
        QTimer::singleShot(5000,&window,[&]{if(auto* dialog=window.findChild<QDialog*>("panelizationDialog"))dialog->reject();});
        window.executeCommand("PANEL");timer.stop();QVERIFY(accepted);QVERIFY(window.canvas()->document!=original);QCOMPARE(original->entities.size(),1);QVERIFY(!original->modified());
        QVERIFY(window.canvas()->document->modified());window.canvas()->document->markSaved();
    }
    void automaticPanelWithNoSelectionGeneratesActualEntities() {
        MainWindow window(false);Document source;Entity shape;shape.kind=Kind::Polyline;shape.closed=true;shape.points={{0,0},{20,0},{20,8},{12,8},{12,10},{0,10}};source.load(shape);
        Entity text;text.kind=Kind::Text;text.points={{0,15}};text.text="Do not array this label";source.load(text);source.markSaved();window.addDocument(source);window.canvas()->selectIds({});
        auto original=window.canvas()->document;auto* action=window.findChild<QAction*>("panelizationAction");QVERIFY(action);QCOMPARE(action->text(),QString("拼版"));QVERIFY(!window.findChild<QAction*>("fpcMaterialAction"));
        bool clicked=false;QTimer timer;timer.setInterval(20);connect(&timer,&QTimer::timeout,&window,[&]{if(auto* dialog=window.findChild<QDialog*>("panelizationDialog")) {
            auto* combo=dialog->findChild<QComboBox*>("panelContourCandidates");if(combo&&combo->count()>0){clicked=true;dialog->findChild<QPushButton*>("panelGenerate")->click();}
        }});timer.start();QTimer::singleShot(10000,&window,[&]{if(auto* dialog=window.findChild<QDialog*>("panelizationDialog"))dialog->reject();});
        action->trigger();timer.stop();QVERIFY(clicked);auto result=window.canvas()->document;QVERIFY(result!=original);int count=0;for(const auto& e:result->entities)if(e.layer=="PANEL_PCS")++count;QCOMPARE(count,4);
        QTemporaryDir temp;auto output=temp.filePath("panel.dxf");io::DxfAdapter::write(*result,output);QCOMPARE(io::DxfAdapter::read(output).entities.size(),result->entities.size());
        QCOMPARE(original->entities.size(),2);QVERIFY(!original->modified());result->markSaved();
    }
    void productionE2DeleteAndAutomaticGenerate() {
        auto path=qEnvironmentVariable("CAD_TEST_PANEL_SOURCE");if(path.isEmpty())QSKIP("Set CAD_TEST_PANEL_SOURCE for real E2 deletion/automatic panel generation.");
        MainWindow window(false);window.addDocument(io::DxfAdapter::read(path));auto original=window.canvas()->document;
        QSet<EntityId> cleanup;for(const auto& e:original->entities)if(e.readOnly&&e.bounds().center().y()<140&&e.bounds().center().x()>95&&e.bounds().center().x()<210)cleanup.insert(e.id);
        QVERIFY(!cleanup.isEmpty());auto old=original->entities.size();window.canvas()->selectIds(cleanup);window.canvas()->deleteSelection();QVERIFY(original->entities.size()<old);
        auto bytes=io::DxfAdapter::serialize(*original);auto reread=io::DxfAdapter::parse(bytes);QCOMPARE(reread.entities.size(),original->entities.size());
        QSet<EntityId> roi;auto box=QRectF(110.94,150.58,65.34,22.94);
        for(const auto& e:original->entities)if(!e.readOnly&&(e.kind==Kind::Line||e.kind==Kind::Arc||e.kind==Kind::Polyline||e.kind==Kind::Rectangle)) {
            auto b=contour::bounds(e);if(box.contains(b.topLeft())&&box.contains(b.bottomRight()))roi.insert(e.id);
        }
        window.canvas()->selectIds(roi);bool clicked=false,confirmed=false;QTimer timer;timer.setInterval(20);QTimer confirmations;confirmations.setInterval(10);
        connect(&confirmations,&QTimer::timeout,&window,[&]{if(auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){confirmed=true;qInfo()<<"Generation dialog:"<<message->windowTitle()<<message->text();if(auto* yes=message->button(QMessageBox::Yes))yes->click();else message->accept();}});confirmations.start();
        connect(&timer,&QTimer::timeout,&window,[&]{
            if(auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){confirmed=true;if(auto* yes=message->button(QMessageBox::Yes))yes->click();else message->accept();return;}
            if(auto* dialog=window.findChild<QDialog*>("panelizationDialog"))if(dialog->findChild<QComboBox*>("panelContourCandidates")->count()>0&&!clicked){clicked=true;dialog->findChild<QPushButton*>("panelGenerate")->click();}
        });timer.start();QTimer::singleShot(15000,&window,[&]{if(auto* dialog=window.findChild<QDialog*>("panelizationDialog"))dialog->reject();});
        window.executeCommand("PANEL");timer.stop();confirmations.stop();QVERIFY(clicked);QVERIFY(confirmed);auto result=window.canvas()->document;QVERIFY(result!=original);
        int count=0;for(const auto& e:result->entities)if(e.layer=="PANEL_PCS"){++count;auto b=contour::bounds(e);QVERIFY(std::abs(b.width()-65.3)<0.001);QVERIFY(std::abs(b.height()-22.9)<0.001);}QCOMPARE(count,4);
        auto output=qEnvironmentVariable("CAD_TEST_PANEL_UI_OUTPUT");if(!output.isEmpty()){io::DxfAdapter::write(*result,output+"-auto.dxf");io::DxfAdapter::write(*original,output+"-cleaned.dxf");window.resize(1180,760);window.show();window.canvas()->fitDrawing();QTest::qWait(30);QVERIFY(window.grab().save(output+"-generated.png"));}
        result->markSaved();original->markSaved();
    }
    void crossingSelectDeletesYellowExternalDimensionAndReopens() {
        auto document=std::make_shared<Document>(io::DxfAdapter::parse(editableDimensionFixture()));
        CadCanvas canvas(document);canvas.resize(800,600);canvas.show();canvas.grid=false;canvas.fitDrawing();QTest::qWait(20);
        auto start=canvas.screenAt({125,12}).toPoint(),end=canvas.screenAt({115,8}).toPoint();
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,start);QTest::mouseMove(&canvas,end);QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,end);
        QVERIFY(!canvas.selection.isEmpty());QTest::keyClick(&canvas,Qt::Key_Delete);QVERIFY(document->entities.isEmpty());
        QTemporaryDir temp;auto output=temp.filePath("deleted.dxf");io::DxfAdapter::write(*document,output);QVERIFY(io::DxfAdapter::read(output).entities.isEmpty());
        QVERIFY(document->undo());canvas.synchronize();QVERIFY(!document->entities.isEmpty());QVERIFY(document->deletedSources.isEmpty());
    }
    void whiteStraightFrameLinesSelectAndDeleteInBothDirections() {
        auto document=std::make_shared<Document>();
        for(auto points:QVector<QVector<QPointF>>{{{0,0},{100,0}},{{0,0},{0,70}},{{100,0},{100,70}},{{0,70},{100,70}},{{20,0},{20,15}}}) {
            Entity e;e.points=points;e.color=QColor("#ffffff");document->load(e);
        }
        CadCanvas canvas(document);canvas.resize(800,600);canvas.grid=false;canvas.show();canvas.fitDrawing();QTest::qWait(20);
        auto select=[&](QPointF a,QPointF b){auto first=canvas.screenAt(a).toPoint(),last=canvas.screenAt(b).toPoint();QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,first);QTest::mouseMove(&canvas,last);QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,last);};
        select({-2,72},{102,-2});QCOMPARE(canvas.selection.size(),5);
        QTest::keyClick(&canvas,Qt::Key_Delete);QVERIFY(document->entities.isEmpty());QVERIFY(document->undo());canvas.synchronize();
        select({50,71},{40,69});QCOMPARE(canvas.selection.size(),1);QTest::keyClick(&canvas,Qt::Key_Delete);QCOMPARE(document->entities.size(),4);
        QVERIFY(document->undo());canvas.synchronize();select({1,35},{-1,25});QCOMPARE(canvas.selection.size(),1);QTest::keyClick(&canvas,Qt::Key_Delete);QCOMPARE(document->entities.size(),4);
    }
    void whiteReadOnlyTextWhitespaceSelectAndDelete() {
        auto d=io::DxfAdapter::parse("0\nSECTION\n2\nENTITIES\n0\nMTEXT\n10\n0\n20\n0\n40\n3\n71\n1\n1\nA                         B\n0\nENDSEC\n0\nEOF\n");
        auto document=std::make_shared<Document>(d);CadCanvas canvas(document);canvas.resize(800,600);canvas.grid=false;canvas.show();canvas.fitDrawing();QTest::qWait(20);
        auto e=document->entities.begin().value();QVERIFY(e.readOnly);auto box=CadCanvas::pathFor(e).boundingRect();auto center=box.center();QVERIFY(!CadCanvas::pathFor(e).contains(center));
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,canvas.screenAt(center).toPoint());QCOMPARE(canvas.selection.size(),1);
        QTest::keyClick(&canvas,Qt::Key_Delete);QVERIFY(document->entities.isEmpty());QVERIFY(io::DxfAdapter::parse(io::DxfAdapter::serialize(*document)).entities.isEmpty());
    }
    void productionWhiteFrameCleanupAndRoundtrip() {
        auto path=qEnvironmentVariable("CAD_TEST_PANEL_REFERENCE");if(path.isEmpty())QSKIP("Set CAD_TEST_PANEL_REFERENCE to E2-FPC-PANLE-20261124.dxf.");
        auto document=std::make_shared<Document>(io::DxfAdapter::read(path));CadCanvas canvas(document);canvas.resize(1400,900);canvas.grid=false;canvas.show();canvas.fitDrawing();QTest::qWait(30);
        // Original engineering title/frame region, separate from the panel at X > 450.
        auto start=canvas.screenAt({0,285}).toPoint(),end=canvas.screenAt({420,0}).toPoint();
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,start);QTest::mouseMove(&canvas,end);QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,end);
        int whiteLines=0,whiteText=0;for(auto id:canvas.selection){const auto& e=document->entities[id];if(e.kind==Kind::Line&&e.layer=="图框")++whiteLines;if(e.kind==Kind::Text)++whiteText;}
        QVERIFY(whiteLines>20);QVERIFY(whiteText>10);auto targets=document->deletionTargets(canvas.selection);auto count=document->entities.size()-targets.size();
        QTest::keyClick(&canvas,Qt::Key_Delete);QCOMPARE(document->entities.size(),count);auto saved=io::DxfAdapter::serialize(*document);auto reopened=io::DxfAdapter::parse(saved);QCOMPARE(reopened.entities.size(),count);
        for(const auto& e:reopened.entities)if(e.layer=="图框"&&e.kind==Kind::Line)QVERIFY(e.bounds().center().x()>420);
        auto output=qEnvironmentVariable("CAD_TEST_PANEL_UI_OUTPUT");if(!output.isEmpty()){io::DxfAdapter::write(*document,output+"-white-cleaned.dxf");canvas.fitDrawing();QTest::qWait(20);QVERIFY(canvas.grab().save(output+"-white-cleaned.png"));}
        QVERIFY(document->undo());canvas.synchronize();QVERIFY(document->entities.size()>count);QVERIFY(document->redo());QCOMPARE(document->entities.size(),count);
        // Full select/Delete removes every displayable colour, even read-only INSERTs.
        QSet<EntityId> all;for(const auto& e:document->entities)all.insert(e.id);canvas.selectIds(all);canvas.deleteSelection();QVERIFY(document->entities.isEmpty());
        QVERIFY(io::DxfAdapter::parse(io::DxfAdapter::serialize(*document)).entities.isEmpty());
    }
    void recoveryDialogRestoresUntitledAndRetainsDirtyState() {
        QTemporaryDir temp;QString oldSnapshot;
        {
            RecoveryManager manager(temp.path());auto document=std::make_shared<Document>();Entity line;line.points={{0,0},{15,0}};
            document->commit("draw",{},{line});manager.track(document);QSignalSpy saved(&manager,&RecoveryManager::snapshotSaved);
            manager.captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);
            QDirIterator it(temp.path(),{"*.cad-recovery"},QDir::Files,QDirIterator::Subdirectories);QVERIFY(it.hasNext());oldSnapshot=it.next();
        }
        MainWindow window(false,temp.path());window.show();
        auto* manager=window.findChild<RecoveryManager*>("recoveryManager");QVERIFY(manager);QSignalSpy saved(manager,&RecoveryManager::snapshotSaved);
        bool found=false;QTimer closer;
        connect(&closer,&QTimer::timeout,[&]{
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog||dialog->objectName()!="recoveryDialog")return;
            auto* list=dialog->findChild<QListWidget*>("recoveryEntries");auto* button=dialog->findChild<QPushButton*>("restoreRecoveryButton");
            if(list&&button&&list->count()==1){found=true;closer.stop();list->item(0)->setSelected(true);button->click();}
            else {closer.stop();dialog->reject();}
        });closer.start(5);window.showRecoveryDialog(true);QVERIFY(found);
        QCOMPARE(window.canvas()->document->entities.size(),1);QVERIFY(window.canvas()->document->modified());QVERIFY(window.canvas()->document->path.isEmpty());
        QVERIFY(window.windowTitle().contains("已恢复"));QVERIFY(window.windowTitle().endsWith(" *"));QVERIFY(!window.canvas()->document->canUndo());
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);QVERIFY(!QFile::exists(oldSnapshot));
        window.canvas()->document->markSaved();window.executeCommand("SELECT");manager->cleanSavedDocuments();QVERIFY(manager->available().isEmpty());
    }
    void corruptRecoveryCanBeDeferredOrExplicitlyDiscarded() {
        QTemporaryDir temp;
        {
            RecoveryManager manager(temp.path());auto document=std::make_shared<Document>();Entity line;line.points={{0,0},{10,0}};
            document->commit("draw",{},{line});manager.track(document);QSignalSpy saved(&manager,&RecoveryManager::snapshotSaved);manager.captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);
        }
        RecoveryManager reader(temp.path());auto entries=reader.available();QCOMPARE(entries.size(),1);auto path=entries.first().filePath;
        QFile corrupt(path);QVERIFY(corrupt.open(QIODevice::WriteOnly|QIODevice::Truncate));corrupt.write("broken");corrupt.close();
        MainWindow window(false,temp.path());bool listed=false;QTimer later;
        connect(&later,&QTimer::timeout,[&]{if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            if(dialog->objectName()=="recoveryDialog"){auto* list=dialog->findChild<QListWidget*>("recoveryEntries");listed=list&&list->count()==1&&list->item(0)->text().contains("无法读取");later.stop();dialog->reject();}
        }});later.start(5);window.showRecoveryDialog(true);QVERIFY(listed);QVERIFY(QFile::exists(path));QVERIFY(window.canvas()->document->entities.isEmpty());
        bool requested=false,confirmed=false;QTimer discard,confirm;
        connect(&confirm,&QTimer::timeout,[&]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {confirmed=true;box->button(QMessageBox::Yes)->click();confirm.stop();}});
        connect(&discard,&QTimer::timeout,[&]{
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog||dialog->objectName()!="recoveryDialog"||requested)return;
            requested=true;discard.stop();auto* list=dialog->findChild<QListWidget*>("recoveryEntries");list->item(0)->setSelected(true);confirm.start(5);dialog->findChild<QPushButton*>("discardRecoveryButton")->click();
        });discard.start(5);window.showRecoveryDialog();discard.stop();confirm.stop();QVERIFY(requested);QVERIFY(confirmed);QVERIFY(!QFile::exists(path));
    }
    void canceledClosePreservesRecoveryAcrossAllTabs() {
        QTemporaryDir temp;MainWindow window(false,temp.path());window.show();
        window.executeCommand("LINE");window.executeCommand("0,0");window.executeCommand("10,0");
        auto first=window.canvas()->document;window.addDocument(Document{});window.executeCommand("LINE");window.executeCommand("0,0");window.executeCommand("20,0");
        auto second=window.canvas()->document;auto* manager=window.findChild<RecoveryManager*>();QSignalSpy saved(manager,&RecoveryManager::snapshotSaved);manager->captureNow();
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(),2,5000);int answers=0;QTimer responder;
        connect(&responder,&QTimer::timeout,[&]{
            if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                ++answers;box->button(answers==1?QMessageBox::Discard:QMessageBox::Cancel)->click();
            }
        });responder.start(5);QVERIFY(!window.close());responder.stop();QCOMPARE(answers,2);
        QVERIFY(first->modified());QVERIFY(second->modified());QVERIFY(window.isVisible());
        QDirIterator it(temp.path(),{"*.cad-recovery"},QDir::Files,QDirIterator::Subdirectories);int files=0;while(it.hasNext()){it.next();++files;}QCOMPARE(files,2);
        first->markSaved();second->markSaved();QVERIFY(window.close());
        QTRY_VERIFY_WITH_TIMEOUT(!manager->busy(),5000);QVERIFY(manager->available().isEmpty());
    }
    void closeTabDiscardCleansOnlyItsOwnSnapshot() {
        QTemporaryDir temp;MainWindow window(false,temp.path());window.show();
        window.executeCommand("LINE");window.executeCommand("0,0");window.executeCommand("10,0");auto first=window.canvas()->document;
        window.addDocument(Document{});window.executeCommand("CIRCLE");window.executeCommand("0,0");window.executeCommand("5,0");
        auto* tabs=window.findChild<QTabWidget*>("documentTabs");auto* manager=window.findChild<RecoveryManager*>();QSignalSpy saved(manager,&RecoveryManager::snapshotSaved);
        manager->captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),2,5000);
        QTimer responder;connect(&responder,&QTimer::timeout,[&]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))box->button(QMessageBox::Discard)->click();});
        responder.start(5);tabs->tabCloseRequested(1);responder.stop();QCOMPARE(tabs->count(),1);QVERIFY(first->modified());
        QDirIterator it(temp.path(),{"*.cad-recovery"},QDir::Files,QDirIterator::Subdirectories);int files=0;while(it.hasNext()){it.next();++files;}QCOMPARE(files,1);
        first->markSaved();QVERIFY(window.close());
    }
    void backgroundSaveDoesNotMarkLaterEditSaved() {
        QTemporaryDir temp;MainWindow window(false,temp.filePath("recovery"));window.show();auto* c=window.canvas();
        auto path=temp.filePath("saved.dxf");c->document->path=path;
        window.executeCommand("LINE");window.executeCommand("0,0");window.executeCommand("10,0");
        auto actions=window.findChildren<QAction*>();QAction* save=nullptr;for(auto* action:actions)if(action->shortcut()==QKeySequence("Ctrl+S"))save=action;
        QVERIFY(save);save->trigger();c->document->setUnits(1);
        auto* console=window.findChild<QTextEdit*>();QTRY_VERIFY_WITH_TIMEOUT(console->toPlainText().contains("已保存："+path),5000);
        QVERIFY(c->document->modified());QCOMPARE(io::DxfAdapter::read(path).units,4);QCOMPARE(c->document->units,1);
        auto* manager=window.findChild<RecoveryManager*>();QSignalSpy snapshots(manager,&RecoveryManager::snapshotSaved);manager->captureNow();QTRY_COMPARE_WITH_TIMEOUT(snapshots.size(),1,5000);
        save->trigger();QTRY_VERIFY_WITH_TIMEOUT(!c->document->modified(),5000);QCOMPARE(io::DxfAdapter::read(path).units,1);
        QDirIterator it(temp.filePath("recovery"),{"*.cad-recovery"},QDir::Files,QDirIterator::Subdirectories);QVERIFY(!it.hasNext());
        QVERIFY(window.close());
    }
    void failedSaveRetainsDirtyStateAndSnapshot() {
        QTemporaryDir temp;MainWindow window(false,temp.filePath("recovery"));window.show();window.executeCommand("LINE");window.executeCommand("0,0");window.executeCommand("10,0");
        auto* manager=window.findChild<RecoveryManager*>();QSignalSpy saved(manager,&RecoveryManager::snapshotSaved);manager->captureNow();QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);
        window.canvas()->document->path=temp.filePath("missing-parent/save.dxf");
        QTimer dismiss;connect(&dismiss,&QTimer::timeout,[&]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))box->accept();});dismiss.start(5);
        for(auto* action:window.findChildren<QAction*>())if(action->shortcut()==QKeySequence("Ctrl+S"))action->trigger();
        auto* console=window.findChild<QTextEdit*>();QTRY_VERIFY_WITH_TIMEOUT(console->toPlainText().contains("DXF 保存失败"),5000);dismiss.stop();
        QVERIFY(window.canvas()->document->modified());QVERIFY(!QFile::exists(window.canvas()->document->path));
        QDirIterator it(temp.filePath("recovery"),{"*.cad-recovery"},QDir::Files,QDirIterator::Subdirectories);QVERIFY(it.hasNext());
        window.canvas()->document->markSaved();manager->cleanSavedDocuments();
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
    void importedTextAndDimensionRender() {
        auto document=std::make_shared<Document>(io::DxfAdapter::parse(textDimensionFixture()));
        for(const auto& e:document->entities)if(e.kind==Kind::Text) {
            auto path=CadCanvas::pathFor(e);QVERIFY(!path.isEmpty());
            QVERIFY(document->bounds().contains(path.boundingRect()));
            if(e.points[0]==QPointF(50,60)){QVERIFY(std::abs(path.boundingRect().bottom()-60)<1e-6);QVERIFY(path.boundingRect().top()<60);}
            if(e.points[0]==QPointF(40,50)){QVERIFY(std::abs(path.boundingRect().right()-40)<1e-6);}
        }
        CadCanvas canvas(document);canvas.resize(800,600);canvas.grid=false;canvas.show();canvas.fitDrawing();QTest::qWait(20);
        auto probe=canvas.screenAt({15,20}).toPoint();QVERIFY(canvas.grab().toImage().pixelColor(probe)!=QColor("#101922"));
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,probe);QCOMPARE(canvas.selection.size(),1);
        auto targets=document->deletionTargets(canvas.selection);auto expected=document->entities.size()-targets.size();QVERIFY(!targets.isEmpty());canvas.deleteSelection();QCOMPARE(document->entities.size(),expected);
        QCOMPARE(io::DxfAdapter::parse(io::DxfAdapter::serialize(*document)).entities.size(),expected);QVERIFY(document->undo());canvas.synchronize();QCOMPARE(document->entities.size(),4);
    }
    void wrappedTextUsesSameBoundsAsPainting() {
        auto document=std::make_shared<Document>();
        Entity text;text.kind=Kind::Text;text.readOnly=true;text.points={{10,20}};text.height=2;
        text.textBoxWidth=24;text.textVertical=3;text.text=QString::fromUtf8("技术要求：Long English text wraps at its column width. 一二三四五六七八九十\n第二条说明");
        auto wrapped=CadCanvas::pathFor(text).boundingRect();
        QVERIFY2(wrapped.width()<=text.textBoxWidth+0.5,qPrintable(QString("wrapped width %1 exceeds column %2").arg(wrapped.width()).arg(text.textBoxWidth)));QVERIFY(wrapped.height()>text.height*3);
        QCOMPARE(text.bounds(),wrapped);
        auto unwrapped=text;unwrapped.textBoxWidth=0;QVERIFY(unwrapped.bounds().width()>wrapped.width()*2);
        for(double rotation:{0.,90.,33.})for(int anchor:{0,1,2}) {
            auto rotated=text;rotated.rotation=rotation;rotated.textHorizontal=anchor;
            QCOMPARE(rotated.bounds(),CadCanvas::pathFor(rotated).boundingRect());
        }
        document->load(text);CadCanvas canvas(document);canvas.resize(800,600);canvas.grid=false;canvas.show();canvas.fitDrawing();QTest::qWait(20);
        auto path=CadCanvas::pathFor(text);auto screen=QRectF(canvas.screenAt(path.boundingRect().topLeft()),canvas.screenAt(path.boundingRect().bottomRight())).normalized();
        QVERIFY(canvas.rect().contains(screen.toAlignedRect()));
        QImage image(800,600,QImage::Format_ARGB32);image.fill(Qt::white);QPainter painter(&image);canvas.renderDocument(painter,image.rect());painter.end();
        QVERIFY(!image.isNull());
    }
    void dimensionTextDoubleClickEditAndUndo() {
        auto source=editableDimensionFixture();source.replace("0\nDIMENSION\n5\n12", "0\nDIMENSION\n67\n1\n5\n12");
        MainWindow window(false);window.addDocument(io::DxfAdapter::parse(source));window.show();QTest::qWait(30);
        auto* c=window.canvas();c->fitDrawing();Entity label;
        for(const auto& e:c->document->entities)if(!e.dimensionTextKey.isEmpty())label=e;
        QVERIFY(label.id);auto glyphs=CadCanvas::pathFor(label);
        auto unicode=label;unicode.text=QString::fromUtf8("120.000±0.2");
        QCOMPARE(glyphs,CadCanvas::pathFor(unicode));QCOMPARE(text::display("%%p %%D %%C"),QString::fromUtf8("± ° Ø"));
        QPointF hit;
        for(int i=0;i<glyphs.elementCount();++i){auto e=glyphs.elementAt(i);if(e.isLineTo()){hit={e.x,e.y};break;}}
        bool shown=false,previewOk=false;
        QTimer::singleShot(0,[&]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(!dialog||dialog->objectName()!="textEditDialog")return;
            shown=true;auto* input=dialog->findChild<QLineEdit*>("textOverrideInput");if(!input){dialog->reject();return;}
            input->setText("<>%%P0.05");auto* preview=dialog->findChild<QLabel*>("textOverridePreview");
            previewOk=preview&&preview->text()==QString::fromUtf8("120.000±0.05");dialog->accept();});
        QTest::mouseDClick(c,Qt::LeftButton,Qt::NoModifier,c->screenAt(hit).toPoint());
        QVERIFY(shown);QVERIFY(previewOk);QCOMPARE(c->document->entities[label.id].text,QString("120.000%%P0.05"));
        QVERIFY(CadCanvas::pathFor(c->document->entities[label.id])!=glyphs);
        auto saved=io::DxfAdapter::parse(io::DxfAdapter::serialize(*c->document));bool found=false;
        for(const auto& e:saved.entities)if(e.text=="120.000%%P0.05")found=true;
        QVERIFY(found);
        window.executeCommand("UNDO");QCOMPARE(c->document->entities[label.id].text,label.text);
        window.executeCommand("REDO");QCOMPARE(c->document->entities[label.id].text,QString("120.000%%P0.05"));
        c->selectIds({label.id});c->deleteSelection();QVERIFY(!c->document->entities.contains(label.id));QVERIFY(c->document->undo());c->document->markSaved();
    }
    void textEditingVisibleControlsAndSpaceHit() {
        MainWindow window(false);auto* c=window.canvas();Entity label;label.kind=Kind::Text;label.points={{0,0}};
        label.height=3;label.rotation=33;label.text="A                         B";c->document->load(label);
        label=c->document->entities.begin().value();auto glyphs=CadCanvas::pathFor(label);auto hit=glyphs.boundingRect().center();
        QVERIFY(!glyphs.contains(hit));Entity line;line.points={hit-QPointF(2,0),hit+QPointF(2,0)};c->document->load(line);c->synchronize();
        window.show();QTest::qWait(30);c->fitDrawing();
        auto* action=window.findChild<QAction*>("editTextAction");auto* button=window.findChild<QPushButton*>("editTextButton");
        auto* toolbar=window.findChild<QToolBar*>("drawingToolbar");auto* dock=window.findChild<QDockWidget*>("对象属性");
        QVERIFY(action);QVERIFY(button);QVERIFY(toolbar);QVERIFY(dock);QVERIFY(toolbar->actions().contains(action));
        QVERIFY(!action->isEnabled());QVERIFY(!button->isEnabled());
        QTest::mouseClick(c,Qt::LeftButton,Qt::NoModifier,c->screenAt(hit).toPoint());
        QCOMPARE(c->selection,QSet<EntityId>{label.id});QVERIFY(action->isEnabled());QVERIFY(button->isEnabled());QVERIFY(dock->isVisible());
        bool shown=false;
        auto closeEditor=[&]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
            shown=dialog->objectName()=="textEditDialog";dialog->reject();};
        QTimer::singleShot(0,closeEditor);QTest::mouseDClick(c,Qt::LeftButton,Qt::NoModifier,c->screenAt(hit).toPoint());QVERIFY(shown);
        shown=false;QTimer::singleShot(0,closeEditor);QTest::mouseClick(button,Qt::LeftButton);QVERIFY(shown);
        shown=false;QTimer::singleShot(0,closeEditor);action->trigger();QVERIFY(shown);
        auto layers=c->document->layers;layers[label.layer].locked=true;c->document->changeLayers("lock",layers);c->synchronize();
        QVERIFY(!action->isEnabled());QVERIFY(!button->isEnabled());QVERIFY(c->document->undo());c->synchronize();
        QVERIFY(action->isEnabled());c->selectIds({});QVERIFY(!action->isEnabled());QVERIFY(!button->isEnabled());
        c->document->markSaved();
    }
    void dimensionLinesDoubleClickOpensTextEditor() {
        auto source=editableDimensionFixture();source.replace("0\nDIMENSION\n5\n12", "0\nDIMENSION\n67\n1\n5\n12");
        MainWindow window(false);window.addDocument(io::DxfAdapter::parse(source));window.show();QTest::qWait(30);
        auto* c=window.canvas();c->fitDrawing();bool shown=false;
        QTimer::singleShot(0,[&]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
            shown=dialog->objectName()=="textEditDialog";dialog->reject();});
        QTest::mouseDClick(c,Qt::LeftButton,Qt::NoModifier,c->screenAt({20,10}).toPoint());QVERIFY(shown);
        QCOMPARE(c->selection.size(),1);QVERIFY(c->document->textEditable(*c->selection.begin()));
    }
    void localDwgTextExtents() {
        auto path=qEnvironmentVariable("CAD_TEST_TEXT_EXTENTS_DWG");if(path.isEmpty())QSKIP("Set CAD_TEST_TEXT_EXTENTS_DWG to check the production wrapping regression.");
        MainWindow window(false);window.setProperty("checkOpen",true);window.show();QSignalSpy opened(&window,&MainWindow::fileOpened),failed(&window,&MainWindow::fileOpenFailed);
        window.openPath(path);QTRY_VERIFY_WITH_TIMEOUT(opened.count()+failed.count()>0,30000);
        QVERIFY2(failed.isEmpty(),failed.isEmpty()?"":qPrintable(failed.first().first().toString()));
        auto document=window.canvas()->document;QVERIFY(document->entities.size()>3800);
        auto box=document->bounds();QVERIFY(box.width()<450);QVERIFY(box.height()<350);
        bool found=false;
        for(const auto& e:document->entities)if(e.kind==Kind::Text&&e.text.startsWith(QString::fromUtf8("技术要求"))) {
            found=true;QVERIFY(e.text.count('\n')>=20);QVERIFY(std::abs(e.textBoxWidth-92.79310788853839)<1e-6);
            auto glyphs=CadCanvas::pathFor(e);QCOMPARE(e.bounds(),glyphs.boundingRect());QVERIFY(glyphs.boundingRect().width()<95);
            SpatialIndex index;index.rebuild(*document);QVERIFY(index.query(glyphs.boundingRect()).contains(e.id));
        }
        QVERIFY(found);window.canvas()->fitDrawing();QTest::qWait(200);
        auto screenshot=qEnvironmentVariable("CAD_TEST_SCREENSHOT");if(!screenshot.isEmpty())QVERIFY(window.grab().save(screenshot));
        qInfo()<<"Production wrapping regression:"<<document->entities.size()<<"entities, bounds"<<box;
    }
    void localRedBoardTextEditor() {
        auto path=qEnvironmentVariable("CAD_TEST_DIMENSION_DXF");if(path.isEmpty())QSKIP("Set CAD_TEST_DIMENSION_DXF for the red-board editor regression.");
        branding::AppBrand::apply(*qobject_cast<QApplication*>(QCoreApplication::instance()));
        MainWindow window(false);window.setProperty("checkOpen",true);window.resize(1600,1000);window.show();window.openPath(path);
        QTRY_COMPARE_WITH_TIMEOUT(window.canvas()->document->path,path,30000);
        auto* c=window.canvas();Entity label;
        for(const auto& e:c->document->entities)if(!e.dimensionTextKey.isEmpty()&&e.text=="120.000%%P0.2")label=e;
        QVERIFY(label.id);c->fitDrawing();QTest::qWait(30);bool shown=false;
        QTimer::singleShot(0,[&]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
            auto* input=dialog->findChild<QLineEdit*>("textOverrideInput");if(!input){dialog->reject();return;}
            shown=true;input->setText("120.00%%P0.1");dialog->accept();});
        QTest::mouseDClick(c,Qt::LeftButton,Qt::NoModifier,c->screenAt(CadCanvas::pathFor(label).boundingRect().center()).toPoint());
        QVERIFY(shown);QCOMPARE(c->document->entities[label.id].text,QString("120.00%%P0.1"));
        QVERIFY(window.findChild<QPushButton*>("editTextButton")->isEnabled());
        auto saved=io::DxfAdapter::parse(io::DxfAdapter::serialize(*c->document));QCOMPARE(saved.entities.size(),c->document->entities.size());
        auto screenshot=qEnvironmentVariable("CAD_TEST_SCREENSHOT");c->fitDrawing();QTest::qWait(100);
        if(!screenshot.isEmpty())QVERIFY(window.grab().save(screenshot));
        window.executeCommand("UNDO");QCOMPARE(c->document->entities[label.id],label);c->document->markSaved();
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
