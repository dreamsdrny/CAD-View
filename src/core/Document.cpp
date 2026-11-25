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
    deletedSources=forward?c.deletedSourcesAfter:c.deletedSourcesBefore;
    if(!layers.contains(currentLayer)) currentLayer="0";
}
void Document::push(Change c) {
    if(savedCursor_>cursor_)savedCursor_=-1;
    history_.resize(cursor_);
    apply(c,true); history_.append(std::move(c)); ++cursor_;
    ++revision_;
    if(history_.size()>100) { history_.removeFirst(); --cursor_; if(savedCursor_>=0)--savedCursor_; }
    // Defensive: a saved cursor must always refer to an entry that still exists.
    if(savedCursor_<0||savedCursor_>cursor_)savedCursor_=-1;
}
QVector<EntityId> Document::commit(QString label,QVector<Entity> before,QVector<Entity> after) {
    if(before==after) return {};
    QVector<EntityId> ids;ids.reserve(after.size());
    for(auto& e:after) {
        if(!e.id)e.id=allocateId();
        nextId_=std::max(nextId_,e.id+1);
        ids.append(e.id);
    }
    push({std::move(label),std::move(before),std::move(after),layers,layers,units,units,deletedSources,deletedSources});
    return ids;
}
void Document::changeLayers(QString label,QMap<QString,Layer> next) {
    if(next!=layers) push({std::move(label),{},{},layers,std::move(next),units,units,deletedSources,deletedSources});
}
void Document::setUnits(int next) { if(next!=units)push({"设置单位",{},{},layers,layers,units,next,deletedSources,deletedSources}); }
bool Document::undo() { if(!canUndo())return false; apply(history_[--cursor_],false);++revision_;return true; }
bool Document::redo() { if(!canRedo())return false; apply(history_[cursor_++],true);++revision_;return true; }
bool Document::canUndo() const {return cursor_>0;}
bool Document::canRedo() const {return cursor_<history_.size();}
bool Document::modified() const {return savedCursor_!=cursor_;}
void Document::markSaved() {savedCursor_=cursor_;}
void Document::markUnsaved() {savedCursor_=-1;}
bool Document::editable(EntityId id) const {
    if(!entities.contains(id))return false;
    const auto l=layers.value(entities.value(id).layer);
    return l.visible&&!l.locked&&!entities.value(id).readOnly;
}
bool Document::textEditable(EntityId id) const {
    if(!entities.contains(id))return false;
    const auto& e=entities[id];const auto l=layers.value(e.layer);
    return l.visible&&!l.locked&&((!e.readOnly&&(e.kind==Kind::Text||e.kind==Kind::Dimension))||!e.dimensionTextKey.isEmpty());
}
bool Document::editText(EntityId id,QString text) {
    if(!textEditable(id)||text.contains('\n')||text.contains('\r')||text.size()>20000)return false;
    auto before=entities.value(id),after=before;
    if(!before.dimensionTextKey.isEmpty()) {
        // This editor accepts single-line content, not MTEXT formatting commands.
        if(text.contains('\\')||text.contains('{')||text.contains('}')||text.count("<>")>1)return false;
        if(text.isEmpty())text="<>";
        if(text.trimmed().isEmpty())return false;
        after.dimensionTextOverride=text;after.text=text;
        after.text.replace("<>",before.dimensionAutoText);
    } else after.text=text;
    commit("编辑文字 / 标注",{before},{after});return true;
}
QVector<Entity> Document::selected(const QSet<EntityId>& ids,bool editableOnly) const {
    QVector<Entity> result;
    for(auto id:ids) if(entities.contains(id)&&(!editableOnly||editable(id)))result.append(entities.value(id));
    std::sort(result.begin(),result.end(),[](const Entity& a,const Entity& b){return a.id<b.id;});
    return result;
}
QVector<Entity> Document::deletionTargets(const QSet<EntityId>& ids) const {
    QSet<EntityId> targets;QSet<QString> roots;
    for(auto id:ids)if(entities.contains(id)) {
        const auto& e=entities[id];auto layer=layers.value(e.layer);
        if(!layer.visible||layer.locked)continue;
        if(!e.readOnly)targets.insert(id);
        else if(!e.sourceKey.isEmpty()&&baseline.contains(e.sourceKey))roots.insert(e.sourceKey.section('/',0,0));
    }
    // A read-only preview represents an entire source DIMENSION/MTEXT/INSERT.
    // Deleting any part removes that instance, never a shared block definition.
    for(const auto& root:roots) {
        QSet<EntityId> group;bool locked=false;
        for(const auto& e:entities)if(!e.sourceKey.isEmpty()&&e.sourceKey.section('/',0,0)==root){group.insert(e.id);locked|=layers.value(e.layer).locked;}
        if(!locked)targets.unite(group);
    }
    return selected(targets);
}
bool Document::deleteEntities(const QSet<EntityId>& ids) {
    auto before=deletionTargets(ids);if(before.isEmpty())return false;
    auto next=deletedSources;
    for(const auto& e:before)if(e.readOnly&&!e.sourceKey.isEmpty())next.insert(e.sourceKey.section('/',0,0));
    push({"删除",before,{},layers,layers,units,units,deletedSources,next});return true;
}
QRectF Document::bounds(bool visibleOnly) const {
    QVector<QPointF> extrema;
    for(const auto& e:entities) if(!visibleOnly||layers.value(e.layer).visible) {auto b=e.bounds();extrema+={b.topLeft(),b.bottomRight()};}
    return extrema.isEmpty()?QRectF(-50,-35,100,70):geo::bounds(extrema);
}
QString Document::unitName() const {
    switch(units) {case 1:return "inch";case 4:return "mm";case 5:return "cm";case 6:return "m";case 9:return "mil";case 13:return "µm";default:return "图形单位";}
}
double Document::mmPerUnit(int units) {
    switch(units) {case 1:return 25.4;case 5:return 10.;case 6:return 1000.;case 9:return 0.0254;case 13:return 0.001;case 4:default:return 1.;}
}
double Document::mmPerUnit() const {return mmPerUnit(units);}
QString Document::dualLengthInUnits(double length,int units,int mmPrecision,int milPrecision) {
    double mm=length*mmPerUnit(units);
    return QString::number(mm,'f',mmPrecision)+" mm / "+QString::number(mm/0.0254,'f',milPrecision)+" mil";
}
QString Document::dualLength(double length,int mmPrecision,int milPrecision) const {
    return dualLengthInUnits(length,units,mmPrecision,milPrecision);
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
