#include "io/DwgAdapter.h"
#include "io/DxfAdapter.h"
#include "ui/CadCanvas.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QSysInfo>
#include <QWheelEvent>
#include <QMouseEvent>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <memory>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_LINUX)
#include <sys/resource.h>
#endif

using namespace cad;
namespace {
double milliseconds(const QElapsedTimer& timer){return timer.nsecsElapsed()/1000000.0;}
qint64 peakResidentBytes() {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);
    return GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory))?qint64(memory.PeakWorkingSetSize):-1;
#elif defined(Q_OS_LINUX)
    rusage usage{};return getrusage(RUSAGE_SELF,&usage)==0?qint64(usage.ru_maxrss)*1024:-1;
#else
    return -1;
#endif
}
Document synthetic(int count) {
    Document d;
    for(int i=0;i<count;++i){double x=(i%1000)*3.,y=(i/1000)*3.;Entity e;e.points={{x,y},{x+1,y+1}};d.load(e);}
    return d;
}
void paint(ui::CadCanvas& canvas,QImage& image) {
    QPainter painter(&image);canvas.render(&painter);
}
QJsonObject stats(QVector<double> values) {
    std::sort(values.begin(),values.end());double sum=0;for(double v:values)sum+=v;
    return {{"min",values.first()},{"median",values[values.size()/2]},{"p95",values[int(std::ceil(values.size()*0.95))-1]},{"max",values.last()},{"mean",sum/values.size()}};
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);QCoreApplication::setApplicationName("cad_benchmark");
    QCommandLineParser parser;parser.setApplicationDescription("Reproducible offscreen CAD load and rendering benchmark");parser.addHelpOption();
    QCommandLineOption input("input","Input DXF or DWG path (omit for generated lines).","path");
    QCommandLineOption count("count","Generated line count, 1..500000.","n","100000");
    QCommandLineOption iterations("iterations","Repeated paint count, 1..100.","n","10");
    QCommandLineOption output("output","Write JSON results to this path; otherwise stdout.","path");
    parser.addOptions({input,count,iterations,output});parser.process(app);
    bool ok=false;int n=parser.value(count).toInt(&ok);if(!ok||n<1||n>500000)parser.showHelp(2);
    int repeats=parser.value(iterations).toInt(&ok);if(!ok||repeats<1||repeats>100)parser.showHelp(2);
    try {
        QElapsedTimer timer,total;total.start();timer.start();Document d;QString source=parser.value(input);
        if(source.isEmpty())d=synthetic(n);else d=io::DwgAdapter::open(source,{});
        double load=milliseconds(timer);qint64 afterLoad=peakResidentBytes();int entityCount=int(d.entities.size());
        QJsonArray warnings;for(const auto& w:d.warnings)warnings.append(w);
        auto doc=std::make_shared<Document>(std::move(d));timer.restart();
        ui::CadCanvas canvas(doc);canvas.resize(1280,800);canvas.grid=false;
        double prepare=milliseconds(timer);timer.restart();canvas.fitDrawing();double fit=milliseconds(timer);
        QImage image(1280,800,QImage::Format_ARGB32_Premultiplied);timer.restart();paint(canvas,image);double firstPaint=milliseconds(timer);
        QVector<double> warm;for(int i=0;i<repeats;++i){timer.restart();paint(canvas,image);warm.append(milliseconds(timer));}
        QPointF center(640,400);QWheelEvent zoomIn(center,center,QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        timer.restart();QApplication::sendEvent(&canvas,&zoomIn);paint(canvas,image);double zoom=milliseconds(timer);
        QVector<double> pan;for(int i=0;i<repeats;++i) {
            QPointF start(640,400),end(650+i,405);QMouseEvent press(QEvent::MouseButtonPress,start,start,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
            QMouseEvent move(QEvent::MouseMove,end,end,Qt::NoButton,Qt::MiddleButton,Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease,end,end,Qt::MiddleButton,Qt::NoButton,Qt::NoModifier);
            timer.restart();QApplication::sendEvent(&canvas,&press);QApplication::sendEvent(&canvas,&move);QApplication::sendEvent(&canvas,&release);paint(canvas,image);pan.append(milliseconds(timer));
        }
        QJsonObject result{{"schema",1},{"mode",source.isEmpty()?"synthetic_lines":"file"},{"input",source.isEmpty()?QString():QFileInfo(source).absoluteFilePath()},
            {"inputBytes",source.isEmpty()?0:double(QFileInfo(source).size())},{"requestedEntities",source.isEmpty()?n:0},{"displayEntities",entityCount},
            {"platform",QSysInfo::prettyProductName()},{"cpuArchitecture",QSysInfo::currentCpuArchitecture()},{"qt",qVersion()},
            {"viewport",QJsonArray{1280,800}},{"iterations",repeats},{"warnings",warnings},
            {"timeMs",QJsonObject{{"loadOrGenerate",load},{"canvasAndIndex",prepare},{"fit",fit},{"firstPaint",firstPaint},{"warmPaint",stats(warm)},{"zoomAndPaint",zoom},{"panAndPaint",stats(pan)},{"total",milliseconds(total)}}},
            {"peakResidentBytesAfterLoad",double(afterLoad)},{"peakResidentBytes",double(peakResidentBytes())},
            {"notes","Offscreen QWidget render; peak memory is process-wide; generated mode measures document generation, not DXF parsing."}};
        QByteArray json=QJsonDocument(result).toJson();
        if(parser.isSet(output)){QSaveFile file(parser.value(output));if(!file.open(QIODevice::WriteOnly)||file.write(json)!=json.size()||!file.commit())throw std::runtime_error("Cannot write benchmark output");}
        else {QFile file;file.open(stdout,QIODevice::WriteOnly);file.write(json);}
        return 0;
    }catch(const std::exception& error){fprintf(stderr,"Benchmark failed: %s\n",error.what());return 1;}
}
