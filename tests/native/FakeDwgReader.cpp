#include <QCoreApplication>
#include <QFile>
#include <QThread>
#include <cstdio>

// Process-contract tests only. Actual DWG decoding is tested with upstream samples.
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments()!=QStringList{app.arguments().first(),"-v1","-o","converted.dxf","drawing.dwg"})return 10;
    QFile source("drawing.dwg");if(!source.open(QIODevice::ReadOnly))return 11;
    auto bytes=source.readAll().mid(7);
    if(bytes=="WAIT"){QThread::sleep(10);return 0;}
    if(bytes=="FAIL"){fputs("unsupported proxy data",stderr);return 17;}
    QFile output("converted.dxf");if(!output.open(QIODevice::WriteOnly))return 12;
    if(bytes=="EMPTY")return 0;
    if(bytes.startsWith("WARN\n")){fputs("Warning: skipped proxy object",stderr);bytes.remove(0,5);}
    return output.write(bytes)==bytes.size()?0:13;
}
