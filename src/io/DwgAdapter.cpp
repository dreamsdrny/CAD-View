#include "DwgAdapter.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <stdexcept>

namespace cad::io {
static void fail(QString message){throw std::runtime_error(message.toUtf8().constData());}
void DwgAdapter::convert(const QString& input,const QString& output,const QString& exe) {
    if(!QFileInfo::exists(exe))fail("DWG 需要在文件菜单配置 ODA File Converter。");
    QTemporaryDir temp;if(!temp.isValid())fail("无法创建转换临时目录。");
    auto in=temp.path()+"/input",out=temp.path()+"/output";QDir().mkpath(in);QDir().mkpath(out);
    if(!QFile::copy(input,in+"/"+QFileInfo(input).fileName()))fail("无法复制待转换文件。");
    QProcess process;process.start(exe,{in,out,"ACAD2018",QFileInfo(output).suffix().toUpper(),"0","1",QFileInfo(input).fileName()});
    if(!process.waitForStarted(10000))fail("无法启动 ODA 转换器。");
    if(!process.waitForFinished(120000)){process.kill();process.waitForFinished();fail("DWG 转换超时。");}
    QFile file(out+"/"+QFileInfo(input).completeBaseName()+"."+QFileInfo(output).suffix());
    if(process.exitCode()!=0||!file.open(QIODevice::ReadOnly))fail("ODA 转换失败："+QString::fromLocal8Bit(process.readAllStandardError()));
    QSaveFile target(output);auto data=file.readAll();
    if(!target.open(QIODevice::WriteOnly)||target.write(data)!=data.size()||!target.commit())fail("无法保存转换结果。");
}
Document DwgAdapter::open(const QString& path,const QString& converter) {
    if(QFileInfo(path).suffix().compare("dwg",Qt::CaseInsensitive)!=0)return DxfAdapter::read(path);
    QTemporaryDir temp;auto intermediate=temp.path()+"/converted.dxf";convert(path,intermediate,converter);
    auto d=DxfAdapter::read(intermediate);d.path=path;d.title=QFileInfo(path).completeBaseName();return d;
}
void DwgAdapter::save(const Document& d,const QString& path,const QString& converter) {
    if(QFileInfo(path).suffix().compare("dwg",Qt::CaseInsensitive)!=0){DxfAdapter::write(d,path);return;}
    QTemporaryDir temp;auto intermediate=temp.path()+"/converted.dxf";DxfAdapter::write(d,intermediate);convert(intermediate,path,converter);
}
}
