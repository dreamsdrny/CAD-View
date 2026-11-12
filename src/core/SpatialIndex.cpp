#include "SpatialIndex.h"
#include <algorithm>

namespace cad {
std::unique_ptr<SpatialIndex::Node> SpatialIndex::build(QVector<Entry> entries) {
    if(entries.isEmpty())return {};
    auto node=std::make_unique<Node>();
    node->box=entries[0].box;
    for(const auto& e:entries)node->box=node->box.united(e.box);
    if(entries.size()<=16) {node->entries=std::move(entries);return node;}
    bool x=node->box.width()>=node->box.height();
    std::sort(entries.begin(),entries.end(),[x](const Entry& a,const Entry& b){return x?a.box.center().x()<b.box.center().x():a.box.center().y()<b.box.center().y();});
    auto middle=entries.size()/2;
    node->left=build(entries.mid(0,middle));node->right=build(entries.mid(middle));return node;
}
void SpatialIndex::rebuild(const Document& document) {
    QVector<Entry> entries;entries.reserve(document.entities.size());
    for(const auto& e:document.entities)if(document.layers.value(e.layer).visible) entries.append({e.id,e.bounds().adjusted(-1e-6,-1e-6,1e-6,1e-6)});
    root_=build(std::move(entries));
}
void SpatialIndex::collect(const Node* node,QRectF region,QVector<EntityId>& out) {
    if(!node||!node->box.intersects(region))return;
    for(const auto& e:node->entries)if(e.box.intersects(region))out.append(e.id);
    collect(node->left.get(),region,out);collect(node->right.get(),region,out);
}
QVector<EntityId> SpatialIndex::query(QRectF region) const {QVector<EntityId> out;collect(root_.get(),region.normalized(),out);return out;}
}
