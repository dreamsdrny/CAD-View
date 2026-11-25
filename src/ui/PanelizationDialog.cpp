#include "PanelizationDialog.h"
#include "CadCanvas.h"
#include "core/ContourOperations.h"
#include "io/DwgAdapter.h"
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QSignalBlocker>
#include <QTextDocument>
#include <QPageLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QScrollArea>
#include <QTabWidget>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QPdfWriter>
#include <QPainter>
#include <QScreen>
#include <QGuiApplication>
#include <QResizeEvent>
#include <QShowEvent>
#include <functional>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace cad::ui {
namespace {
class PanelCanvas : public CadCanvas {
public:
    using CadCanvas::CadCanvas;
    std::function<void(QPointF)> point;
    bool previewOnly=false;
protected:
    void mousePressEvent(QMouseEvent* e) override {
        if(point&&e->button()==Qt::LeftButton){point(modelAt(e->position()));return;}
        if(!previewOnly||e->button()==Qt::MiddleButton)CadCanvas::mousePressEvent(e);
    }
    void mouseDoubleClickEvent(QMouseEvent*) override {}
    void mouseReleaseEvent(QMouseEvent* e) override {if(!previewOnly||e->button()==Qt::MiddleButton)CadCanvas::mouseReleaseEvent(e);}
    void resizeEvent(QResizeEvent* e) override {CadCanvas::resizeEvent(e);if(previewOnly)fitDrawing();}
    void showEvent(QShowEvent* e) override {CadCanvas::showEvent(e);if(previewOnly)fitDrawing();}
    void keyPressEvent(QKeyEvent* e) override {if(!previewOnly||e->key()!=Qt::Key_Delete)CadCanvas::keyPressEvent(e);}
};
QDoubleSpinBox* number(QString name,double initial,double minimum=0) {
    auto* box=new QDoubleSpinBox;box->setObjectName(name);box->setDecimals(6);box->setRange(minimum,1e6);box->setValue(initial);return box;
}
QCheckBox* check(QString text,QString name,bool value=false) {
    auto* box=new QCheckBox(text);box->setObjectName(name);box->setChecked(value);return box;
}
QString errorText(const std::exception& e){return QString::fromUtf8(e.what());}
QDoubleSpinBox* materialNumber(QString name,double initial,double maximum=100000) {
    auto* box=new QDoubleSpinBox;box->setObjectName(name);box->setDecimals(6);box->setRange(0,maximum);box->setValue(initial);return box;
}
QSpinBox* materialCount(QString name,int initial,int maximum) {
    auto* box=new QSpinBox;box->setObjectName(name);box->setRange(0,maximum);box->setValue(initial);return box;
}
QString percentage(double n){return cad::value(n,2)+"%";}
QString signedNumber(qint64 n){return (n>0?"+":"")+QString::number(n);}
}
namespace fpc=engineering::fpc;
class MaterialPreviewCanvas : public CadCanvas {
public:
    using CadCanvas::CadCanvas;
protected:
    void mousePressEvent(QMouseEvent* e) override {if(e->button()==Qt::MiddleButton)CadCanvas::mousePressEvent(e);}
    void mouseReleaseEvent(QMouseEvent* e) override {if(e->button()==Qt::MiddleButton)CadCanvas::mouseReleaseEvent(e);}
    void mouseDoubleClickEvent(QMouseEvent*) override {}
    void keyPressEvent(QKeyEvent* e) override {if(e->key()!=Qt::Key_Delete)CadCanvas::keyPressEvent(e);}
    void resizeEvent(QResizeEvent* e) override {CadCanvas::resizeEvent(e);fitDrawing();}
    void showEvent(QShowEvent* e) override {CadCanvas::showEvent(e);fitDrawing();}
};
// The material planner is intentionally a section of the one "拼版" dialog.
// It has no independent window/entry; the SET currently being edited is always
// the first comparison row and the source of automatic PCS->SET search.
class MaterialPlanSection : public QWidget {
public:
    explicit MaterialPlanSection(QString converter,QWidget* parent=nullptr);
    ~MaterialPlanSection() override;
    std::function<void(const fpc::SetSpec&)> onApply;
    std::function<void(Document)> onDrawing;
    void setCurrentSet(std::optional<fpc::SetSpec> set);
    void activate();
    bool busy() const {return busy_;}
    void cancelAndWait();
    void saveReport(QString path,bool pdf=false);
private:
    struct Job {fpc::SearchResult result;QString error;quint64 generation=0;};
    QString converter_;
    std::optional<fpc::SetSpec> current_;
    QDoubleSpinBox *width_,*height_,*left_,*right_,*bottom_,*top_,*gapX_,*gapY_,*yield_,*minSetWidth_,*minSetHeight_,*maxSetWidth_,*maxSetHeight_;
    QSpinBox *order_,*available_,*maxRows_,*maxCols_,*minPcs_,*maxPcs_;
    QCheckBox *rotate_,*mixed_,*nested_,*detail_,*pcsRotate_;
    QComboBox *delivery_,*displayUnits_,*sort_,*format_;
    QTableWidget *manual_,*results_;
    QLabel *currentLabel_,*searchHint_,*summary_,*status_;
    QPushButton *search_,*compare_,*cancel_,*apply_,*generate_;
    CadCanvas* preview_;
    fpc::Material calculatedMaterial_;
    QVector<fpc::Scheme> schemes_;
    QVector<int> visibleOrder_;
    int baseline_=-1;
    bool updating_=false,busy_=false,valid_=false;
    quint64 generation_=0;
    double previousScale_=1;
    std::shared_ptr<std::atomic_bool> cancelled_;
    QFutureWatcher<Job> watcher_;
    fpc::Material material() const;
    void addManual(fpc::SetSpec set);
    void compare();
    void search();
    void invalidate(QString reason={});
    void publish(QVector<fpc::Scheme> schemes);
    void rebuildTable();
    int selectedIndex() const;
    void selectScheme();
    void exportResults();
    void writePdf(const QString& path,bool report);
    void unitsChanged();
    void setBusy(bool busy);
};
MaterialPlanSection::MaterialPlanSection(QString converter,QWidget* parent)
    :QWidget(parent),converter_(std::move(converter)) {
    setObjectName("panelMaterialSection");
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
    auto* hint=new QLabel("材料方案是拼版的一部分：当前 SET 会自动作为第一个比较方案。大料工作区严格扣除四边预留，SET 不重叠且满足材料净距。SET 占料率≠PCS 实际面积利用率；硬板规则尚未纳入。");
    hint->setWordWrap(true);layout->addWidget(hint);
    auto* splitter=new QSplitter;splitter->setChildrenCollapsible(false);layout->addWidget(splitter,1);
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setMinimumWidth(285);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);auto* inputs=new QWidget;scroll->setWidget(inputs);splitter->addWidget(scroll);
    auto* form=new QFormLayout(inputs);form->setRowWrapPolicy(QFormLayout::WrapLongRows);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    displayUnits_=new QComboBox;displayUnits_->setObjectName("panelMaterialUnits");displayUnits_->addItems({"mm","inch（1 in = 25.4 mm）"});form->addRow("大料输入单位",displayUnits_);
    width_=materialNumber("panelMaterialWidth",467.868);height_=materialNumber("panelMaterialHeight",620.268);form->addRow("大料 X 宽",width_);form->addRow("大料 Y 高",height_);
    auto* example=new QPushButton("附件示例 18.42″ × 24.42″");form->addRow(example);connect(example,&QPushButton::clicked,this,[this]{if(busy_)return;width_->setValue(467.868/previousScale_);height_->setValue(620.268/previousScale_);});
    left_=materialNumber("panelMaterialMarginLeft",10);right_=materialNumber("panelMaterialMarginRight",10);bottom_=materialNumber("panelMaterialMarginBottom",10);top_=materialNumber("panelMaterialMarginTop",10);
    form->addRow("左预留",left_);form->addRow("右预留",right_);form->addRow("下预留",bottom_);form->addRow("上预留",top_);
    gapX_=materialNumber("panelMaterialGapX",2);gapY_=materialNumber("panelMaterialGapY",2);form->addRow("材料上 SET X 净距",gapX_);form->addRow("材料上 SET Y 净距",gapY_);
    rotate_=new QCheckBox("允许 SET 旋转 90°（无方向限制）");rotate_->setObjectName("panelMaterialAllowRotate");rotate_->setChecked(true);mixed_=new QCheckBox("比较 0°/90° 分区混排");mixed_->setObjectName("panelMaterialMixed");mixed_->setChecked(true);form->addRow(rotate_);form->addRow(mixed_);
    order_=materialCount("panelMaterialOrder",1000,1000000000);available_=materialCount("panelMaterialAvailable",100,1000000000);form->addRow("订单 PCS 数量",order_);form->addRow("已有大料张数",available_);
    yield_=materialNumber("panelMaterialYield",100);yield_->setMinimum(0.000001);form->addRow("预计良率 (%)",yield_);
    delivery_=new QComboBox;delivery_->setObjectName("panelMaterialDelivery");delivery_->addItems({"按 PCS 良率（零散 PCS 可出货）","按整 SET 良率（完整 SET 交货）"});form->addRow("出货规则",delivery_);
    maxRows_=materialCount("panelMaterialSearchRows",4,8);maxRows_->setMinimum(1);maxCols_=materialCount("panelMaterialSearchColumns",4,8);maxCols_->setMinimum(1);minPcs_=materialCount("panelMaterialSearchMinPcs",1,64);minPcs_->setMinimum(1);maxPcs_=materialCount("panelMaterialSearchMaxPcs",16,64);maxPcs_->setMinimum(1);
    form->addRow("搜索最大行数",maxRows_);form->addRow("搜索最大列数",maxCols_);form->addRow("每 SET 最少 PCS",minPcs_);form->addRow("每 SET 最多 PCS",maxPcs_);
    minSetWidth_=materialNumber("panelMaterialMinSetWidth",0);minSetHeight_=materialNumber("panelMaterialMinSetHeight",0);maxSetWidth_=materialNumber("panelMaterialMaxSetWidth",1000);maxSetHeight_=materialNumber("panelMaterialMaxSetHeight",1000);
    form->addRow("SET 最小 X (mm)",minSetWidth_);form->addRow("SET 最小 Y (mm)",minSetHeight_);form->addRow("SET 最大 X (mm)",maxSetWidth_);form->addRow("SET 最大 Y (mm)",maxSetHeight_);
    pcsRotate_=new QCheckBox("允许 PCS 在 SET 内转 90°");pcsRotate_->setObjectName("panelMaterialPcsRotate");pcsRotate_->setChecked(true);form->addRow(pcsRotate_);
    nested_=new QCheckBox("搜索行/列/棋盘交替 180° 紧排");nested_->setChecked(true);nested_->setObjectName("panelMaterialNested");form->addRow(nested_);
    search_=new QPushButton("搜索不同 PCS→SET 拼法（后台）");search_->setObjectName("panelMaterialSearch");form->addRow(search_);search_->setEnabled(false);
    searchHint_=new QLabel("自动搜索需要当前拼版 SET 的真实 PCS 外形；请先在“拼版预览 / 连接筋”页确认外轮廓。");searchHint_->setWordWrap(true);form->addRow(searchHint_);
    cancel_=new QPushButton("取消搜索");cancel_->setEnabled(false);cancel_->setObjectName("panelMaterialCancel");form->addRow(cancel_);
    sort_=new QComboBox;sort_->setObjectName("panelMaterialSort");sort_->addItems({"每大料 PCS 产出优先","所需材料张数优先","SET 占料率优先","PCS 实际面积利用率优先"});form->addRow("结果排序",sort_);
    auto* tabs=new QTabWidget;tabs->setObjectName("panelMaterialTabs");splitter->addWidget(tabs);auto* comparison=new QWidget;auto* right=new QVBoxLayout(comparison);tabs->addTab(comparison,"方案与产出比较");
    currentLabel_=new QLabel;currentLabel_->setObjectName("panelMaterialCurrent");currentLabel_->setWordWrap(true);right->addWidget(currentLabel_);
    auto* manualHint=new QLabel("额外比较 SET（长度 mm，面积 mm²）：当前拼版始终自动参与比较，无需重复填写。PCS 面积未知填 0；边距是 SET 工艺边，不是大料预留。");manualHint->setWordWrap(true);right->addWidget(manualHint);
    manual_=new QTableWidget(0,9);manual_->setObjectName("panelMaterialManualSets");manual_->setHorizontalHeaderLabels({"方案","SET X mm","SET Y mm","PCS/SET","PCS面积mm²","左边mm","右边mm","下边mm","上边mm"});manual_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);manual_->setMaximumHeight(160);right->addWidget(manual_);
    auto* buttons=new QHBoxLayout;right->addLayout(buttons);auto* add=new QPushButton("增加比较 SET");auto* remove=new QPushButton("删除选中 SET");compare_=new QPushButton("计算 / 比较 SET 方案");compare_->setObjectName("panelMaterialCompare");buttons->addWidget(add);buttons->addWidget(remove);buttons->addWidget(compare_);
    results_=new QTableWidget(0,15);results_->setObjectName("panelMaterialSchemes");results_->setHorizontalHeaderLabels({"方案","SET尺寸mm","PCS/SET","大料排法","SET/Panel","PCS/Panel","SET占大料%","SET占工作区%","PCS实际占SET%","PCS实际占大料%","所需材料张","预计出货PCS","超交PCS","已有材料预计PCS","相对基准PCS/Panel"});results_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);results_->setSelectionBehavior(QAbstractItemView::SelectRows);results_->setSelectionMode(QAbstractItemView::SingleSelection);results_->setEditTriggers(QAbstractItemView::NoEditTriggers);right->addWidget(results_,1);
    auto* baselineButton=new QPushButton("当前选中方案设为比较基准");baselineButton->setObjectName("panelMaterialBaseline");right->addWidget(baselineButton);
    auto* summaryScroll=new QScrollArea;summaryScroll->setWidgetResizable(true);summaryScroll->setMinimumHeight(105);summaryScroll->setMaximumHeight(185);summaryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    summary_=new QLabel;summary_->setObjectName("panelMaterialMetrics");summary_->setTextFormat(Qt::PlainText);summary_->setWordWrap(true);summary_->setAlignment(Qt::AlignTop|Qt::AlignLeft);summaryScroll->setWidget(summary_);right->addWidget(summaryScroll);
    auto* previewWidget=new QWidget;auto* previewLayout=new QVBoxLayout(previewWidget);preview_=new MaterialPreviewCanvas(std::make_shared<Document>());preview_->setObjectName("panelMaterialPreview");preview_->grid=false;previewLayout->addWidget(preview_,1);detail_=new QCheckBox("显示真实 PCS 外形（仅几何来源方案，最多 2000 PCS）");detail_->setObjectName("panelMaterialDetailed");previewLayout->addWidget(detail_);tabs->addTab(previewWidget,"大料排布预览");
    status_=new QLabel;status_->setObjectName("panelMaterialStatus");status_->setWordWrap(true);layout->addWidget(status_);
    auto* actions=new QHBoxLayout;layout->addLayout(actions);apply_=new QPushButton("应用到 SET 拼版");apply_->setToolTip("将已验证的行列、旋转及步距回填到拼版页，然后重新生成工艺边/连接筋。");apply_->setObjectName("panelMaterialApply");generate_=new QPushButton("生成大料排布图");generate_->setObjectName("panelMaterialGenerate");format_=new QComboBox;format_->setObjectName("panelMaterialFormat");format_->addItems({"CSV 方案计算表","PDF 方案对比报告","DXF 大料排布图","PDF 大料排布图","DWG 大料排布图（需ODA）"});auto* exportButton=new QPushButton("导出");exportButton->setObjectName("panelMaterialExport");actions->addWidget(apply_);actions->addWidget(generate_);actions->addWidget(format_);actions->addWidget(exportButton);
    for(auto* button:findChildren<QPushButton*>())button->setAutoDefault(false);
    connect(compare_,&QPushButton::clicked,this,[this]{compare();});connect(search_,&QPushButton::clicked,this,[this]{search();});connect(cancel_,&QPushButton::clicked,this,[this]{if(cancelled_)cancelled_->store(true);status_->setText("正在取消，等待当前候选校验结束…");});
    connect(add,&QPushButton::clicked,this,[this]{if(busy_)return;fpc::SetSpec s;s.name="比较 SET";s.width=100;s.height=150;s.units=1;addManual(s);invalidate();});
    connect(remove,&QPushButton::clicked,this,[this]{if(busy_)return;if(manual_->currentRow()>=0){manual_->removeRow(manual_->currentRow());invalidate();}});
    connect(baselineButton,&QPushButton::clicked,this,[this]{int index=selectedIndex();if(index>=0){baseline_=index;rebuildTable();}});
    connect(apply_,&QPushButton::clicked,this,[this]{int index=selectedIndex();if(!valid_||index<0||!schemes_[index].set.hasGeometry)return;if(onApply)onApply(schemes_[index].set);status_->setText("已应用到 SET 拼版；请回到拼版页核对行列、旋转和工艺筋后重新生成。");});
    connect(generate_,&QPushButton::clicked,this,[this]{try{int index=selectedIndex();if(!valid_||index<0)return;auto d=fpc::drawing(calculatedMaterial_,schemes_[index],detail_->isChecked());if(onDrawing)onDrawing(std::move(d));status_->setText("大料排布图已在主窗口新标签打开；该图不含电路/钻孔生产数据，请另存并工程复核。");}catch(const std::exception& e){QMessageBox::warning(this,"生成失败",errorText(e));}});
    connect(exportButton,&QPushButton::clicked,this,[this]{exportResults();});connect(results_,&QTableWidget::itemSelectionChanged,this,[this]{if(!updating_)selectScheme();});connect(sort_,&QComboBox::currentIndexChanged,this,[this]{rebuildTable();});connect(detail_,&QCheckBox::toggled,this,[this]{selectScheme();});
    connect(displayUnits_,&QComboBox::currentIndexChanged,this,[this]{unitsChanged();});
    for(auto* box:{width_,height_,left_,right_,bottom_,top_,gapX_,gapY_,yield_,minSetWidth_,minSetHeight_,maxSetWidth_,maxSetHeight_})connect(box,&QDoubleSpinBox::valueChanged,this,[this]{invalidate();});
    for(auto* box:{order_,available_,maxRows_,maxCols_,minPcs_,maxPcs_})connect(box,&QSpinBox::valueChanged,this,[this]{invalidate();});
    for(auto* box:{rotate_,mixed_,nested_,pcsRotate_})connect(box,&QCheckBox::toggled,this,[this]{invalidate();});
    connect(delivery_,&QComboBox::currentIndexChanged,this,[this]{invalidate();});connect(manual_,&QTableWidget::itemChanged,this,[this]{invalidate();});
    connect(&watcher_,&QFutureWatcher<Job>::finished,this,[this]{auto job=watcher_.result();setBusy(false);if(job.generation!=generation_){status_->setText("拼版 SET 已变化，旧搜索结果已丢弃；请重新搜索。");return;}if(!job.error.isEmpty()){status_->setText(job.error);return;}publish(std::move(job.result.schemes));status_->setText(QString("校验 %1 个候选，拒绝 %2 个几何不可行方案。%3").arg(job.result.considered).arg(job.result.rejectedGeometry).arg(job.result.notes.join(" ")));});
    calculatedMaterial_=material();apply_->setEnabled(false);generate_->setEnabled(false);
    splitter->setStretchFactor(0,1);splitter->setStretchFactor(1,4);splitter->setSizes({300,900});setCurrentSet(std::nullopt);
}
MaterialPlanSection::~MaterialPlanSection(){cancelAndWait();}
void MaterialPlanSection::setCurrentSet(std::optional<fpc::SetSpec> set) {
    auto same=[](const std::optional<fpc::SetSpec>& a,const std::optional<fpc::SetSpec>& b){
        if(a.has_value()!=b.has_value())return false;
        if(!a)return true;
        auto near=[](double x,double y){return std::abs(x-y)<1e-6;};
        return near(a->width,b->width)&&near(a->height,b->height)&&a->units==b->units&&near(a->left,b->left)&&near(a->right,b->right)&&near(a->bottom,b->bottom)&&near(a->top,b->top)&&a->hasGeometry==b->hasGeometry;
    };
    bool changed=!same(current_,set);
    if(changed&&busy_&&cancelled_)cancelled_->store(true);
    if(changed)++generation_;
    current_=std::move(set);
    if(current_)currentLabel_->setText(QString("当前拼版 SET：%1 × %2 mm，%3 PCS/SET，单 PCS 面积 %4 mm²，SET 工艺边 左/右/下/上 %5/%6/%7/%8 mm。%9")
        .arg(value(current_->width,3),value(current_->height,3)).arg(current_->units).arg(value(current_->unitArea,3)).arg(value(current_->left,3),value(current_->right,3),value(current_->bottom,3),value(current_->top,3)).arg(current_->hasGeometry?"将自动参与比较，并可搜索不同 PCS→SET 拼法。":"当前 SET 没有真实 PCS 几何，只能比较已有尺寸。"));
    else currentLabel_->setText("当前拼版尚未确认 SET。");
    bool canSearch=current_&&current_->hasGeometry;
    search_->setEnabled(!busy_&&canSearch);searchHint_->setVisible(!canSearch);
    if(canSearch)searchHint_->setText("自动搜索会基于当前 PCS 外形生成不同 SET；当前 SET 仍会保留为比较基准。");
    if(changed)invalidate("拼版 SET 已变化，请重新计算材料方案。");
}
void MaterialPlanSection::activate(){if(!busy_&&!valid_&&(current_||manual_->rowCount()>0))compare();}
void MaterialPlanSection::cancelAndWait(){if(!busy_)return;if(cancelled_)cancelled_->store(true);watcher_.waitForFinished();busy_=false;}
void MaterialPlanSection::addManual(fpc::SetSpec s) {
    bool was=updating_;updating_=true;int row=manual_->rowCount();manual_->insertRow(row);
    QStringList values{s.name,value(s.width,6),value(s.height,6),QString::number(s.units),value(s.unitArea,6),value(s.left,6),value(s.right,6),value(s.bottom,6),value(s.top,6)};
    for(int col=0;col<values.size();++col)manual_->setItem(row,col,new QTableWidgetItem(values[col]));
    updating_=was;
}
void MaterialPlanSection::invalidate(QString reason){
    if(updating_)return;
    valid_=false;apply_->setEnabled(false);generate_->setEnabled(false);
    status_->setText(reason.isEmpty()?"材料输入已变化，原结果失效，请重新计算/搜索。":reason);
}
fpc::Material MaterialPlanSection::material() const {
    fpc::Material m;double scale=previousScale_;m.width=width_->value()*scale;m.height=height_->value()*scale;m.left=left_->value()*scale;m.right=right_->value()*scale;m.bottom=bottom_->value()*scale;m.top=top_->value()*scale;m.gapX=gapX_->value()*scale;m.gapY=gapY_->value()*scale;m.allowRotate=rotate_->isChecked();m.mixedOrientations=mixed_->isChecked();m.order=order_->value();m.availablePanels=available_->value();m.yieldPercent=yield_->value();m.delivery=delivery_->currentIndex()?fpc::Delivery::CompleteSets:fpc::Delivery::Pieces;return m;
}
void MaterialPlanSection::unitsChanged() {
    double next=displayUnits_->currentIndex()?25.4:1;updating_=true;
    for(auto* box:{width_,height_,left_,right_,bottom_,top_,gapX_,gapY_})box->setValue(box->value()*previousScale_/next);
    previousScale_=next;updating_=false;invalidate();
}
void MaterialPlanSection::compare() {
    if(busy_)return;
    try {
        auto m=material();QVector<fpc::SetSpec> list;
        if(current_)list.append(*current_);
        for(int row=0;row<manual_->rowCount();++row) {
            fpc::SetSpec s;s.name=manual_->item(row,0)?manual_->item(row,0)->text():QString("SET %1").arg(row+1);double values[8];
            for(int col=1;col<9;++col){bool ok=false;auto* item=manual_->item(row,col);values[col-1]=item?item->text().toDouble(&ok):0;if(!ok||!std::isfinite(values[col-1]))throw std::runtime_error("SET 表格请输入有效数值。");}
            if(values[2]!=std::floor(values[2])||values[2]<1||values[2]>100000)throw std::runtime_error("PCS/SET 必须是 1–100000 的整数。");
            s.width=values[0];s.height=values[1];s.units=int(values[2]);s.unitArea=values[3];s.left=values[4];s.right=values[5];s.bottom=values[6];s.top=values[7];
            list.append(s);
        }
        if(list.isEmpty())throw std::runtime_error("请先确认当前拼版 SET，或增加一个比较 SET。");
        calculatedMaterial_=m;publish(fpc::compare(m,list));status_->setText(QString("已计算 %1 个可行排布（当前拼版 + %2 个比较 SET）。0°/90°混排不允许 SET 重叠；空面积显示未知。").arg(schemes_.size()).arg(manual_->rowCount()));
    }catch(const std::exception& e){valid_=false;apply_->setEnabled(false);generate_->setEnabled(false);status_->setText(errorText(e));}
}
void MaterialPlanSection::search() {
    if(busy_||!current_||!current_->hasGeometry)return;
    try {
        auto m=material();fpc::workArea(m);calculatedMaterial_=m;fpc::SearchOptions o;o.maxRows=maxRows_->value();o.maxColumns=maxCols_->value();o.minPieces=minPcs_->value();o.maxPieces=maxPcs_->value();o.nested=nested_->isChecked();o.allowPcsQuarterTurn=pcsRotate_->isChecked();o.minSetWidth=minSetWidth_->value();o.minSetHeight=minSetHeight_->value();o.maxSetWidth=maxSetWidth_->value();o.maxSetHeight=maxSetHeight_->value();auto source=current_->source;auto process=current_->options;auto baseline=*current_;quint64 generation=generation_;
        valid_=false;cancelled_=std::make_shared<std::atomic_bool>(false);auto token=cancelled_;setBusy(true);status_->setText("后台搜索中：生成不同 PCS/SET、旋转及紧排候选，逐一校验真实外形与材料容量（45秒/512候选预算）…");
        watcher_.setFuture(QtConcurrent::run([m,source,process,o,token,baseline,generation]{Job job;job.generation=generation;try{
            job.result=fpc::search(m,source,process,o,[token]{return token->load();});
            try{auto original=fpc::compare(m,{baseline});for(auto& s:original)job.result.schemes.append(std::move(s));}catch(const std::exception&){}
        }catch(const std::exception& e){job.error=errorText(e);}return job;}));
    }catch(const std::exception& e){status_->setText(errorText(e));}
}
void MaterialPlanSection::setBusy(bool busy) {
    busy_=busy;search_->setEnabled(!busy&&current_&&current_->hasGeometry);compare_->setEnabled(!busy);cancel_->setEnabled(busy);manual_->setEnabled(!busy);
    for(auto* box:findChildren<QDoubleSpinBox*>())box->setEnabled(!busy);
    for(auto* box:findChildren<QSpinBox*>())box->setEnabled(!busy);
    for(QWidget* box:QVector<QWidget*>{rotate_,mixed_,nested_,pcsRotate_,delivery_,displayUnits_})box->setEnabled(!busy);
    apply_->setEnabled(!busy&&valid_);generate_->setEnabled(!busy&&valid_);
}
void MaterialPlanSection::publish(QVector<fpc::Scheme> schemes){
    QVector<fpc::Scheme> unique;QSet<QString> seen;
    for(auto& s:schemes){QString key=value(s.set.width,6)+":"+value(s.set.height,6)+":"+QString::number(s.set.units)+":"+s.layout.method;if(seen.contains(key))continue;seen.insert(key);unique.append(std::move(s));}
    schemes_=std::move(unique);
    std::stable_sort(schemes_.begin(),schemes_.end(),[](const fpc::Scheme& a,const fpc::Scheme& b){if(a.metrics.piecesPerPanel!=b.metrics.piecesPerPanel)return a.metrics.piecesPerPanel>b.metrics.piecesPerPanel;return a.metrics.setPanelUtilization>b.metrics.setPanelUtilization;});
    valid_=!schemes_.isEmpty();baseline_=valid_?0:-1;
    if(current_)for(int i=0;i<schemes_.size();++i)if(schemes_[i].set.name==current_->name&&schemes_[i].set.hasGeometry){baseline_=i;break;}
    rebuildTable();
}
int MaterialPlanSection::selectedIndex() const {int row=results_->currentRow();return row>=0&&row<visibleOrder_.size()?visibleOrder_[row]:-1;}
void MaterialPlanSection::rebuildTable() {
    int selected=selectedIndex();updating_=true;visibleOrder_.clear();for(int i=0;i<schemes_.size();++i)visibleOrder_.append(i);
    std::stable_sort(visibleOrder_.begin(),visibleOrder_.end(),[this](int a,int b){const auto& x=schemes_[a].metrics;const auto& y=schemes_[b].metrics;
        if(sort_->currentIndex()==1&&x.panelsRequired!=y.panelsRequired)return x.panelsRequired<y.panelsRequired;
        if(sort_->currentIndex()==2&&x.setPanelUtilization!=y.setPanelUtilization)return x.setPanelUtilization>y.setPanelUtilization;
        if(sort_->currentIndex()==3&&x.pcsPanelUtilization!=y.pcsPanelUtilization)return x.pcsPanelUtilization>y.pcsPanelUtilization;
        return x.piecesPerPanel>y.piecesPerPanel;});
    results_->setRowCount(schemes_.size());
    for(int row=0;row<visibleOrder_.size();++row){int index=visibleOrder_[row];const auto& s=schemes_[index];const auto& x=s.metrics;
        QStringList values{(index==baseline_?"[基准] ":"")+s.set.name,value(s.set.width,3)+" × "+value(s.set.height,3),QString::number(s.set.units),s.layout.method,QString::number(x.setsPerPanel),QString::number(x.piecesPerPanel),percentage(x.setPanelUtilization),percentage(x.setWorkUtilization),x.trueAreaKnown?percentage(x.pcsSetUtilization):"未知",x.trueAreaKnown?percentage(x.pcsPanelUtilization):"未知",QString::number(x.panelsRequired),QString::number(x.expectedShipment),QString::number(x.excess),QString::number(x.availableShipment),baseline_>=0?signedNumber(x.piecesPerPanel-schemes_[baseline_].metrics.piecesPerPanel):"—"};
        for(int col=0;col<values.size();++col)results_->setItem(row,col,new QTableWidgetItem(values[col]));
    }
    if(valid_){int row=visibleOrder_.indexOf(selected);results_->selectRow(row>=0?row:0);}updating_=false;selectScheme();
}
void MaterialPlanSection::selectScheme() {
    int index=selectedIndex();apply_->setEnabled(valid_&&!busy_&&index>=0&&schemes_[index].set.hasGeometry);generate_->setEnabled(valid_&&!busy_&&index>=0);
    if(index<0){summary_->setText("没有可放入材料的方案，请检查尺寸/边距/方向限制。");*preview_->document=Document{};preview_->synchronize();detail_->setEnabled(false);return;}
    const auto& s=schemes_[index];const auto& x=s.metrics;
    detail_->setEnabled(s.set.hasGeometry);if(!s.set.hasGeometry){QSignalBlocker blocker(detail_);detail_->setChecked(false);}
    QString delta;if(baseline_>=0){const auto& b=schemes_[baseline_];delta=QString("相对基准：PCS/Panel %1；SET面积变化 %2%；SET占料率变化 %3 个百分点；所需大料 %4 张。")
        .arg(signedNumber(x.piecesPerPanel-b.metrics.piecesPerPanel),value(100*(x.setArea/b.metrics.setArea-1),2),value(x.setPanelUtilization-b.metrics.setPanelUtilization,2),signedNumber(x.panelsRequired-b.metrics.panelsRequired));}
    summary_->setText(QString("大料面积 %1 mm²；工作区面积 %2 mm²；SET面积 %3 mm²；去工艺边SET区域 %4 mm²。\n%5 SET/Panel × %6 PCS/SET = %7 PCS/Panel；SET占大料 %8，去边SET占大料 %9。\n订单 %10 PCS，预计良率 %11%；需 %12 张大料，预计出货 %13 PCS，预计超交 %14 PCS；现有 %15 张预计可出 %16 PCS。\n%17\n预计出货为按所选 PCS/完整SET 规则取整的数量估算，不是保交数量；同层数/工艺未在算法中推断。")
        .arg(value(x.panelArea,3),value(x.workArea,3),value(x.setArea,3),value(x.setInnerArea,3)).arg(x.setsPerPanel).arg(s.set.units).arg(x.piecesPerPanel).arg(percentage(x.setPanelUtilization),percentage(x.setInnerPanelUtilization)).arg(calculatedMaterial_.order).arg(value(calculatedMaterial_.yieldPercent,2)).arg(x.panelsRequired).arg(x.expectedShipment).arg(x.excess).arg(calculatedMaterial_.availablePanels).arg(x.availableShipment).arg(delta));
    try{*preview_->document=fpc::drawing(calculatedMaterial_,s,detail_->isChecked());preview_->synchronize();preview_->fitDrawing();}catch(const std::exception& e){*preview_->document=Document{};preview_->synchronize();status_->setText(errorText(e));}
}
void MaterialPlanSection::writePdf(const QString& path,bool report) {
    QTemporaryDir temp;if(!temp.isValid())throw std::runtime_error("无法创建 PDF 临时目录。");auto temporary=temp.filePath("fpc.pdf");
    {QPdfWriter writer(temporary);writer.setPageSize(QPageSize(QPageSize::A4));writer.setPageOrientation(QPageLayout::Landscape);writer.setResolution(150);
        if(report){QString html="<h1>拼版材料方案比较</h1><p>材料 "+value(calculatedMaterial_.width,3)+" × "+value(calculatedMaterial_.height,3)+" mm；订单 "+QString::number(calculatedMaterial_.order)+" PCS；预计良率 "+value(calculatedMaterial_.yieldPercent,2)+"%。</p><p>大料预留 左/右/下/上："+value(calculatedMaterial_.left,3)+" / "+value(calculatedMaterial_.right,3)+" / "+value(calculatedMaterial_.bottom,3)+" / "+value(calculatedMaterial_.top,3)+" mm；SET间距 X/Y："+value(calculatedMaterial_.gapX,3)+" / "+value(calculatedMaterial_.gapY,3)+" mm；SET旋转："+(calculatedMaterial_.allowRotate?"允许":"禁止")+"；出货规则："+(calculatedMaterial_.delivery==fpc::Delivery::Pieces?"按PCS良率":"按完整SET良率")+"。</p><p>SET占料率以整张大料面积为分母。PCS面积未知时不计算真实面积利用率。预计出货非保证，非全局最优排料；硬板规则未纳入。手动尺寸方案不代表已验证PCS内部轮廓。</p><table border='1' cellspacing='0' cellpadding='4'><tr><th>方案</th><th>SET mm</th><th>PCS/SET</th><th>排法</th><th>SET/Panel</th><th>PCS/Panel</th><th>SET占料%</th><th>PCS占料%</th><th>材料张数</th><th>预计出货PCS</th></tr>";
            for(int i:visibleOrder_){const auto& s=schemes_[i];const auto& x=s.metrics;html+=QString("<tr><td>")+(i==baseline_?"[基准] ":"")+s.set.name.toHtmlEscaped()+"</td><td>"+value(s.set.width,3)+"×"+value(s.set.height,3)+"</td><td>"+QString::number(s.set.units)+"</td><td>"+s.layout.method.toHtmlEscaped()+"</td><td>"+QString::number(x.setsPerPanel)+"</td><td>"+QString::number(x.piecesPerPanel)+"</td><td>"+percentage(x.setPanelUtilization)+"</td><td>"+(x.trueAreaKnown?percentage(x.pcsPanelUtilization):"未知")+"</td><td>"+QString::number(x.panelsRequired)+"</td><td>"+QString::number(x.expectedShipment)+"</td></tr>";}
            html+="</table><h2>变化量（相对选定基准）</h2><table border='1' cellspacing='0' cellpadding='4'><tr><th>方案</th><th>SET面积变化%</th><th>PCS/Panel变化</th><th>SET占料率百分点</th><th>材料张数变化</th></tr>";
            if(baseline_>=0)for(int i:visibleOrder_){const auto& s=schemes_[i];const auto& b=schemes_[baseline_];html+="<tr><td>"+s.set.name.toHtmlEscaped()+"</td><td>"+value(100*(s.metrics.setArea/b.metrics.setArea-1),2)+"</td><td>"+signedNumber(s.metrics.piecesPerPanel-b.metrics.piecesPerPanel)+"</td><td>"+value(s.metrics.setPanelUtilization-b.metrics.setPanelUtilization,2)+"</td><td>"+signedNumber(s.metrics.panelsRequired-b.metrics.panelsRequired)+"</td></tr>";}
            html+="</table>";QTextDocument document;document.setHtml(html);document.print(&writer);
        } else {int index=selectedIndex();if(index<0)throw std::runtime_error("未选择方案。");auto d=fpc::drawing(calculatedMaterial_,schemes_[index],detail_->isChecked());CadCanvas output(std::make_shared<Document>(std::move(d)));QPainter painter(&writer);if(!painter.isActive())throw std::runtime_error("PDF 绘制失败。");output.renderDocument(painter,{0,0,double(writer.width()),double(writer.height())});}
    }
    QFile input(temporary);QSaveFile output(path);output.setDirectWriteFallback(false);if(!input.open(QIODevice::ReadOnly)||!output.open(QIODevice::WriteOnly))throw std::runtime_error("PDF 文件无法写入。");auto bytes=input.readAll();if(output.write(bytes)!=bytes.size()||!output.commit())throw std::runtime_error("PDF 原子保存失败。");
}
void MaterialPlanSection::exportResults() {
    if(!valid_||busy_||selectedIndex()<0){QMessageBox::warning(this,"先计算","输入已改变或没有可行方案，请重新计算。");return;}
    int format=format_->currentIndex();QString extension=format==0?"csv":format==2?"dxf":format==4?"dwg":"pdf";
    if(format==4&&io::DwgAdapter::findConverter(converter_).isEmpty()){QMessageBox::warning(this,"DWG 需要 ODA","请先配置 ODA，或输出 DXF / PDF。");return;}
    QString path=QFileDialog::getSaveFileName(this,"导出拼版材料方案","FPC-material."+extension,extension.toUpper()+" (*."+extension+")");if(path.isEmpty())return;if(!path.endsWith("."+extension,Qt::CaseInsensitive))path+="."+extension;
    try {
        if(format==0||format==1)saveReport(path,format==1);
        else if(format==3)writePdf(path,false);
        else io::DwgAdapter::save(fpc::drawing(calculatedMaterial_,schemes_[selectedIndex()],detail_->isChecked()),path,converter_);
        status_->setText("已导出："+path+"；PDF 图纸适合页面，非 1:1；报告数量为预计值。");
    }catch(const std::exception& e){QMessageBox::warning(this,"导出失败",errorText(e));}
}
void MaterialPlanSection::saveReport(QString path,bool pdf) {
    if(!valid_||busy_)throw std::runtime_error("结果无效或搜索未完成，请重新计算后导出。");
    if(pdf){writePdf(path,true);return;}
    QSaveFile file(path);file.setDirectWriteFallback(false);auto bytes=fpc::csv(calculatedMaterial_,schemes_,baseline_).toUtf8();
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())throw std::runtime_error("CSV 保存失败。");
}
PanelizationDialog::PanelizationDialog(const Document& source,const QSet<EntityId>& selection,QString converter,QWidget* parent)
    :QDialog(parent),original_(source),converter_(std::move(converter)) {
    setWindowTitle("拼版 · 单 PCS → ARRAY → SET");setObjectName("panelizationDialog");resize(1160,740);
    if(auto* screen=QGuiApplication::primaryScreen())resize(size().boundedTo(screen->availableGeometry().size()*0.92));
    auto* layout=new QVBoxLayout(this);
    auto* hint=new QLabel("框选单 PCS 区域后点击“自动拼版”：自动排除文字/标注并识别闭合外形。也可手动选择准确外形线并确认。多版本轮廓请核对候选。\n设置排布后点击“生成新拼版图纸”，结果在主窗口新标签打开；不是仅预览。仅平移/旋转，不镜像，尺寸均为 mm，原图不修改。\n“材料方案 / 利用率”页已并入本窗口：当前 SET 自动作为第一个比较方案，无需另开窗口。");
    hint->setWordWrap(true);layout->addWidget(hint);
    auto* modes=new QTabWidget;modes->setObjectName("panelModeTabs");layout->addWidget(modes,1);
    auto* panelPage=new QWidget;auto* panelPageLayout=new QVBoxLayout(panelPage);panelPageLayout->setContentsMargins(0,0,0,0);modes->addTab(panelPage,"拼版 SET");
    auto* splitter=new QSplitter;splitter->setChildrenCollapsible(false);panelPageLayout->addWidget(splitter,1);
    auto* tabs=new QTabWidget;tabs->setObjectName("panelTabs");tabs_=tabs;splitter->addWidget(tabs);
    auto* sourceWidget=new QWidget;auto* sourceLayout=new QVBoxLayout(sourceWidget);
    auto* copyCanvas=new PanelCanvas(std::make_shared<Document>(source));copyCanvas->grid=false;sourceCanvas_=copyCanvas;sourceCanvas_->setObjectName("panelSourceCanvas");sourceLayout->addWidget(sourceCanvas_,1);
    auto* confirm=new QPushButton("确认选中轮廓（支持分段直线 / 圆弧）");confirm->setObjectName("panelConfirmContour");sourceLayout->addWidget(confirm);connect(confirm,&QPushButton::clicked,this,[this]{confirmContour();});
    auto* cleanup=new QHBoxLayout;sourceLayout->addLayout(cleanup);
    auto* erase=new QPushButton("删除选中内容（仅此副本）");erase->setObjectName("panelDeleteSource");cleanup->addWidget(erase);connect(erase,&QPushButton::clicked,this,[this]{sourceCanvas_->deleteSelection();});
    auto* undo=new QPushButton("撤销清理");cleanup->addWidget(undo);connect(undo,&QPushButton::clicked,this,[this]{if(sourceCanvas_->document->undo())sourceCanvas_->synchronize();});
    auto* automatic=new QPushButton("自动拼版（识别所选区域 / 当前图）");automatic->setObjectName("panelAutomatic");sourceLayout->addWidget(automatic);connect(automatic,&QPushButton::clicked,this,[this]{automaticPanel();});
    tabs->addTab(sourceWidget,"原图外轮廓选择");
    auto* panelWidget=new QWidget;auto* panelLayout=new QVBoxLayout(panelWidget);
    auto* panelCanvas=new PanelCanvas(std::make_shared<Document>());panelCanvas->previewOnly=true;preview_=panelCanvas;preview_->grid=false;preview_->setObjectName("panelPreviewCanvas");panelLayout->addWidget(preview_,1);
    auto* bridgeButtons=new QHBoxLayout;panelLayout->addLayout(bridgeButtons);
    auto* pick=new QPushButton("手动筋：点两端");pick->setToolTip("在实际 PCS / 工艺边内点起点和终点，表格可精确调整。");pick->setCheckable(true);pick->setObjectName("panelPickBridge");bridgeButtons->addWidget(pick);
    auto start=std::make_shared<std::optional<QPointF>>();
    connect(pick,&QPushButton::toggled,this,[this,panelCanvas,start](bool active){start->reset();panelCanvas->point=active?std::function<void(QPointF)>([this,start](QPointF p){
        if(!*start){*start=p;status_->setText("已选起点，请点另一 PCS / 工艺边上的终点。");}
        else {addBridge({**start,p,bridgeWidth_->value()});start->reset();regenerate();}
    }):std::function<void(QPointF)>{};});
    auto* editable=new QPushButton("建议筋 → 手动编辑");editable->setObjectName("panelEditSuggestions");bridgeButtons->addWidget(editable);
    connect(editable,&QPushButton::clicked,this,[this]{if(!result_)return;auto list=result_->bridges;refreshing_=true;bridges_->setRowCount(0);
        for(const auto& e:list)if(e.points.size()==4)addBridge({(e.points[0]+e.points[3])/2,(e.points[1]+e.points[2])/2,geo::length(e.points[0],e.points[3])});
        autoBridges_->setChecked(false);refreshing_=false;regenerate();});
    tabs->addTab(panelWidget,"拼版预览 / 连接筋");
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setMinimumWidth(285);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);splitter->addWidget(scroll);
    auto* parameters=new QWidget;scroll->setWidget(parameters);auto* form=new QFormLayout(parameters);form->setRowWrapPolicy(QFormLayout::WrapLongRows);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    contours_=new QComboBox;contours_->setObjectName("panelContourCandidates");form->addRow("已确认外轮廓",contours_);
    rotation_=new QComboBox;rotation_->setObjectName("panelRotation");rotation_->addItems({"0°（原图方向）","90°","180°","270°"});form->addRow("整体旋转",rotation_);
    rows_=new QSpinBox;rows_->setObjectName("panelRows");rows_->setRange(1,100);rows_->setValue(2);columns_=new QSpinBox;columns_->setObjectName("panelColumns");columns_->setRange(1,100);columns_->setValue(2);
    form->addRow("行数",rows_);form->addRow("列数",columns_);
    fixed_=check("固定 SET / 大板尺寸","panelFixed");fit_=check("自动计算数量（非最优排料）","panelFitCount");form->addRow(fixed_);form->addRow(fit_);
    width_=number("panelWidth",200);height_=number("panelHeight",150);form->addRow("SET 宽 X (mm)",width_);form->addRow("SET 高 Y (mm)",height_);
    pitch_=check("使用步距（交错紧排）","panelExplicitPitch");form->addRow(pitch_);
    gapX_=number("panelGapX",2);gapY_=number("panelGapY",2);form->addRow("X 外接框净距 / 步距",gapX_);form->addRow("Y 外接框净距 / 步距",gapY_);
    gapX_->setToolTip("普通模式：两块 PCS 外接矩形之间的横向净距；步距模式：相邻列基点 ΔX。异形轮廓另做真实净距检查。");gapY_->setToolTip("普通模式：外接矩形纵向净距；步距模式：相邻行基点 ΔY。可用于 180° 交错紧排，但不可重叠。");
    alternate_=check("隔行旋转 180°（非镜像）","panelAlternateRows");form->addRow(alternate_);
    alternateColumns_=check("隔列旋转 180°（非镜像）","panelAlternateColumns");form->addRow(alternateColumns_);
    offset_=number("panelRowOffset",0,-1e6);minimum_=number("panelMinimumGap",0.2);form->addRow("隔行 X 偏移",offset_);form->addRow("真实轮廓最小净距",minimum_);
    left_=number("panelLeft",5);right_=number("panelRight",5);bottom_=number("panelBottom",5);top_=number("panelTop",5);
    form->addRow("左预留",left_);form->addRow("右预留",right_);form->addRow("下预留",bottom_);form->addRow("上预留",top_);
    rails_=check("整体四边工艺框架","panelRails",true);form->addRow(rails_);
    railWidth_=number("panelRailWidth",3,0.001);railClearance_=number("panelRailClearance",2);form->addRow("工艺边宽",railWidth_);form->addRow("PCS 到工艺边最小净距",railClearance_);
    autoBridges_=check("自动建议筋（需工程确认）","panelAutomaticBridges",true);form->addRow(autoBridges_);
    bridgeWidth_=number("panelBridgeWidth",1,0.001);form->addRow("新连接筋宽",bridgeWidth_);
    bridges_=new QTableWidget(0,5);bridges_->setObjectName("panelBridgeTable");bridges_->setHorizontalHeaderLabels({"起X","起Y","终X","终Y","筋宽"});bridges_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);bridges_->setMinimumHeight(130);form->addRow("手动筋（SET 坐标 mm）",bridges_);
    auto* manualButtons=new QHBoxLayout;auto* add=new QPushButton("增加");auto* remove=new QPushButton("删除选中筋");manualButtons->addWidget(add);manualButtons->addWidget(remove);form->addRow(manualButtons);
    connect(add,&QPushButton::clicked,this,[this]{addBridge({{0,0},{1,0},bridgeWidth_->value()});regenerate();});
    connect(remove,&QPushButton::clicked,this,[this]{int row=bridges_->currentRow();if(row>=0){refreshing_=true;bridges_->removeRow(row);refreshing_=false;regenerate();}});
    tolerance_=number("panelTolerance",0.001,0.000001);tolerance_->setMaximum(0.1);form->addRow("连接 / 融合弦高公差",tolerance_);
    precision_=new QSpinBox;precision_->setObjectName("panelPrecision");precision_->setRange(0,6);precision_->setValue(3);form->addRow("尺寸小数位",precision_);
    content_=new QComboBox;content_->setObjectName("panelContent");content_->addItems({"PCS + 工艺边 + 连接筋（设计图）","融合边界（含内部加工槽）"});form->addRow("图纸内容",content_);
    dimensions_=check("PCS / ARRAY / SET 长宽标注","panelDimensions",true);reference_=check("融合图附带 PCS 参考轮廓","panelReference");form->addRow(dimensions_);form->addRow(reference_);
    auto* formats=new QHBoxLayout;dxf_=check("DXF","panelExportDxf",true);pdf_=check("PDF","panelExportPdf",true);dwg_=check("DWG","panelExportDwg");formats->addWidget(dxf_);formats->addWidget(pdf_);formats->addWidget(dwg_);form->addRow("导出格式",formats);
    if(io::DwgAdapter::findConverter(converter_).isEmpty()){dwg_->setEnabled(false);dwg_->setToolTip("DWG 写出需先在 文件 → 配置 DWG 转换器 中配置 ODA。");}
    status_=new QLabel;status_->setObjectName("panelStatus");status_->setWordWrap(true);status_->setTextFormat(Qt::PlainText);panelPageLayout->addWidget(status_);
    previewTimer_=new QTimer(this);previewTimer_->setSingleShot(true);previewTimer_->setInterval(120);connect(previewTimer_,&QTimer::timeout,this,[this]{updatePreview();});
    auto* actions=new QHBoxLayout;panelPageLayout->addLayout(actions);generate_=new QPushButton("生成新拼版图纸");generate_->setObjectName("panelGenerate");generate_->setAutoDefault(false);generate_->setDefault(false);actions->addWidget(generate_);
    auto* exportButton=new QPushButton("导出所选格式 / 内容…");exportButton->setObjectName("panelExport");actions->addWidget(exportButton);auto* cancel=new QPushButton("关闭");actions->addWidget(cancel);
    for(auto* button:findChildren<QPushButton*>())button->setAutoDefault(false);
    connect(generate_,&QPushButton::clicked,this,&PanelizationDialog::accept);connect(cancel,&QPushButton::clicked,this,&QDialog::reject);connect(exportButton,&QPushButton::clicked,this,[this]{exportFiles();});
    for(auto* box:parameters->findChildren<QDoubleSpinBox*>())connect(box,&QDoubleSpinBox::valueChanged,this,[this]{regenerate();});
    for(auto* box:parameters->findChildren<QSpinBox*>())connect(box,&QSpinBox::valueChanged,this,[this]{regenerate();});
    for(auto* box:{fixed_,fit_,pitch_,alternate_,alternateColumns_,rails_,autoBridges_})connect(box,&QCheckBox::toggled,this,[this]{regenerate();});
    for(auto* box:{dimensions_,reference_})connect(box,&QCheckBox::toggled,this,[this]{updatePreview();});
    connect(content_,&QComboBox::currentIndexChanged,this,[this]{updatePreview();});connect(contours_,&QComboBox::currentIndexChanged,this,[this]{regenerate();});connect(rotation_,&QComboBox::currentIndexChanged,this,[this]{regenerate();});
    connect(bridges_,&QTableWidget::itemChanged,this,[this]{regenerate();});
    sourceCanvas_->selectIds(selection);
    QTimer::singleShot(0,this,[this]{sourceCanvas_->fitDrawing();if(!sourceCanvas_->selection.isEmpty())sourceCanvas_->centerOnSelection();automaticPanel();});
    splitter->setStretchFactor(0,3);splitter->setStretchFactor(1,2);splitter->setSizes({700,360});
    material_=new MaterialPlanSection(converter_,this);
    modes->addTab(material_,"材料方案 / 利用率");
    material_->onApply=[this,modes](const fpc::SetSpec& set){applyMaterial(set);modes->setCurrentIndex(0);tabs_->setCurrentIndex(1);};
    material_->onDrawing=[this](Document d){if(onDrawing)onDrawing(std::move(d));};
    connect(modes,&QTabWidget::currentChanged,this,[this](int index){if(index==1&&material_)material_->activate();});
    material_->setCurrentSet(std::nullopt);
}
void PanelizationDialog::reject(){if(material_)material_->cancelAndWait();QDialog::reject();}
PanelizationDialog::~PanelizationDialog(){if(material_)material_->cancelAndWait();}
void PanelizationDialog::saveMaterialReport(QString path,bool pdf){if(!material_)throw std::runtime_error("材料方案页尚未初始化。");material_->saveReport(std::move(path),pdf);}
void PanelizationDialog::applyMaterial(const engineering::fpc::SetSpec& set) {
    auto o=set.options;refreshing_=true;
    rows_->setValue(o.rows);columns_->setValue(o.columns);rotation_->setCurrentIndex(geo::normalizeAngle(o.rotation)/90);
    fixed_->setChecked(o.fixedPanel);fit_->setChecked(o.fitCount);
    if(o.fixedPanel){width_->setValue(o.panelWidth);height_->setValue(o.panelHeight);}
    pitch_->setChecked(o.explicitPitch);gapX_->setValue(o.explicitPitch?o.pitchX:o.gapX);gapY_->setValue(o.explicitPitch?o.pitchY:o.gapY);
    alternate_->setChecked(o.alternateRows);alternateColumns_->setChecked(o.alternateColumns);offset_->setValue(o.rowOffset);
    bridges_->setRowCount(0);for(const auto& b:o.bridges)addBridge(b);
    refreshing_=false;regenerate();
}
void PanelizationDialog::confirmContour() {
    try {
        automaticCandidates_=false;
        auto selected=sourceCanvas_->document->selected(sourceCanvas_->selection);
        auto candidates=contour::assemble(selected,tolerance_->value()/original_.mmPerUnit());
        setCandidates(candidates);
    } catch(const std::exception& e){result_.reset();refreshing_=true;candidates_.clear();contours_->clear();refreshing_=false;*preview_->document=Document{};preview_->synchronize();status_->setText(errorText(e));}
}
void PanelizationDialog::setCandidates(QVector<Entity> candidates) {
    double scale=original_.mmPerUnit();if(!std::isfinite(scale)||scale<=0)scale=1;
    for(auto& e:candidates){for(auto& p:e.points)p*=scale;e.radius*=scale;e.height*=scale;e.textBoxWidth*=scale;e.constantWidth=0;e.widths.clear();e.id=0;e.color={};}
    refreshing_=true;candidates_=std::move(candidates);contours_->clear();
    for(int i=0;i<candidates_.size();++i){auto b=contour::bounds(candidates_[i]);contours_->addItem(QString("轮廓 %1 · %2 × %3 mm · 面积 %4%5").arg(i+1).arg(value(b.width(),6),value(b.height(),6),value(candidates_[i].area(),3)).arg(candidates_[i].sourceAttributes=="panel-auto-approx"?" · 折线近似":""));}
    refreshing_=false;regenerate();if(result_)tabs_->setCurrentIndex(1);
}
void PanelizationDialog::automaticPanel() {
    try {
        automaticCandidates_=true;
        auto input=sourceCanvas_->document->selected(sourceCanvas_->selection);
        if(input.isEmpty())for(const auto& e:sourceCanvas_->document->entities)if(sourceCanvas_->document->layers.value(e.layer).visible)input.append(e);
        // Deterministic input order is essential for repeated near-coincident DXF projections.
        std::sort(input.begin(),input.end(),[](const Entity& a,const Entity& b){return a.id<b.id;});
        setCandidates(contour::detect(input,tolerance_->value()/original_.mmPerUnit()));
        if(result_&&candidates_.size()>1)status_->setText(status_->text()+"\n自动发现多个闭合轮廓，当前候选仅供预览；请选择与原单板一致的版本，不能按颜色认定最终版本。");
    } catch(const std::exception& e){result_.reset();candidates_.clear();refreshing_=true;contours_->clear();refreshing_=false;status_->setText(errorText(e));tabs_->setCurrentIndex(0);}
}
void PanelizationDialog::accept() {
    try {
        if(!result_)automaticPanel();
        if(!result_){QMessageBox::warning(this,"无法生成拼版",status_->text());return;}
        regenerate();if(!result_){QMessageBox::warning(this,"无法生成拼版",status_->text());return;}
        if(automaticCandidates_&&contours_->currentIndex()>=0) {
            const auto& selected=candidates_[contours_->currentIndex()];auto box=contour::bounds(selected);bool ambiguous=false;
            for(int i=0;i<candidates_.size();++i)if(i!=contours_->currentIndex()) {
                auto other=contour::bounds(candidates_[i]);
                if(geo::length(other.topLeft(),box.topLeft())<tolerance_->value()*4&&geo::length(other.bottomRight(),box.bottomRight())<tolerance_->value()*4
                   &&std::abs(selected.area()-candidates_[i].area())>tolerance_->value()*(selected.length()+candidates_[i].length()))ambiguous=true;
            }
            if((ambiguous||selected.sourceAttributes=="panel-auto-approx")&&QMessageBox::question(this,"确认自动识别外形",
                QString("原图存在同尺寸不同形状的候选，或当前候选经过折线区域合并。\n当前 %1 × %2 mm，面积 %3 mm²。\n请核对凹口、修改版及原图方向；自动识别不能判断最终制造版本。\n确定使用当前预览外形生成拼版？").arg(value(box.width(),6),value(box.height(),6),value(selected.area(),3)),
                QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
        }
        generated_=engineering::panel::drawing(*result_,options(),content_->currentIndex()==1,dimensions_->isChecked(),reference_->isChecked());
        QDialog::accept();
    } catch(const std::exception& e){QMessageBox::warning(this,"生成失败",errorText(e));}
}
engineering::panel::Options PanelizationDialog::options() const {
    engineering::panel::Options o;o.rows=rows_->value();o.columns=columns_->value();o.rotation=rotation_->currentIndex()*90;
    o.fixedPanel=fixed_->isChecked();o.fitCount=fit_->isChecked();o.panelWidth=width_->value();o.panelHeight=height_->value();
    o.explicitPitch=pitch_->isChecked();o.gapX=o.pitchX=gapX_->value();o.gapY=o.pitchY=gapY_->value();o.rowOffset=offset_->value();o.alternateRows=alternate_->isChecked();o.alternateColumns=alternateColumns_->isChecked();o.minimumGap=minimum_->value();
    o.left=left_->value();o.right=right_->value();o.bottom=bottom_->value();o.top=top_->value();o.rails=rails_->isChecked();o.railWidth=railWidth_->value();o.railClearance=railClearance_->value();
    o.automaticBridges=autoBridges_->isChecked();o.bridgeWidth=bridgeWidth_->value();o.tolerance=tolerance_->value();o.precision=precision_->value();
    for(int row=0;row<bridges_->rowCount();++row){double values[5];for(int col=0;col<5;++col){bool ok=false;auto* item=bridges_->item(row,col);values[col]=item?item->text().toDouble(&ok):0;if(!ok||!std::isfinite(values[col]))throw std::runtime_error("连接筋表格请输入有效数字。");}o.bridges.append({{values[0],values[1]},{values[2],values[3]},values[4]});}
    return o;
}
void PanelizationDialog::regenerate() {
    if(refreshing_)return;
    result_.reset();generated_.reset();
    if(contours_->currentIndex()<0||contours_->currentIndex()>=candidates_.size())return;
    try {
        auto o=options();rows_->setEnabled(!o.fitCount);columns_->setEnabled(!o.fitCount);width_->setEnabled(o.fixedPanel);height_->setEnabled(o.fixedPanel);
        result_=engineering::panel::generate(candidates_[contours_->currentIndex()],o);generate_->setEnabled(true);
        // Preview rendering builds a complete document; defer it while the user
        // is still typing so spin-box changes do not visibly jitter.
        previewTimer_->start();
        auto& r=*result_;status_->setText(QString("PCS %1 × %2 mm | ARRAY %3 × %4 mm | SET %5 × %6 mm | %7 行 × %8 列 = %9 PCS | 实际最小净距 %10 mm | PCS 面积利用率 %11%\n%12\n自动筋仅为几何建议，不包含铜层禁布、鼠咬孔、V-cut 或强度校核。PDF 适合页面，非 1:1。")
            .arg(value(r.pcsBounds.width(),6),value(r.pcsBounds.height(),6),value(r.arrayBounds.width(),6),value(r.arrayBounds.height(),6),value(r.panelBounds.width(),6),value(r.panelBounds.height(),6))
            .arg(r.rows).arg(r.columns).arg(r.pcs.size()).arg(value(r.minimumGap,6),value(r.utilization,2),r.warnings.join("\n")));
        if(material_)material_->setCurrentSet(fpc::fromPanel("当前 SET",r,options()));
    } catch(const std::exception& e){result_.reset();*preview_->document=Document{};preview_->synchronize();status_->setText(errorText(e));if(material_)material_->setCurrentSet(std::nullopt);}
}
Document PanelizationDialog::outputDocument() const {
    if(generated_)return *generated_;
    if(!result_)throw std::runtime_error("没有有效拼版。");
    return engineering::panel::drawing(*result_,options(),content_->currentIndex()==1,dimensions_->isChecked(),reference_->isChecked());
}
void PanelizationDialog::updatePreview() {
    if(!result_)return;
    *preview_->document=outputDocument();preview_->synchronize();preview_->fitDrawing();
}
void PanelizationDialog::addBridge(engineering::panel::Bridge b) {
    bool previous=refreshing_;refreshing_=true;int row=bridges_->rowCount();bridges_->insertRow(row);double values[]{b.a.x(),b.a.y(),b.b.x(),b.b.y(),b.width};
    for(int col=0;col<5;++col)bridges_->setItem(row,col,new QTableWidgetItem(value(values[col],6)));
    refreshing_=previous;
}
void PanelizationDialog::exportFiles() {
    if(!result_){QMessageBox::warning(this,"没有有效拼版","请先确认正确外轮廓并修正参数。");return;}
    QStringList formats;if(dxf_->isChecked())formats<<"dxf";if(pdf_->isChecked())formats<<"pdf";if(dwg_->isChecked())formats<<"dwg";
    if(formats.isEmpty()){QMessageBox::warning(this,"选择格式","请至少选择一种输出格式。");return;}
    auto file=QFileDialog::getSaveFileName(this,"选择输出文件基名（各格式共用）",original_.title+"-panel.dxf","拼版文件 (*.dxf *.pdf *.dwg)");if(file.isEmpty())return;
    QFileInfo info(file);QString base=info.dir().filePath(info.completeBaseName());QStringList destinations;
    for(const auto& format:formats){QString path=base+"."+format;if(!original_.path.isEmpty()&&QFileInfo(path).absoluteFilePath().compare(QFileInfo(original_.path).absoluteFilePath(),Qt::CaseInsensitive)==0){QMessageBox::warning(this,"保护原图","不能覆盖原始单板文件，请选择新的输出名称。");return;}destinations.append(path);}
    QStringList existing;for(const auto& path:destinations)if(QFileInfo::exists(path))existing<<path;
    if(!existing.isEmpty()&&QMessageBox::question(this,"确认覆盖输出文件",existing.join("\n"),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
    QStringList written;
    try {
        auto document=outputDocument();
        for(int i=0;i<formats.size();++i) {
            if(formats[i]=="pdf") {
                QTemporaryDir temp;if(!temp.isValid())throw std::runtime_error("无法创建 PDF 临时目录。");
                auto temporary=temp.filePath("panel.pdf");
                {QPdfWriter writer(temporary);writer.setPageSize(QPageSize(QPageSize::A4));writer.setResolution(300);QPainter painter(&writer);if(!painter.isActive())throw std::runtime_error("无法绘制 PDF。");preview_->renderDocument(painter,{0,0,double(writer.width()),double(writer.height())});painter.end();}
                QFile input(temporary);QSaveFile output(destinations[i]);output.setDirectWriteFallback(false);
                if(!input.open(QIODevice::ReadOnly)||!output.open(QIODevice::WriteOnly))throw std::runtime_error("无法写入 PDF 文件。");
                auto bytes=input.readAll();if(output.write(bytes)!=bytes.size()||!output.commit())throw std::runtime_error("PDF 原子保存失败。");
            } else io::DwgAdapter::save(document,destinations[i],converter_);
            written.append(destinations[i]);
        }
        QMessageBox::information(this,"已导出",written.join("\n")+"\nPDF 为适合页面输出，非 1:1。请在供应商 CAD 中复核几何与工艺要求。");
    } catch(const std::exception& e){QMessageBox::warning(this,"输出失败",errorText(e)+(written.isEmpty()?"":"\n已完成：\n"+written.join("\n")));}
}
}
