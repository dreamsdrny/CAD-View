#include "MeasurePanel.h"
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTime>
#include <QVariant>

namespace cad::ui {
MeasurePanel::MeasurePanel(QWidget* parent):QWidget(parent) {
    auto* layout=new QVBoxLayout(this);
    auto* liveCaption=new QLabel("实时测量（mm / mil 同步显示）");
    liveCaption->setObjectName("measureCaption");
    layout->addWidget(liveCaption);
    live_=new QLabel("—");
    live_->setObjectName("measureLive");
    live_->setWordWrap(true);
    QFont liveFont=live_->font();liveFont.setBold(true);liveFont.setPointSize(liveFont.pointSize()+1);live_->setFont(liveFont);
    live_->setMinimumHeight(42);
    layout->addWidget(live_);
    note_=new QLabel;
    note_->setObjectName("measureNote");
    note_->setWordWrap(true);
    note_->hide();
    layout->addWidget(note_);
    layout->addWidget(new QLabel("测量记录（单击定位对象）"));
    records_=new QListWidget;
    records_->setObjectName("measureRecords");
    layout->addWidget(records_,1);
    auto* buttons=new QHBoxLayout;
    auto* clear=new QPushButton("清空记录");
    connect(clear,&QPushButton::clicked,this,&MeasurePanel::clearRecords);
    buttons->addWidget(clear);
    buttons->addStretch(1);
    layout->addLayout(buttons);
    connect(records_,&QListWidget::itemClicked,this,[this](QListWidgetItem* item){
        QVector<EntityId> ids;
        for(const auto& value:item->data(Qt::UserRole).toList()) ids.append(value.toULongLong());
        emit recordActivated(ids);
    });
}
void MeasurePanel::setLive(QString text) {
    live_->setText(text.isEmpty()?"—":text);
}
void MeasurePanel::setUnitNote(QString text) {
    note_->setText(text);
    note_->setVisible(!text.isEmpty());
}
void MeasurePanel::addRecord(QString text, const QVector<EntityId>& ids) {
    auto* item=new QListWidgetItem(QTime::currentTime().toString("HH:mm:ss")+" · "+text,records_);
    QVariantList values;
    for(auto id:ids) if(id) values.append(QVariant::fromValue<qulonglong>(id));
    item->setData(Qt::UserRole,values);
    records_->scrollToBottom();
}
void MeasurePanel::clearRecords() {
    records_->clear();
}
}
