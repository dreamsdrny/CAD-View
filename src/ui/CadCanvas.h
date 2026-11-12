#pragma once
#include "core/SpatialIndex.h"
#include <QWidget>
#include <QPainterPath>
#include <memory>

namespace cad::ui {
enum class Tool {Select,Line,Polyline,Rectangle,Circle,Arc,Ellipse,Text,Move,Copy,Mirror,Distance,Horizontal,Vertical,Continuous,Angle,Coordinate,Dimension,Zoom};
class CadCanvas : public QWidget {
    Q_OBJECT
public:
    explicit CadCanvas(std::shared_ptr<Document> document,QWidget* parent=nullptr);
    std::shared_ptr<Document> document;
    QSet<EntityId> selection;
    bool snap=true,ortho=false,polar=false,grid=true;
    Tool tool() const {return tool_;}
    const QVector<QPointF>& inputPoints() const {return points_;}
    QPointF cursorPoint() const {return cursor_;}
    QPointF modelAt(QPointF screen) const;
    QPointF screenAt(QPointF model) const;
    void synchronize();
    void fitDrawing();
    void setTool(Tool tool);
    void acceptPoint(QPointF point);
    void finish(bool closed=false);
    void deleteSelection();
    void selectIds(QSet<EntityId> ids);
    void centerOnSelection();
    void renderDocument(QPainter& painter,QRectF target,bool darkBackground=false);
    static QString toolName(Tool tool);
    static QPainterPath pathFor(const Entity& entity);
signals:
    void documentChanged();
    void selectionChanged();
    void message(QString text);
    void coordinates(QPointF point);
    void toolChanged();
protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    struct Cached {Entity entity;QPainterPath path;};
    QHash<EntityId,Cached> cache_;
    SpatialIndex index_;
    Tool tool_=Tool::Select;
    QVector<QPointF> points_;
    QPointF center_{},cursor_{};
    double scale_=8;
    bool panning_=false,dragging_=false,hasCursor_=false;
    QPointF lastPan_,dragStart_,dragEnd_;
    std::optional<QPair<QPointF,QString>> snapPoint_;
    QTransform viewTransform() const;
    QPointF constrained(QPointF point);
    void add(Entity entity);
};
}
