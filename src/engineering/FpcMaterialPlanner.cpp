#include "FpcMaterialPlanner.h"
#include "core/ContourOperations.h"
#include "core/Editing.h"
#include "core/SpatialIndex.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <QElapsedTimer>

namespace cad::engineering::fpc {
namespace {
void fail(QString text){throw std::runtime_error(text.toUtf8().constData());}
constexpr int maxSets=10000;
qint64 multiply(qint64 a,qint64 b) {
    if(a<0||b<0||(a&&b>std::numeric_limits<qint64>::max()/a))fail("数量计算溢出，请减少订单/材料数量。");
    return a*b;
}
qint64 floorCount(long double value) {
    if(!std::isfinite(value)||value<0||value>1e15L)fail("出货数量超出支持范围。");
    return qint64(std::floor(value+1e-9L));
}
void validateMaterial(const Material& m) {
    for(double v:{m.width,m.height,m.left,m.right,m.bottom,m.top,m.gapX,m.gapY,m.yieldPercent})if(!std::isfinite(v))fail("材料参数必须是有限数值。");
    if(m.width<=0||m.height<=0||std::max(m.width,m.height)>100000)fail("软板材料长宽需为正数且不超过 100000 mm。");
    if(std::min({m.left,m.right,m.bottom,m.top,m.gapX,m.gapY})<0)fail("材料边距及 SET 间距不能为负。");
    if(m.order<0||m.availablePanels<0||m.order>1000000000LL||m.availablePanels>1000000000LL)fail("订单/材料张数需为 0–10 亿。");
    if(m.yieldPercent<=0||m.yieldPercent>100)fail("预计良率需大于 0 且不超过 100%。");
    if(m.width-m.left-m.right<=0||m.height-m.bottom-m.top<=0)fail("材料尺寸小于四边预留，工作区域为空。");
}
void validateSet(const SetSpec& s) {
    for(double v:{s.width,s.height,s.unitArea,s.left,s.right,s.bottom,s.top})if(!std::isfinite(v))fail("SET 参数必须是有限数值。");
    if(s.width<=0||s.height<=0||s.units<1||s.units>100000||s.unitArea<0||std::min({s.left,s.right,s.bottom,s.top})<0)fail("SET 长宽、PCS 数量、面积或工艺边无效。");
    if(s.width-s.left-s.right<=0||s.height-s.bottom-s.top<=0)fail("SET 工艺边占满整个 SET。");
    if(s.unitArea*s.units>s.width*s.height+1e-6)fail("PCS 实际总面积超过 SET 面积，请核对单位、数量及面积。");
    if(s.unitArea*s.units>(s.width-s.left-s.right)*(s.height-s.bottom-s.top)+1e-6)fail("PCS 总面积超过去工艺边 SET 区域，请核对面积与边距。");
}
int fit(double available,double size,double gap) {
    double count=std::floor((available+gap+1e-8)/(size+gap));
    if(count>maxSets)fail("单张材料容纳超过 10000 SET，请核对尺寸单位或增大 SET。");
    return std::max(0,int(count));
}
Layout grid(QRectF region,double w,double h,double gx,double gy,int angle) {
    Layout layout;layout.method=angle?"整体 90° 网格":"原方向网格";
    int columns=fit(region.width(),w,gx),rows=fit(region.height(),h,gy);
    if(qint64(rows)*columns>maxSets)fail("单张材料超过 10000 SET。");
    for(int r=0;r<rows;++r)for(int c=0;c<columns;++c)layout.placements.append({{region.left()+c*(w+gx),region.top()+r*(h+gy),w,h},angle});
    return layout;
}
double used(int count,double size,double gap){return count?count*size+(count-1)*gap:0;}
QString quote(QString s){
    // Spreadsheet applications interpret a leading = + - @ (or tab/CR) as a
    // formula even inside quoted CSV fields; prefix it with an apostrophe and
    // keep the record on one physical line.
    s.replace('\r'," ");s.replace('\n'," ");
    if(!s.isEmpty()&&QString("=+-@\t").contains(s.at(0)))s.prepend('\'');
    s.replace('"',"\"\"");return '"'+s+'"';
}
Entity rectangle(QRectF box,QString layer) {Entity e;e.kind=Kind::Polyline;e.closed=true;e.layer=layer;e.points={box.topLeft(),box.topRight(),box.bottomRight(),box.bottomLeft()};return e;}
}
QRectF workArea(const Material& m) {validateMaterial(m);return {m.left,m.bottom,m.width-m.left-m.right,m.height-m.bottom-m.top};}
QVector<Layout> pack(const Material& m,double w,double h) {
    auto work=workArea(m);SetSpec spec;spec.width=w;spec.height=h;validateSet(spec);
    QVector<Layout> result;
    auto append=[&](Layout layout){if(layout.placements.isEmpty())return;for(const auto& existing:result)if(existing.placements.size()==layout.placements.size()&&existing.method==layout.method)return;result.append(std::move(layout));};
    append(grid(work,w,h,m.gapX,m.gapY,0));
    if(m.allowRotate)append(grid(work,h,w,m.gapX,m.gapY,90));
    if(m.allowRotate&&m.mixedOrientations&&std::abs(w-h)>1e-8) {
        // Two horizontal or vertical shelves, each internally a verified grid.
        // Enumerate cuts at occupied-row/column boundaries, not an arbitrary
        // continuous cut; no SET straddles a cut. Not global optimal nesting.
        for(bool vertical:{false,true})for(bool firstRotated:{false,true}) {
            double aw=firstRotated?h:w,ah=firstRotated?w:h,bw=firstRotated?w:h,bh=firstRotated?h:w;
            int count=fit(vertical?work.width():work.height(),vertical?aw:ah,vertical?m.gapX:m.gapY);
            if(count>250)fail("混排分区搜索超过 250 个切分位置，请增大 SET 或关闭分区混排。");
            Layout best;best.method=vertical?"分区混排（左右 0°/90°）":"分区混排（上下 0°/90°）";
            for(int n=1;n<=count;++n) {
                double extent=used(n,vertical?aw:ah,vertical?m.gapX:m.gapY);
                QRectF first=work,second=work;
                if(vertical){first.setWidth(extent);second.setLeft(work.left()+extent+m.gapX);}
                else{first.setHeight(extent);second.setTop(work.top()+extent+m.gapY);}
                auto a=grid(first,aw,ah,m.gapX,m.gapY,firstRotated?90:0);
                Layout combined=a;combined.method=best.method;
                if(second.width()>0&&second.height()>0){auto b=grid(second,bw,bh,m.gapX,m.gapY,firstRotated?0:90);combined.placements+=b.placements;}
                if(combined.placements.size()>maxSets)fail("混排超过 10000 SET。");
                bool has0=false,has90=false;for(const auto& p:combined.placements){has0|=p.rotation==0;has90|=p.rotation==90;}
                if(has0&&has90&&combined.placements.size()>best.placements.size())best=std::move(combined);
            }
            append(std::move(best));
        }
    }
    std::stable_sort(result.begin(),result.end(),[](const Layout& a,const Layout& b){return a.placements.size()>b.placements.size();});
    return result;
}
void validateLayout(const Material& m,const SetSpec& s,const Layout& layout) {
    auto work=workArea(m);validateSet(s);
    if(layout.placements.isEmpty()||layout.placements.size()>maxSets)fail("该方案不能放入材料或超过数量限制。");
    Document rectangles;
    for(int i=0;i<layout.placements.size();++i){const auto& box=layout.placements[i].box;if(!geo::finite(box.topLeft())||!geo::finite(box.bottomRight())||box.width()<=0||box.height()<=0)fail("SET 排布含无效坐标/尺寸。");auto e=rectangle(box,"0");e.id=i+1;rectangles.load(e);}
    SpatialIndex index;index.rebuild(rectangles);
    for(int i=0;i<layout.placements.size();++i) {
        auto p=layout.placements[i];if(p.rotation!=0&&p.rotation!=90)fail("SET 排布只允许 0° 或 90°，不允许镜像。");
        if(p.rotation&&!m.allowRotate)fail("材料方向禁止旋转，此方案无效。");
        double w=p.rotation?s.height:s.width,h=p.rotation?s.width:s.height;
        if(!geo::finite(p.box.topLeft())||!geo::finite(p.box.bottomRight())||std::abs(p.box.width()-w)>1e-6||std::abs(p.box.height()-h)>1e-6)fail("SET 位置/尺寸与实际方案不一致。");
        if(p.box.left()<work.left()-1e-6||p.box.right()>work.right()+1e-6||p.box.top()<work.top()-1e-6||p.box.bottom()>work.bottom()+1e-6)fail("SET 超出材料工作区域。");
        for(auto candidate:index.query(p.box.adjusted(-m.gapX-1e-6,-m.gapY-1e-6,m.gapX+1e-6,m.gapY+1e-6))) {
            int j=int(candidate)-1;if(j>=i)continue;
            const auto& b=layout.placements[j].box;
            bool xSeparated=p.box.right()+m.gapX<=b.left()+1e-6||b.right()+m.gapX<=p.box.left()+1e-6;
            bool ySeparated=p.box.bottom()+m.gapY<=b.top()+1e-6||b.bottom()+m.gapY<=p.box.top()+1e-6;
            if(!xSeparated&&!ySeparated)fail("SET 重叠或不满足材料上的 X/Y 加工间距。");
        }
    }
}
Metrics calculate(const Material& m,const SetSpec& s,const Layout& layout) {
    validateLayout(m,s,layout);Metrics x;auto work=workArea(m);
    x.panelArea=m.width*m.height;x.workArea=work.width()*work.height();x.setArea=s.width*s.height;x.setInnerArea=(s.width-s.left-s.right)*(s.height-s.bottom-s.top);
    x.setsPerPanel=layout.placements.size();x.piecesPerPanel=multiply(x.setsPerPanel,s.units);
    x.setPanelUtilization=100*x.setsPerPanel*x.setArea/x.panelArea;x.setWorkUtilization=100*x.setsPerPanel*x.setArea/x.workArea;
    x.setInnerPanelUtilization=100*x.setsPerPanel*x.setInnerArea/x.panelArea;
    x.trueAreaKnown=s.unitArea>0;
    if(x.trueAreaKnown){x.pcsSetUtilization=100*s.unitArea*s.units/x.setArea;x.pcsSetInnerUtilization=100*s.unitArea*s.units/x.setInnerArea;x.pcsPanelUtilization=100*s.unitArea*x.piecesPerPanel/x.panelArea;}
    long double yield=m.yieldPercent/100.L;
    auto shipment=[&](qint64 panels){return m.delivery==Delivery::Pieces?floorCount(multiply(panels,x.piecesPerPanel)*yield):multiply(floorCount(multiply(panels,x.setsPerPanel)*yield),s.units);};
    x.availableGrossPieces=multiply(m.availablePanels,x.piecesPerPanel);x.availableShipment=shipment(m.availablePanels);
    if(m.order) {
        long double goodNeeded=m.delivery==Delivery::CompleteSets?std::ceil((long double)m.order/s.units):m.order;
        long double grossPerPanel=m.delivery==Delivery::CompleteSets?x.setsPerPanel:x.piecesPerPanel;
        long double required=std::ceil(goodNeeded/(grossPerPanel*yield));
        if(required>1000000000LL)fail("预计需要超过 10 亿张材料，请核对良率与订单。");
        x.panelsRequired=qint64(required);
        // Rounding/floor yield can require a small adjustment; bound the search
        // so an invalid combination cannot spin indefinitely.
        for(int guard=0;shipment(x.panelsRequired)<m.order;++guard) {
            if(guard>1000000||x.panelsRequired>=1000000000LL)fail("出货张数无法在限制内满足订单，请核对良率与 PCS 数量。");
            ++x.panelsRequired;
        }
        for(int guard=0;x.panelsRequired>0&&shipment(x.panelsRequired-1)>=m.order;++guard) {
            if(guard>1000000)fail("出货张数无法在限制内满足订单，请核对良率与 PCS 数量。");
            --x.panelsRequired;
        }
    }
    x.requiredGrossPieces=multiply(x.panelsRequired,x.piecesPerPanel);x.expectedShipment=shipment(x.panelsRequired);x.excess=std::max(qint64(0),x.expectedShipment-m.order);
    return x;
}
QVector<Scheme> compare(const Material& m,const QVector<SetSpec>& sets) {
    QVector<Scheme> result;for(const auto& s:sets){validateSet(s);for(const auto& layout:pack(m,s.width,s.height))result.append({s,layout,calculate(m,s,layout)});}
    std::stable_sort(result.begin(),result.end(),[](const Scheme& a,const Scheme& b){if(a.metrics.piecesPerPanel!=b.metrics.piecesPerPanel)return a.metrics.piecesPerPanel>b.metrics.piecesPerPanel;return a.metrics.setPanelUtilization>b.metrics.setPanelUtilization;});
    return result;
}
SetSpec fromPanel(QString name,const panel::Result& r,const panel::Options& options) {
    SetSpec s;s.name=std::move(name);s.width=r.panelBounds.width();s.height=r.panelBounds.height();s.units=r.pcs.size();s.unitArea=r.source.area();s.left=options.left;s.right=options.right;s.bottom=options.bottom;s.top=options.top;
    s.hasGeometry=true;s.source=r.source;s.options=options;return s;
}
SearchResult search(const Material& m,const Entity& pcs,const panel::Options& process,const SearchOptions& settings,const std::function<bool()>& cancelled,const std::function<void(int,int)>& progress) {
    workArea(m);contour::validate(pcs,process.tolerance);
    if(settings.maxRows<1||settings.maxColumns<1||settings.maxRows>8||settings.maxColumns>8||settings.minPieces<1||settings.maxPieces<settings.minPieces||settings.maxPieces>64||settings.maxResults<1||settings.maxResults>50)fail("自动搜索范围无效：行列最多 8，PCS 最多 64，保留方案最多 50。");
    for(double v:{settings.minSetWidth,settings.minSetHeight,settings.maxSetWidth,settings.maxSetHeight})if(!std::isfinite(v)||v<0||v>100000)fail("SET 工艺尺寸上下限无效。");
    if(settings.maxSetWidth<=0||settings.maxSetHeight<=0||settings.minSetWidth>settings.maxSetWidth||settings.minSetHeight>settings.maxSetHeight)fail("SET 尺寸上限必须大于零且不小于下限。");
    if(process.rails&&std::min({process.left,process.right,process.bottom,process.top})+1e-9<process.railWidth+process.railClearance)fail("SET 工艺边预留不足，先在拼版界面调整工艺边宽度/净距。");
    SearchResult output;QVector<Scheme> proposals;QSet<QString> seen;QElapsedTimer timer;timer.start();
    QVector<int> angles=settings.allowPcsQuarterTurn?QVector<int>{0,90}:QVector<int>{int(geo::normalizeAngle(process.rotation))};
    for(int angle:angles)for(int rows=1;rows<=settings.maxRows;++rows)for(int cols=1;cols<=settings.maxColumns;++cols) {
        if(rows*cols<settings.minPieces||rows*cols>settings.maxPieces)continue;
        auto source=pcs;source.rotate({},angle);auto b=contour::bounds(source);
        auto propose=[&](panel::Options o,QString mode) {
            double dx=o.explicitPitch?o.pitchX:b.width()+o.gapX,dy=o.explicitPitch?o.pitchY:b.height()+o.gapY;
            SetSpec s;s.name=QString("%1x%2 PCS %3° %4").arg(rows).arg(cols).arg(angle).arg(mode);if(o.explicitPitch)s.name+=QString(" Δ%1/%2").arg(value(dx,3),value(dy,3));s.width=b.width()+(cols-1)*dx+o.left+o.right;s.height=b.height()+(rows-1)*dy+o.bottom+o.top;s.units=rows*cols;s.unitArea=pcs.area();s.left=o.left;s.right=o.right;s.bottom=o.bottom;s.top=o.top;
            s.hasGeometry=true;s.source=pcs;s.options=o;
            if(s.width+1e-6<settings.minSetWidth||s.height+1e-6<settings.minSetHeight||s.width>settings.maxSetWidth+1e-6||s.height>settings.maxSetHeight+1e-6)return;
            if(s.unitArea*s.units>(s.width-s.left-s.right)*(s.height-s.bottom-s.top)+1e-6)return;
            QString key=QString("%1:%2:%3:%4:%5").arg(rows).arg(cols).arg(angle).arg(o.alternateColumns).arg(o.alternateRows)+":"+value(dx,6)+":"+value(dy,6);
            if(cancelled&&cancelled())fail("软板方案搜索已取消。");
            if(timer.elapsed()>45000){output.truncated=true;return;}
            if(seen.contains(key))return;
            seen.insert(key);
            auto layouts=pack(m,s.width,s.height);if(!layouts.isEmpty())proposals.append({s,layouts.first(),calculate(m,s,layouts.first())});
        };
        auto o=process;o.rows=rows;o.columns=cols;o.rotation=angle;o.fixedPanel=false;o.fitCount=false;o.rowOffset=0;o.explicitPitch=false;o.alternateRows=false;o.alternateColumns=false;o.bridges.clear();
        if(process.explicitPitch){o.gapX=o.gapY=std::max(2.,process.minimumGap);}
        propose(o,"规则");
        if(settings.nested) {
            for(double fraction:{0.55,0.65,0.75,0.85,0.95}) {
                if(cols>1){auto tight=o;tight.explicitPitch=true;tight.alternateColumns=true;tight.pitchX=b.width()*fraction+o.minimumGap;tight.pitchY=b.height()+o.gapY;propose(tight,"交替列 180° 紧排");
                    if(rows>1){tight.alternateRows=true;propose(tight,"行列交替 180° 列紧排");}}
                if(rows>1){auto tight=o;tight.explicitPitch=true;tight.alternateRows=true;tight.pitchX=b.width()+o.gapX;tight.pitchY=b.height()*fraction+o.minimumGap;propose(tight,"交替行 180° 紧排");
                    if(cols>1){tight.alternateColumns=true;propose(tight,"行列交替 180° 行紧排");}}
            }
        }
    }
    std::stable_sort(proposals.begin(),proposals.end(),[](const Scheme& a,const Scheme& b){if(a.metrics.piecesPerPanel!=b.metrics.piecesPerPanel)return a.metrics.piecesPerPanel>b.metrics.piecesPerPanel;return a.metrics.setPanelUtilization>b.metrics.setPanelUtilization;});
    int limit=std::min(512,int(proposals.size()));QSet<QString> kept;
    // Establish two ordinary fallbacks, then test ranked tight layouts as well.
    // Do not exhaust the result quota on ordinary grids before trying nesting.
    QVector<int> validationOrder;
    for(int i=0;i<proposals.size()&&validationOrder.size()<2;++i)if(!proposals[i].set.options.explicitPitch)validationOrder.append(i);
    for(int i=0;i<proposals.size();++i)if(!validationOrder.contains(i))validationOrder.append(i);
    for(int i=0;i<limit;++i) {
        if(cancelled&&cancelled())fail("软板方案搜索已取消。");
        if(timer.elapsed()>45000){output.truncated=true;output.notes<<"达到 45 秒计算预算，保留已经完成几何验证的方案。";break;}
        if(progress)progress(i,limit);
        ++output.considered;
        const auto& p=proposals[validationOrder[i]];
        try {
            auto r=panel::generate(p.set.source,p.set.options,false);
            auto s=fromPanel(p.set.name,r,p.set.options);auto layouts=pack(m,s.width,s.height);
            if(layouts.isEmpty())continue;
            // Keep alternative orientations/mixed shelves for every admitted SET.
            for(const auto& layout:layouts) {
                auto metrics=calculate(m,s,layout);QString key=value(s.width,6)+":"+value(s.height,6)+":"+QString::number(s.units)+":"+layout.method;
                if(!kept.contains(key)){kept.insert(key);output.schemes.append({s,layout,metrics});}
            }
        }catch(const std::exception&){++output.rejectedGeometry;}
        if(output.schemes.size()>=settings.maxResults*3&&i>=20) {output.truncated=i+1<proposals.size();break;}
    }
    if(limit<proposals.size())output.truncated=true;
    if(cancelled&&cancelled())fail("软板方案搜索已取消。");
    std::stable_sort(output.schemes.begin(),output.schemes.end(),[](const Scheme& a,const Scheme& b){if(a.metrics.piecesPerPanel!=b.metrics.piecesPerPanel)return a.metrics.piecesPerPanel>b.metrics.piecesPerPanel;return a.metrics.setPanelUtilization>b.metrics.setPanelUtilization;});
    if(output.schemes.size()>settings.maxResults)output.schemes.resize(settings.maxResults);
    output.notes<<"只搜索有限行列、0/90°和交替行/列180°步距候选；几何重叠/净距验证后才发布。不是全局最优异形套排，胶片/补强方向及连筋强度需工程确认。";
    if(output.truncated)output.notes<<"达到候选/结果预算，未穷举全部方案。";
    if(!process.bridges.isEmpty())output.notes<<"自动方案未复用原手动连接筋；应用方案后需重新确认工艺筋。";
    return output;
}
Document drawing(const Material& m,const Scheme& s,bool detailed) {
    auto metrics=calculate(m,s.set,s.layout);Document d;d.title="FPC 材料排布 - "+s.set.name;d.units=4;
    for(auto [name,color]:QVector<QPair<QString,QString>>{{"FPC_MATERIAL","#ffce73"},{"FPC_WORK","#8196a6"},{"FPC_SET","#56d9bd"},{"FPC_PCS","#c4b5fd"},{"FPC_DIM","#8fc8e8"},{"FPC_INFO","#8fc8e8"}})d.layers.insert(name,Layer{name,QColor(color)});
    d.load(rectangle({0,0,m.width,m.height},"FPC_MATERIAL"));d.load(rectangle(workArea(m),"FPC_WORK"));
    QVector<Entity> pcs;
    if(detailed&&s.set.hasGeometry){pcs=panel::generate(s.set.source,s.set.options,false).pcs;if(qint64(pcs.size())*s.layout.placements.size()>2000||qint64(pcs.size())*s.layout.placements.size()*s.set.source.points.size()>200000)fail("详细材料图超过 2000 PCS / 200000 顶点，请关闭详细外形或缩小范围。");}
    for(int i=0;i<s.layout.placements.size();++i) {
        const auto& p=s.layout.placements[i];d.load(rectangle(p.box,"FPC_SET"));
        for(auto e:pcs){e=edit::clone(e);e.layer="FPC_PCS";if(p.rotation){e.rotate({},90);e.translate({s.set.height,0});}e.translate(p.box.topLeft());auto b=contour::bounds(e);if(b.left()<p.box.left()-1e-6||b.right()>p.box.right()+1e-6||b.top()<p.box.top()-1e-6||b.bottom()>p.box.bottom()+1e-6)fail("详细 PCS 超出对应 SET，禁止输出。");d.load(e);}
        Entity label;label.kind=Kind::Text;label.layer="FPC_INFO";label.height=std::clamp(std::min(p.box.width(),p.box.height())/15,1.,4.);label.points={p.box.center()};label.text=QString("%1 [%2°] %3PCS").arg(i+1).arg(p.rotation).arg(s.set.units);d.load(label);
    }
    double h=std::clamp(std::min(m.width,m.height)/80,2.,6.);Entity dim;dim.kind=Kind::Dimension;dim.layer="FPC_DIM";dim.dimensionFormat=1;dim.height=h;dim.text="PANEL <>";
    dim.points={{0,0},{m.width,0},{m.width/2,-h*5}};d.load(dim);dim.points={{m.width,0},{m.width,m.height},{m.width+h*5,m.height/2}};d.load(dim);
    auto info=[&](QString text,int line){Entity e;e.kind=Kind::Text;e.layer="FPC_INFO";e.height=h;e.points={{0,m.height+h*(3+line*2)}};e.text=std::move(text);d.load(e);};
    info(QString("FPC: %1 SET/Panel x %2 PCS/SET = %3 PCS/Panel; SET area utilization %4% (full material)").arg(metrics.setsPerPanel).arg(s.set.units).arg(metrics.piecesPerPanel).arg(value(metrics.setPanelUtilization,2)),0);
    info(QString("SET %1 x %2 mm; gap %3/%4 mm; rotation %5; %6").arg(value(s.set.width,3),value(s.set.height,3),value(m.gapX,3),value(m.gapY,3),m.allowRotate?"allowed":"forbidden",s.layout.method),1);
    info(QString("Order %1 PCS; expected yield %2%; required %3 Panels; expected shipment %4 PCS (estimate, not guaranteed)").arg(m.order).arg(value(m.yieldPercent,2)).arg(metrics.panelsRequired).arg(metrics.expectedShipment),2);
    d.markUnsaved();return d;
}
QString csv(const Material& m,const QVector<Scheme>& schemes,int baseline) {
    QString text(QChar(0xFEFF));
    text+="软板方案,大料宽mm,大料高mm,SET宽mm,SET高mm,PCS每SET,排布方式,SET每Panel,PCS每Panel,SET面积占大料%,SET面积占工作区%,去边SET面积占大料%,PCS实际面积占SET%,PCS实际面积占去边SET%,PCS实际面积占大料%,订单PCS,预计良率%,出货规则,所需大料张数,预计出货PCS,预计超交PCS,已有材料张数,已有材料预计出货PCS,基准方案,相对基准SET面积变化%,相对基准PCS每Panel变化,相对基准SET占料百分点,相对基准材料张数变化,大料左预留mm,大料右预留mm,大料下预留mm,大料上预留mm,SET在材料上X间距mm,SET在材料上Y间距mm,SET左工艺边mm,SET右工艺边mm,SET下工艺边mm,SET上工艺边mm,允许SET转90,PCS实际面积mm2,PCS来源\r\n";
    for(const auto& s:schemes){auto x=calculate(m,s.set,s.layout);QStringList cells{quote(s.set.name),value(m.width,6),value(m.height,6),value(s.set.width,6),value(s.set.height,6),QString::number(s.set.units),quote(s.layout.method),QString::number(x.setsPerPanel),QString::number(x.piecesPerPanel),value(x.setPanelUtilization,4),value(x.setWorkUtilization,4),value(x.setInnerPanelUtilization,4),x.trueAreaKnown?value(x.pcsSetUtilization,4):"未知",x.trueAreaKnown?value(x.pcsSetInnerUtilization,4):"未知",x.trueAreaKnown?value(x.pcsPanelUtilization,4):"未知",QString::number(m.order),value(m.yieldPercent,4),m.delivery==Delivery::Pieces?"按PCS预计良率":"整SET预计良率",QString::number(x.panelsRequired),QString::number(x.expectedShipment),QString::number(x.excess),QString::number(m.availablePanels),QString::number(x.availableShipment)};
        if(baseline>=0&&baseline<schemes.size()){const auto& b=schemes[baseline];auto bm=calculate(m,b.set,b.layout);cells<<quote(b.set.name)<<value(100*(x.setArea/bm.setArea-1),4)<<QString::number(x.piecesPerPanel-bm.piecesPerPanel)<<value(x.setPanelUtilization-bm.setPanelUtilization,4)<<QString::number(x.panelsRequired-bm.panelsRequired);}else cells<<""<<""<<""<<""<<"";
        cells<<value(m.left,6)<<value(m.right,6)<<value(m.bottom,6)<<value(m.top,6)<<value(m.gapX,6)<<value(m.gapY,6)<<value(s.set.left,6)<<value(s.set.right,6)<<value(s.set.bottom,6)<<value(s.set.top,6)<<(m.allowRotate?"是":"否")<<value(s.set.unitArea,6)<<(s.set.hasGeometry?"确认的CAD闭合轮廓":"手动尺寸/面积；未验证PCS内部几何");
        text+=cells.join(',')+"\r\n";}
    return text;
}
}
