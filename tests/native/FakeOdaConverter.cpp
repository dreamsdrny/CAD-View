#include <QCoreApplication>
#include <QDir>
#include <QFile>

// Contract stub only: this checks process arguments, not real DWG compatibility.
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);auto args=app.arguments();
    if(args.size()!=8||args[3]!="ACAD2018"||args[5]!="0"||args[6]!="1")return 10;
    if(args[7]!="drawing.dwg"&&args[7]!="drawing.dxf")return 11;
    QDir input(args[1]);auto files=input.entryList(QDir::Files);
    if(files.size()!=1||files.first()!=args[7])return 12;
    QFile file(input.filePath(args[7]));if(!file.open(QIODevice::ReadOnly))return 13;
    auto bytes=file.readAll();if(bytes=="FAIL")return 17;
    QFile output(QDir(args[2]).filePath("drawing."+args[4].toLower()));
    if(!output.open(QIODevice::WriteOnly))return 14;
    if(bytes=="EMPTY")return 0;
    return output.write(bytes)==bytes.size()?0:15;
}
