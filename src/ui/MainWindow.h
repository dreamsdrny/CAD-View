#pragma once
#include "CadCanvas.h"
#include "engineering/Analysis.h"
#include <QMainWindow>
#include <QSettings>
#include <QPointer>
#include <functional>

class QTabWidget;class QTableWidget;class QComboBox;class QListWidget;class QTextEdit;class QLineEdit;class QLabel;class QAction;class QPushButton;
namespace cad::ui {
class MeasurePanel;
class RecoveryManager;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(bool demo=false, QString recoveryDirectory={});
    CadCanvas* canvas() const;
    void addDocument(Document document, QString recoveredFrom={});
    void openPath(QString path);
    void executeCommand(QString text);
    void showStartupTutorial(bool startup=false);
    void showRecoveryDialog(bool startup=false);
signals:
    void fileOpened(QString path);
    void fileOpenFailed(QString message);
protected:
    void closeEvent(QCloseEvent*) override;
private:
    QTabWidget* tabs_;
    QTableWidget *properties_,*layers_;
    QComboBox *currentLayer_,*scope_;
    QListWidget* results_;
    MeasurePanel* measurePanel_=nullptr;
    QTextEdit* console_;
    QLineEdit* command_;
    QLabel *coordinate_,*count_;
    QLabel* textEditHint_=nullptr;
    QPushButton* textEditButton_=nullptr;
    QAction* editTextAction_=nullptr;
    QAction *undo_,*redo_,*snap_,*ortho_,*polar_,*grid_;
    QMap<Tool,QAction*> tools_;
    QSettings settings_{"EngineeringTools","CADWorkbenchNative"};
    bool refreshing_=false;
    int activeJobs_=0;
    QHash<CadCanvas*,int> jobsByCanvas_;
    RecoveryManager* recovery_=nullptr;
    QHash<CadCanvas*,QString> recoveryIds_;
    QAction* action(QString text,std::function<void()> callback,QString shortcut={});
    void buildUi();
    void refresh();
    void refreshProperties();
    void log(QString text);
    void error(QString text);
    void editProperties();
    void editText();
    void editSelected(QString operation);
    void runAnalysis(QString operation);
    QVector<Entity> scopeEntities() const;
    void changeLayer(int row,int column);
    void addLayer();
    void deleteLayer();
    void saveFile(bool saveAs=false,std::function<void(bool)> completion={});
    void exportPdf();
    void panelize();
    void closeTab(int index);
    bool confirmDiscard(CadCanvas* canvas,bool closingWindow=false);
    void beginJob(CadCanvas* canvas,QString message);
    void endJob(CadCanvas* canvas);
};
QString applicationStyle();
}
