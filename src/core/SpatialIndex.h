#pragma once
#include "Document.h"
#include <memory>

namespace cad {
// Balanced bounding-volume hierarchy: no duplicated cells for very long CAD lines.
class SpatialIndex {
public:
    void rebuild(const Document& document);
    QVector<EntityId> query(QRectF region) const;
private:
    struct Entry { EntityId id; QRectF box; };
    struct Node { QRectF box; QVector<Entry> entries; std::unique_ptr<Node> left,right; };
    std::unique_ptr<Node> root_;
    static std::unique_ptr<Node> build(QVector<Entry> entries);
    static void collect(const Node* node,QRectF region,QVector<EntityId>& out);
};
}
