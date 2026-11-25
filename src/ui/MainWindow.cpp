#include "MainWindow.h"
#include "CommandParser.h"
#include "MeasurePanel.h"
#include "RecoveryManager.h"
#include "PanelizationDialog.h"
#include "branding/AppBrand.h"
#include "core/Editing.h"
#include "core/TextLayout.h"
#include "core/DimensionLayout.h"
#include "io/DwgAdapter.h"
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QDockWidget>
#include <QComboBox>
#include <QListWidget>
#include <QTextEdit>
#include <QTextDocument>
#include <QLineEdit>
#include <QLabel>
#include <QAction>
#include <QActionGroup>
#include <QMenuBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QColorDialog>
#include <QFileInfo>
#include <QDir>
#include <QCloseEvent>
#include <QStatusBar>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QProgressDialog>
#include <QPdfWriter>
#include <QPainter>
#include <QSaveFile>
#include <QDialog>
#include <QTextBrowser>
#include <QCheckBox>
#include <cmath>
#include <stdexcept>
#include <QRegularExpression>
#include <algorithm>
#include <atomic>

namespace cad::ui {
QString applicationStyle() {return branding::AppBrand::styleSheet();}
namespace {
struct OpenResult {Document document;QString error;};
struct SaveResult {QString error;};
struct AnalysisResult {QVector<engineering::Finding> findings;QString error;double maxCurveError=0;bool precisionReported=false;};
QString exceptionText(const std::exception& e){return QString::fromUtf8(e.what());}
QDoubleSpinBox* number(double value,double min=-1e9,double max=1e9){auto* w=new QDoubleSpinBox;w->setRange(min,max);w->setDecimals(6);w->setValue(value);return w;}
bool dialogButtons(QDialog& dialog,QFormLayout& layout) {
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout.addRow(buttons);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    return dialog.exec()==QDialog::Accepted;
}
// Keep the initial workspace compact and usable on 1366x768-class displays.
QSize compactWindowSize() {
    QSize preferred(1180,760);
    if(auto* screen=QGuiApplication::primaryScreen()) {
        auto available=screen->availableGeometry().size();
        preferred.setWidth(std::min(preferred.width(),std::max(760,int(available.width()*0.92))));
        preferred.setHeight(std::min(preferred.height(),std::max(520,int(available.height()*0.88))));
        preferred=preferred.boundedTo(available);
    }
    return preferred;
}
void keepWindowOnScreen(QWidget* window) {
    if(window->isMaximized()||window->isFullScreen())return;
    auto* screen=window->screen()?window->screen():QGuiApplication::primaryScreen();
    if(!screen)return;
    auto available=screen->availableGeometry();
    auto size=window->size().boundedTo(available.size());
    if(size!=window->size())window->resize(size);
    auto frame=window->frameGeometry();
    auto visible=frame.intersected(available.adjusted(24,24,-24,-24));
    // A frame that merely touches the screen edge is effectively unusable;
    // require a meaningful visible area before keeping the restored position.
    if(visible.width()<120||visible.height()<80) {
        frame.moveCenter(available.center());
        window->move(frame.topLeft());
    }
}
}
MainWindow::MainWindow(bool demo,QString recoveryDirectory) {
    recovery_=new RecoveryManager(std::move(recoveryDirectory),this);recovery_->setObjectName("recoveryManager");
    setWindowIcon(branding::AppBrand::icon());setMinimumSize(640,480);resize(compactWindowSize());buildUi();
    if(!QStandardPaths::isTestModeEnabled()) {
        auto saved=settings_.value("ui/windowGeometry").toByteArray();
        if(!saved.isEmpty())restoreGeometry(saved);
        keepWindowOnScreen(this);
    }
    connect(recovery_,&RecoveryManager::failure,this,&MainWindow::log);
    addDocument(demo?Document::demo():Document{});
    log("CAD · Qiao / C++ / Qt 6 · 滚轮缩放 / 中键平移 / F3 捕捉 / F8 正交");
    log("视图：方向键移动图纸（Shift 加速 / Ctrl 微调），+ / - 缩放，Home 显示全部；命令栏输入时方向键仍用于移动光标。");
    if(demo)log("已打开 PCB 演示板。");
    else log("已新建空白图纸；可从 文件 → 打开演示板 查看 PCB 示例。");
    log("命令：LINE → 0,0 → @20,0；距离<角度：20<45。后台文件读取与工程分析已启用。");
    log("每 60 秒自动生成未保存修改的恢复快照，不覆盖原图；文件 → 恢复未保存图纸 可查看异常退出前的快照。");
}
CadCanvas* MainWindow::canvas() const {return qobject_cast<CadCanvas*>(tabs_->currentWidget());}
QAction* MainWindow::action(QString text,std::function<void()> callback,QString shortcut) {
    auto* a=new QAction(text,this);if(!shortcut.isEmpty())a->setShortcut(QKeySequence(shortcut));connect(a,&QAction::triggered,this,[callback]{callback();});return a;
}
void MainWindow::buildUi() {
    setDockOptions(QMainWindow::AllowTabbedDocks|QMainWindow::AnimatedDocks);
    tabs_=new QTabWidget;tabs_->setObjectName("documentTabs");tabs_->setTabsClosable(true);tabs_->setMovable(true);setCentralWidget(tabs_);
    auto* file=menuBar()->addMenu("文件");
    auto* newAction=action("新建",[this]{addDocument(Document{});},"Ctrl+N");file->addAction(newAction);
    auto* open=action("打开 DXF / DWG",[this]{auto p=QFileDialog::getOpenFileName(this,"打开图纸",{},"CAD (*.dxf *.dwg)");if(!p.isEmpty())openPath(p);},"Ctrl+O");file->addAction(open);
    auto* save=action("保存",[this]{saveFile();},"Ctrl+S");file->addAction(save);file->addAction(action("另存为",[this]{saveFile(true);},"Ctrl+Shift+S"));
    file->addAction(action("导出 PDF",[this]{exportPdf();}));
    auto* recover=action("恢复未保存图纸…",[this]{showRecoveryDialog();});recover->setObjectName("recoverDocumentsAction");file->addAction(recover);
    auto* snapshot=action("立即创建恢复快照",[this]{recovery_->captureNow();log("已请求后台恢复快照，仅记录未保存修改，不替代手动保存。");});snapshot->setObjectName("captureRecoveryAction");file->addAction(snapshot);
    file->addAction(action("设置单位",[this]{QStringList names{"图形单位","inch","mm","cm","m","mil","µm"};QVector<int> codes{0,1,4,5,6,9,13};bool ok=false;
        auto name=QInputDialog::getItem(this,"设置单位","仅声明单位，不缩放坐标；测量 / 标注按该单位换算 mm 与 mil",names,std::max(0,int(codes.indexOf(canvas()->document->units))),false,&ok);
        if(ok){canvas()->document->setUnits(codes[names.indexOf(name)]);results_->clear();refresh();}}));
    file->addAction(action("配置 DWG 转换器",[this]{auto p=QFileDialog::getOpenFileName(this,"选择 ODAFileConverter.exe",io::DwgAdapter::findConverter(settings_.value("converter").toString()),"ODA 转换器 (ODAFileConverter.exe)");if(!p.isEmpty()){settings_.setValue("converter",p);log("已配置 DWG 转换器："+p);}}));
    file->addAction(action("打开演示板",[this]{addDocument(Document::demo());}));file->addSeparator();file->addAction(action("退出",[this]{close();},"Alt+F4"));
    auto* edit=menuBar()->addMenu("编辑");
    undo_=action("撤销",[this]{canvas()->setTool(Tool::Select);if(canvas()->document->undo()){results_->clear();canvas()->synchronize();refresh();}},"Ctrl+Z");
    redo_=action("恢复",[this]{canvas()->setTool(Tool::Select);if(canvas()->document->redo()){results_->clear();canvas()->synchronize();refresh();}},"Ctrl+Y");edit->addActions({undo_,redo_});
    edit->addAction(action("全选可见对象",[this]{QSet<EntityId> ids;for(const auto& e:canvas()->document->entities)if(canvas()->document->layers.value(e.layer).visible)ids.insert(e.id);canvas()->selectIds(ids);},"Ctrl+A"));
    auto* erase=action("删除选中",[this]{canvas()->deleteSelection();},"Delete");erase->setObjectName("deleteSelectionAction");edit->addAction(erase);
    for(auto op:QStringList{"旋转","阵列","偏移","修剪","延伸","分解","合并"})edit->addAction(action(op,[this,op]{editSelected(op);}));
    edit->addAction(action("编辑 / 批量属性",[this]{editProperties();}));
    editTextAction_=action("编辑文字 / 标注",[this]{editText();},"Ctrl+E");editTextAction_->setObjectName("editTextAction");
    editTextAction_->setToolTip("选择文字或尺寸标注后点击编辑；也可在画布上直接双击文字（Ctrl+E）");edit->addAction(editTextAction_);
    auto* draw=menuBar()->addMenu("绘图");auto* measure=menuBar()->addMenu("测量");auto* view=menuBar()->addMenu("视图");
    auto* fit=action("显示全部",[this]{canvas()->fitDrawing();},"Home");view->addAction(fit);
    view->addAction(action("居中选中对象",[this]{canvas()->centerOnSelection();canvas()->setFocus();}));
    view->addAction(action("框选缩放",[this]{canvas()->setTool(Tool::Zoom);}));
    auto* navigation=view->addMenu("平移与缩放");
    auto panAction=[&](QString text,QPointF delta){navigation->addAction(action(text,[this,delta]{canvas()->panByPixels(delta);canvas()->setFocus();}));};
    panAction("向左移动图纸（←）",{-48,0});panAction("向右移动图纸（→）",{48,0});
    panAction("向上移动图纸（↑）",{0,-48});panAction("向下移动图纸（↓）",{0,48});
    navigation->addSeparator();
    navigation->addAction(action("放大（+）",[this]{canvas()->zoomBy(1.2);canvas()->setFocus();}));
    navigation->addAction(action("缩小（-）",[this]{canvas()->zoomBy(1/1.2);canvas()->setFocus();}));
    view->addAction(action("设置坐标原点",[this]{canvas()->setCoordinateOrigin(canvas()->cursorPoint());log("坐标原点已设为当前光标位置；状态栏与坐标命令按此原点显示 mm / mil。");}));
    view->addAction(action("重置坐标原点",[this]{canvas()->setCoordinateOrigin({0,0});log("坐标原点已重置为图纸原点 (0,0)。");}));
    auto mode=[&](QString name,QString shortcut,bool CadCanvas::*member){auto* a=new QAction(name,this);a->setCheckable(true);a->setShortcut(QKeySequence(shortcut));connect(a,&QAction::triggered,this,[this,member](bool v){canvas()->*member=v;
        if(member==&CadCanvas::ortho&&v){canvas()->polar=false;polar_->setChecked(false);}if(member==&CadCanvas::polar&&v){canvas()->ortho=false;ortho_->setChecked(false);}canvas()->update();});view->addAction(a);return a;};
    snap_=mode("对象捕捉","F3",&CadCanvas::snap);ortho_=mode("正交","F8",&CadCanvas::ortho);polar_=mode("45° 极轴","F10",&CadCanvas::polar);grid_=mode("网格","F7",&CadCanvas::grid);
    auto* engineering=menuBar()->addMenu("工程分析");for(auto op:QStringList{"板框","孔径","间距","焊盘","工艺边","重复"})engineering->addAction(action(op,[this,op]{runAnalysis(op);}));
    auto* panelAction=action("拼版",[this]{panelize();});panelAction->setObjectName("panelizationAction");engineering->addAction(panelAction);
    engineering->addAction(action("清理重复对象",[this]{editSelected("清理");}));
    auto* help=menuBar()->addMenu("帮助");help->addAction(action("操作教程 / 快速上手",[this]{showStartupTutorial(false);}));help->addAction(action("关于 CAD / 支持范围",[this]{QMessageBox::information(this,branding::AppBrand::name()+" "+branding::AppBrand::version(),"Qiao · Qt 6 + C++20 / CMake\nASCII DXF 原生读写，DWG 通过内置 LibreDWG 或可选 ODA 读取。保存 DWG 需要 ODA。\n支持基础二维绘图、编辑、测量、图层和 PCB 分析。\n暂不显示的块、HATCH、复杂文字、布局和 3D 对象保留在转换后的 DXF 中，原 DWG 不保证无损转换。\nLibreDWG GPLv3 许可证及对应源码位于 tools/libredwg。\n圆角/倒角、填充、Gerber 对照、图纸空间后续实现。详见使用说明。");}));
    auto* toolbar=addToolBar("文件与视图");toolbar->setObjectName("mainToolbar");toolbar->setMovable(false);
    auto* brandIcon=new QLabel;brandIcon->setObjectName("brandIcon");brandIcon->setPixmap(branding::AppBrand::icon().pixmap(28,28));brandIcon->setToolTip("Qiao");toolbar->addWidget(brandIcon);
    auto* brand=new QLabel(branding::AppBrand::name()+" · "+branding::AppBrand::iconLabel());brand->setObjectName("brand");toolbar->addWidget(brand);toolbar->addActions({newAction,open,save,undo_,redo_,fit,snap_,ortho_,polar_,grid_});
    addToolBarBreak();auto* drawing=addToolBar("绘图与测量");drawing->setObjectName("drawingToolbar");drawing->setMovable(false);auto* group=new QActionGroup(this);
    for(auto t:{Tool::Select,Tool::Line,Tool::Polyline,Tool::Rectangle,Tool::Circle,Tool::Arc,Tool::Ellipse,Tool::Text,Tool::Move,Tool::Copy,Tool::Mirror,Tool::Distance,Tool::Gap,Tool::Angle,Tool::Dimension}) {
        auto* a=action(CadCanvas::toolName(t),[this,t]{canvas()->setTool(t);canvas()->setFocus();});a->setCheckable(true);group->addAction(a);tools_.insert(t,a);drawing->addAction(a);
        if(t>=Tool::Line&&t<=Tool::Text)draw->addAction(a);
        if(t>=Tool::Distance)measure->addAction(a);
    }
    drawing->addSeparator();drawing->addAction(editTextAction_);
    drawing->addAction(erase);
    drawing->addAction(panelAction);
    for(auto t:{Tool::Horizontal,Tool::Vertical,Tool::Continuous,Tool::Coordinate})measure->addAction(action(CadCanvas::toolName(t),[this,t]{canvas()->setTool(t);}));
    measure->addAction(action("对象面积 / 半径 / 周长",[this]{for(const auto& e:canvas()->document->selected(canvas()->selection)){QStringList text;for(auto [k,v]:e.properties(*canvas()->document))text.append(k+": "+v);log(text.join(" · "));}}));
    auto dock=[&](QString title,QWidget* widget,Qt::DockWidgetArea area){auto* d=new QDockWidget(title,this);d->setObjectName(title);d->setWidget(widget);addDockWidget(area,d);view->addAction(d->toggleViewAction());return d;};
    auto* propsWidget=new QWidget;auto* propsLayout=new QVBoxLayout(propsWidget);properties_=new QTableWidget(0,2);properties_->setHorizontalHeaderLabels({"属性","数值"});properties_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);properties_->verticalHeader()->hide();properties_->setEditTriggers(QAbstractItemView::NoEditTriggers);propsLayout->addWidget(properties_);
    textEditHint_=new QLabel("双击画布上的文字 / 标注即可编辑，也可先选中再点击下方按钮。");textEditHint_->setObjectName("textEditHint");textEditHint_->setWordWrap(true);textEditHint_->setTextFormat(Qt::PlainText);propsLayout->addWidget(textEditHint_);
    textEditButton_=new QPushButton("编辑文字 / 标注（双击可编辑）");textEditButton_->setObjectName("editTextButton");textEditButton_->setToolTip(editTextAction_->toolTip());connect(textEditButton_,&QPushButton::clicked,this,&MainWindow::editText);propsLayout->addWidget(textEditButton_);
    auto* propertyButton=new QPushButton("编辑 / 批量修改属性");connect(propertyButton,&QPushButton::clicked,this,&MainWindow::editProperties);propsLayout->addWidget(propertyButton);connect(properties_,&QTableWidget::cellDoubleClicked,this,[this](int row,int){
        auto selected=canvas()->document->selected(canvas()->selection);auto key=properties_->item(row,0)->text();
        if(selected.size()==1&&canvas()->document->textEditable(selected.first().id)&&(key=="文字"||key=="标注文字"||key=="标注文字覆盖"))editText();else editProperties();
    });auto* propertiesDock=dock("对象属性",propsWidget,Qt::RightDockWidgetArea);propertiesDock->setMinimumWidth(240);
    auto* layerWidget=new QWidget;auto* layerLayout=new QVBoxLayout(layerWidget);layerLayout->addWidget(new QLabel("当前绘图图层"));currentLayer_=new QComboBox;layerLayout->addWidget(currentLayer_);
    layers_=new QTableWidget(0,5);layers_->setHorizontalHeaderLabels({"图层","可见","锁定","颜色","线宽"});layers_->verticalHeader()->hide();layers_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);for(int i=1;i<5;++i)layers_->setColumnWidth(i,45);layers_->setEditTriggers(QAbstractItemView::NoEditTriggers);layerLayout->addWidget(layers_);
    auto* buttons=new QHBoxLayout;for(auto name:QStringList{"新增","删除"}){auto* b=new QPushButton(name);buttons->addWidget(b);connect(b,&QPushButton::clicked,this,[this,name]{name=="新增"?addLayer():deleteLayer();});}layerLayout->addLayout(buttons);auto* layersDock=dock("图层管理",layerWidget,Qt::RightDockWidgetArea);tabifyDockWidget(layersDock,propertiesDock);layersDock->raise();
    connect(currentLayer_,&QComboBox::currentTextChanged,this,[this](QString name){if(!refreshing_&&canvas()&&canvas()->document->layers.contains(name))canvas()->document->currentLayer=name;});
    connect(layers_,&QTableWidget::itemChanged,this,[this](QTableWidgetItem* item){if(!refreshing_)changeLayer(item->row(),item->column());});
    connect(layers_,&QTableWidget::cellDoubleClicked,this,[this](int row,int col){if(col>=3)changeLayer(row,col);});
    auto* analysisWidget=new QWidget;auto* analysisLayout=new QVBoxLayout(analysisWidget);auto* hint=new QLabel("选区优先；未选择时按以下图层检查。\n钻孔/焊盘用途由工程师指定。");hint->setWordWrap(true);analysisLayout->addWidget(hint);scope_=new QComboBox;analysisLayout->addWidget(scope_);
    for(auto op:QStringList{"板框","孔径","间距","焊盘","工艺边","重复"}){auto* b=new QPushButton(op+"分析");connect(b,&QPushButton::clicked,this,[this,op]{runAnalysis(op);});analysisLayout->addWidget(b);}
    results_=new QListWidget;results_->setObjectName("analysisResults");analysisLayout->addWidget(results_);auto* analysisDock=dock("PCB / FPC 工程分析",analysisWidget,Qt::LeftDockWidgetArea);analysisDock->setMinimumWidth(230);analysisDock->hide();
    connect(results_,&QListWidget::itemClicked,this,[this](QListWidgetItem* item){QSet<EntityId> ids;for(const auto& v:item->data(Qt::UserRole).toList())ids.insert(v.toULongLong());canvas()->selectIds(ids);canvas()->centerOnSelection();});
    measurePanel_=new MeasurePanel;auto* measureDock=dock("标注测量",measurePanel_,Qt::LeftDockWidgetArea);measureDock->setMinimumWidth(210);
    tabifyDockWidget(analysisDock,measureDock);
    // A blank drawing starts with the canvas maximised. The panel appears when a
    // measurement tool is selected and remains available from the View menu.
    measureDock->hide();
    connect(measurePanel_,&MeasurePanel::recordActivated,this,[this](const QVector<EntityId>& ids){if(ids.isEmpty())return;canvas()->selectIds(QSet<EntityId>(ids.begin(),ids.end()));canvas()->centerOnSelection();});
    auto* consoleWidget=new QWidget;auto* consoleLayout=new QVBoxLayout(consoleWidget);console_=new QTextEdit;console_->setReadOnly(true);console_->setMaximumHeight(64);console_->document()->setMaximumBlockCount(5000);consoleLayout->addWidget(console_);command_=new QLineEdit;command_->setObjectName("commandInput");command_->setPlaceholderText("命令 > LINE / CIRCLE / DIST · X,Y · @ΔX,ΔY · 距离<角度");consoleLayout->addWidget(command_);auto* consoleDock=dock("命令与测量记录",consoleWidget,Qt::BottomDockWidgetArea);
    resizeDocks({consoleDock},{104},Qt::Vertical);resizeDocks({layersDock},{230},Qt::Horizontal);
    connect(command_,&QLineEdit::returnPressed,this,[this]{auto text=command_->text();command_->clear();executeCommand(text);});
    coordinate_=new QLabel;count_=new QLabel;statusBar()->addPermanentWidget(count_);statusBar()->addPermanentWidget(coordinate_);
    connect(tabs_,&QTabWidget::currentChanged,this,[this]{if(canvas()){results_->clear();refresh();}});connect(tabs_,&QTabWidget::tabCloseRequested,this,&MainWindow::closeTab);
}
void MainWindow::addDocument(Document document,QString recoveredFrom) {
    auto* c=new CadCanvas(std::make_shared<Document>(std::move(document)));connect(c,&CadCanvas::documentChanged,this,[this]{results_->clear();refresh();});
    recoveryIds_.insert(c,recovery_->track(c->document,std::move(recoveredFrom)));
    connect(c,&CadCanvas::selectionChanged,this,[this,c]{if(c!=canvas())return;refreshProperties();
        if(c->selection.size()==1&&c->document->textEditable(*c->selection.begin()))
            if(auto* propertiesDock=findChild<QDockWidget*>("对象属性"))propertiesDock->raise();
    });connect(c,&CadCanvas::message,this,&MainWindow::log);
    connect(c,&CadCanvas::textEditRequested,this,&MainWindow::editText);
    connect(c,&CadCanvas::coordinates,this,[this,c](QPointF p){if(c==canvas()){QPointF r=p-c->coordinateOrigin();coordinate_->setText("X "+c->document->dualLength(r.x())+" · Y "+c->document->dualLength(r.y()));}});
    connect(c,&CadCanvas::toolChanged,this,[this]{if(!canvas())return;for(auto it=tools_.begin();it!=tools_.end();++it)it.value()->setChecked(it.key()==canvas()->tool());measurePanel_->setLive("—");
        auto tool=canvas()->tool();if(tool==Tool::Distance||tool==Tool::Gap||tool==Tool::Horizontal||tool==Tool::Vertical||tool==Tool::Continuous||tool==Tool::Angle||tool==Tool::Coordinate)
            if(auto* dock=findChild<QDockWidget*>("标注测量")){dock->show();dock->raise();}});
    connect(c,&CadCanvas::liveMeasure,this,[this,c](QString text){if(c==canvas())measurePanel_->setLive(text);});
    connect(c,&CadCanvas::measured,this,[this,c](QString text,const QVector<EntityId>& ids){if(c==canvas())measurePanel_->addRecord(text,ids);});
    tabs_->setCurrentIndex(tabs_->addTab(c,c->document->title));refresh();QTimer::singleShot(0,c,[c]{c->fitDrawing();c->setFocus();});
}
void MainWindow::refreshProperties() {
    if(!canvas())return;
    auto entities=canvas()->document->selected(canvas()->selection);QVector<QPair<QString,QString>> props;
    if(entities.size()==1)props=entities[0].properties(*canvas()->document);else props={{"已选对象",QString::number(entities.size())}};
    properties_->setRowCount(props.size());for(qsizetype i=0;i<props.size();++i){properties_->setItem(i,0,new QTableWidgetItem(props[i].first));properties_->setItem(i,1,new QTableWidgetItem(props[i].second));}
    bool canEdit=entities.size()==1&&canvas()->document->textEditable(entities.first().id);
    editTextAction_->setEnabled(canEdit);textEditButton_->setEnabled(canEdit);
    if(canEdit)textEditHint_->setText("当前文字："+text::display(entities.first().text)+"\n双击文字或点击按钮修改；%%P / ± 表示正负公差。");
    else if(entities.size()==1&&(entities.first().kind==Kind::Text||entities.first().kind==Kind::Dimension))
        textEditHint_->setText("此文字所在图层已锁定，或复杂只读文字尚不支持编辑。");
    else textEditHint_->setText("双击画布上的文字 / 标注即可编辑，也可先选中再点击下方按钮。");
}
void MainWindow::refresh() {
    if(!canvas())return;
    recovery_->cleanSavedDocuments();
    auto& d=*canvas()->document;refreshing_=true;
    for(int i=0;i<tabs_->count();++i){auto* c=qobject_cast<CadCanvas*>(tabs_->widget(i));tabs_->setTabText(i,c->document->title+(c->document->modified()?" *":""));}
    setWindowTitle(branding::AppBrand::name()+" — "+d.title+(d.modified()?" *":""));count_->setText(QString("模型空间 | %1 对象 | %2 | 后台任务 %3    ").arg(d.entities.size()).arg(d.unitName()).arg(activeJobs_));
    undo_->setEnabled(d.canUndo());redo_->setEnabled(d.canRedo());
    snap_->setChecked(canvas()->snap);ortho_->setChecked(canvas()->ortho);polar_->setChecked(canvas()->polar);grid_->setChecked(canvas()->grid);
    for(auto it=tools_.begin();it!=tools_.end();++it)it.value()->setChecked(it.key()==canvas()->tool());
    QString scope=scope_->currentText();scope_->clear();scope_->addItem("全部可见图层");currentLayer_->clear();layers_->setRowCount(d.layers.size());int row=0;
    for(const auto& l:d.layers){scope_->addItem(l.name);currentLayer_->addItem(l.name);layers_->setItem(row,0,new QTableWidgetItem(l.name));
        for(int col:{1,2}){auto* item=new QTableWidgetItem;item->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsUserCheckable);item->setCheckState((col==1?l.visible:l.locked)?Qt::Checked:Qt::Unchecked);layers_->setItem(row,col,item);}
        auto* color=new QTableWidgetItem("●");color->setForeground(l.color);layers_->setItem(row,3,color);layers_->setItem(row,4,new QTableWidgetItem(value(l.lineweight,2)));++row;}
    currentLayer_->setCurrentText(d.currentLayer);scope_->setCurrentText(d.layers.contains(scope)?scope:"全部可见图层");refreshing_=false;refreshProperties();
    measurePanel_->setUnitNote(d.units==0?"图纸未声明单位：mm / mil 换算按 1 图形单位 = 1 mm。":QString());
}
void MainWindow::log(QString text){
    if(text.startsWith("DWG 引擎诊断：")&&text.size()>4000)text=text.left(4000)+"\n…诊断较长，界面仅显示前 4000 字符；完整诊断可用 --check-open 输出 JSON。";
    console_->append(text.toHtmlEscaped());statusBar()->showMessage(text.section('\n',0,0).left(300),10000);
}
void MainWindow::error(QString text){log(text);QMessageBox::warning(this,"操作未完成",text);}
void MainWindow::beginJob(CadCanvas* c,QString text){++activeJobs_;if(c)++jobsByCanvas_[c];log(text);refresh();}
void MainWindow::endJob(CadCanvas* c){--activeJobs_;if(c&&--jobsByCanvas_[c]<=0)jobsByCanvas_.remove(c);refresh();}
void MainWindow::openPath(QString path) {
    beginJob(nullptr,"后台读取："+path);auto converter=settings_.value("converter").toString();auto* watcher=new QFutureWatcher<OpenResult>(this);
    auto cancelled=std::make_shared<std::atomic_bool>(false);auto* progress=new QProgressDialog("正在读取图纸…","取消",0,0,this);progress->setWindowModality(Qt::NonModal);progress->setMinimumDuration(0);progress->setAutoClose(false);
    connect(progress,&QProgressDialog::canceled,this,[cancelled]{cancelled->store(true);});progress->show();
    connect(watcher,&QFutureWatcher<OpenResult>::finished,this,[this,watcher,progress,cancelled]{
        auto result=watcher->result();progress->deleteLater();endJob(nullptr);
        if(cancelled->load()){log("图纸读取已取消。");emit fileOpenFailed("图纸读取已取消。");}
        else if(!result.error.isEmpty()){
            log(result.error);emit fileOpenFailed(result.error);
            if(!property("checkOpen").toBool())QMessageBox::warning(this,"操作未完成",result.error);
        } else {
            addDocument(std::move(result.document));const auto& d=*canvas()->document;
            for(const auto& warning:d.warnings)log(warning);
            bool visible=false;for(const auto& e:d.entities)visible|=d.layers.value(e.layer).visible;
            if(visible)log("图纸已载入："+d.path);
            else if(d.entities.isEmpty())log("文件已解析，但没有可显示的模型对象；请查看兼容性提示。");
            else log("文件已解析，但所有对象所在图层均隐藏/冻结；请在图层管理中开启显示。");
            emit fileOpened(d.path);
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([path,converter,cancelled]{OpenResult r;try{io::DxfReadOptions options;options.cancelled=[cancelled]{return cancelled->load();};r.document=io::DwgAdapter::open(path,converter,options);}catch(const std::exception& e){r.error=exceptionText(e);}return r;}));
}
void MainWindow::saveFile(bool saveAs,std::function<void(bool)> completion) {
    auto* c=canvas();if(jobsByCanvas_.value(c)>0){log("当前文档仍有后台任务，请等待完成。");if(completion)completion(false);return;}
    QString path=c->document->path;
    if(QFileInfo(path).suffix().compare("dwg",Qt::CaseInsensitive)==0&&io::DwgAdapter::findConverter(settings_.value("converter").toString()).isEmpty()) {
        path=QFileInfo(path).dir().filePath(QFileInfo(path).completeBaseName()+".dxf");saveAs=true;
        log("内置引擎仅用于 DWG 读取；请选择 DXF 保存位置，原 DWG 不会被覆盖。");
    }
    if(saveAs||path.isEmpty()){QString filter;path=QFileDialog::getSaveFileName(this,"保存图纸",path.isEmpty()?c->document->title+".dxf":path,"DXF (*.dxf);;DWG (*.dwg)",&filter);if(path.isEmpty()){if(completion)completion(false);return;}if(QFileInfo(path).suffix().isEmpty())path+=filter.startsWith("DWG")?".dwg":".dxf";}
    auto suffix=QFileInfo(path).suffix().toLower();if(suffix!="dxf"&&suffix!="dwg"){error("请使用 .dxf 或 .dwg 扩展名。");if(completion)completion(false);return;}
    // Value snapshot is implicitly shared by Qt; GUI remains editable during save.
    Document snapshot=*c->document;auto converter=settings_.value("converter").toString();QPointer<CadCanvas> guarded(c);beginJob(c,"后台保存："+path);
    // Compare revisions instead of deep-comparing every entity after the write.
    // The snapshot is a cheap value copy; revision() is O(1) and changes on any
    // documented edit, so it reliably detects concurrent modification.
    quint64 savedRevision=snapshot.revision();
    auto* watcher=new QFutureWatcher<SaveResult>(this);connect(watcher,&QFutureWatcher<SaveResult>::finished,this,[this,watcher,guarded,path,snapshot,savedRevision,completion]{
        auto r=watcher->result();endJob(guarded);bool ok=r.error.isEmpty();if(!ok)error(r.error);else if(guarded){guarded->document->path=path;guarded->document->title=QFileInfo(path).completeBaseName();
            if(guarded->document->revision()==savedRevision)guarded->document->markSaved();
            refresh();log("已保存："+path);}if(completion)completion(ok);watcher->deleteLater();});
    watcher->setFuture(QtConcurrent::run([snapshot,path,converter]{SaveResult r;try{io::DwgAdapter::save(snapshot,path,converter);}catch(const std::exception& e){r.error=exceptionText(e);}return r;}));
}
bool MainWindow::confirmDiscard(CadCanvas* c,bool closingWindow) {
    if(!c->document->modified())return true;
    tabs_->setCurrentWidget(c);
    auto answer=QMessageBox::question(this,"保存修改","是否保存“"+c->document->title+"”？",QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel);
    if(answer==QMessageBox::Save){QPointer<CadCanvas> guarded(c);saveFile(false,[this,guarded,closingWindow](bool saved){
        if(saved&&guarded&&!guarded->document->modified()){if(closingWindow)QTimer::singleShot(0,this,&QWidget::close);else closeTab(tabs_->indexOf(guarded));}
        });return false;}return answer==QMessageBox::Discard;
}
void MainWindow::closeTab(int index){auto* c=qobject_cast<CadCanvas*>(tabs_->widget(index));if(!c)return;if(jobsByCanvas_.value(c)>0){log("请等待该文档的后台任务完成再关闭。");return;}if(!confirmDiscard(c))return;recovery_->forget(recoveryIds_.take(c));tabs_->removeTab(index);c->deleteLater();if(tabs_->count()==0)addDocument(Document{});}
void MainWindow::closeEvent(QCloseEvent* e){if(activeJobs_){log("请等待后台任务完成再退出。");e->ignore();return;}for(int i=0;i<tabs_->count();++i)if(!confirmDiscard(qobject_cast<CadCanvas*>(tabs_->widget(i)),true)){e->ignore();return;}for(const auto& id:recoveryIds_)recovery_->forget(id);recoveryIds_.clear();
    if(!QStandardPaths::isTestModeEnabled())settings_.setValue("ui/windowGeometry",saveGeometry());
    e->accept();}
void MainWindow::changeLayer(int row,int col) {
    if(col<1||row<0)return;
    auto name=layers_->item(row,0)->text();auto next=canvas()->document->layers;auto& l=next[name];
    if(col==1)l.visible=layers_->item(row,col)->checkState()==Qt::Checked;
    else if(col==2)l.locked=layers_->item(row,col)->checkState()==Qt::Checked;
    else if(col==3){auto color=QColorDialog::getColor(l.color,this,"图层颜色");if(!color.isValid())return;l.color=color;}
    else if(col==4){bool ok=false;auto width=QInputDialog::getDouble(this,"线宽","线宽 (mm)",l.lineweight,0.05,2.11,2,&ok);if(!ok)return;l.lineweight=width;}
    canvas()->document->changeLayers("图层修改",next);canvas()->synchronize();results_->clear();refresh();
}
void MainWindow::addLayer(){bool ok=false;auto name=QInputDialog::getText(this,"新增图层","名称",QLineEdit::Normal,{},&ok).trimmed();if(!ok)return;
    if(name.isEmpty()||canvas()->document->layers.contains(name)||name.contains(QRegularExpression(R"([<>/\\":;?*|=,])"))){error("图层名为空、重复或包含无效字符。");return;}
    auto layers=canvas()->document->layers;layers.insert(name,Layer{name});canvas()->document->changeLayers("新增图层",layers);canvas()->document->currentLayer=name;refresh();}
void MainWindow::deleteLayer(){int row=layers_->currentRow();if(row<0)return;auto name=layers_->item(row,0)->text();if(name=="0"){error("默认图层不能删除。");return;}
    for(const auto& e:canvas()->document->entities)if(e.layer==name){error("图层仍含对象，请先移走或删除对象。");return;}
    if(io::DxfAdapter::sourceLayerInUse(*canvas()->document,name)){error("源文件块、布局或未支持对象仍引用此图层。");return;}
    auto next=canvas()->document->layers;next.remove(name);canvas()->document->changeLayers("删除图层",next);refresh();}
void MainWindow::exportPdf(){auto path=QFileDialog::getSaveFileName(this,"导出 PDF",canvas()->document->title+".pdf","PDF (*.pdf)");if(path.isEmpty())return;if(!path.endsWith(".pdf",Qt::CaseInsensitive))path+=".pdf";
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)){error("无法创建 PDF："+file.errorString());return;}
    {
        QPdfWriter writer(&file);writer.setPageSize(QPageSize(QPageSize::A4));writer.setResolution(300);QPainter painter(&writer);
        if(!painter.isActive()){file.cancelWriting();error("无法创建 PDF。");return;}
        canvas()->renderDocument(painter,QRectF(0,0,writer.width(),writer.height()));painter.end();
    }
    if(!file.commit()){error("PDF 原子保存失败："+file.errorString());return;}
    log("已导出 PDF（适合页面，非指定工程比例）："+path);}
void MainWindow::panelize() {
    auto* c=canvas();
    if(jobsByCanvas_.value(c)>0){log("当前文档有后台任务，请稍后拼版。");return;}
    PanelizationDialog dialog(*c->document,c->selection,settings_.value("converter").toString(),this);
    dialog.onDrawing=[this](Document d){addDocument(std::move(d));canvas()->fitDrawing();canvas()->setFocus();log("材料排布图已在新标签打开；不自动保存，请 Ctrl+S。");};
    if(dialog.exec()==QDialog::Accepted){addDocument(dialog.outputDocument());canvas()->fitDrawing();canvas()->setFocus();log("拼版完成：结果已在新标签“PCB-FPC 拼版”打开。原图未修改，请 Ctrl+S 保存；尺寸来自实际外形。");}
}
void MainWindow::editProperties() {
    auto selected=canvas()->document->selected(canvas()->selection);
    if(selected.size()==1&&!selected[0].dimensionTextKey.isEmpty()){editText();return;}
    auto before=canvas()->document->selected(canvas()->selection,true);if(before.isEmpty()){log("请先选择可编辑对象。");return;}
    QDialog dialog(this);dialog.setWindowTitle("对象 / 批量属性");QFormLayout form(&dialog);auto* layer=new QComboBox;layer->addItem("保持原图层");for(const auto& l:canvas()->document->layers)if(l.visible&&!l.locked)layer->addItem(l.name);form.addRow("图层",layer);
    auto* color=new QComboBox;color->addItems({"保持原颜色","随图层","自定义"});form.addRow("颜色",color);QVector<QPair<QDoubleSpinBox*,QDoubleSpinBox*>> points;QDoubleSpinBox *radius=nullptr,*height=nullptr;QLineEdit* text=nullptr;
    auto original=before[0];if(before.size()==1){if(original.kind!=Kind::Ellipse)for(qsizetype i=0;i<original.points.size();++i){auto* x=number(original.points[i].x());auto* y=number(original.points[i].y());points.append(qMakePair(x,y));form.addRow(QString("点 %1 X").arg(i+1),x);form.addRow(QString("点 %1 Y").arg(i+1),y);}
        if(original.kind==Kind::Circle||original.kind==Kind::Arc){radius=number(original.radius,0.000001);form.addRow("半径",radius);}
        if(original.kind==Kind::Text||original.kind==Kind::Dimension){text=new QLineEdit(original.text);form.addRow("文字 / 标注 <> = 实测值",text);}
        if(original.kind==Kind::Text||original.kind==Kind::Dimension){height=number(original.height>geo::epsilon?original.height:2,0.000001);form.addRow(original.kind==Kind::Text?"文字高度":"标注字高",height);}}
    if(!dialogButtons(dialog,form))return;
    QColor chosen;if(color->currentIndex()==2){chosen=QColorDialog::getColor(Qt::white,this,"对象颜色");if(!chosen.isValid())return;}
    auto after=before;if(after.size()==1){for(qsizetype i=0;i<points.size();++i)after[0].points[i]={points[i].first->value(),points[i].second->value()};
        if(radius)after[0].radius=radius->value();
        if(text)after[0].text=text->text();
        if(height)after[0].height=height->value();
        if(after[0].kind==Kind::Line&&geo::length(after[0].points[0],after[0].points[1])<geo::epsilon){error("不能生成零长度直线。");return;}
        if(after[0].kind==Kind::Rectangle&&after[0].points!=original.points)after[0].kind=Kind::Polyline;}
    for(auto& e:after){if(layer->currentIndex()>0)e.layer=layer->currentText();if(color->currentIndex()==1)e.color={};else if(chosen.isValid())e.color=chosen;}
    canvas()->document->commit("修改属性",before,after);canvas()->synchronize();results_->clear();refresh();
}
void MainWindow::editText() {
    auto* c=canvas();auto selected=c->document->selected(c->selection);
    if(selected.size()!=1){log("请先选择一个文字或标注文字对象。");return;}
    auto original=selected.first();
    if(!c->document->textEditable(original.id)){log("此对象不能编辑文字：图层已锁定，或该预览不支持安全文字编辑。");return;}
    bool external=!original.dimensionTextKey.isEmpty();
    QDialog dialog(this);dialog.setObjectName("textEditDialog");dialog.setWindowTitle(external?"编辑标注文字 / 公差":"编辑文字 / 标注");QFormLayout form(&dialog);
    auto* input=new QLineEdit(external?original.dimensionTextOverride:original.text);input->setObjectName("textOverrideInput");input->setMinimumWidth(360);form.addRow("文字内容",input);
    auto* hint=new QLabel(external?"<> = 原实测值；%%P 或 ± = 正负公差。\n仅修改显示文字，不改变板框、尺寸线或箭头。":"<> = 标注实测值；%%P 或 ± = 正负公差。");form.addRow(hint);
    auto* preview=new QLabel;preview->setObjectName("textOverridePreview");preview->setTextFormat(Qt::PlainText);form.addRow("显示预览",preview);
    auto updatePreview=[=]{auto text=input->text();if(external)text.replace("<>",original.dimensionAutoText);
        else if(original.kind==Kind::Dimension){auto label=original;label.text=text;text=dimension::label(label,c->document->units);}
        preview->setText(text::display(text));};
    connect(input,&QLineEdit::textChanged,&dialog,[=]{updatePreview();});updatePreview();input->selectAll();
    if(!dialogButtons(dialog,form))return;
    if(!c->document->editText(original.id,input->text())){error("请输入单行文字。外部标注仅支持普通文字、一个 <> 占位符和 %%P / ± 公差，不支持 MTEXT 格式命令。");return;}
    c->synchronize();results_->clear();refresh();
}
static QString startupTutorialHtml() {
    return QStringLiteral(
    "<h2 style='margin-bottom:4px;'>欢迎使用 CAD · Qiao</h2>"
    "<p style='color:#555;'>PCB / FPC 二维绘图与测量工具。以下是快速上手操作教程。</p>"
    "<h3>视图导航</h3><ul>"
    "<li><b>滚轮</b> 缩放；<b>中键拖动</b> 平移；<b>方向键</b> 移动图纸（Shift 加速、Ctrl 微调）；<b>+ / -</b> 缩放；<b>Home</b> 显示全部。</li>"
    "<li>方向键在画布获得焦点时移动图纸；光标在命令栏时仍正常移动文字光标。单击画布即可重新聚焦。</li>"
    "<li>视图 → 框选缩放：拖一个矩形放大该区域；视图 → 居中选中对象可快速定位。</li></ul>"
    "<h3>选择对象</h3><ul>"
    "<li>单击选择；<b>Shift</b> 加选 / 减选。</li>"
    "<li>左→右拖动 = 窗口选择（完全包含）；右→左 = 交叉选择（触碰即选）。</li>"
    "<li>单击对象后右侧显示属性；双击属性或点“编辑 / 批量修改属性”可修改。</li>"
    "<li>双击文字 / 标注文字，或选中后按 Ctrl+E 可编辑内容和公差；外部标注仅修改显示文字，不改变几何尺寸。%%P 显示为 ±。</li></ul>"
    "<h3>绘图</h3><ul>"
    "<li>工具栏或命令：<b>LINE</b> 直线、<b>CIRCLE</b> 圆、<b>ARC</b> 圆弧、<b>PL</b> 多段线、<b>REC</b> 矩形、<b>TEXT</b> 文字。</li>"
    "<li>精确输入：<b>10,20</b> 绝对坐标；<b>@5,0</b> 相对坐标；<b>20&lt;45</b> 距离&lt;角度。</li>"
    "<li><b>Esc</b> 返回选择；多段线 <b>Enter</b> 完成、<b>C</b> 闭合。</li>"
    "<li><b>F3</b> 对象捕捉、<b>F8</b> 正交、<b>F10</b> 极轴、<b>F7</b> 网格。</li></ul>"
    "<h3>测量与标注（mm / mil 双单位）</h3><ul>"
    "<li><b>对象间距</b>（命令 <b>GAP</b> / <b>MEASURE</b>）：依次单击两个对象。直线按中心到中心、圆 / 弧按边到边，线宽自动计入；结果同时显示 mm 与 mil，并在图纸上生成一条标注。</li>"
    "<li>标注字高随间距自适应（小间距用小字）；选中标注后在属性里可改“标注字高”。</li>"
    "<li><b>DI</b> 距离、<b>ID</b> 坐标；左侧“标注测量”面板实时显示并记录，单击记录可定位对象。</li>"
    "<li>状态栏坐标为 mm / mil 双单位；视图 → <b>设置坐标原点</b> 以当前光标为原点，<b>重置坐标原点</b> 回到 (0,0)。</li></ul>"
    "<h3>图层与工程分析</h3><ul>"
    "<li>右侧“图层管理”：新增 / 删除 / 可见 / 锁定 / 颜色 / 线宽。</li>"
    "<li>工程分析：板框、孔径、间距、焊盘、工艺边、重复；选区优先，单击结果定位图形。</li></ul>"
    "<h3>文件与编辑</h3><ul>"
    "<li><b>Ctrl+N</b> 新建、<b>Ctrl+O</b> 打开、<b>Ctrl+S</b> 保存、<b>Ctrl+Shift+S</b> 另存为。</li>"
    "<li>原生读写 ASCII DXF；DWG 可直接读取（保存 DWG 需 ODA）；可导出 PDF。</li>"
    "<li>每 60 秒后台生成恢复快照，不覆盖原图；异常退出后从“文件 → 恢复未保存图纸”恢复副本，并另存为。快照不替代手动保存。</li>"
    "<li><b>Ctrl+Z</b> 撤销、<b>Ctrl+Y</b> 恢复、<b>Delete</b> 删除未锁定对象。</li>"
    "<li>编辑命令：OFFSET 偏移、ROTATE 旋转、ARRAY 阵列、TRIM 修剪、EXTEND 延伸、EXPLODE 分解、JOIN 合并。</li></ul>"
    "<p style='color:#555;'>完整说明见“帮助 → 关于”与 docs/USER_GUIDE.md。点击“开始使用”进入绘图界面。</p>");
}
void MainWindow::showStartupTutorial(bool startup) {
    const QString key="ui/showStartupTutorial";
    if(startup&&!settings_.value(key,true).toBool())return;
    QDialog dialog(this);dialog.setWindowTitle("操作教程 · 快速上手");dialog.resize(760,600);
    auto* layout=new QVBoxLayout(&dialog);
    auto* browser=new QTextBrowser;browser->setHtml(startupTutorialHtml());layout->addWidget(browser,1);
    auto* check=new QCheckBox("启动时显示本教程");check->setChecked(settings_.value(key,true).toBool());layout->addWidget(check);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok);buttons->button(QDialogButtonBox::Ok)->setText("开始使用");layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    dialog.exec();
    settings_.setValue(key,check->isChecked());
    if(canvas())canvas()->setFocus();
}
void MainWindow::showRecoveryDialog(bool startup) {
    auto entries=recovery_->available();
    // Do not offer snapshots that are already open in this window.
    for(auto it=entries.begin();it!=entries.end();) {
        bool open=false;for(int i=0;i<tabs_->count();++i)open|=tabs_->widget(i)->property("recoveredFrom").toString()==it->filePath;
        if(open)it=entries.erase(it);else ++it;
    }
    if(entries.isEmpty()){if(!startup)log("没有待恢复的快照。自动恢复目录："+recovery_->directory());return;}
    QDialog dialog(this);dialog.setObjectName("recoveryDialog");dialog.setWindowTitle("恢复未保存图纸");dialog.resize(800,460);
    QVBoxLayout layout(&dialog);auto* hint=new QLabel("发现上次退出后保留的恢复快照。恢复只打开副本，原 DXF / DWG 不会被覆盖。\n每 60 秒检查并排队生成快照；恢复后请另存为。损坏快照可保留或明确丢弃。");hint->setWordWrap(true);layout.addWidget(hint);
    auto* list=new QListWidget;list->setObjectName("recoveryEntries");list->setSelectionMode(QAbstractItemView::ExtendedSelection);layout.addWidget(list,1);
    for(const auto& entry:entries) {
        auto label=entry.title+" · "+(entry.savedAt.isValid()?entry.savedAt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss"):"时间未知");
        label+="\n"+(entry.originalPath.isEmpty()?"未命名图纸":entry.originalPath);
        if(!entry.error.isEmpty())label+="\n无法读取："+entry.error;
        auto* item=new QListWidgetItem(label,list);item->setData(Qt::UserRole,entry.filePath);item->setToolTip(entry.filePath);
    }
    auto* buttons=new QDialogButtonBox;auto* restore=buttons->addButton("恢复选中",QDialogButtonBox::ActionRole);restore->setObjectName("restoreRecoveryButton");
    auto* discard=buttons->addButton("丢弃选中…",QDialogButtonBox::ActionRole);discard->setObjectName("discardRecoveryButton");
    auto* later=buttons->addButton("稍后处理",QDialogButtonBox::RejectRole);layout.addWidget(buttons);
    auto update=[=]{bool selected=!list->selectedItems().isEmpty();restore->setEnabled(selected);discard->setEnabled(selected);};
    connect(list,&QListWidget::itemSelectionChanged,&dialog,update);update();connect(later,&QPushButton::clicked,&dialog,&QDialog::reject);
    connect(restore,&QPushButton::clicked,&dialog,[&]{
        for(auto* item:list->selectedItems()) {
            auto path=item->data(Qt::UserRole).toString();auto it=std::find_if(entries.cbegin(),entries.cend(),[&](const auto& entry){return entry.filePath==path;});
            if(it==entries.cend())continue;
            try {
                auto document=recovery_->restore(*it);auto original=it->originalPath;addDocument(std::move(document),path);canvas()->setProperty("recoveredFrom",path);
                log("已恢复副本："+it->title+"；原文件："+(original.isEmpty()?"未命名":original)+"。请另存为，恢复前的撤销历史不保留。");
                delete item;
            } catch(const std::exception& e) {log(exceptionText(e));QMessageBox::warning(&dialog,"恢复未完成",exceptionText(e));}
        }
        recovery_->captureNow();if(list->count()==0)dialog.accept();
    });
    connect(discard,&QPushButton::clicked,&dialog,[&]{
        if(QMessageBox::question(&dialog,"丢弃恢复快照","确定永久删除选中的恢复快照？原图不会被删除。",QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
        for(auto* item:list->selectedItems()) {
            RecoveryEntry entry;entry.filePath=item->data(Qt::UserRole).toString();QString error;
            if(recovery_->discard(entry,&error))delete item;else {log(error);QMessageBox::warning(&dialog,"无法丢弃",error);}
        }
        if(list->count()==0)dialog.accept();
    });
    dialog.exec();
    if(canvas())canvas()->setFocus();
}
void MainWindow::editSelected(QString op) {
    auto before=canvas()->document->selected(canvas()->selection,true);if(before.isEmpty()&&op!="清理"){log("请先选择可编辑对象。");return;}
    try {QVector<Entity> after;bool replace=true,ok=false;
        if(op=="旋转"){QDialog dialog(this);dialog.setWindowTitle(op);QFormLayout form(&dialog);QVector<QPointF> extrema;for(const auto& e:before){auto b=e.bounds();extrema+={b.topLeft(),b.bottomRight()};}auto center=geo::bounds(extrema).center();auto* x=number(center.x());auto* y=number(center.y());auto* degrees=number(90);form.addRow("中心 X",x);form.addRow("中心 Y",y);form.addRow("角度 (°，逆时针)",degrees);if(!dialogButtons(dialog,form))return;after=before;for(auto& e:after)e.rotate({x->value(),y->value()},degrees->value());}
        else if(op=="偏移"){double amount=QInputDialog::getDouble(this,op,"正值：圆/凸闭合轮廓外扩，直线向左",1,-1e6,1e6,4,&ok);if(!ok)return;for(const auto& e:before)after.append(edit::offset(e,amount));replace=false;}
        else if(op=="分解"){for(const auto& e:before)after+=edit::explode(e);}
        else if(op=="合并")after={edit::join(before)};
        else if(op=="修剪"||op=="延伸"){if(before.size()!=2)throw std::runtime_error("请选择两条直线。");for(const auto& e:before)if(e.kind!=Kind::Line||e.points.size()!=2)throw std::runtime_error("修剪/延伸需要两条有效直线。");QStringList labels;for(const auto& e:before)labels.append(QString("%1: %2,%3 → %4,%5").arg(e.id).arg(value(e.points[0].x()),value(e.points[0].y()),value(e.points[1].x()),value(e.points[1].y())));
            auto target=QInputDialog::getItem(this,op,"要修改的直线（另一条作为边界）",labels,0,false,&ok);if(!ok)return;int i=labels.indexOf(target);auto endpoint=QInputDialog::getItem(this,op,"修改端点",{"起点","终点"},1,false,&ok);if(!ok)return;after={edit::trimExtend(before[i],before[1-i],endpoint=="起点"?0:1,op=="延伸")};before={before[i]};}
        else if(op=="阵列"){int rows=QInputDialog::getInt(this,op,"行数（含原对象）",2,1,100,1,&ok);if(!ok)return;int columns=QInputDialog::getInt(this,op,"列数（含原对象）",2,1,100,1,&ok);if(!ok)return;
            double dx=QInputDialog::getDouble(this,op,"列距 ΔX",10,-1e6,1e6,4,&ok);if(!ok)return;double dy=QInputDialog::getDouble(this,op,"行距 ΔY",10,-1e6,1e6,4,&ok);if(!ok)return;
            if(rows*columns*before.size()>10000)throw std::runtime_error("单次阵列最多 10000 个对象。");
            for(int i=0;i<rows;++i)for(int j=0;j<columns;++j)if(i||j)for(const auto& e:before){auto copy=edit::clone(e);copy.translate({j*dx,i*dy});after.append(copy);}
            replace=false;}
        else if(op=="清理"){auto findings=engineering::duplicates(scopeEntities());QSet<EntityId> remove;for(const auto& f:findings){QList<EntityId> ids=f.ids.values();std::sort(ids.begin(),ids.end());for(qsizetype i=1;i<ids.size();++i)if(canvas()->document->editable(ids[i]))remove.insert(ids[i]);}before=canvas()->document->selected(remove,true);if(before.isEmpty()){log("没有可清理的重复对象。");return;}
            QStringList candidates;for(const auto& e:before)candidates.append(QString("ID %1 · %2 · 图层 %3").arg(e.id).arg(e.typeName(),e.layer));
            QDialog confirm(this);confirm.setWindowTitle("确认清理重复对象");QVBoxLayout layout(&confirm);layout.addWidget(new QLabel(QString("将删除以下 %1 个对象，保留每组中 ID 最小的对象：").arg(before.size())));auto* list=new QListWidget;list->addItems(candidates);layout.addWidget(list);QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout.addWidget(&buttons);connect(&buttons,&QDialogButtonBox::accepted,&confirm,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&confirm,&QDialog::reject);if(confirm.exec()!=QDialog::Accepted)return;}
        for(auto& e:after)if(!e.id)e.id=canvas()->document->allocateId();
        canvas()->document->commit(op,replace?before:QVector<Entity>{},after);QSet<EntityId> ids;for(const auto& e:after)ids.insert(e.id);canvas()->selectIds(ids);canvas()->synchronize();results_->clear();refresh();log(op+"完成。");
    }catch(const std::exception& e){error(exceptionText(e));}
}
QVector<Entity> MainWindow::scopeEntities() const {
    auto* c=canvas();if(!c->selection.isEmpty())return c->document->selected(c->selection);QVector<Entity> result;for(const auto& e:c->document->entities)if(c->document->layers.value(e.layer).visible&&(scope_->currentIndex()==0||e.layer==scope_->currentText()))result.append(e);return result;
}
void MainWindow::runAnalysis(QString op) {
    if(auto* dock=findChild<QDockWidget*>("PCB / FPC 工程分析")){dock->show();dock->raise();}
    auto* c=canvas();if(jobsByCanvas_.value(c)>0){log("当前文档的后台任务尚未完成。");return;}auto scope=scopeEntities();if(scope.isEmpty()){log("范围内没有对象。");return;}
    double first=0,second=0;bool ok=false;std::optional<Entity> board;
    if(op=="板框"||op=="工艺边"){QVector<Entity> closed;for(const auto& e:scope)if(e.isClosed()&&e.kind!=Kind::SplinePreview)closed.append(e);if(closed.size()!=1){error("请只选择一个闭合板框，或选择只包含一个板框的图层。");return;}board=closed[0];}
    if(op=="间距"||op=="工艺边"){first=QInputDialog::getDouble(this,op,"最小要求 ("+c->document->unitName()+")",op=="间距"?0.2:3,0.000001,1e6,4,&ok);if(!ok)return;}
    if(op=="焊盘"){first=QInputDialog::getDouble(this,op,"最小宽度",1,0.000001,1e6,4,&ok);if(!ok)return;second=QInputDialog::getDouble(this,op,"最小高度",1,0.000001,1e6,4,&ok);if(!ok)return;}
    if(op=="工艺边"){scope.clear();for(const auto& e:c->document->entities)if(c->document->layers.value(e.layer).visible&&e.id!=board->id)scope.append(e);}
    Document snapshot=*c->document;QString unit=snapshot.unitName();QPointer<CadCanvas> guarded(c);beginJob(c,"后台执行 "+op+"分析…");auto* watcher=new QFutureWatcher<AnalysisResult>(this);
    connect(watcher,&QFutureWatcher<AnalysisResult>::finished,this,[this,watcher,guarded,snapshot]{auto r=watcher->result();endJob(guarded);
        if(!r.error.isEmpty()){results_->clear();error(r.error);}else if(guarded==canvas()&&guarded->document->entities==snapshot.entities&&guarded->document->layers==snapshot.layers&&guarded->document->units==snapshot.units){results_->clear();for(const auto& f:r.findings){auto* item=new QListWidgetItem(f.description,results_);QVariantList ids;for(auto id:f.ids)ids.append(QVariant::fromValue<qulonglong>(id));item->setData(Qt::UserRole,ids);}if(r.findings.isEmpty())results_->addItem("未发现违反当前规则的对象 / 无匹配对象");QString message=QString("检查完成：%1 条结果。").arg(r.findings.size());if(r.precisionReported)message+=QString(" 曲线离散最大误差上界 %1 图形单位。").arg(value(r.maxCurveError,9));log(message+"单击结果定位。");}
        else log("检查期间文档或当前标签已变化，结果已忽略；请重新检查。");
        watcher->deleteLater();});
    watcher->setFuture(QtConcurrent::run([op,scope,board,first,second,unit]{AnalysisResult r;try{if(op=="板框"||op=="间距"||op=="焊盘"||op=="工艺边"){r.precisionReported=true;auto inspect=[&](const Entity& e){geo::TessellationStatus status;e.outline(0.005,&status);r.maxCurveError=std::max(r.maxCurveError,status.maxError);if(status.budgetExceeded)throw std::runtime_error(("曲线离散超过预算，实际误差上界："+value(status.maxError,9)).toUtf8().constData());};for(const auto& e:scope)inspect(e);if(board)inspect(*board);}
        if(op=="板框")r.findings=engineering::board(*board,unit);else if(op=="孔径")r.findings=engineering::holes(scope);else if(op=="间距")r.findings=engineering::clearances(scope,first);else if(op=="焊盘")r.findings=engineering::pads(scope,first,second);else if(op=="工艺边")r.findings=engineering::edges(*board,scope,first);else r.findings=engineering::duplicates(scope);}catch(const std::exception& e){r.error=exceptionText(e);}return r;}));
    log("分析按图形单位；曲线间距离散公差 0.005。不包含线宽、网络与叠层规则。");
}
void MainWindow::executeCommand(QString text) {
    text=text.trimmed();if(text.isEmpty()){canvas()->finish();return;}log("> "+text);auto cmd=text.toUpper();
    if(cmd=="PANEL"||cmd=="PANELARRAY"){panelize();return;}
    if(cmd=="FPCPLAN"||cmd=="MATERIAL"){panelize();return;}
    if(cmd=="DDEDIT"||cmd=="TEXTEDIT"||cmd=="DIMEDIT"){editText();return;}
    QMap<QString,Tool> aliases{{"L",Tool::Line},{"LINE",Tool::Line},{"PL",Tool::Polyline},{"PLINE",Tool::Polyline},{"REC",Tool::Rectangle},{"RECTANG",Tool::Rectangle},{"C",Tool::Circle},{"CIRCLE",Tool::Circle},{"ARC",Tool::Arc},{"ELLIPSE",Tool::Ellipse},{"TEXT",Tool::Text},{"M",Tool::Move},{"MOVE",Tool::Move},{"CO",Tool::Copy},{"COPY",Tool::Copy},{"MI",Tool::Mirror},{"MIRROR",Tool::Mirror},{"DI",Tool::Distance},{"DIST",Tool::Distance},{"GAP",Tool::Gap},{"MEASURE",Tool::Gap},{"ANGLE",Tool::Angle},{"ID",Tool::Coordinate},{"DIM",Tool::Dimension},{"DIMALIGNED",Tool::Dimension},{"SELECT",Tool::Select}};
    if(cmd=="C"&&canvas()->tool()==Tool::Polyline){canvas()->finish(true);return;}if(aliases.contains(cmd)){canvas()->setTool(aliases[cmd]);return;}
    if(cmd=="UNDO"||cmd=="U"){undo_->trigger();return;}if(cmd=="REDO"){redo_->trigger();return;}if(cmd=="ERASE"||cmd=="E"){canvas()->deleteSelection();return;}
    if(cmd=="ZOOM"||cmd=="Z"||cmd=="EXTENTS"){canvas()->fitDrawing();return;}
    QMap<QString,QString> edits{{"OFFSET","偏移"},{"O","偏移"},{"ROTATE","旋转"},{"RO","旋转"},{"ARRAY","阵列"},{"TRIM","修剪"},{"TR","修剪"},{"EXTEND","延伸"},{"EX","延伸"},{"EXPLODE","分解"},{"X","分解"},{"JOIN","合并"},{"J","合并"}};
    if(edits.contains(cmd)){editSelected(edits[cmd]);return;}if(cmd=="AREA"){for(const auto& e:canvas()->document->selected(canvas()->selection)){if(e.kind==Kind::SplinePreview||e.kind==Kind::Hatch){log("只读预览填充或样条不支持面积/长度测量。");continue;}log("面积 "+value(e.area())+" · 周长/长度 "+value(e.length()));}return;}
    if(canvas()->tool()==Tool::Select||canvas()->tool()==Tool::Zoom){log("请先输入绘图或测量命令。");return;}
    auto base=canvas()->inputPoints().isEmpty()?QPointF{}:canvas()->inputPoints().last();
    auto point=parseCommandPoint(text,base,canvas()->cursorPoint());
    if(point)canvas()->acceptPoint(*point);else log("命令无效：支持 X,Y、@ΔX,ΔY、距离<角度。");
}
}
