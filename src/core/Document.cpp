#include "Document.h"
#include <algorithm>

namespace cad {
EntityId Document::allocateId() { return nextId_++; }
void Document::load(Entity e) { if(!e.id)e.id=allocateId(); nextId_=std::max(nextId_,e.id+1); entities.insert(e.id,std::move(e)); }
void Document::apply(const Change& c,bool forward) {
    const auto& remove=forward?c.before:c.after;
    const auto& insert=forward?c.after:c.before;
    for(const auto& e:remove) entities.remove(e.id);
    for(const auto& e:insert) entities.insert(e.id,e);
    layers=forward?c.layersAfter:c.layersBefore;
    units=forward?c.unitsAfter:c.unitsBefore;
    if(!layers.contains(currentLayer)) currentLayer="0";
}
void Document::push(Change c) {
    if(savedCursor_>cursor_)savedCursor_=-1;
    history_.resize(cursor_);
    apply(c,true); history_.append(std::move(c)); ++cursor_;
    if(history_.size()>100) { history_.removeFirst(); --cursor_; if(savedCursor_>=0)--savedCursor_; }
}
void Document::commit(QString label,QVector<Entity> before,QVector<Entity> after) {
    if(before==after) return;
    for(auto& e:after) {
        if(!e.id)e.id=allocateId();
        nextId_=std::max(nextId_,e.id+1);
    }
    push({std::move(label),std::move(before),std::move(after),layers,layers,units,units});
}
void Document::changeLayers(QString label,QMap<QString,Layer> next) {
    if(next!=layers) push({std::move(label),{},{},layers,std::move(next),units,units});
}
void Document::setUnits(int next) { if(next!=units)push({"设置单位",{},{},layers,layers,units,next}); }
bool Document::undo() { if(!canUndo())return false; apply(history_[--cursor_],false);return true; }
bool Document::redo() { if(!canRedo())return false; apply(history_[cursor_++],true);return true; }
bool Document::canUndo() const {return cursor_>0;}
bool Document::canRedo() const {return cursor_<history_.size();}
bool Document::modified() const {return savedCursor_!=cursor_;}
void Document::markSaved() {savedCursor_=cursor_;}
bool Document::editable(EntityId id) const {
    if(!entities.contains(id))return false;
    const auto l=layers.value(entities.value(id).layer);
    return l.visible&&!l.locked;
}
QVector<Entity> Document::selected(const QSet<EntityId>& ids,bool editableOnly) const {
    QVector<Entity> result;
    for(auto id:ids) if(entities.contains(id)&&(!editableOnly||editable(id)))result.append(entities.value(id));
    std::sort(result.begin(),result.end(),[](const Entity& a,const Entity& b){return a.id<b.id;});
    return result;
}
QRectF Document::bounds(bool visibleOnly) const {
    QVector<QPointF> extrema;
    for(const auto& e:entities) if(!visibleOnly||layers.value(e.layer).visible) {auto b=e.bounds();extrema+={b.topLeft(),b.bottomRight()};}
    return extrema.isEmpty()?QRectF(-50,-35,100,70):geo::bounds(extrema);
}
QString Document::unitName() const {
    switch(units) {case 1:return "inch";case 4:return "mm";case 5:return "cm";case 6:return "m";case 9:return "mil";case 13:return "µm";default:return "图形单位";}
}
Document Document::demo() {
    Document d; d.title="PCB 演示板"; d.currentLayer="BOARD";
    for(auto [name,color]:QVector<QPair<QString,QString>>{{"BOARD","#56d9bd"},{"DRILL","#ffce73"},{"PAD","#7ea7ff"},{"SILK","#c4b5fd"}})
        d.layers.insert(name,Layer{name,QColor(color)});
    Entity board;board.kind=Kind::Rectangle;board.layer="BOARD";board.closed=true;board.points={{0,0},{80,0},{80,50},{0,50}};d.load(board);
    for(double x:{5.,75.})for(double y:{5.,45.}) {Entity e;e.kind=Kind::Circle;e.layer="DRILL";e.points={{x,y}};e.radius=1.6;d.load(e);}
    for(double x=24;x<=56;x+=4)for(double y:{15.,35.}) {Entity e;e.kind=Kind::Rectangle;e.layer="PAD";e.closed=true;e.points={{x-1,y-1.5},{x+1,y-1.5},{x+1,y+1.5},{x-1,y+1.5}};d.load(e);}
    Entity silk;silk.kind=Kind::Rectangle;silk.layer="SILK";silk.closed=true;silk.points={{22,18},{58,18},{58,32},{22,32}};d.load(silk);
    Entity text;text.kind=Kind::Text;text.layer="SILK";text.points={{29,23}};text.text="Qiao CAD";text.height=2.2;d.load(text);
    Entity dim;dim.kind=Kind::Dimension;dim.points={{0,0},{80,0},{40,-8}};dim.text="<>";d.load(dim);
    d.markSaved();return d;
}
}
