#pragma once
#include "core/Entity.h"
#include <QWidget>

class QLabel;class QListWidget;class QPushButton;
namespace cad::ui {
// 标注测量 panel: live dual-unit readout while picking, plus a clickable
// history of completed measurements (click locates the involved objects).
class MeasurePanel : public QWidget {
    Q_OBJECT
public:
    explicit MeasurePanel(QWidget* parent=nullptr);
    void setLive(QString text);
    void setUnitNote(QString text);
    void addRecord(QString text, const QVector<EntityId>& ids);
    void clearRecords();
signals:
    void recordActivated(QVector<EntityId> ids);
private:
    QLabel *live_=nullptr,*note_=nullptr;
    QListWidget* records_=nullptr;
};
}
