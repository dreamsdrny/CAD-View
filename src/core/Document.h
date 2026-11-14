#pragma once
#include "Entity.h"
#include <QMap>
#include <QHash>
#include <QSet>
#include <QByteArray>

namespace cad {
struct Layer {
    QString name="0";
    QColor color=QColor("#8fc8e8");
    bool visible=true, locked=false;
    double lineweight=0.25;
    bool operator==(const Layer&) const = default;
};
struct Change {
    QString label;
    QVector<Entity> before, after;
    QMap<QString,Layer> layersBefore, layersAfter;
    int unitsBefore=4, unitsAfter=4;
};
class Document {
public:
    QString title="未命名",path,currentLayer="0";
    int units=4;
    QHash<EntityId,Entity> entities;
    QMap<QString,Layer> layers{{"0",Layer{}}};
    QMap<QString,Layer> sourceLayers; // Imported layer state; unchanged fields retain their original DXF tags.
    QByteArray sourceDxf;
    QHash<QString,Entity> baseline;
    QSet<QString> importedKeys;
    QStringList warnings;

    EntityId allocateId();
    void load(Entity e);
    void commit(QString label, QVector<Entity> before, QVector<Entity> after);
    void changeLayers(QString label, QMap<QString,Layer> next);
    void setUnits(int next);
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    bool modified() const;
    void markSaved();
    bool editable(EntityId id) const;
    QVector<Entity> selected(const QSet<EntityId>& ids, bool editableOnly=false) const;
    QRectF bounds(bool visibleOnly=true) const;
    QString unitName() const;
    static Document demo();
private:
    QVector<Change> history_;
    qsizetype cursor_=0;
    qint64 savedCursor_=0;
    EntityId nextId_=1;
    void apply(const Change& c,bool forward);
    void push(Change c);
};
}
