#pragma once
#include "CadCanvas.h"
#include "engineering/Analysis.h"
#include <QMainWindow>
#include <QSettings>
#include <QPointer>
#include <functional>

class QTabWidget;class QTableWidget;class QComboBox;class QListWidget;class QTextEdit;class QLineEdit;class QLabel;class QAction;
namespace cad::ui {
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(bool demo=true);
    CadCanvas* canvas() const;
    void addDocument(Document document);
    void openPath(QString path);
    void executeCommand(QString text);
protected:
    void closeEvent(QCloseEvent*) override;
private:
    QTabWidget* tabs_;
    QTableWidget *properties_,*layers_;
    QComboBox *currentLayer_,*scope_;
    QListWidget* results_;
    QTextEdit* console_;
    QLineEdit* command_;
    QLabel *coordinate_,*count_;
    QAction *undo_,*redo_,*snap_,*ortho_,*polar_,*grid_;
    QMap<Tool,QAction*> tools_;
    QSettings settings_{"EngineeringTools","CADWorkbenchNative"};
    bool refreshing_=false;
    int activeJobs_=0;
    QHash<CadCanvas*,int> jobsByCanvas_;
    QAction* action(QString text,std::function<void()> callback,QString shortcut={});
    void buildUi();
    void refresh();
    void refreshProperties();
    void log(QString text);
    void error(QString text);
    void editProperties();
    void editSelected(QString operation);
    void runAnalysis(QString operation);
    QVector<Entity> scopeEntities() const;
    void changeLayer(int row,int column);
    void addLayer();
    void deleteLayer();
    void saveFile(bool saveAs=false,std::function<void(bool)> completion={});
    void exportPdf();
    void closeTab(int index);
    bool confirmDiscard(CadCanvas* canvas,bool closingWindow=false);
    void beginJob(CadCanvas* canvas,QString message);
    void endJob(CadCanvas* canvas);
};
QString applicationStyle();
}
