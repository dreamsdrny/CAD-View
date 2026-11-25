#pragma once
#include "engineering/FpcMaterialPlanner.h"
#include "engineering/Panelization.h"
#include <QDialog>
#include <functional>
#include <optional>

class QComboBox;class QSpinBox;class QDoubleSpinBox;class QCheckBox;class QLabel;class QTableWidget;class QPushButton;class QTabWidget;class QTimer;
namespace cad::ui {
class CadCanvas;
class MaterialPlanSection;
class PanelizationDialog : public QDialog {
public:
    PanelizationDialog(const Document& source,const QSet<EntityId>& selection,QString converter,QWidget* parent=nullptr);
    ~PanelizationDialog() override;
    Document outputDocument() const;
    void accept() override;
    // The merged material tab uses this callback so generated material drawings
    // open in the main window instead of a separate material dialog.
    std::function<void(Document)> onDrawing;
    // Test / automation hook: material report export without a file dialog.
    void saveMaterialReport(QString path,bool pdf=false);
protected:
    void reject() override;
private:
    Document original_;
    QString converter_;
    CadCanvas *sourceCanvas_,*preview_;
    MaterialPlanSection* material_=nullptr;
    QComboBox *contours_,*rotation_,*content_;
    QSpinBox *rows_,*columns_,*precision_;
    QCheckBox *fixed_,*fit_,*alternate_,*alternateColumns_,*pitch_,*rails_,*autoBridges_,*dimensions_,*reference_,*dxf_,*dwg_,*pdf_;
    QDoubleSpinBox *width_,*height_,*gapX_,*gapY_,*offset_,*minimum_,*left_,*right_,*bottom_,*top_,*railWidth_,*railClearance_,*bridgeWidth_,*tolerance_;
    QTableWidget* bridges_;
    QLabel* status_;
    QPushButton* generate_;
    QTabWidget* tabs_;
    QTimer* previewTimer_;
    std::optional<Document> generated_;
    QVector<Entity> candidates_;
    std::optional<engineering::panel::Result> result_;
    bool refreshing_=false;
    bool automaticCandidates_=false;
    void confirmContour();
    void automaticPanel();
    void setCandidates(QVector<Entity> candidates);
    void regenerate();
    void updatePreview();
    void addBridge(engineering::panel::Bridge bridge);
    void applyMaterial(const engineering::fpc::SetSpec& set);
    void exportFiles();
    engineering::panel::Options options() const;
};
}
