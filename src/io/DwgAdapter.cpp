#include "DwgAdapter.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <stdexcept>

namespace cad::io {
static void fail(QString message){throw std::runtime_error(message.toUtf8().constData());}
void DwgAdapter::convert(const QString& input,const QString& output,const QString& exe,const std::function<bool()>& cancelled) {
    if(!QFileInfo::exists(exe))fail("DWG 需要在文件菜单配置 ODA File Converter。");
    QTemporaryDir temp;if(!temp.isValid())fail("无法创建转换临时目录。");
    auto in=temp.path()+"/input",out=temp.path()+"/output";if(!QDir().mkpath(in)||!QDir().mkpath(out))fail("无法创建转换输入/输出目录。");
    QFile source(input),copied(in+"/"+QFileInfo(input).fileName());
    if(!source.open(QIODevice::ReadOnly)||!copied.open(QIODevice::WriteOnly))fail("无法复制待转换文件。");
    while(!source.atEnd()){if(cancelled&&cancelled())fail("DWG 转换已取消。");auto data=source.read(1024*1024);if(data.isEmpty()&&source.error()!=QFileDevice::NoError)fail("读取待转换文件失败："+source.errorString());if(copied.write(data)!=data.size())fail("复制待转换文件失败："+copied.errorString());}
    copied.close();source.close();
    QProcess process;process.start(exe,{in,out,"ACAD2018",QFileInfo(output).suffix().toUpper(),"0","1",QFileInfo(input).fileName()});
    if(!process.waitForStarted(10000))fail("无法启动 ODA 转换器。");
    QElapsedTimer timer;timer.start();
    while(process.state()!=QProcess::NotRunning&&!process.waitForFinished(100)) {
        if((cancelled&&cancelled())||timer.elapsed()>120000){process.kill();process.waitForFinished();fail(cancelled&&cancelled()?"DWG 转换已取消。":"DWG 转换超时。");}
    }
    QFile file(out+"/"+QFileInfo(input).completeBaseName()+"."+QFileInfo(output).suffix());
    if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0||!file.open(QIODevice::ReadOnly))fail("ODA 转换失败（退出码 "+QString::number(process.exitCode())+"）："+QString::fromLocal8Bit(process.readAllStandardError())+QString::fromLocal8Bit(process.readAllStandardOutput()));
    QSaveFile target(output);if(!target.open(QIODevice::WriteOnly))fail("无法保存转换结果："+target.errorString());
    while(!file.atEnd()){if(cancelled&&cancelled())fail("DWG 转换已取消。");auto data=file.read(1024*1024);if(data.isEmpty()&&file.error()!=QFileDevice::NoError)fail("读取转换结果失败："+file.errorString());if(target.write(data)!=data.size())fail("无法保存转换结果："+target.errorString());}
    if(!target.commit())fail("无法保存转换结果："+target.errorString());
}
Document DwgAdapter::open(const QString& path,const QString& converter,const DxfReadOptions& options) {
    if(QFileInfo(path).suffix().compare("dwg",Qt::CaseInsensitive)!=0)return DxfAdapter::read(path,options);
    QTemporaryDir temp;if(!temp.isValid())fail("无法创建转换临时目录。");auto intermediate=temp.path()+"/converted.dxf";convert(path,intermediate,converter,options.cancelled);
    auto d=DxfAdapter::read(intermediate,options);d.path=path;d.title=QFileInfo(path).completeBaseName();return d;
}
void DwgAdapter::save(const Document& d,const QString& path,const QString& converter) {
    if(QFileInfo(path).suffix().compare("dwg",Qt::CaseInsensitive)!=0){DxfAdapter::write(d,path);return;}
    QTemporaryDir temp;if(!temp.isValid())fail("无法创建转换临时目录。");auto intermediate=temp.path()+"/converted.dxf";DxfAdapter::write(d,intermediate);convert(intermediate,path,converter);
}
}
