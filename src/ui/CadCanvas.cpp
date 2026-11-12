#include "CadCanvas.h"
#include "core/Editing.h"
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
CadCanvas::CadCanvas(std::shared_ptr<Document> d,QWidget* parent):QWidget(parent),document(std::move(d)) {
    setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);setMinimumSize(320,240);synchronize();
}
QTransform CadCanvas::viewTransform() const {
    QTransform t;t.translate(width()/2.,height()/2.);t.scale(scale_,-scale_);t.translate(-center_.x(),-center_.y());return t;
}
QPointF CadCanvas::modelAt(QPointF p) const {return viewTransform().inverted().map(p);}
QPointF CadCanvas::screenAt(QPointF p) const {return viewTransform().map(p);}
QPainterPath CadCanvas::pathFor(const Entity& e) {
    QPainterPath path;if(e.points.isEmpty())return path;
    if(e.kind==Kind::Text) {
        QFont font("Microsoft YaHei");font.setPixelSize(100);QPainterPath raw;
        auto lines=e.text.split('\n');for(qsizetype i=0;i<lines.size();++i)raw.addText(0,i*130,font,lines[i]);
        QTransform t;t.translate(e.points[0].x(),e.points[0].y());t.rotate(e.rotation);t.scale(e.height/100,-e.height/100);return t.map(raw);
    }
    if(e.kind==Kind::Dimension) {
        auto a=e.points[0],b=e.points[1],c=e.points[2],delta=c-geo::project(c,a,b,false),aa=a+delta,bb=b+delta;
        auto line=[&](QPointF p,QPointF q){path.moveTo(p);path.lineTo(q);};line(a,aa);line(b,bb);line(aa,bb);
        double len=geo::length(a,b);if(len>geo::epsilon){auto u=(b-a)/len;double size=std::min(1.,len/10);
            for(auto p:{aa,bb})for(double side:{-1.,1.})line(p,p+u*((p==aa?1:-1)*size)+QPointF(-u.y(),u.x())*(side*size/3));}
        Entity label;label.kind=Kind::Text;label.points={(aa+bb)/2+QPointF(0,0.8)};label.text=e.text;label.text.replace("<>",value(len,3));label.height=2;path.addPath(pathFor(label));return path;
    }
    if(e.kind==Kind::Circle){path.addEllipse(e.points[0],e.radius,e.radius);return path;}
    auto points=e.outline(0.01);if(!points.isEmpty()){path.moveTo(points[0]);for(qsizetype i=1;i<points.size();++i)path.lineTo(points[i]);if(e.isClosed())path.closeSubpath();}return path;
}
void CadCanvas::synchronize() {
    // Recreate only paths whose geometry changed. QColor/layer changes are read at paint time.
    for(auto it=cache_.begin();it!=cache_.end();)if(!document->entities.contains(it.key()))it=cache_.erase(it);else ++it;
    for(const auto& e:document->entities)if(!cache_.contains(e.id)||cache_.value(e.id).entity!=e)cache_.insert(e.id,{e,pathFor(e)});
    for(auto it=selection.begin();it!=selection.end();)if(!document->entities.contains(*it)||!document->layers.value(document->entities.value(*it).layer).visible)it=selection.erase(it);else ++it;
    index_.rebuild(*document);update();emit selectionChanged();
}
void CadCanvas::fitDrawing() {
    auto b=document->bounds();center_=b.center();scale_=std::clamp(std::min(width()/std::max(10.,b.width()),height()/std::max(10.,b.height()))*0.85,1e-5,1e5);update();
}
QString CadCanvas::toolName(Tool tool) {
    switch(tool){case Tool::Select:return "选择";case Tool::Line:return "直线";case Tool::Polyline:return "多段线";
    case Tool::Rectangle:return "矩形";case Tool::Circle:return "圆";case Tool::Arc:return "圆弧";case Tool::Ellipse:return "椭圆";
    case Tool::Text:return "文字";case Tool::Move:return "移动";case Tool::Copy:return "复制";case Tool::Mirror:return "镜像";
    case Tool::Distance:return "距离";case Tool::Horizontal:return "水平距离";case Tool::Vertical:return "垂直距离";
    case Tool::Continuous:return "连续测量";case Tool::Angle:return "角度";case Tool::Coordinate:return "坐标";
    case Tool::Dimension:return "对齐标注";case Tool::Zoom:return "框选缩放";}return {};
}
void CadCanvas::setTool(Tool tool) {
    if((tool==Tool::Move||tool==Tool::Copy||tool==Tool::Mirror)&&document->selected(selection,true).isEmpty()){emit message("请先选择未锁定图层中的对象。");return;}
    tool_=tool;points_.clear();snapPoint_.reset();dragging_=false;
    setCursor(tool==Tool::Select?Qt::ArrowCursor:Qt::CrossCursor);emit toolChanged();
    QString hint=" · 指定点，Esc 结束；可在命令栏输入精确坐标";
    if(tool==Tool::Select)hint=" · 单击 / 左→右窗口选择 / 右→左交叉选择 / Shift 多选";
    if(tool==Tool::Polyline)hint=" · 依次指定顶点；Enter 完成，C 闭合";
    if(tool==Tool::Arc)hint=" · 圆心 → 起点 → 终点（逆时针）";
    if(tool==Tool::Ellipse)hint=" · 圆心 → 长轴端点 → 短轴点";
    if(tool==Tool::Angle)hint=" · 顶点 → 第一条射线 → 第二条射线";
    emit message(toolName(tool)+hint);update();
}
QPointF CadCanvas::constrained(QPointF p) {
    snapPoint_.reset();double tolerance=10/scale_;
    if(snap) {
        auto candidates=index_.query(QRectF(p-QPointF(tolerance,tolerance),QSizeF(tolerance*2,tolerance*2)));
        QVector<QPair<QPointF,QString>> snaps;QVector<QPair<QPointF,QPointF>> segments;
        for(auto id:candidates){const auto e=document->entities.value(id);snaps+=e.snapPoints();
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
void CadCanvas::add(Entity e) {
    auto l=document->layers.value(document->currentLayer);
    if(l.locked||!l.visible){emit message("当前图层已锁定或隐藏，请切换图层。");points_.clear();return;}
    e.layer=document->currentLayer;document->commit("绘制 "+e.typeName(),{},{e});synchronize();emit documentChanged();
}
void CadCanvas::acceptPoint(QPointF p) {
    if(!geo::finite(p)){emit message("坐标必须为有限数值。");return;}
    points_.append(p);qsizetype n=points_.size();std::optional<Entity> result;
    Entity e;
    if(tool_==Tool::Coordinate){emit message("X = "+value(p.x())+" · Y = "+value(p.y())+" "+document->unitName());points_.clear();}
    else if(tool_==Tool::Text){bool ok=false;e.text=QInputDialog::getMultiLineText(this,"插入文字","文字内容",{},&ok);
        if(ok&&!e.text.isEmpty()){e.height=QInputDialog::getDouble(this,"文字高度","高度",2.5,0.000001,1e6,4,&ok);if(ok){e.kind=Kind::Text;e.points={p};result=e;}}points_.clear();}
    else if(tool_==Tool::Line&&n==2){if(geo::length(points_[0],points_[1])>geo::epsilon){e.points=points_;result=e;}points_={p};}
    else if(tool_==Tool::Rectangle&&n==2){auto a=points_[0],b=points_[1];if(std::abs(a.x()-b.x())>geo::epsilon&&std::abs(a.y()-b.y())>geo::epsilon){e.kind=Kind::Rectangle;e.closed=true;e.points={a,{b.x(),a.y()},b,{a.x(),b.y()}};result=e;}points_.clear();}
    else if(tool_==Tool::Circle&&n==2){e.kind=Kind::Circle;e.points={points_[0]};e.radius=geo::length(points_[0],p);if(e.radius>geo::epsilon)result=e;points_.clear();}
    else if(tool_==Tool::Arc&&n==3){e.kind=Kind::Arc;e.points={points_[0]};e.radius=geo::length(points_[0],points_[1]);e.start=geo::angle(points_[0],points_[1]);e.sweep=std::fmod(geo::angle(points_[0],p)-e.start+360,360);if(e.radius>geo::epsilon&&e.sweep>geo::epsilon)result=e;points_.clear();}
    else if(tool_==Tool::Ellipse&&n==3){auto c=points_[0],m=points_[1];double r=geo::length(c,m);if(r>geo::epsilon){e.kind=Kind::Ellipse;e.points={c,m-c};e.closed=true;e.ratio=geo::length(p,geo::project(p,c,m,false))/r;if(e.ratio>0&&e.ratio<=1)result=e;else emit message("短轴必须大于 0 且不超过长轴。");}points_.clear();}
    else if((tool_==Tool::Distance||tool_==Tool::Horizontal||tool_==Tool::Vertical)&&n==2){auto d=p-points_[0];double len=tool_==Tool::Distance?geo::length(points_[0],p):tool_==Tool::Horizontal?std::abs(d.x()):std::abs(d.y());
        emit message("测量 = "+value(len)+" "+document->unitName()+" · ΔX "+value(std::abs(d.x()))+" · ΔY "+value(std::abs(d.y())));points_.clear();}
    else if(tool_==Tool::Continuous&&n>1)emit message("本段 "+value(geo::length(points_[n-2],p))+" · 累计 "+value(geo::polyLength(points_))+" "+document->unitName());
    else if(tool_==Tool::Angle&&n==3){double delta=std::abs(geo::angle(points_[0],points_[1])-geo::angle(points_[0],p));emit message("夹角 = "+value(std::min(delta,360-delta))+"°");points_.clear();}
    else if(tool_==Tool::Dimension&&n==3){if(geo::length(points_[0],points_[1])>geo::epsilon){e.kind=Kind::Dimension;e.points=points_;e.text="<>";result=e;}points_.clear();}
    else if((tool_==Tool::Move||tool_==Tool::Copy||tool_==Tool::Mirror)&&n==2){auto before=document->selected(selection,true),after=before;auto a=points_[0];
        if(geo::length(a,p)>geo::epsilon){for(auto& item:after){if(tool_!=Tool::Move){item.id=document->allocateId();item.sourceKey.clear();}if(tool_==Tool::Mirror)item.mirror(a,p);else item.translate(p-a);}
            document->commit(toolName(tool_),tool_==Tool::Move?before:QVector<Entity>{},after);selection.clear();for(const auto& item:after)selection.insert(item.id);synchronize();emit documentChanged();}
        setTool(Tool::Select);
    }
    if(result)add(*result);
    update();
}
void CadCanvas::finish(bool closed) {
    if(tool_==Tool::Polyline&&points_.size()>=(closed?3:2)){Entity e;e.kind=Kind::Polyline;e.points=points_;e.closed=closed;add(e);}
    if(tool_==Tool::Continuous&&points_.size()>1)emit message("累计长度 = "+value(geo::polyLength(points_))+" "+document->unitName());
    setTool(Tool::Select);
}
void CadCanvas::deleteSelection() {
    auto before=document->selected(selection,true);if(before.isEmpty()){emit message("没有可删除的对象。");return;}
    document->commit("删除",before,{});selection.clear();synchronize();emit documentChanged();
}
void CadCanvas::selectIds(QSet<EntityId> ids){selection=std::move(ids);emit selectionChanged();update();}
void CadCanvas::centerOnSelection(){QVector<QPointF> pts;for(const auto& e:document->selected(selection)){auto b=e.bounds();pts+={b.topLeft(),b.bottomRight()};}if(!pts.isEmpty()){center_=geo::bounds(pts).center();update();}}
void CadCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);p.fillRect(rect(),QColor("#101922"));p.setRenderHint(QPainter::Antialiasing);p.setTransform(viewTransform());
    auto world=QRectF(modelAt({0.,double(height())}),modelAt({double(width()),0.})).normalized();
    if(grid){double step=std::pow(10,std::floor(std::log10(35/scale_)));if(step*scale_<18)step*=5;QPen pen(QColor("#22303e"));pen.setCosmetic(true);p.setPen(pen);
        for(double x=std::floor(world.left()/step)*step;x<world.right();x+=step)p.drawLine(QPointF(x,world.top()),QPointF(x,world.bottom()));
        for(double y=std::floor(world.top()/step)*step;y<world.bottom();y+=step)p.drawLine(QPointF(world.left(),y),QPointF(world.right(),y));}
    QPen axis(QColor("#3a5265"));axis.setCosmetic(true);p.setPen(axis);p.drawLine(QPointF(0,world.top()),QPointF(0,world.bottom()));p.drawLine(QPointF(world.left(),0),QPointF(world.right(),0));
    auto visible=index_.query(world);std::sort(visible.begin(),visible.end());
    for(auto id:visible){const auto& cached=cache_[id];const auto& e=cached.entity;auto layer=document->layers.value(e.layer);
        QPen pen(selection.contains(id)?QColor("#f4c66c"):(e.color.isValid()?e.color:layer.color),std::clamp(layer.lineweight*4,1.,3.)+(selection.contains(id)?1:0));pen.setCosmetic(true);p.setPen(pen);
        p.setBrush(e.kind==Kind::Text?QBrush(pen.color()):QBrush(Qt::NoBrush));p.drawPath(cached.path);}
    QPen preview(QColor("#56d9bd"),1.4,Qt::DashLine);preview.setCosmetic(true);p.setPen(preview);p.setBrush(Qt::NoBrush);
    if(!points_.isEmpty()&&hasCursor_){Entity e;e.points=points_;e.points.append(cursor_);
        if(tool_==Tool::Rectangle){auto a=e.points[0],b=cursor_;p.drawRect(QRectF(a,b).normalized());}
        else if(tool_==Tool::Circle||tool_==Tool::Arc){e.kind=Kind::Circle;e.radius=geo::length(e.points[0],e.points[1]);e.points={e.points[0]};p.drawPath(pathFor(e));}
        else if(tool_==Tool::Dimension&&e.points.size()>=3){e.kind=Kind::Dimension;e.text="<>";p.drawPath(pathFor(e));}
        else if(tool_==Tool::Move||tool_==Tool::Copy||tool_==Tool::Mirror){for(auto ghost:document->selected(selection,true)){if(geo::length(points_[0],cursor_)<geo::epsilon)continue;if(tool_==Tool::Mirror)ghost.mirror(points_[0],cursor_);else ghost.translate(cursor_-points_[0]);p.drawPath(pathFor(ghost));}}
        else {e.kind=Kind::Polyline;p.drawPath(pathFor(e));}}
    p.resetTransform();
    if(dragging_){QColor color(dragEnd_.x()<dragStart_.x()?"#56d9bd":"#7ea7ff");p.setPen(QPen(color,1,dragEnd_.x()<dragStart_.x()?Qt::DashLine:Qt::SolidLine));color.setAlpha(35);p.setBrush(color);p.drawRect(QRectF(dragStart_,dragEnd_).normalized());}
    if(snapPoint_){auto pos=screenAt(snapPoint_->first);p.setPen(QPen(QColor("#ffce73"),1.5));p.setBrush(Qt::NoBrush);p.drawRect(QRectF(pos-QPointF(5,5),QSizeF(10,10)));p.drawText(pos+QPointF(12,-10),snapPoint_->second);}
    if(hasCursor_&&tool_!=Tool::Select){auto pos=screenAt(cursor_);p.setPen(QColor("#8cacc2"));p.drawLine(pos-QPointF(15,0),pos+QPointF(15,0));p.drawLine(pos-QPointF(0,15),pos+QPointF(0,15));
        if(!points_.isEmpty())p.drawText(pos+QPointF(18,22),value(geo::length(points_.last(),cursor_),3)+" / "+value(geo::angle(points_.last(),cursor_),1)+"°");}
}
void CadCanvas::wheelEvent(QWheelEvent* e) {
    auto before=modelAt(e->position());scale_=std::clamp(scale_*(e->angleDelta().y()>0?1.2:1/1.2),1e-5,1e5);center_+=before-modelAt(e->position());update();e->accept();
}
void CadCanvas::mousePressEvent(QMouseEvent* e) {
    setFocus();if(e->button()==Qt::MiddleButton){panning_=true;lastPan_=e->position();setCursor(Qt::ClosedHandCursor);}
    else if(e->button()==Qt::RightButton)finish();
    else if(e->button()==Qt::LeftButton){if(tool_==Tool::Select||tool_==Tool::Zoom){dragging_=true;dragStart_=dragEnd_=e->position();}else acceptPoint(constrained(modelAt(e->position())));}
    update();
}
void CadCanvas::mouseMoveEvent(QMouseEvent* e) {
    if(panning_){center_+=modelAt(lastPan_)-modelAt(e->position());lastPan_=e->position();}
    if(dragging_)dragEnd_=e->position();
    hasCursor_=true;cursor_=constrained(modelAt(e->position()));emit coordinates(cursor_);update();
}
void CadCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if(e->button()==Qt::MiddleButton){panning_=false;setCursor(tool_==Tool::Select?Qt::ArrowCursor:Qt::CrossCursor);}
    if(e->button()==Qt::LeftButton&&dragging_){dragging_=false;auto end=e->position();auto box=QRectF(modelAt(dragStart_),modelAt(end)).normalized();
        if(tool_==Tool::Zoom){if(geo::length(dragStart_,end)>5){center_=box.center();scale_=std::clamp(std::min(width()/std::max(box.width(),1e-8),height()/std::max(box.height(),1e-8)),1e-5,1e5);}setTool(Tool::Select);}
        else {bool shift=e->modifiers().testFlag(Qt::ShiftModifier);QSet<EntityId> ids=shift?selection:QSet<EntityId>{};
            if(geo::length(dragStart_,end)<5){auto pos=modelAt(end);double tolerance=7/scale_;auto candidates=index_.query(QRectF(pos-QPointF(tolerance,tolerance),QSizeF(2*tolerance,2*tolerance)));EntityId bestId=0;double best=std::numeric_limits<double>::infinity();
                QPainterPathStroker stroker;stroker.setWidth(tolerance*2);
                for(auto id:candidates){const auto& path=cache_[id].path;if(stroker.createStroke(path).contains(pos)||path.contains(pos)){auto d=document->entities.value(id).bounds();double score=d.width()*d.height();if(score<best){best=score;bestId=id;}}}
                if(bestId){if(shift&&ids.contains(bestId))ids.remove(bestId);else ids.insert(bestId);}
            }else for(auto id:index_.query(box)){bool crossing=end.x()<dragStart_.x();const auto& path=cache_[id].path;
                if(crossing?(path.intersects(box)||box.contains(path.boundingRect())):box.contains(path.boundingRect()))ids.insert(id);}
            selectIds(ids);
        }
    }update();
}
void CadCanvas::keyPressEvent(QKeyEvent* e) {
    if(e->key()==Qt::Key_Escape)setTool(Tool::Select);
    else if(e->key()==Qt::Key_Return||e->key()==Qt::Key_Enter)finish();
    else if(e->key()==Qt::Key_C&&tool_==Tool::Polyline)finish(true);
    else if(e->key()==Qt::Key_Delete)deleteSelection();else QWidget::keyPressEvent(e);
}
void CadCanvas::renderDocument(QPainter& painter,QRectF target,bool dark) {
    auto box=document->bounds();double s=std::min(target.width()/std::max(box.width()+4,1.),target.height()/std::max(box.height()+4,1.));
    painter.save();if(dark)painter.fillRect(target,QColor("#101922"));painter.translate(target.center());painter.scale(s,-s);painter.translate(-box.center());painter.setRenderHint(QPainter::Antialiasing);
    for(const auto& e:document->entities)if(document->layers.value(e.layer).visible){QPen pen(dark?(e.color.isValid()?e.color:document->layers.value(e.layer).color):QColor("#172633"),0);pen.setCosmetic(true);painter.setPen(pen);painter.setBrush(e.kind==Kind::Text?QBrush(pen.color()):QBrush(Qt::NoBrush));painter.drawPath(cache_.value(e.id).path);}
    painter.restore();
}
}
