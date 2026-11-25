#include "CadCanvas.h"
#include "branding/AppBrand.h"
#include "core/Editing.h"
#include "core/TextLayout.h"
#include "core/DimensionLayout.h"
#include <QPainter>
#include <QPainterPathStroker>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QInputDialog>
#include <QLineEdit>
#include <cmath>
#include <algorithm>
#include <limits>

namespace cad::ui {
namespace {
QPainterPath textHitArea(const Entity& e,const QPainterPath& glyphs) {
    // Hit-test the oriented label rectangle, including spaces and holes inside glyphs.
    // Undo rotation before computing bounds to avoid an oversized box for diagonal text.
    QTransform rotation;rotation.translate(e.points[0].x(),e.points[0].y());rotation.rotate(e.rotation);rotation.translate(-e.points[0].x(),-e.points[0].y());
    QPainterPath rectangle;rectangle.addRect(rotation.inverted().map(glyphs).boundingRect());
    return rotation.map(rectangle);
}
}
CadCanvas::CadCanvas(std::shared_ptr<Document> d,QWidget* parent):QWidget(parent),document(std::move(d)) {
    qRegisterMetaType<QVector<EntityId>>();
    setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);setMinimumSize(320,240);
    setToolTip("方向键移动图纸 · Shift 加速 · Ctrl 微调 · + / - 缩放 · 滚轮缩放 · 中键拖动平移 · Home 显示全部");
    synchronize();
}
QTransform CadCanvas::viewTransform() const {
    QTransform t;t.translate(width()/2.,height()/2.);t.scale(scale_,-scale_);t.translate(-center_.x(),-center_.y());return t;
}
QPointF CadCanvas::modelAt(QPointF p) const {return viewTransform().inverted().map(p);}
QPointF CadCanvas::screenAt(QPointF p) const {return viewTransform().map(p);}
QPainterPath CadCanvas::pathFor(const Entity& e, const Document* document) {
    QPainterPath path;if(e.points.isEmpty())return path;
    if(e.kind==Kind::Hatch) {
        path.setFillRule(Qt::OddEvenFill);
        for(qsizetype i=0;i<e.hatchLoops.size();++i) {
            auto begin=e.hatchLoops[i],end=i+1<e.hatchLoops.size()?e.hatchLoops[i+1]:e.points.size();
            Entity loop;loop.kind=Kind::Polyline;loop.closed=true;loop.points=e.points.mid(begin,end-begin);loop.bulges=e.bulges.mid(begin,end-begin);
            auto outline=loop.outline(0.001);if(outline.isEmpty())continue;
            path.moveTo(outline.first());for(qsizetype j=1;j<outline.size();++j)path.lineTo(outline[j]);path.closeSubpath();
        }
        return path;
    }
    if(e.kind==Kind::Text)return text::path(e);
    if(e.kind==Kind::Dimension) {
        if(e.dimensionFormat==1){for(const auto& part:dimension::graphics(e,document?document->units:4))path.addPath(pathFor(part,document));return path;}
        auto a=e.points[0],b=e.points[1],c=e.points[2],delta=c-geo::project(c,a,b,false),aa=a+delta,bb=b+delta;
        auto line=[&](QPointF p,QPointF q){path.moveTo(p);path.lineTo(q);};line(a,aa);line(b,bb);line(aa,bb);
        double len=geo::length(a,b);if(len>geo::epsilon){auto u=(b-a)/len;double size=std::min(1.,len/10);
            for(auto p:{aa,bb})for(double side:{-1.,1.})line(p,p+u*((p==aa?1:-1)*size)+QPointF(-u.y(),u.x())*(side*size/3));}
        double h=e.height>geo::epsilon?e.height:std::clamp(len*0.1,0.3,3.0);
        Entity label;label.kind=Kind::Text;label.points={(aa+bb)/2+QPointF(0,h*0.4)};label.text=e.text;label.text.replace("<>",document?document->dualLength(len):value(len,3));label.height=h;path.addPath(pathFor(label));return path;
    }
    if(e.kind==Kind::Circle){path.addEllipse(e.points[0],e.radius,e.radius);return path;}
    if(e.maximumWidth()>0) {
        QPainterPath filled;filled.setFillRule(Qt::WindingFill);
        qsizetype count=e.closed?e.points.size():e.points.size()-1;
        for(qsizetype i=0;i<count;++i) {
            auto a=e.points[i],b=e.points[(i+1)%e.points.size()];
            auto arc=geo::bulgeArc(a,b,e.bulges.value(i));
            auto pts=arc?geo::arcPoints(*arc,0.001):QVector<QPointF>{a,b};
            auto widths=e.constantWidth>0?qMakePair(e.constantWidth,e.constantWidth):e.widths.value(i);
            QPainterPath center;center.moveTo(a);for(qsizetype j=1;j<pts.size();++j)center.lineTo(pts[j]);
            if(widths.first==0&&widths.second==0){filled.addPath(center);continue;}
            if(widths.first==widths.second) {
                QPainterPathStroker stroker;stroker.setWidth(widths.first);stroker.setCapStyle(Qt::FlatCap);stroker.setJoinStyle(Qt::MiterJoin);
                filled.addPath(stroker.createStroke(center));
            } else {
                QVector<QPointF> left,right;
                for(qsizetype j=0;j<pts.size();++j) {
                    QPointF normal;
                    if(arc)normal=(pts[j]-arc->center)/arc->radius*(arc->sweep>0?-1:1);
                    else {auto delta=b-a;double length=geo::length(a,b);normal=length>0?QPointF(-delta.y(),delta.x())/length:QPointF{};}
                    double half=(widths.first+(widths.second-widths.first)*j/(pts.size()-1))/2;
                    left.append(pts[j]+normal*half);right.append(pts[j]-normal*half);
                }
                QPainterPath strip;strip.moveTo(left.first());for(qsizetype j=1;j<left.size();++j)strip.lineTo(left[j]);
                for(auto it=right.crbegin();it!=right.crend();++it)strip.lineTo(*it);
                strip.closeSubpath();filled.addPath(strip);
            }
        }
        return filled;
    }
    auto points=e.outline(0.01);if(!points.isEmpty()){path.moveTo(points[0]);for(qsizetype i=1;i<points.size();++i)path.lineTo(points[i]);if(e.isClosed())path.closeSubpath();}return path;
}
void CadCanvas::synchronize() {
    // Keep geometry snapshots, but defer path generation until an entity is visible,
    // hit-tested or exported. Large drawings often contain mostly off-screen objects.
    for(auto it=cache_.begin();it!=cache_.end();)if(!document->entities.contains(it.key()))it=cache_.erase(it);else ++it;
    for(auto entityIt=document->entities.cbegin();entityIt!=document->entities.cend();++entityIt) {
        const auto& e=entityIt.value();
        auto cached=cache_.find(e.id);
        if(cached==cache_.end())cache_.insert(e.id,{e,{},false});
        else if(cached->entity!=e)*cached={e,{},false};
    }
    for(auto it=selection.begin();it!=selection.end();)if(!document->entities.contains(*it)||!document->layers.value(document->entities.value(*it).layer).visible)it=selection.erase(it);else ++it;
    index_.rebuild(*document);update();emit selectionChanged();
}
const QPainterPath& CadCanvas::cachedPath(EntityId id) {
    auto it=cache_.find(id);
    if(it==cache_.end()) {
        auto entity=document->entities.constFind(id);
        if(entity==document->entities.constEnd()){static const QPainterPath empty;return empty;}
        it=cache_.insert(id,{entity.value(),{},false});
    }
    // Dimensions re-render every paint so the mm/mil label always matches current units.
    if(it->entity.kind==Kind::Dimension){it->path=pathFor(it->entity,document.get());return it->path;}
    if(!it->pathReady){it->path=pathFor(it->entity,document.get());it->pathReady=true;}
    return it->path;
}
void CadCanvas::fitDrawing() {
    auto b=document->bounds();center_=b.center();scale_=std::clamp(std::min(width()/std::max(10.,b.width()),height()/std::max(10.,b.height()))*0.85,1e-5,1e5);update();
}
void CadCanvas::panByPixels(QPointF delta) {
    if(delta.isNull())return;
    // Screen Y is inverted, so moving the sheet down increases center_.y.
    center_+=QPointF(-delta.x()/scale_,delta.y()/scale_);
    refreshCursorFromMouse();update();
}
void CadCanvas::zoomBy(double factor) {
    if(!(factor>0))return;
    auto anchor=hasMouseScreen_?lastMouseScreen_:QPointF(width()/2.,height()/2.);
    auto before=modelAt(anchor);
    scale_=std::clamp(scale_*factor,1e-5,1e5);
    center_+=before-modelAt(anchor);
    refreshCursorFromMouse();update();
}
void CadCanvas::refreshCursorFromMouse() {
    if(!hasCursor_||!hasMouseScreen_)return;
    cursor_=constrained(modelAt(lastMouseScreen_));emit coordinates(cursor_);
}
QString CadCanvas::toolName(Tool tool) {
    switch(tool){case Tool::Select:return "选择";case Tool::Line:return "直线";case Tool::Polyline:return "多段线";
    case Tool::Rectangle:return "矩形";case Tool::Circle:return "圆";case Tool::Arc:return "圆弧";case Tool::Ellipse:return "椭圆";
    case Tool::Text:return "文字";case Tool::Move:return "移动";case Tool::Copy:return "复制";case Tool::Mirror:return "镜像";
    case Tool::Distance:return "距离";case Tool::Gap:return "对象间距";case Tool::Horizontal:return "水平距离";case Tool::Vertical:return "垂直距离";
    case Tool::Continuous:return "连续测量";case Tool::Angle:return "角度";case Tool::Coordinate:return "坐标";
    case Tool::Dimension:return "对齐标注";case Tool::Zoom:return "框选缩放";}return {};
}
void CadCanvas::setTool(Tool tool) {
    if((tool==Tool::Move||tool==Tool::Copy||tool==Tool::Mirror)&&document->selected(selection,true).isEmpty()){emit message("请先选择未锁定图层中的对象。");return;}
    tool_=tool;points_.clear();snapPoint_.reset();dragging_=false;
    gapFirst_=0;gapHover_=0;gapPreview_.reset();
    setCursor(tool==Tool::Select?Qt::ArrowCursor:Qt::CrossCursor);emit toolChanged();
    QString hint=" · 指定点，Esc 结束；可在命令栏输入精确坐标";
    if(tool==Tool::Select)hint=" · 单击 / 左→右窗口选择 / 右→左交叉选择 / Shift 多选";
    if(tool==Tool::Polyline)hint=" · 依次指定顶点；Enter 完成，C 闭合";
    if(tool==Tool::Arc)hint=" · 圆心 → 起点 → 终点（逆时针）";
    if(tool==Tool::Ellipse)hint=" · 圆心 → 长轴端点 → 短轴点";
    if(tool==Tool::Angle)hint=" · 顶点 → 第一条射线 → 第二条射线";
    if(tool==Tool::Gap)hint=" · 依次拾取两个对象：直线 → 中心到中心；圆 / 圆弧 → 边到边；线宽自动计入，并生成双单位标注";
    emit message(toolName(tool)+hint);update();
}
QPointF CadCanvas::constrained(QPointF p) {
    snapPoint_.reset();
    // Selection, zoom, entity-gap measuring and panning do not accept snapped drawing points.
    if(tool_==Tool::Select||tool_==Tool::Zoom||tool_==Tool::Gap||panning_)return p;
    double tolerance=10/scale_;
    if(snap) {
        auto candidates=index_.query(QRectF(p-QPointF(tolerance,tolerance),QSizeF(tolerance*2,tolerance*2)));
        QVector<QPair<QPointF,QString>> snaps;QVector<QPair<QPointF,QPointF>> segments;
        for(auto id:candidates){const auto e=document->entities.value(id);if(e.kind==Kind::SplinePreview)continue;snaps+=e.snapPoints();
            if(e.kind==Kind::Line||e.kind==Kind::Polyline||e.kind==Kind::Rectangle){qsizetype count=e.closed?e.points.size():e.points.size()-1;
                for(qsizetype i=0;i<count;++i)if(std::abs(e.bulges.value(i))<geo::epsilon){auto a=e.points[i],b=e.points[(i+1)%e.points.size()];
                    if(geo::length(p,geo::project(p,a,b))<tolerance*2)segments.append({a,b});
                    if(!points_.isEmpty()){auto foot=geo::project(points_.last(),a,b,false);if(geo::length(foot,geo::project(foot,a,b))<geo::epsilon)snaps.append({foot,"垂足"});}
                }
            }
        }
        if(segments.size()<100)for(qsizetype i=0;i<segments.size();++i)for(qsizetype j=i+1;j<segments.size();++j){auto hit=geo::intersection(segments[i].first,segments[i].second,segments[j].first,segments[j].second);if(hit)snaps.append({*hit,"交点"});}
        double best=tolerance;for(const auto& candidate:snaps){double d=geo::length(candidate.first,p);if(d<best){best=d;snapPoint_=candidate;}}
        if(snapPoint_)return snapPoint_->first;
    }
    if(!points_.isEmpty()){auto base=points_.last();if(tool_==Tool::Circle||tool_==Tool::Arc||tool_==Tool::Ellipse||tool_==Tool::Angle)base=points_.first();auto d=p-base;
        if(ortho)p=std::abs(d.x())>=std::abs(d.y())?QPointF(p.x(),base.y()):QPointF(base.x(),p.y());
        else if(polar){double t=std::round(geo::angle(base,p)/45)*45*geo::pi/180;p=base+QPointF(std::cos(t),std::sin(t))*geo::length(base,p);}}
    return p;
}
EntityId CadCanvas::add(Entity e) {
    auto l=document->layers.value(document->currentLayer);
    if(l.locked||!l.visible){emit message("当前图层已锁定或隐藏，请切换图层。");points_.clear();return 0;}
    e.layer=document->currentLayer;
    // commit() assigns ids to its own copies, so take the id from its return value
    // instead of the local entity (which stays 0).
    auto ids=document->commit("绘制 "+e.typeName(),{},{e});synchronize();emit documentChanged();
    return ids.isEmpty()?0:ids.first();
}
EntityId CadCanvas::pickEntity(QPointF position, bool measurableOnly) {
    double tolerance=7/scale_;
    auto candidates=index_.query(QRectF(position-QPointF(tolerance,tolerance),QSizeF(2*tolerance,2*tolerance)));
    EntityId bestId=0;double best=std::numeric_limits<double>::infinity();
    bool bestText=false;
    QPainterPathStroker stroker;stroker.setWidth(tolerance*2);
    for(auto id:candidates){
        if(measurableOnly&&!measure::measurable(document->entities.value(id)))continue;
        const auto& path=cachedPath(id);const auto& entity=document->entities.value(id);
        bool editableText=!measurableOnly&&entity.kind==Kind::Text&&document->textEditable(id);
        auto hit=!measurableOnly&&entity.kind==Kind::Text?textHitArea(entity,path):path;
        if(stroker.createStroke(hit).contains(position)||hit.contains(position)){
            auto b=entity.bounds();double score=b.width()*b.height();
            if((editableText&&!bestText)||(editableText==bestText&&score<best)){best=score;bestId=id;bestText=editableText;}
        }
    }
    return bestId;
}
EntityId CadCanvas::pickTextForEditing(QPointF position) {
    auto id=pickEntity(position);
    if(!id)return 0;
    if(document->textEditable(id))return id;
    // A dimension's lines/arrows and its label share the same source root.
    // Double-clicking that dimension can open the label editor without exploding it.
    auto root=document->entities.value(id).sourceKey.section('/',0,0);
    if(root.isEmpty())return 0;
    for(const auto& e:document->entities)
        if(e.dimensionTextKey==root&&document->textEditable(e.id))return e.id;
    return 0;
}
QString CadCanvas::gapReport(const Entity& first, const Entity& second, const measure::Gap& result) const {
    bool bothRound=(first.kind==Kind::Circle||first.kind==Kind::Arc)&&(second.kind==Kind::Circle||second.kind==Kind::Arc);
    QStringList parts;
    if(bothRound) {
        parts<<"边到边 = "+document->dualLength(result.edgeDistance);
        if(result.centerDistance>=0)parts<<"圆心距 = "+document->dualLength(result.centerDistance);
    } else if(result.centerDistance>=0) {
        parts<<"中心距 = "+document->dualLength(result.centerDistance);
        if(std::abs(result.edgeDistance-result.centerDistance)>geo::epsilon)
            parts<<"边到边 = "+document->dualLength(result.edgeDistance);
    } else parts<<"边到边 = "+document->dualLength(result.edgeDistance);
    return "对象间距 · "+parts.join(" · ");
}
void CadCanvas::pickForGap(QPointF position) {
    auto id=pickEntity(position,true);
    if(!id){emit message("此处没有可测量的对象，请直接点击图形。");return;}
    if(!gapFirst_){gapFirst_=id;gapPreview_.reset();emit message("已拾取第一个对象（"+document->entities.value(id).typeName()+"），请拾取第二个对象。");update();return;}
    if(id==gapFirst_){emit message("请拾取另一个不同的对象。");return;}
    auto first=document->entities.value(gapFirst_),second=document->entities.value(id);
    auto result=measure::gap(first,second);
    if(!result){gapFirst_=0;gapHover_=0;gapPreview_.reset();emit message("无法计算这两个对象的间距（曲线过于复杂或对象无效）。");update();return;}
    auto text=gapReport(first,second,*result);
    EntityId dimensionId=0;
    if(result->edgeDistance>geo::epsilon) {
        Entity dimension;dimension.kind=Kind::Dimension;
        auto a=result->a,b=result->b;
        double span=geo::length(a,b);
        dimension.height=std::clamp(span*0.1,0.3,3.0);
        auto normal=span>geo::epsilon?QPointF(-(b-a).y(),(b-a).x())/span:QPointF(0,1);
        dimension.points={a,b,(a+b)/2+normal*std::max(dimension.height*2.,span*0.35)};
        dimension.text="<>";
        dimensionId=add(dimension);
        if(!dimensionId)text+=" · 标注未创建（当前图层锁定或隐藏）";
    } else emit message("对象相交或重叠，间距为 0，未创建标注。");
    emit measured(text,{gapFirst_,id,dimensionId});
    emit message(text);
    gapFirst_=0;gapHover_=0;gapPreview_.reset();update();
}
void CadCanvas::acceptPoint(QPointF p) {
    if(tool_==Tool::Gap){emit message("对象间距测量请用鼠标直接拾取图形对象。");return;}
    if(!geo::finite(p)){emit message("坐标必须为有限数值。");return;}
    points_.append(p);qsizetype n=points_.size();std::optional<Entity> result;
    Entity e;
    if(tool_==Tool::Coordinate){QPointF r=p-origin_;emit message("X = "+document->dualLength(r.x())+" · Y = "+document->dualLength(r.y()));points_.clear();}
    else if(tool_==Tool::Text){bool ok=false;e.text=QInputDialog::getText(this,"插入文字","单行文字内容",QLineEdit::Normal,{},&ok);
        if(ok&&!e.text.isEmpty()){e.height=QInputDialog::getDouble(this,"文字高度","高度",2.5,0.000001,1e6,4,&ok);if(ok){e.kind=Kind::Text;e.points={p};result=e;}}points_.clear();}
    else if(tool_==Tool::Line&&n==2){if(geo::length(points_[0],points_[1])>geo::epsilon){e.points=points_;result=e;}points_={p};}
    else if(tool_==Tool::Rectangle&&n==2){auto a=points_[0],b=points_[1];if(std::abs(a.x()-b.x())>geo::epsilon&&std::abs(a.y()-b.y())>geo::epsilon){e.kind=Kind::Rectangle;e.closed=true;e.points={a,{b.x(),a.y()},b,{a.x(),b.y()}};result=e;}points_.clear();}
    else if(tool_==Tool::Circle&&n==2){e.kind=Kind::Circle;e.points={points_[0]};e.radius=geo::length(points_[0],p);if(e.radius>geo::epsilon)result=e;points_.clear();}
    else if(tool_==Tool::Arc&&n==3){e.kind=Kind::Arc;e.points={points_[0]};e.radius=geo::length(points_[0],points_[1]);e.start=geo::angle(points_[0],points_[1]);e.sweep=std::fmod(geo::angle(points_[0],p)-e.start+360,360);if(e.radius>geo::epsilon&&e.sweep>geo::epsilon)result=e;points_.clear();}
    else if(tool_==Tool::Ellipse&&n==3){auto c=points_[0],m=points_[1];double r=geo::length(c,m);if(r>geo::epsilon){e.kind=Kind::Ellipse;e.points={c,m-c};e.closed=true;e.ratio=geo::length(p,geo::project(p,c,m,false))/r;if(e.ratio>0&&e.ratio<=1)result=e;else emit message("短轴必须大于 0 且不超过长轴。");}points_.clear();}
    else if((tool_==Tool::Distance||tool_==Tool::Horizontal||tool_==Tool::Vertical)&&n==2){auto d=p-points_[0];double len=tool_==Tool::Distance?geo::length(points_[0],p):tool_==Tool::Horizontal?std::abs(d.x()):std::abs(d.y());
        auto text=toolName(tool_)+" = "+document->dualLength(len);
        emit message(text+" · ΔX "+value(std::abs(d.x()))+" · ΔY "+value(std::abs(d.y()))+" "+document->unitName());
        emit measured(text,{});points_.clear();}
    else if(tool_==Tool::Continuous&&n>1)emit message("本段 "+document->dualLength(geo::length(points_[n-2],p))+" · 累计 "+document->dualLength(geo::polyLength(points_))+" "+document->unitName());
    else if(tool_==Tool::Angle&&n==3){double delta=std::abs(geo::angle(points_[0],points_[1])-geo::angle(points_[0],p));emit message("夹角 = "+value(std::min(delta,360-delta))+"°");points_.clear();}
    else if(tool_==Tool::Dimension&&n==3){if(geo::length(points_[0],points_[1])>geo::epsilon){e.kind=Kind::Dimension;e.points=points_;e.text="<>";e.height=std::clamp(geo::length(points_[0],points_[1])*0.1,0.3,3.0);result=e;}points_.clear();}
    else if((tool_==Tool::Move||tool_==Tool::Copy||tool_==Tool::Mirror)&&n==2){auto before=document->selected(selection,true),after=before;auto a=points_[0];
        if(geo::length(a,p)>geo::epsilon){for(auto& item:after){if(tool_!=Tool::Move){item.id=document->allocateId();item.sourceKey.clear();item.sourceAttributes.clear();}if(tool_==Tool::Mirror)item.mirror(a,p);else item.translate(p-a);}
            document->commit(toolName(tool_),tool_==Tool::Move?before:QVector<Entity>{},after);selection.clear();for(const auto& item:after)selection.insert(item.id);synchronize();emit documentChanged();}
        setTool(Tool::Select);
    }
    if(result){EntityId id=add(*result);
        if(id&&result->kind==Kind::Dimension)
            emit measured("对齐标注 = "+document->dualLength(geo::length(result->points[0],result->points[1])),{id});}
    update();
}
void CadCanvas::finish(bool closed) {
    if(tool_==Tool::Polyline&&points_.size()>=(closed?3:2)){Entity e;e.kind=Kind::Polyline;e.points=points_;e.closed=closed;add(e);}
    if(tool_==Tool::Continuous&&points_.size()>1){
        auto text="连续测量 累计 = "+document->dualLength(geo::polyLength(points_));
        emit message(text+" "+document->unitName());emit measured(text,{});}
    setTool(Tool::Select);
}
void CadCanvas::deleteSelection() {
    if(selection.isEmpty()){emit message("未选中图形：先按 Esc 返回选择，框选或点选后再删除。右→左交叉框选可删除相交的白色图框/线条；网格和坐标轴不是图纸对象。");return;}
    auto before=document->deletionTargets(selection);if(before.isEmpty()){emit message("没有可删除的对象：请先选择内容；锁定图层需先解锁。");return;}
    auto count=before.size();document->deleteEntities(selection);selection.clear();synchronize();emit documentChanged();
    emit message(QString("已删除 %1 个显示对象；外部标注 / 只读块按完整实例删除，Ctrl+Z 可撤销。").arg(count));
}
void CadCanvas::selectIds(QSet<EntityId> ids){selection=std::move(ids);emit selectionChanged();update();}
void CadCanvas::centerOnSelection(){QVector<QPointF> pts;for(const auto& e:document->selected(selection)){auto b=e.bounds();pts+={b.topLeft(),b.bottomRight()};}if(!pts.isEmpty()){center_=geo::bounds(pts).center();update();}}
void CadCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);p.fillRect(rect(),QColor("#101922"));p.setRenderHint(QPainter::Antialiasing);p.setTransform(viewTransform());
    auto world=QRectF(modelAt({0.,double(height())}),modelAt({double(width()),0.})).normalized();
    if(grid){double step=std::pow(10,std::floor(std::log10(35/scale_)));if(step*scale_<18)step*=5;QPen pen(QColor("#22303e"));pen.setCosmetic(true);p.setPen(pen);
        // Draw by integer index instead of accumulating the step: at very large
        // coordinates value+step can round back to value and stall forever.
        auto drawGrid=[&](bool vertical) {
            double begin=vertical?world.left():world.top(),end=vertical?world.right():world.bottom();
            if(!std::isfinite(begin)||!std::isfinite(end)||!(step>0)||begin>=end)return;
            double first=std::floor(begin/step),count=std::ceil(end/step)-first;
            if(!std::isfinite(first)||!(count>=0)||count>100000)return;
            for(qint64 i=0;i<=qint64(count);++i){double value=(first+i)*step;if(!std::isfinite(value))break;
                if(vertical)p.drawLine(QPointF(value,world.top()),QPointF(value,world.bottom()));
                else p.drawLine(QPointF(world.left(),value),QPointF(world.right(),value));}
        };
        drawGrid(true);drawGrid(false);}
    QPen axis(QColor("#3a5265"));axis.setCosmetic(true);p.setPen(axis);p.drawLine(QPointF(0,world.top()),QPointF(0,world.bottom()));p.drawLine(QPointF(world.left(),0),QPointF(world.right(),0));
    auto visible=index_.query(world);std::sort(visible.begin(),visible.end());
    for(auto id:visible){auto entity=document->entities.constFind(id);if(entity==document->entities.constEnd())continue;
        const auto& path=cachedPath(id);const auto& e=entity.value();auto layer=document->layers.value(e.layer);
        QPen pen(selection.contains(id)?QColor("#f4c66c"):(e.color.isValid()?e.color:layer.color),std::clamp(layer.lineweight*4,1.,3.)+(selection.contains(id)?1:0));pen.setCosmetic(true);p.setPen(pen);
        p.setBrush(e.kind==Kind::Text||e.kind==Kind::Hatch||e.maximumWidth()>0?QBrush(pen.color()):QBrush(Qt::NoBrush));p.drawPath(path);}
    QPen preview(branding::AppBrand::accent(),1.4,Qt::DashLine);preview.setCosmetic(true);p.setPen(preview);p.setBrush(Qt::NoBrush);
    if(!points_.isEmpty()&&hasCursor_){Entity e;e.points=points_;e.points.append(cursor_);
        if(tool_==Tool::Rectangle){auto a=e.points[0],b=cursor_;p.drawRect(QRectF(a,b).normalized());}
        else if(tool_==Tool::Circle||tool_==Tool::Arc){e.kind=Kind::Circle;e.radius=geo::length(e.points[0],e.points[1]);e.points={e.points[0]};p.drawPath(pathFor(e,document.get()));}
        else if(tool_==Tool::Dimension&&e.points.size()>=3){e.kind=Kind::Dimension;e.text="<>";p.drawPath(pathFor(e,document.get()));}
        else if(tool_==Tool::Move||tool_==Tool::Copy||tool_==Tool::Mirror){for(auto ghost:document->selected(selection,true)){if(geo::length(points_[0],cursor_)<geo::epsilon)continue;if(tool_==Tool::Mirror)ghost.mirror(points_[0],cursor_);else ghost.translate(cursor_-points_[0]);p.drawPath(pathFor(ghost,document.get()));}}
        else {e.kind=Kind::Polyline;p.drawPath(pathFor(e,document.get()));}}
    if(tool_==Tool::Gap) {
        for(auto id:{gapFirst_,gapHover_}) if(id&&document->entities.contains(id)) {
            QPen highlight(id==gapFirst_?QColor("#ffd76c"):branding::AppBrand::accent(),2.6);highlight.setCosmetic(true);
            p.setPen(highlight);p.setBrush(Qt::NoBrush);p.drawPath(cachedPath(id));
        }
        if(gapPreview_) {
            QPen marker(QColor("#ffd76c"),1.6,Qt::DashLine);marker.setCosmetic(true);p.setPen(marker);p.setBrush(Qt::NoBrush);
            p.drawLine(gapPreview_->a,gapPreview_->b);
            double dot=4/scale_;
            p.drawEllipse(gapPreview_->a,dot,dot);p.drawEllipse(gapPreview_->b,dot,dot);
        }
    }
    p.resetTransform();
    if(dragging_){QColor color(dragEnd_.x()<dragStart_.x()?branding::AppBrand::accent():QColor("#7ea7ff"));p.setPen(QPen(color,1,dragEnd_.x()<dragStart_.x()?Qt::DashLine:Qt::SolidLine));color.setAlpha(35);p.setBrush(color);p.drawRect(QRectF(dragStart_,dragEnd_).normalized());}
    if(snapPoint_){auto pos=screenAt(snapPoint_->first);p.setPen(QPen(QColor("#ffce73"),1.5));p.setBrush(Qt::NoBrush);p.drawRect(QRectF(pos-QPointF(5,5),QSizeF(10,10)));p.drawText(pos+QPointF(12,-10),snapPoint_->second);}
    if(tool_==Tool::Gap&&gapPreview_) {
        auto mid=screenAt((gapPreview_->a+gapPreview_->b)/2);
        QFont font=p.font();font.setBold(true);p.setFont(font);p.setPen(QColor("#ffd76c"));
        p.drawText(mid.toPoint()+QPoint(10,-10),document->dualLength(gapPreview_->edgeDistance));
    }
    if(hasCursor_&&tool_!=Tool::Select){auto pos=screenAt(cursor_);p.setPen(QColor("#8cacc2"));p.drawLine(pos-QPointF(15,0),pos+QPointF(15,0));p.drawLine(pos-QPointF(0,15),pos+QPointF(0,15));
        if(!points_.isEmpty())p.drawText(pos+QPointF(18,22),document->dualLength(geo::length(points_.last(),cursor_))+" · "+value(geo::angle(points_.last(),cursor_),1)+"°");}
}
void CadCanvas::wheelEvent(QWheelEvent* e) {
    auto before=modelAt(e->position());scale_=std::clamp(scale_*(e->angleDelta().y()>0?1.2:1/1.2),1e-5,1e5);center_+=before-modelAt(e->position());update();e->accept();
}
void CadCanvas::mousePressEvent(QMouseEvent* e) {
    setFocus();lastMouseScreen_=e->position();hasMouseScreen_=true;
    if(e->button()==Qt::MiddleButton){panning_=true;lastPan_=e->position();setCursor(Qt::ClosedHandCursor);}
    else if(e->button()==Qt::RightButton)finish();
    else if(e->button()==Qt::LeftButton){if(tool_==Tool::Select||tool_==Tool::Zoom){dragging_=true;dragStart_=dragEnd_=e->position();}else if(tool_==Tool::Gap)pickForGap(modelAt(e->position()));else acceptPoint(constrained(modelAt(e->position())));}
    update();
}
void CadCanvas::mouseDoubleClickEvent(QMouseEvent* e) {
    if(e->button()!=Qt::LeftButton||tool_!=Tool::Select){QWidget::mouseDoubleClickEvent(e);return;}
    dragging_=false;auto id=pickTextForEditing(modelAt(e->position()));
    if(id){selectIds({id});emit textEditRequested();}
    e->accept();update();
}
void CadCanvas::mouseMoveEvent(QMouseEvent* e) {
    lastMouseScreen_=e->position();hasMouseScreen_=true;
    if(panning_){center_+=modelAt(lastPan_)-modelAt(e->position());lastPan_=e->position();}
    if(dragging_)dragEnd_=e->position();
    hasCursor_=true;cursor_=constrained(modelAt(e->position()));emit coordinates(cursor_);
    if(tool_==Tool::Gap&&!panning_) {
        gapHover_=pickEntity(cursor_,true);
        gapPreview_.reset();
        if(gapFirst_&&gapHover_&&gapHover_!=gapFirst_) {
            gapPreview_=measure::gap(document->entities.value(gapFirst_),document->entities.value(gapHover_));
            if(gapPreview_) emit liveMeasure(gapReport(document->entities.value(gapFirst_),document->entities.value(gapHover_),*gapPreview_));
        } else if(gapFirst_) emit liveMeasure("已拾取 1 个对象，请拾取第二个对象");
        else emit liveMeasure(gapHover_?"悬停对象，单击开始测量":"拾取两个对象：线 → 中心到中心，弧 → 边到边");
    } else if(!points_.isEmpty()&&!panning_) {
        if(tool_==Tool::Distance) emit liveMeasure("距离 = "+document->dualLength(geo::length(points_.last(),cursor_)));
        else if(tool_==Tool::Horizontal) emit liveMeasure("水平距离 = "+document->dualLength(std::abs(cursor_.x()-points_.last().x())));
        else if(tool_==Tool::Vertical) emit liveMeasure("垂直距离 = "+document->dualLength(std::abs(cursor_.y()-points_.last().y())));
        else if(tool_==Tool::Continuous) emit liveMeasure("本段 = "+document->dualLength(geo::length(points_.last(),cursor_))+" · 累计 = "+document->dualLength(geo::polyLength(points_)));
        else if(tool_==Tool::Dimension&&points_.size()>=2) emit liveMeasure("标注 = "+document->dualLength(geo::length(points_[0],points_[1])));
    }
    update();
}
void CadCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if(e->button()==Qt::MiddleButton){panning_=false;setCursor(tool_==Tool::Select?Qt::ArrowCursor:Qt::CrossCursor);}
    if(e->button()==Qt::LeftButton&&dragging_){dragging_=false;auto end=e->position();auto box=QRectF(modelAt(dragStart_),modelAt(end)).normalized();
        if(tool_==Tool::Zoom){if(geo::length(dragStart_,end)>5){center_=box.center();scale_=std::clamp(std::min(width()/std::max(box.width(),1e-8),height()/std::max(box.height(),1e-8)),1e-5,1e5);}setTool(Tool::Select);}
        else {bool shift=e->modifiers().testFlag(Qt::ShiftModifier);QSet<EntityId> ids=shift?selection:QSet<EntityId>{};
            if(geo::length(dragStart_,end)<5){auto bestId=pickEntity(modelAt(end));
                if(bestId){if(shift&&ids.contains(bestId))ids.remove(bestId);else ids.insert(bestId);}
            }else {
                bool crossing=end.x()<dragStart_.x();double tolerance=1.5/scale_;
                QPainterPathStroker stroker;stroker.setWidth(2*tolerance);
                for(auto id:index_.query(box.adjusted(-tolerance,-tolerance,tolerance,tolerance))) {
                    const auto& path=cachedPath(id);auto b=path.boundingRect();
                    // QRectF::contains(QRectF) rejects zero-width/height bounds.
                    // Horizontal/vertical open lines must be selected by coordinates,
                    // and crossing selection must intersect the stroke, not a fill.
                    bool contained=b.left()>=box.left()-geo::epsilon&&b.right()<=box.right()+geo::epsilon
                        &&b.top()>=box.top()-geo::epsilon&&b.bottom()<=box.bottom()+geo::epsilon;
                    bool hit=contained;
                    if(crossing&&!hit)hit=path.intersects(box)||stroker.createStroke(path).intersects(box);
                    if(crossing&&!hit&&document->entities.value(id).kind==Kind::Text)hit=textHitArea(document->entities.value(id),path).intersects(box);
                    if(hit)ids.insert(id);
                }
            }
            selectIds(ids);
        }
    }update();
}
void CadCanvas::keyPressEvent(QKeyEvent* e) {
    double step=e->modifiers().testFlag(Qt::ShiftModifier)?240.:e->modifiers().testFlag(Qt::ControlModifier)?12.:48.;
    if(e->key()==Qt::Key_Left){panByPixels({-step,0});e->accept();}
    else if(e->key()==Qt::Key_Right){panByPixels({step,0});e->accept();}
    else if(e->key()==Qt::Key_Up){panByPixels({0,-step});e->accept();}
    else if(e->key()==Qt::Key_Down){panByPixels({0,step});e->accept();}
    else if(e->key()==Qt::Key_Plus||e->key()==Qt::Key_Equal){zoomBy(1.2);e->accept();}
    else if(e->key()==Qt::Key_Minus){zoomBy(1/1.2);e->accept();}
    else if(e->key()==Qt::Key_Escape)setTool(Tool::Select);
    else if(e->key()==Qt::Key_Return||e->key()==Qt::Key_Enter)finish();
    else if(e->key()==Qt::Key_C&&tool_==Tool::Polyline)finish(true);
    else if(e->key()==Qt::Key_Delete)deleteSelection();else QWidget::keyPressEvent(e);
}
void CadCanvas::renderDocument(QPainter& painter,QRectF target,bool dark) {
    auto box=document->bounds();double s=std::min(target.width()/std::max(box.width()+4,1.),target.height()/std::max(box.height()+4,1.));
    painter.save();if(dark)painter.fillRect(target,QColor("#101922"));painter.translate(target.center());painter.scale(s,-s);painter.translate(-box.center());painter.setRenderHint(QPainter::Antialiasing);
    for(const auto& e:document->entities)if(document->layers.value(e.layer).visible){QPen pen(dark?(e.color.isValid()?e.color:document->layers.value(e.layer).color):QColor("#172633"),0);pen.setCosmetic(true);painter.setPen(pen);painter.setBrush(e.kind==Kind::Text||e.kind==Kind::Hatch||e.maximumWidth()>0?QBrush(pen.color()):QBrush(Qt::NoBrush));painter.drawPath(cachedPath(e.id));}
    painter.restore();
}
}
