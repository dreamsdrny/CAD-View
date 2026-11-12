#pragma once
#include "core/Document.h"

namespace cad::engineering {
struct Finding {QString description;QSet<EntityId> ids;double value=0;};
double boundaryGap(const Entity& a,const Entity& b);
double clearance(const Entity& a,const Entity& b);
QVector<Finding> holes(const QVector<Entity>& entities);
QVector<Finding> board(const Entity& entity,const QString& unit);
QVector<Finding> clearances(const QVector<Entity>& entities,double minimum);
QVector<Finding> edges(const Entity& board,const QVector<Entity>& entities,double minimum);
QVector<Finding> pads(const QVector<Entity>& entities,double width,double height);
QVector<Finding> duplicates(const QVector<Entity>& entities);
}
