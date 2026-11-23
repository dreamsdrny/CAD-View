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
QString DwgAdapter::findReader() {
    auto executable=[](const QString& path){QFileInfo info(path);return info.isFile()&&info.isExecutable()?info.absoluteFilePath():QString{};};
    // An explicit environment override is authoritative, including an invalid/empty path.
    if(qEnvironmentVariableIsSet("CAD_LIBREDWG_READER"))return executable(qEnvironmentVariable("CAD_LIBREDWG_READER"));
    for(const auto& path:QStringList{QCoreApplication::applicationDirPath()+"/tools/libredwg/dwg2dxf.exe",
            QCoreApplication::applicationDirPath()+"/dwg2dxf.exe",QStandardPaths::findExecutable("dwg2dxf.exe")}) {
        auto found=executable(path);if(!found.isEmpty())return found;
    }
    return {};
}
QString DwgAdapter::version(const QString& path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))fail("无法读取 DWG："+file.errorString());
    auto header=file.read(6);
    const QMap<QByteArray,QString> versions{{"AC1009","R11/R12"},{"AC1012","R13"},{"AC1014","R14"},
        {"AC1015","2000/2002"},{"AC1018","2004/2006"},{"AC1021","2007/2009"},
        {"AC1024","2010/2012"},{"AC1027","2013/2017"},{"AC1032","2018 系列"}};
    if(!versions.contains(header))fail("不是受支持的 DWG 文件头（"+QString::fromLatin1(header.toHex())+"）。文件可能损坏、加密或仅修改了扩展名。");
    return "DWG "+versions.value(header)+" / "+QString::fromLatin1(header);
}
static Document readWithLibreDwg(const QString& path,const QString& reader,const DxfReadOptions& options,QString& diagnosticsText) {
    QTemporaryDir temp;if(!temp.isValid())fail("无法创建 DWG 读取临时目录。");
    QFile source(path),copied(temp.filePath("drawing.dwg"));
    if(!source.open(QIODevice::ReadOnly)||!copied.open(QIODevice::WriteOnly))fail("无法复制 DWG 读取文件。");
    while(!source.atEnd()) {
        if(options.cancelled&&options.cancelled())fail("DWG 读取已取消。");
        auto bytes=source.read(1024*1024);
        if(bytes.isEmpty()&&source.error()!=QFileDevice::NoError)fail("DWG 读取失败："+source.errorString());
        if(copied.write(bytes)!=bytes.size())fail("DWG 临时文件写入失败："+copied.errorString());
    }
    copied.close();source.close();
    // Relative ASCII arguments work even when the Windows user/temp path is Chinese.
    QProcess process;process.setWorkingDirectory(temp.path());process.start(reader,{"-v1","-o","converted.dxf","drawing.dwg"});
    if(!process.waitForStarted(10000))fail("无法启动内置 DWG 读取引擎："+process.errorString());
    QElapsedTimer timer;timer.start();QByteArray diagnostics;
    auto drain=[&]{diagnostics+=process.readAllStandardError();process.readAllStandardOutput();if(diagnostics.size()>65536)diagnostics=diagnostics.right(65536);};
    do {
        process.waitForFinished(100);drain();
        bool cancelled=options.cancelled&&options.cancelled();
        if(cancelled||timer.elapsed()>120000||QFileInfo(temp.filePath("converted.dxf")).size()>options.maxBytes) {
            process.kill();process.waitForFinished(5000);
            fail(cancelled?"DWG 读取已取消。":timer.elapsed()>120000?"DWG 读取超时。":"DWG 转换结果超过大小限制。");
        }
    }while(process.state()!=QProcess::NotRunning);
    drain();
    if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0)
        fail("LibreDWG 读取失败（退出码 "+QString::number(process.exitCode())+"）："+QString::fromUtf8(diagnostics));
    auto d=DxfAdapter::read(temp.filePath("converted.dxf"),options);
    diagnosticsText=QString::fromUtf8(diagnostics).trimmed();
    return d;
}
void DwgAdapter::convert(const QString& input,const QString& output,const QString& exe,const std::function<bool()>& cancelled) {
    auto converter=findConverter(exe);
    if(converter.isEmpty())fail("DWG 保存需要 ODA File Converter。请通过“文件 → 配置 DWG 转换器”选择程序，或另存为 DXF。内置 LibreDWG 仅用于读取。");
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
    if(QFileInfo(path).size()>options.maxBytes)fail("DWG 文件超过大小限制。");
    auto format=version(path);
    auto executable=findConverter(converter);
    QTemporaryDir temp;if(!temp.isValid())fail("无法创建转换临时目录。");auto intermediate=temp.path()+"/converted.dxf";
    QString diagnostics,engine;Document d;
    if(!executable.isEmpty()){convert(path,intermediate,executable,options.cancelled);d=DxfAdapter::read(intermediate,options);engine="ODA File Converter";}
    else {
        auto reader=findReader();if(reader.isEmpty())fail("未找到 DWG 读取引擎。请使用含 tools/libredwg 的完整程序包，或配置 ODA File Converter。不会自动改读同名 DXF。");
        d=readWithLibreDwg(path,reader,options,diagnostics);engine="LibreDWG";
    }
    d.path=path;d.title=QFileInfo(path).completeBaseName();
    d.warnings.prepend("已从原始 DWG 读取："+format+"；引擎："+engine+"。专有代理对象、3D 和布局可能不完整；转换后的 DXF 不保证无损保留原 DWG。建议另存为 DXF，保留原图备份。");
    if(!diagnostics.isEmpty())d.warnings.append("DWG 引擎诊断："+diagnostics);
    if(d.entities.isEmpty())d.warnings.append("此 DWG 已解析，但没有可显示的二维模型对象；可能为空图、仅含布局/3D/不支持对象，或转换引擎未导出图形。不能将空白画布视为完整打开。");
    return d;
}
void DwgAdapter::save(const Document& d,const QString& path,const QString& converter) {
    if(QFileInfo(path).suffix().compare("dwg",Qt::CaseInsensitive)!=0){DxfAdapter::write(d,path);return;}
    QTemporaryDir temp;if(!temp.isValid())fail("无法创建转换临时目录。");auto intermediate=temp.path()+"/converted.dxf";DxfAdapter::write(d,intermediate);convert(intermediate,path,converter);
}
}
