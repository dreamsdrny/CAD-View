#include "Panelization.h"
#include "core/ContourOperations.h"
#include "core/Measure.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cad::engineering::panel {
namespace {
void fail(QString message){throw std::runtime_error(message.toUtf8().constData());}
Entity rectangle(QRectF b,QString layer) {
    Entity e;e.kind=Kind::Polyline;e.closed=true;e.layer=layer;
    e.points={b.topLeft(),b.topRight(),b.bottomRight(),b.bottomLeft()};return e;
}
Entity bridgeShape(const Bridge& bridge,double tolerance) {
    auto d=bridge.b-bridge.a;double length=geo::length(bridge.a,bridge.b);
    if(!std::isfinite(bridge.width)||bridge.width<=2*tolerance||length<=tolerance||!geo::finite(bridge.a)||!geo::finite(bridge.b))fail("连接筋宽度/端点无效，宽度需大于两倍融合公差。");
    auto u=d/length,n=QPointF(-u.y(),u.x())*bridge.width/2;
    auto a=bridge.a-u*(tolerance*4),b=bridge.b+u*(tolerance*4);
    Entity e;e.kind=Kind::Polyline;e.closed=true;e.layer="PANEL_BRIDGE";e.points={a+n,b+n,b-n,a-n};return e;
}
}
Result generate(const Entity& source,const Options& o,bool manufacturing) {
    if(o.rows<1||o.columns<1||o.rows>100||o.columns>100)fail("行列数需为 1–100。");
    for(double n:{o.gapX,o.gapY,o.pitchX,o.pitchY,o.rowOffset,o.minimumGap,o.panelWidth,o.panelHeight,o.left,o.right,o.bottom,o.top,o.railWidth,o.railClearance,o.bridgeWidth,o.tolerance})
        if(!std::isfinite(n)||std::abs(n)>1e6)fail("拼版参数不是有限有效数值或超过限制。");
    if(o.gapX<0||o.gapY<0||o.minimumGap<0||std::min({o.left,o.right,o.bottom,o.top,o.railClearance})<0)fail("间距/四边预留不能为负数；交错紧排请使用步距模式。");
    if(o.rotation%90!=0)fail("第一版仅支持 0/90/180/270° 旋转，不支持镜像。");
    if(o.rails&&(o.railWidth<=0||o.railWidth<=2*o.tolerance))fail("工艺边宽度需大于两倍融合公差。");
    if(source.readOnly)fail("不能使用只读预览作为 PCS 模板。");
    if(o.tolerance<1e-6||o.tolerance>0.1)fail("融合公差需在 0.000001–0.1 mm 范围内。");
    contour::validate(source,o.tolerance);
    Result r;r.source=source;r.source.id=0;r.source.sourceKey.clear();r.source.sourceAttributes.clear();
    r.source.layer="PANEL_PCS";r.source.color={};r.source.widths.clear();r.source.constantWidth=0;
    auto original=contour::bounds(r.source);r.source.translate(-original.topLeft());r.pcsBounds=contour::bounds(r.source);
    auto oriented=r.source;oriented.rotate({},o.rotation);auto box=contour::bounds(oriented);oriented.translate(-box.topLeft());box=contour::bounds(oriented);
    double dx=o.explicitPitch?o.pitchX:box.width()+o.gapX,dy=o.explicitPitch?o.pitchY:box.height()+o.gapY;
    if(dx<=o.tolerance||dy<=o.tolerance)fail("阵列步距必须为正数。");
    r.rows=o.rows;r.columns=o.columns;
    if(o.fitCount&&!o.fixedPanel)fail("自动数量需启用固定大板。");
    if(o.fixedPanel) {
        double availableW=o.panelWidth-o.left-o.right,availableH=o.panelHeight-o.bottom-o.top;
        if(availableW<=0||availableH<=0)fail("固定大板尺寸小于四边预留。");
        if(o.fitCount) {
            r.rows=int(std::clamp(std::floor((availableH-box.height())/dy+1+1e-9),0.,100.));
            double offset=r.rows>1?std::abs(o.rowOffset):0;
            r.columns=int(std::clamp(std::floor((availableW-box.width()-offset)/dx+1+1e-9),0.,100.));
            if(r.rows<1||r.columns<1)fail("固定大板中无法容纳一个 PCS（已扣除边距和交错偏移）。");
        }
    }
    if(r.rows*r.columns>400)fail("第一版单次拼版最多 400 PCS，请减小行列数。");
    if(qint64(r.rows)*r.columns*source.points.size()>200000)fail("拼版超过 200000 个几何顶点，请减少 PCS 数量或整理外轮廓。");
    for(int row=0;row<r.rows;++row)for(int col=0;col<r.columns;++col) {
        auto e=oriented;
        if((o.alternateRows&&(row%2))!=(o.alternateColumns&&(col%2))){e.rotate({},180);e.translate(-contour::bounds(e).topLeft());}
        e.translate({col*dx+(row%2?o.rowOffset:0),row*dy});r.pcs.append(e);
    }
    QVector<QPointF> extrema;
    for(const auto& e:r.pcs){auto b=contour::bounds(e);extrema+={b.topLeft(),b.bottomRight()};}
    auto array=geo::bounds(extrema);
    double width=o.fixedPanel?o.panelWidth:array.width()+o.left+o.right;
    double height=o.fixedPanel?o.panelHeight:array.height()+o.bottom+o.top;
    if(width+o.tolerance<array.width()+o.left+o.right||height+o.tolerance<array.height()+o.bottom+o.top)fail("PCS 阵列超出固定大板或侵入四边预留，请减少数量/步距。");
    QPointF position(o.left+(width-o.left-o.right-array.width())/2,o.bottom+(height-o.bottom-o.top-array.height())/2);
    for(auto& e:r.pcs)e.translate(position-array.topLeft());
    r.arrayBounds={position,array.size()};r.panelBounds={0,0,width,height};
    qsizetype sampledPoints=0;
    for(const auto& e:r.pcs){auto path=contour::region(e,o.tolerance);sampledPoints+=path.elementCount();if(sampledPoints>200000)fail("融合预览超过 200000 个离散顶点，请减少 PCS 数量或调整弦高公差。");r.regions.append(path);}
    r.minimumGap=std::numeric_limits<double>::infinity();
    qint64 pairBudget=0;
    for(int i=0;i<r.pcs.size();++i)for(int j=i+1;j<r.pcs.size();++j) {
        auto a=contour::bounds(r.pcs[i]),b=contour::bounds(r.pcs[j]);
        double lower=geo::boxGap(a,b);
        if(lower>r.minimumGap&&lower>=o.minimumGap)continue;
        pairBudget+=qint64(r.pcs[i].points.size())*r.pcs[j].points.size();
        if(pairBudget>10000000)fail("精确间距检查超过计算预算，请缩小拼版或简化外轮廓。");
        if(r.regions[i].intersects(r.regions[j]))fail(QString("PCS %1 与 %2 重叠或相接，请调整旋转、步距及交错偏移。").arg(i+1).arg(j+1));
        auto gap=measure::gap(r.pcs[i],r.pcs[j]);if(!gap)fail("无法计算 PCS 真实间距。");
        r.minimumGap=std::min(r.minimumGap,gap->edgeDistance);
        if(gap->edgeDistance+1e-8<o.minimumGap)fail(QString("PCS %1 / %2 实际净距 %3 mm，小于要求 %4 mm。").arg(i+1).arg(j+1).arg(value(gap->edgeDistance,6),value(o.minimumGap,6)));
    }
    if(r.pcs.size()==1)r.minimumGap=0;
    r.utilization=100*r.source.area()*r.pcs.size()/(width*height);
    if(!manufacturing)return r; // Planning ranks validated PCS layouts, without expensive rail/bridge union.
    if(o.rails) {
        double rw=o.railWidth;
        for(double margin:{o.left,o.right,o.bottom,o.top})if(margin+o.tolerance<rw+o.railClearance)fail("四边预留必须覆盖工艺边宽度 + PCS 到工艺边净距。");
        if(width<=2*rw||height<=2*rw)fail("工艺边宽度超过大板尺寸。");
        r.rails={rectangle({0,0,width,rw},"PANEL_RAIL"),rectangle({0,height-rw,width,rw},"PANEL_RAIL"),
                 rectangle({0,rw,rw,height-2*rw},"PANEL_RAIL"),rectangle({width-rw,rw,rw,height-2*rw},"PANEL_RAIL")};
    }
    QVector<Entity> shapes=r.pcs;shapes+=r.rails;
    QVector<QPainterPath> regions=r.regions;
    for(const auto& e:r.rails)regions.append(contour::region(e,o.tolerance));
    QVector<int> parent(shapes.size());for(int i=0;i<parent.size();++i)parent[i]=i;
    auto root=[&](int i){while(parent[i]!=i)i=parent[i];return i;};
    auto unite=[&](int a,int b){parent[root(a)]=root(b);};
    // Four rail pieces form a frame even when they touch only at shared edges.
    for(int i=r.pcs.size()+1;i<shapes.size();++i)unite(r.pcs.size(),i);
    auto addBridge=[&](Bridge bridge,bool automatic) {
        auto e=bridgeShape(bridge,o.tolerance);auto path=contour::region(e,o.tolerance);
        if(!r.panelBounds.adjusted(-o.tolerance,-o.tolerance,o.tolerance,o.tolerance).contains(path.boundingRect()))fail("连接筋超出 SET 外边界，请调整端点或筋宽。");
        QVector<int> contacts;bool endpointA=false,endpointB=false;
        for(int i=0;i<shapes.size();++i) {
            auto onMaterial=[&](QPointF p){if(regions[i].contains(p))return true;Entity probe;probe.kind=Kind::Circle;probe.points={p};probe.radius=o.tolerance*2;return regions[i].intersects(contour::region(probe,o.tolerance/4));};
            endpointA|=onMaterial(bridge.a);
            endpointB|=onMaterial(bridge.b);
            if(regions[i].intersects(path))contacts.append(i);
        }
        if(contacts.size()<2||(!automatic&&(!endpointA||!endpointB)))fail("连接筋没有有效连接两个 PCS / 工艺边，请把端点放到实际轮廓内或边界上。");
        bool distinct=false;for(int i:contacts)if(root(i)!=root(contacts.first()))distinct=true;
        if(!automatic&&!distinct)r.warnings.append("一条手动连接筋连接到已连通区域，请确认是否需要。");
        for(int i:contacts)unite(contacts.first(),i);
        r.bridges.append(e);return true;
    };
    if(o.bridges.size()>1000)fail("手动连接筋超过 1000 条，请减少数量。");
    for(const auto& bridge:o.bridges)addBridge(bridge,false);
    if(o.automaticBridges&&o.rails) {
        struct Candidate{int a,b;measure::Gap gap;};QVector<Candidate> candidates;
        for(int i=0;i<shapes.size();++i)for(int j=i+1;j<shapes.size();++j) {
            if(i>=r.pcs.size()&&j>=r.pcs.size())continue;
            // Minimum-distance spanning tree only needs neighbouring PCS; all rails stay candidates.
            if(j<r.pcs.size()) {
                int rowI=i/r.columns,colI=i%r.columns,rowJ=j/r.columns,colJ=j%r.columns;
                if(std::abs(rowI-rowJ)>1||std::abs(colI-colJ)>1)continue;
            }
            pairBudget+=qint64(shapes[i].points.size())*shapes[j].points.size();
            if(pairBudget>10000000)fail("连接筋建议超过计算预算，请减少数量/简化轮廓或手动放置连接筋。");
            if(auto gap=measure::gap(shapes[i],shapes[j]))candidates.append({i,j,*gap});
        }
        std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){return a.gap.edgeDistance<b.gap.edgeDistance;});
        for(const auto& c:candidates)if(root(c.a)!=root(c.b))addBridge({c.gap.a,c.gap.b,o.bridgeWidth},true);
    }
    if(o.rails)for(int i=0;i<r.pcs.size();++i)if(root(i)!=root(r.pcs.size())) {r.warnings.append("存在未连接工艺边的 PCS；融合图不代表一体框架，请增加连接筋。");break;}
    QPainterPath material;
    for(const auto& path:regions)material=material.isEmpty()?path:material.united(path);
    for(const auto& e:r.bridges)material=material.united(contour::region(e,o.tolerance));
    r.fused=contour::boundaries(material,o.tolerance);
    for(auto& e:r.fused)e.layer="PANEL_FUSED";
    r.utilization=100*r.source.area()*r.pcs.size()/(width*height);
    r.warnings.append(QString("融合边界为折线近似，圆弧弦高公差 %1 mm；PCS 参考外形保留真实圆弧。").arg(value(o.tolerance,6)));
    return r;
}
Document drawing(const Result& r,const Options& o,bool fused,bool dimensions,bool reference) {
    Document d;d.units=4;d.title="PCB-FPC 拼版";
    for(auto [name,color]:QVector<QPair<QString,QString>>{{"PANEL_PCS","#56d9bd"},{"PANEL_RAIL","#ffce73"},{"PANEL_BRIDGE","#e59cff"},{"PANEL_FUSED","#56d9bd"},
            {"PANEL_REFERENCE","#8196a6"},{"PANEL_FRAME","#e0bd52"},{"PANEL_DIM_PCS","#c4b5fd"},{"PANEL_DIM_ARRAY","#8fc8e8"},{"PANEL_DIM_SET","#ffce73"},{"PANEL_INFO","#8fc8e8"}})
        d.layers.insert(name,Layer{name,QColor(color)});
    auto add=[&](const QVector<Entity>& list){for(auto e:list){e.id=0;e.sourceKey.clear();e.sourceAttributes.clear();d.load(e);}};
    if(fused)add(r.fused);else{add(r.pcs);add(r.rails);add(r.bridges);}
    if(!o.rails)d.load(rectangle(r.panelBounds,"PANEL_FRAME"));
    if(fused&&reference){auto list=r.pcs;for(auto& e:list)e.layer="PANEL_REFERENCE";add(list);}
    if(dimensions) {
        double h=std::clamp(std::min(r.panelBounds.width(),r.panelBounds.height())/30,1.2,3.0);
        auto dims=[&](QRectF box,QString layer,QString prefix,double offset,bool verticalOnLeft=false) {
            Entity x;x.kind=Kind::Dimension;x.layer=layer;x.dimensionFormat=1;x.dimensionPrecision=o.precision;x.height=h;x.text=prefix+" <>";
            x.points={box.topLeft(),box.topRight(),box.topLeft()+QPointF(box.width()/2,-offset)};d.load(x);
            x.points=verticalOnLeft?QVector<QPointF>{box.topLeft(),box.bottomLeft(),box.topLeft()+QPointF(-offset,box.height()/2)}
                                   :QVector<QPointF>{box.topRight(),box.bottomRight(),box.topRight()+QPointF(offset,box.height()/2)};d.load(x);
        };
        dims(contour::bounds(r.pcs.first()),"PANEL_DIM_PCS","PCS",h*3,true);
        dims(r.arrayBounds,"PANEL_DIM_ARRAY","ARRAY",h*6);
        dims(r.panelBounds,"PANEL_DIM_SET","SET",h*10);
        Entity info;info.kind=Kind::Text;info.layer="PANEL_INFO";info.height=h;info.points={{0,r.panelBounds.height()+h*3}};
        info.text=QString("%1 ROW x %2 COL = %3 PCS / SET; mm; min gap %4; PCS area utilization %5%; ROT %6 deg%7; NO MIRROR")
            .arg(r.rows).arg(r.columns).arg(r.pcs.size()).arg(value(r.minimumGap,3),value(r.utilization,2)).arg(o.rotation).arg(o.alternateRows||o.alternateColumns?" / alternating +180":"");d.load(info);
    }
    d.currentLayer=fused?"PANEL_FUSED":"PANEL_PCS";d.markUnsaved();return d;
}
}
