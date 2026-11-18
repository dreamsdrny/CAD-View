#include "DwgAdapter.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QStandardPaths>
#include <stdexcept>

namespace cad::io {
static void fail(QString message){throw std::runtime_error(message.toUtf8().constData());}
QString DwgAdapter::findConverter(const QString& configured) {
    auto executable=[](QString path) {
        QFileInfo info(path);
        return info.isFile()&&info.isExecutable()?info.absoluteFilePath():QString{};
    };
    for(const auto& path:QStringList{configured,qEnvironmentVariable("CAD_ODA_CONVERTER"),
            QCoreApplication::applicationDirPath()+"/ODAFileConverter.exe",
            QCoreApplication::applicationDirPath()+"/oda/ODAFileConverter.exe",
            QCoreApplication::applicationDirPath()+"/tools/oda/ODAFileConverter.exe",
            QStandardPaths::findExecutable("ODAFileConverter.exe")}) {
        auto found=executable(path);if(!found.isEmpty())return found;
    }
    for(const auto& root:QStringList{qEnvironmentVariable("ProgramW6432"),qEnvironmentVariable("ProgramFiles"),qEnvironmentVariable("ProgramFiles(x86)")}) {
        if(root.isEmpty())continue;
        for(const auto& vendor:QStringList{root,root+"/ODA",root+"/Open Design Alliance"}) {
            QDir dir(vendor);
            for(const auto& name:dir.entryList({"ODAFileConverter*","ODA File Converter*"},QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name|QDir::Reversed)) {
                auto found=executable(dir.filePath(name+"/ODAFileConverter.exe"));if(!found.isEmpty())return found;
            }
        }
    }
    return {};
}
QString DwgAdapter::companionDxf(const QString& dwgPath) {
    QFileInfo dwg(dwgPath);if(!dwg.isFile()||dwg.suffix().compare("dwg",Qt::CaseInsensitive)!=0)return {};
    auto expected=dwg.completeBaseName()+".dxf";
    for(const auto& name:dwg.dir().entryList(QDir::Files,QDir::Name))
        if(name.compare(expected,Qt::CaseInsensitive)==0)return dwg.dir().absoluteFilePath(name);
    return {};
}
void DwgAdapter::convert(const QString& input,const QString& output,const QString& exe,const std::function<bool()>& cancelled) {
    auto converter=findConverter(exe);
    if(converter.isEmpty())fail("未找到 ODA File Converter。请安装后通过“文件 → 配置 DWG 转换器”选择 ODAFileConverter.exe；或将同名 ASCII DXF 放在 DWG 所在目录后打开。DWG 保存必须使用转换器。");
    if(!QFileInfo(input).isFile())fail("待转换文件不存在："+input);
    QTemporaryDir temp;if(!temp.isValid())fail("无法创建转换临时目录。");
    auto in=temp.path()+"/input",out=temp.path()+"/output";if(!QDir().mkpath(in)||!QDir().mkpath(out))fail("无法创建转换输入/输出目录。");
    // ODA's last argument is a wildcard filter. Never pass user filenames containing [ or ].
    auto stagedName="drawing."+QFileInfo(input).suffix().toLower();
    QFile source(input),copied(in+"/"+stagedName);
    if(!source.open(QIODevice::ReadOnly)||!copied.open(QIODevice::WriteOnly))fail("无法复制待转换文件。");
    while(!source.atEnd()){if(cancelled&&cancelled())fail("DWG 转换已取消。");auto data=source.read(1024*1024);if(data.isEmpty()&&source.error()!=QFileDevice::NoError)fail("读取待转换文件失败："+source.errorString());if(copied.write(data)!=data.size())fail("复制待转换文件失败："+copied.errorString());}
    copied.close();source.close();
    QProcess process;process.start(converter,{in,out,"ACAD2018",QFileInfo(output).suffix().toUpper(),"0","1",stagedName});
    if(!process.waitForStarted(10000))fail("无法启动 ODA 转换器："+process.errorString());
    QElapsedTimer timer;timer.start();
    while(process.state()!=QProcess::NotRunning&&!process.waitForFinished(100)) {
        if((cancelled&&cancelled())||timer.elapsed()>120000){process.kill();process.waitForFinished();fail(cancelled&&cancelled()?"DWG 转换已取消。":"DWG 转换超时。");}
    }
    QFile file(out+"/drawing."+QFileInfo(output).suffix().toLower());
    if(!file.exists())file.setFileName(out+"/drawing."+QFileInfo(output).suffix().toUpper());
    if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0||!file.open(QIODevice::ReadOnly))fail("ODA 转换失败（退出码 "+QString::number(process.exitCode())+"）："+QString::fromLocal8Bit(process.readAllStandardError())+QString::fromLocal8Bit(process.readAllStandardOutput()));
    if(file.size()==0)fail("ODA 转换结果为空，未保存目标文件。");
    QSaveFile target(output);if(!target.open(QIODevice::WriteOnly))fail("无法保存转换结果："+target.errorString());
    while(!file.atEnd()){if(cancelled&&cancelled())fail("DWG 转换已取消。");auto data=file.read(1024*1024);if(data.isEmpty()&&file.error()!=QFileDevice::NoError)fail("读取转换结果失败："+file.errorString());if(target.write(data)!=data.size())fail("无法保存转换结果："+target.errorString());}
    if(!target.commit())fail("无法保存转换结果："+target.errorString());
}
Document DwgAdapter::open(const QString& path,const QString& converter,const DxfReadOptions& options) {
    if(QFileInfo(path).suffix().compare("dwg",Qt::CaseInsensitive)!=0)return DxfAdapter::read(path,options);
    if(!QFileInfo(path).isFile())fail("DWG 文件不存在："+path);
    if(options.cancelled&&options.cancelled())fail("DWG 读取已取消。");
    auto executable=findConverter(converter);
    if(executable.isEmpty()) {
        auto companion=companionDxf(path);
        if(!companion.isEmpty()) {
            auto d=DxfAdapter::read(companion,options);
            d.title=QFileInfo(companion).completeBaseName()+"（DXF 副本）";
            d.warnings.prepend("未找到 ODA 转换器，已打开同目录同名 DXF 副本："+companion+"。这不是直接读取 DWG，无法保证两份图纸一致；保存将写入 DXF，不会覆盖原 DWG。");
            return d;
        }
    }
    QTemporaryDir temp;if(!temp.isValid())fail("无法创建转换临时目录。");auto intermediate=temp.path()+"/converted.dxf";convert(path,intermediate,executable,options.cancelled);
    auto d=DxfAdapter::read(intermediate,options);d.path=path;d.title=QFileInfo(path).completeBaseName();return d;
}
void DwgAdapter::save(const Document& d,const QString& path,const QString& converter) {
    if(QFileInfo(path).suffix().compare("dwg",Qt::CaseInsensitive)!=0){DxfAdapter::write(d,path);return;}
    QTemporaryDir temp;if(!temp.isValid())fail("无法创建转换临时目录。");auto intermediate=temp.path()+"/converted.dxf";DxfAdapter::write(d,intermediate);convert(intermediate,path,converter);
}
}
