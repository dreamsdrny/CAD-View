#include "BrandConfig.h"
#include <QGuiApplication>
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QTextStream>

namespace {
QImage renderIcon(int size) {
    // Draw in normalized 256px coordinates. Small icons use the Q mark;
    // 48px+ icons include the full Qiao label for readability.
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size / 256.0, size / 256.0);

    QLinearGradient gradient(24, 12, 226, 246);
    gradient.setColorAt(0, QColor("#a78bfa"));
    gradient.setColorAt(0.5, QColor("#7c3aed"));
    gradient.setColorAt(1, QColor("#4c1d95"));
    painter.setBrush(gradient);
    painter.setPen(QPen(QColor("#c4b5fd"), 3));
    painter.drawRoundedRect(QRectF(8, 8, 240, 240), 48, 48);

    // CAD-inspired construction accents in the background.
    painter.setPen(QPen(QColor(237, 233, 254, 45), 3));
    painter.drawLine(QPointF(28, 70), QPointF(228, 70));
    painter.drawLine(QPointF(28, 180), QPointF(228, 180));
    painter.drawLine(QPointF(70, 28), QPointF(70, 228));
    painter.drawLine(QPointF(186, 28), QPointF(186, 228));

    const bool showLabel = size >= 48;
    QPointF center(124, showLabel ? 100 : 119);
    double radius = showLabel ? 52 : 67;
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor("#ffffff"), showLabel ? 18 : 24, Qt::SolidLine, Qt::RoundCap));
    painter.drawEllipse(center, radius, radius);
    painter.drawLine(center + QPointF(23, 27), center + QPointF(radius + 12, radius + 15));

    if (showLabel) {
        QFont font("Segoe UI");
        font.setPixelSize(44);
        font.setWeight(QFont::DemiBold);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        painter.setFont(font);
        painter.setPen(QColor("#f5f3ff"));
        painter.drawText(QRectF(20, 180, 216, 53), Qt::AlignCenter,
                         QString::fromUtf8(cad::branding::config::iconLabel));
    }
    return image;
}

bool writeIco(const QString& path) {
    const QList<int> sizes{16, 24, 32, 48, 64, 128, 256};
    QList<QByteArray> images;
    for (int size : sizes) {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::WriteOnly) || !renderIcon(size).save(&buffer, "PNG"))
            return false;
        images.append(bytes);
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + 16 * sizes.size();
    for (qsizetype i = 0; i < sizes.size(); ++i) {
        quint8 dimension = sizes[i] == 256 ? 0 : quint8(sizes[i]);
        stream << dimension << dimension << quint8(0) << quint8(0) << quint16(1) << quint16(32)
               << quint32(images[i].size()) << offset;
        offset += images[i].size();
    }
    for (const auto& bytes : images)
        stream.writeRawData(bytes.constData(), bytes.size());
    return stream.status() == QDataStream::Ok && file.commit();
}
}

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
#ifdef Q_OS_WIN
    // The offscreen plugin does not automatically enumerate Windows fonts.
    QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/seguisb.ttf");
#endif
    const auto args = application.arguments();
    if (args.size() != 2 || !QDir(args[1]).exists()) {
        QTextStream(stderr) << "Usage: cad_icon_generator <existing output directory>\n";
        return 1;
    }
    QDir output(args[1]);
    if (!writeIco(output.filePath("qiao.ico")) || !renderIcon(256).save(output.filePath("qiao.png"))) {
        QTextStream(stderr) << "Could not write Qiao icon assets.\n";
        return 1;
    }
    return 0;
}
