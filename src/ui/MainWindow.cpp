#include "MainWindow.h"
#include "branding/AppBrand.h"
#include "core/Editing.h"
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
#include <QCloseEvent>
#include <QStatusBar>
#include <QTimer>
#include <QProgressDialog>
#include <QPdfWriter>
#include <QPainter>
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
}
MainWindow::MainWindow(bool demo) {setWindowIcon(branding::AppBrand::icon());resize(1450,920);buildUi();addDocument(demo?Document::demo():Document{});log("CAD · Qiao / C++ / Qt 6 · 滚轮缩放 / 中键平移 / F3 捕捉 / F8 正交");log("命令：LINE → 0,0 → @20,0；距离<角度：20<45。后台文件读取与工程分析已启用。");}
CadCanvas* MainWindow::canvas() const {return qobject_cast<CadCanvas*>(tabs_->currentWidget());}
QAction* MainWindow::action(QString text,std::function<void()> callback,QString shortcut) {
    auto* a=new QAction(text,this);if(!shortcut.isEmpty())a->setShortcut(QKeySequence(shortcut));connect(a,&QAction::triggered,this,[callback]{callback();});return a;
}
void MainWindow::buildUi() {
    tabs_=new QTabWidget;tabs_->setTabsClosable(true);tabs_->setMovable(true);setCentralWidget(tabs_);
    auto* file=menuBar()->addMenu("文件");
    auto* newAction=action("新建",[this]{addDocument(Document{});},"Ctrl+N");file->addAction(newAction);
    auto* open=action("打开 DXF / DWG",[this]{auto p=QFileDialog::getOpenFileName(this,"打开图纸",{},"CAD (*.dxf *.dwg)");if(!p.isEmpty())openPath(p);},"Ctrl+O");file->addAction(open);
    auto* save=action("保存",[this]{saveFile();},"Ctrl+S");file->addAction(save);file->addAction(action("另存为",[this]{saveFile(true);},"Ctrl+Shift+S"));
    file->addAction(action("导出 PDF",[this]{exportPdf();}));
    file->addAction(action("设置单位",[this]{QStringList names{"图形单位","inch","mm","cm","m","mil","µm"};QVector<int> codes{0,1,4,5,6,9,13};bool ok=false;
        auto name=QInputDialog::getItem(this,"设置单位","仅声明单位，不缩放坐标",names,std::max(0,int(codes.indexOf(canvas()->document->units))),false,&ok);
        if(ok){canvas()->document->setUnits(codes[names.indexOf(name)]);results_->clear();refresh();}}));
    file->addAction(action("配置 DWG 转换器",[this]{auto p=QFileDialog::getOpenFileName(this,"选择 ODAFileConverter.exe",{},"程序 (*.exe)");if(!p.isEmpty())settings_.setValue("converter",p);}));
    file->addAction(action("打开演示板",[this]{addDocument(Document::demo());}));file->addSeparator();file->addAction(action("退出",[this]{close();},"Alt+F4"));
    auto* edit=menuBar()->addMenu("编辑");
    undo_=action("撤销",[this]{canvas()->setTool(Tool::Select);if(canvas()->document->undo()){results_->clear();canvas()->synchronize();refresh();}},"Ctrl+Z");
    redo_=action("恢复",[this]{canvas()->setTool(Tool::Select);if(canvas()->document->redo()){results_->clear();canvas()->synchronize();refresh();}},"Ctrl+Y");edit->addActions({undo_,redo_});
    edit->addAction(action("全选可见对象",[this]{QSet<EntityId> ids;for(const auto& e:canvas()->document->entities)if(canvas()->document->layers.value(e.layer).visible)ids.insert(e.id);canvas()->selectIds(ids);},"Ctrl+A"));
    edit->addAction(action("删除",[this]{canvas()->deleteSelection();},"Delete"));
    for(auto op:QStringList{"旋转","阵列","偏移","修剪","延伸","分解","合并"})edit->addAction(action(op,[this,op]{editSelected(op);}));
    edit->addAction(action("编辑 / 批量属性",[this]{editProperties();}));
    auto* draw=menuBar()->addMenu("绘图");auto* measure=menuBar()->addMenu("测量");auto* view=menuBar()->addMenu("视图");
    auto* fit=action("显示全部",[this]{canvas()->fitDrawing();},"Home");view->addAction(fit);view->addAction(action("框选缩放",[this]{canvas()->setTool(Tool::Zoom);}));
    auto mode=[&](QString name,QString shortcut,bool CadCanvas::*member){auto* a=new QAction(name,this);a->setCheckable(true);a->setShortcut(QKeySequence(shortcut));connect(a,&QAction::triggered,this,[this,member](bool v){canvas()->*member=v;
        if(member==&CadCanvas::ortho&&v){canvas()->polar=false;polar_->setChecked(false);}if(member==&CadCanvas::polar&&v){canvas()->ortho=false;ortho_->setChecked(false);}canvas()->update();});view->addAction(a);return a;};
    snap_=mode("对象捕捉","F3",&CadCanvas::snap);ortho_=mode("正交","F8",&CadCanvas::ortho);polar_=mode("45° 极轴","F10",&CadCanvas::polar);grid_=mode("网格","F7",&CadCanvas::grid);
    auto* engineering=menuBar()->addMenu("工程分析");for(auto op:QStringList{"板框","孔径","间距","焊盘","工艺边","重复"})engineering->addAction(action(op,[this,op]{runAnalysis(op);}));
    engineering->addAction(action("清理重复对象",[this]{editSelected("清理");}));
    auto* help=menuBar()->addMenu("帮助");help->addAction(action("关于 CAD / 支持范围",[this]{QMessageBox::information(this,branding::AppBrand::name()+" "+branding::AppBrand::version(),"Qiao · Qt 6 + C++20 / CMake\nASCII DXF 原生读写，DWG 通过 ODA 转换器。\n支持基础二维绘图、编辑、测量、图层和 PCB 分析。\n暂不显示的块、HATCH、复杂文字、布局和 3D 对象保留在源 DXF 中。\n圆角/倒角、填充、Gerber 对照、图纸空间后续实现。详见使用说明。");}));
    auto* toolbar=addToolBar("文件与视图");toolbar->setMovable(false);
    auto* brandIcon=new QLabel;brandIcon->setObjectName("brandIcon");brandIcon->setPixmap(branding::AppBrand::icon().pixmap(40,40));brandIcon->setToolTip("Qiao");toolbar->addWidget(brandIcon);
    auto* brand=new QLabel(branding::AppBrand::name()+" · "+branding::AppBrand::iconLabel());brand->setObjectName("brand");toolbar->addWidget(brand);toolbar->addActions({newAction,open,save,undo_,redo_,fit,snap_,ortho_,polar_,grid_});
    addToolBarBreak();auto* drawing=addToolBar("绘图与测量");drawing->setMovable(false);auto* group=new QActionGroup(this);
    for(auto t:{Tool::Select,Tool::Line,Tool::Polyline,Tool::Rectangle,Tool::Circle,Tool::Arc,Tool::Ellipse,Tool::Text,Tool::Move,Tool::Copy,Tool::Mirror,Tool::Distance,Tool::Angle,Tool::Dimension}) {
        auto* a=action(CadCanvas::toolName(t),[this,t]{canvas()->setTool(t);canvas()->setFocus();});a->setCheckable(true);group->addAction(a);tools_.insert(t,a);drawing->addAction(a);
        if(t>=Tool::Line&&t<=Tool::Text)draw->addAction(a);
        if(t>=Tool::Distance)measure->addAction(a);
    }
    for(auto t:{Tool::Horizontal,Tool::Vertical,Tool::Continuous,Tool::Coordinate})measure->addAction(action(CadCanvas::toolName(t),[this,t]{canvas()->setTool(t);}));
    measure->addAction(action("对象面积 / 半径 / 周长",[this]{for(const auto& e:canvas()->document->selected(canvas()->selection)){QStringList text;for(auto [k,v]:e.properties(canvas()->document->unitName()))text.append(k+": "+v);log(text.join(" · "));}}));
    auto dock=[&](QString title,QWidget* widget,Qt::DockWidgetArea area){auto* d=new QDockWidget(title,this);d->setObjectName(title);d->setWidget(widget);addDockWidget(area,d);view->addAction(d->toggleViewAction());return d;};
    auto* propsWidget=new QWidget;auto* propsLayout=new QVBoxLayout(propsWidget);properties_=new QTableWidget(0,2);properties_->setHorizontalHeaderLabels({"属性","数值"});properties_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);properties_->verticalHeader()->hide();properties_->setEditTriggers(QAbstractItemView::NoEditTriggers);propsLayout->addWidget(properties_);
    auto* propertyButton=new QPushButton("编辑 / 批量修改属性");connect(propertyButton,&QPushButton::clicked,this,&MainWindow::editProperties);propsLayout->addWidget(propertyButton);connect(properties_,&QTableWidget::cellDoubleClicked,this,[this]{editProperties();});dock("对象属性",propsWidget,Qt::RightDockWidgetArea)->setMinimumWidth(285);
    auto* layerWidget=new QWidget;auto* layerLayout=new QVBoxLayout(layerWidget);layerLayout->addWidget(new QLabel("当前绘图图层"));currentLayer_=new QComboBox;layerLayout->addWidget(currentLayer_);
    layers_=new QTableWidget(0,5);layers_->setHorizontalHeaderLabels({"图层","可见","锁定","颜色","线宽"});layers_->verticalHeader()->hide();layers_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);for(int i=1;i<5;++i)layers_->setColumnWidth(i,45);layers_->setEditTriggers(QAbstractItemView::NoEditTriggers);layerLayout->addWidget(layers_);
    auto* buttons=new QHBoxLayout;for(auto name:QStringList{"新增","删除"}){auto* b=new QPushButton(name);buttons->addWidget(b);connect(b,&QPushButton::clicked,this,[this,name]{name=="新增"?addLayer():deleteLayer();});}layerLayout->addLayout(buttons);dock("图层管理",layerWidget,Qt::RightDockWidgetArea);
    connect(currentLayer_,&QComboBox::currentTextChanged,this,[this](QString name){if(!refreshing_&&canvas()&&canvas()->document->layers.contains(name))canvas()->document->currentLayer=name;});
    connect(layers_,&QTableWidget::itemChanged,this,[this](QTableWidgetItem* item){if(!refreshing_)changeLayer(item->row(),item->column());});
    connect(layers_,&QTableWidget::cellDoubleClicked,this,[this](int row,int col){if(col>=3)changeLayer(row,col);});
    auto* analysisWidget=new QWidget;auto* analysisLayout=new QVBoxLayout(analysisWidget);auto* hint=new QLabel("选区优先；未选择时按以下图层检查。\n钻孔/焊盘用途由工程师指定。");hint->setWordWrap(true);analysisLayout->addWidget(hint);scope_=new QComboBox;analysisLayout->addWidget(scope_);
    for(auto op:QStringList{"板框","孔径","间距","焊盘","工艺边","重复"}){auto* b=new QPushButton(op+"分析");connect(b,&QPushButton::clicked,this,[this,op]{runAnalysis(op);});analysisLayout->addWidget(b);}
    results_=new QListWidget;analysisLayout->addWidget(results_);dock("PCB / FPC 工程分析",analysisWidget,Qt::LeftDockWidgetArea)->setMinimumWidth(255);
    connect(results_,&QListWidget::itemClicked,this,[this](QListWidgetItem* item){QSet<EntityId> ids;for(const auto& v:item->data(Qt::UserRole).toList())ids.insert(v.toULongLong());canvas()->selectIds(ids);canvas()->centerOnSelection();});
    auto* consoleWidget=new QWidget;auto* consoleLayout=new QVBoxLayout(consoleWidget);console_=new QTextEdit;console_->setReadOnly(true);console_->setMaximumHeight(100);consoleLayout->addWidget(console_);command_=new QLineEdit;command_->setPlaceholderText("命令 > LINE / CIRCLE / DIST · X,Y · @ΔX,ΔY · 距离<角度");consoleLayout->addWidget(command_);dock("命令与测量记录",consoleWidget,Qt::BottomDockWidgetArea);
    connect(command_,&QLineEdit::returnPressed,this,[this]{auto text=command_->text();command_->clear();executeCommand(text);});
    coordinate_=new QLabel;count_=new QLabel;statusBar()->addPermanentWidget(count_);statusBar()->addPermanentWidget(coordinate_);
    connect(tabs_,&QTabWidget::currentChanged,this,[this]{if(canvas()){results_->clear();refresh();}});connect(tabs_,&QTabWidget::tabCloseRequested,this,&MainWindow::closeTab);
}
void MainWindow::addDocument(Document document) {
    auto* c=new CadCanvas(std::make_shared<Document>(std::move(document)));connect(c,&CadCanvas::documentChanged,this,[this]{results_->clear();refresh();});
    connect(c,&CadCanvas::selectionChanged,this,&MainWindow::refreshProperties);connect(c,&CadCanvas::message,this,&MainWindow::log);
    connect(c,&CadCanvas::coordinates,this,[this,c](QPointF p){if(c==canvas())coordinate_->setText("X "+value(p.x())+" · Y "+value(p.y())+" "+c->document->unitName());});
    connect(c,&CadCanvas::toolChanged,this,[this]{if(canvas())for(auto it=tools_.begin();it!=tools_.end();++it)it.value()->setChecked(it.key()==canvas()->tool());});
    tabs_->setCurrentIndex(tabs_->addTab(c,c->document->title));refresh();QTimer::singleShot(0,c,[c]{c->fitDrawing();});
}
void MainWindow::refreshProperties() {
    if(!canvas())return;
    auto entities=canvas()->document->selected(canvas()->selection);QVector<QPair<QString,QString>> props;
    if(entities.size()==1)props=entities[0].properties(canvas()->document->unitName());else props={{"已选对象",QString::number(entities.size())}};
    properties_->setRowCount(props.size());for(qsizetype i=0;i<props.size();++i){properties_->setItem(i,0,new QTableWidgetItem(props[i].first));properties_->setItem(i,1,new QTableWidgetItem(props[i].second));}
}
void MainWindow::refresh() {
    if(!canvas())return;
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
}
void MainWindow::log(QString text){console_->append(text.toHtmlEscaped());statusBar()->showMessage(text,10000);}
void MainWindow::error(QString text){log(text);QMessageBox::warning(this,"操作未完成",text);}
void MainWindow::beginJob(CadCanvas* c,QString text){++activeJobs_;if(c)++jobsByCanvas_[c];log(text);refresh();}
void MainWindow::endJob(CadCanvas* c){--activeJobs_;if(c&&--jobsByCanvas_[c]<=0)jobsByCanvas_.remove(c);refresh();}
void MainWindow::openPath(QString path) {
    beginJob(nullptr,"后台读取："+path);auto converter=settings_.value("converter").toString();auto* watcher=new QFutureWatcher<OpenResult>(this);
    auto cancelled=std::make_shared<std::atomic_bool>(false);auto* progress=new QProgressDialog("正在读取图纸…","取消",0,0,this);progress->setWindowModality(Qt::NonModal);progress->setMinimumDuration(0);progress->setAutoClose(false);
    connect(progress,&QProgressDialog::canceled,this,[cancelled]{cancelled->store(true);});progress->show();
    connect(watcher,&QFutureWatcher<OpenResult>::finished,this,[this,watcher,progress,cancelled]{auto result=watcher->result();progress->deleteLater();endJob(nullptr);if(cancelled->load())log("图纸读取已取消。");else if(!result.error.isEmpty())error(result.error);else{addDocument(std::move(result.document));for(auto warning:canvas()->document->warnings)log(warning);log("图纸已载入。");}watcher->deleteLater();});
    watcher->setFuture(QtConcurrent::run([path,converter,cancelled]{OpenResult r;try{io::DxfReadOptions options;options.cancelled=[cancelled]{return cancelled->load();};r.document=io::DwgAdapter::open(path,converter,options);}catch(const std::exception& e){r.error=exceptionText(e);}return r;}));
}
void MainWindow::saveFile(bool saveAs,std::function<void(bool)> completion) {
    auto* c=canvas();if(jobsByCanvas_.value(c)>0){log("当前文档仍有后台任务，请等待完成。");if(completion)completion(false);return;}
    QString path=c->document->path;if(saveAs||path.isEmpty()){QString filter;path=QFileDialog::getSaveFileName(this,"保存图纸",path.isEmpty()?c->document->title+".dxf":path,"DXF (*.dxf);;DWG (*.dwg)",&filter);if(path.isEmpty()){if(completion)completion(false);return;}if(QFileInfo(path).suffix().isEmpty())path+=filter.startsWith("DWG")?".dwg":".dxf";}
    auto suffix=QFileInfo(path).suffix().toLower();if(suffix!="dxf"&&suffix!="dwg"){error("请使用 .dxf 或 .dwg 扩展名。");if(completion)completion(false);return;}
    // Value snapshot is implicitly shared by Qt; GUI remains editable during save.
    Document snapshot=*c->document;auto converter=settings_.value("converter").toString();QPointer<CadCanvas> guarded(c);beginJob(c,"后台保存："+path);
    auto* watcher=new QFutureWatcher<SaveResult>(this);connect(watcher,&QFutureWatcher<SaveResult>::finished,this,[this,watcher,guarded,path,snapshot,completion]{
        auto r=watcher->result();endJob(guarded);bool ok=r.error.isEmpty();if(!ok)error(r.error);else if(guarded){guarded->document->path=path;guarded->document->title=QFileInfo(path).completeBaseName();
            if(guarded->document->entities==snapshot.entities&&guarded->document->layers==snapshot.layers&&guarded->document->units==snapshot.units)guarded->document->markSaved();
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
void MainWindow::closeTab(int index){auto* c=qobject_cast<CadCanvas*>(tabs_->widget(index));if(jobsByCanvas_.value(c)>0){log("请等待该文档的后台任务完成再关闭。");return;}if(!confirmDiscard(c))return;tabs_->removeTab(index);c->deleteLater();if(tabs_->count()==0)addDocument(Document{});}
void MainWindow::closeEvent(QCloseEvent* e){if(activeJobs_){log("请等待后台任务完成再退出。");e->ignore();return;}for(int i=0;i<tabs_->count();++i)if(!confirmDiscard(qobject_cast<CadCanvas*>(tabs_->widget(i)),true)){e->ignore();return;}e->accept();}
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
    QPdfWriter writer(path);writer.setPageSize(QPageSize(QPageSize::A4));writer.setResolution(300);QPainter painter(&writer);if(!painter.isActive()){error("无法创建 PDF。");return;}
    canvas()->renderDocument(painter,QRectF(0,0,writer.width(),writer.height()));painter.end();log("已导出 PDF（适合页面，非指定工程比例）："+path);}
void MainWindow::editProperties() {
    auto before=canvas()->document->selected(canvas()->selection,true);if(before.isEmpty()){log("请先选择可编辑对象。");return;}
    QDialog dialog(this);dialog.setWindowTitle("对象 / 批量属性");QFormLayout form(&dialog);auto* layer=new QComboBox;layer->addItem("保持原图层");for(const auto& l:canvas()->document->layers)if(l.visible&&!l.locked)layer->addItem(l.name);form.addRow("图层",layer);
    auto* color=new QComboBox;color->addItems({"保持原颜色","随图层","自定义"});form.addRow("颜色",color);QVector<QPair<QDoubleSpinBox*,QDoubleSpinBox*>> points;QDoubleSpinBox *radius=nullptr,*height=nullptr;QLineEdit* text=nullptr;
    auto original=before[0];if(before.size()==1){if(original.kind!=Kind::Ellipse)for(qsizetype i=0;i<original.points.size();++i){auto* x=number(original.points[i].x());auto* y=number(original.points[i].y());points.append(qMakePair(x,y));form.addRow(QString("点 %1 X").arg(i+1),x);form.addRow(QString("点 %1 Y").arg(i+1),y);}
        if(original.kind==Kind::Circle||original.kind==Kind::Arc){radius=number(original.radius,0.000001);form.addRow("半径",radius);}
        if(original.kind==Kind::Text||original.kind==Kind::Dimension){text=new QLineEdit(original.text);form.addRow("文字 / 标注 <> = 实测值",text);}
        if(original.kind==Kind::Text){height=number(original.height,0.000001);form.addRow("文字高度",height);}}
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
    auto* c=canvas();if(jobsByCanvas_.value(c)>0){log("当前文档的后台任务尚未完成。");return;}auto scope=scopeEntities();if(scope.isEmpty()){log("范围内没有对象。");return;}
    double first=0,second=0;bool ok=false;std::optional<Entity> board;
    if(op=="板框"||op=="工艺边"){QVector<Entity> closed;for(const auto& e:scope)if(e.isClosed())closed.append(e);if(closed.size()!=1){error("请只选择一个闭合板框，或选择只包含一个板框的图层。");return;}board=closed[0];}
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
    QMap<QString,Tool> aliases{{"L",Tool::Line},{"LINE",Tool::Line},{"PL",Tool::Polyline},{"PLINE",Tool::Polyline},{"REC",Tool::Rectangle},{"RECTANG",Tool::Rectangle},{"C",Tool::Circle},{"CIRCLE",Tool::Circle},{"ARC",Tool::Arc},{"ELLIPSE",Tool::Ellipse},{"TEXT",Tool::Text},{"M",Tool::Move},{"MOVE",Tool::Move},{"CO",Tool::Copy},{"COPY",Tool::Copy},{"MI",Tool::Mirror},{"MIRROR",Tool::Mirror},{"DI",Tool::Distance},{"DIST",Tool::Distance},{"ANGLE",Tool::Angle},{"ID",Tool::Coordinate},{"DIM",Tool::Dimension},{"DIMALIGNED",Tool::Dimension},{"SELECT",Tool::Select}};
    if(cmd=="C"&&canvas()->tool()==Tool::Polyline){canvas()->finish(true);return;}if(aliases.contains(cmd)){canvas()->setTool(aliases[cmd]);return;}
    if(cmd=="UNDO"||cmd=="U"){undo_->trigger();return;}if(cmd=="REDO"){redo_->trigger();return;}if(cmd=="ERASE"||cmd=="E"){canvas()->deleteSelection();return;}
    if(cmd=="ZOOM"||cmd=="Z"||cmd=="EXTENTS"){canvas()->fitDrawing();return;}
    QMap<QString,QString> edits{{"OFFSET","偏移"},{"O","偏移"},{"ROTATE","旋转"},{"RO","旋转"},{"ARRAY","阵列"},{"TRIM","修剪"},{"TR","修剪"},{"EXTEND","延伸"},{"EX","延伸"},{"EXPLODE","分解"},{"X","分解"},{"JOIN","合并"},{"J","合并"}};
    if(edits.contains(cmd)){editSelected(edits[cmd]);return;}if(cmd=="AREA"){for(const auto& e:canvas()->document->selected(canvas()->selection))log("面积 "+value(e.area())+" · 周长/长度 "+value(e.length()));return;}
    if(canvas()->tool()==Tool::Select||canvas()->tool()==Tool::Zoom){log("请先输入绘图或测量命令。");return;}
    auto base=canvas()->inputPoints().isEmpty()?QPointF{}:canvas()->inputPoints().last();bool relative=text.startsWith('@');if(relative)text.remove(0,1);
    auto parse=[](QString t){bool ok=false;double v=t.toDouble(&ok);if(!ok||!std::isfinite(v))throw std::runtime_error("无效数值");return v;};
    try{QPointF point;if(text.contains(',')){auto parts=text.split(',');if(parts.size()!=2)throw std::runtime_error("无效坐标");point={parse(parts[0]),parse(parts[1])};if(relative)point+=base;}
        else if(text.contains('<')){auto parts=text.split('<');if(parts.size()!=2)throw std::runtime_error("无效极坐标");double len=parse(parts[0]),theta=parse(parts[1])*geo::pi/180;point=base+QPointF(std::cos(theta),std::sin(theta))*len;}
        else {double theta=geo::angle(base,canvas()->cursorPoint())*geo::pi/180;point=base+QPointF(std::cos(theta),std::sin(theta))*parse(text);}canvas()->acceptPoint(point);
    }catch(const std::exception&){log("命令无效：支持 X,Y、@ΔX,ΔY、距离<角度。");}
}
}
