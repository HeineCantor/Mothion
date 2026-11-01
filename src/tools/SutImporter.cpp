#include "tools/SutImporter.h"

#include "core/brush/Effector.h"

#include <QFileInfo>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <QVariant>

#include <algorithm>
#include <deque>
#include <span>

namespace mothion::tools {

namespace {

using Params = QHash<QString, QVariant>;

// --- binary helpers ---------------------------------------------------------

std::span<const std::uint8_t> bytesOf(const QByteArray &data)
{
    return {reinterpret_cast<const std::uint8_t *>(data.constData()), std::size_t(data.size())};
}

bool readInt32BE(const QByteArray &data, qsizetype offset, qint32 &out)
{
    if (offset < 0 || offset + 4 > data.size())
        return false;
    const auto *p = reinterpret_cast<const uchar *>(data.constData() + offset);
    out = qint32((quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]));
    return true;
}

QString utf16le(const QByteArray &data, qsizetype offset, qsizetype bytes)
{
    QString s;
    for (qsizetype i = offset; i + 1 < offset + bytes && i + 1 < data.size(); i += 2) {
        const char16_t c = char16_t(uchar(data[i]) | (uchar(data[i + 1]) << 8));
        if (c == 0)
            break;
        s.append(QChar(c));
    }
    return s;
}

// Files of a tar archive (ustar), by path.
QHash<QString, QByteArray> untar(const QByteArray &tar)
{
    QHash<QString, QByteArray> files;
    qsizetype offset = 0;
    while (offset + 512 <= tar.size()) {
        const QByteArray header = tar.mid(offset, 512);
        if (header.count('\0') == 512)
            break;
        QString name = QString::fromUtf8(header.constData(), qstrnlen(header.constData(), 100));
        if (header.mid(257, 5) == "ustar") {
            const QString prefix = QString::fromUtf8(header.constData() + 345, qstrnlen(header.constData() + 345, 155));
            if (!prefix.isEmpty())
                name = prefix + QLatin1Char('/') + name;
        }
        const qsizetype size = header.mid(124, 12).trimmed().replace('\0', "").toLongLong(nullptr, 8);
        const char type = header[156];
        if (type == '0' || type == '\0')
            files.insert(name, tar.mid(offset + 512, size));
        offset += 512 + (size + 511) / 512 * 512;
    }
    return files;
}

// BrushPatternImageArray: int32 header size, int32 count, then per entry
// int32 entry size, int32 path bytes, UTF-16LE path, ...
QStringList patternPaths(const QByteArray &blob)
{
    QStringList paths;
    qint32 start = 0, count = 0;
    if (!readInt32BE(blob, 0, start) || !readInt32BE(blob, 4, count))
        return paths;
    qsizetype offset = start;
    for (qint32 i = 0; i < count; ++i) {
        qint32 entrySize = 0, pathBytes = 0;
        if (!readInt32BE(blob, offset, entrySize) || !readInt32BE(blob, offset + 4, pathBytes) || entrySize <= 0)
            break;
        paths << utf16le(blob, offset + 8, pathBytes);
        offset += entrySize;
    }
    return paths;
}

// --- parameter access -------------------------------------------------------

double number(const Params &p, const char *key, double fallback = 0.0)
{
    const QVariant v = p.value(QLatin1String(key));
    if (!v.isValid() || v.isNull() || v.typeId() == QMetaType::QByteArray)
        return fallback;
    bool ok = false;
    const double d = v.toDouble(&ok);
    return ok ? d : fallback;
}

bool flag(const Params &p, const char *key) { return number(p, key) != 0.0; }

brush::Effector effector(const Params &p, const char *key)
{
    const QVariant v = p.value(QLatin1String(key));
    if (v.typeId() == QMetaType::QByteArray)
        if (auto e = brush::Effector::parse(bytesOf(v.toByteArray())))
            return *e;
    return {};
}

bool hasBlob(const Params &p, const char *key)
{
    const QVariant v = p.value(QLatin1String(key));
    return v.typeId() == QMetaType::QByteArray && v.toByteArray().size() > 1;
}

// Start/end tapering with at least one parameter actually targeted.
bool usesInOut(const Params &p)
{
    if (!flag(p, "BrushUseIn") && !flag(p, "BrushUseOut"))
        return false;
    const QByteArray targets = p.value(QStringLiteral("BrushInOutTarget")).toByteArray();
    qint32 count = 0;
    if (!readInt32BE(targets, 4, count))
        return false;
    // [12, N, 12, ...] then N triples (id, enabled, value); some files omit the 4th int.
    const qsizetype start = targets.size() >= (4 + 3 * qsizetype(count)) * 4 ? 4 : 3;
    for (qint32 k = 0; k < count; ++k) {
        qint32 enabled = 0;
        if (readInt32BE(targets, (start + 3 * k + 1) * 4, enabled) && enabled != 0)
            return true;
    }
    return false;
}

QStringList unsupported(const Params &p)
{
    QStringList w;
    auto check = [&](bool used, const QString &what) {
        if (used)
            w << what;
    };
    check(number(p, "CompositeMode") != 0, QObject::tr("metodo di fusione"));
    check(hasBlob(p, "TextureImage"), QObject::tr("texture"));
    check(flag(p, "BrushWaterColor"), QObject::tr("acquerello"));
    check(flag(p, "BrushUseWaterEdge"), QObject::tr("bordo acquerello"));
    check(flag(p, "UseDualBrush"), QObject::tr("doppia punta"));
    check(flag(p, "BrushRibbon"), QObject::tr("nastro"));
    check(flag(p, "BrushHueChange") || flag(p, "BrushSaturationChange") || flag(p, "BrushValueChange"),
          QObject::tr("variazione di colore per timbro"));
    check(flag(p, "BrushSubColor"), QObject::tr("colore secondario"));
    check(flag(p, "BrushChangeStrokeColor"), QObject::tr("variazione di colore per tratto"));
    check(flag(p, "BrushUseRevision"), QObject::tr("correzione a posteriori"));
    check(usesInOut(p), QObject::tr("inizio e fine del tratto"));
    check(flag(p, "BrushUseSpray") && flag(p, "BrushSprayUseFixedPoint"), QObject::tr("spruzzo a punti fissi"));
    check(flag(p, "BrushUsePatternImage") && number(p, "BrushPatternOrderType") == 5,
          QObject::tr("ordine delle punte a elenco"));
    check(number(p, "BrushSizeUnit") != 0, QObject::tr("dimensione in unità diverse dai pixel"));
    return w;
}

brush::BrushStyle styleFrom(const Params &p, int tipCount)
{
    brush::BrushStyle s;
    s.sizeEffector = effector(p, "BrushSizeEffector");
    s.atLeastOnePixel = flag(p, "BrushAtLeast1Pixel");
    s.dabOpacity = number(p, "BrushOpacity", 100.0) / 100.0;
    s.dabOpacityEffector = effector(p, "BrushOpacityEffector");
    s.flow = number(p, "BrushFlow", 100.0) / 100.0;
    s.flowEffector = effector(p, "BrushFlowEffector");
    s.adjustFlowBySpacing = flag(p, "BrushAdjustFlowByInterval");
    s.hardness = std::clamp(number(p, "BrushHardness", 100.0) / 100.0, 0.0, 1.0);
    const int spacingMode = int(number(p, "BrushAutoIntervalType"));
    s.spacingMode = spacingMode >= 0 && spacingMode <= 5 ? brush::SpacingMode(spacingMode) : brush::SpacingMode::Custom;
    s.spacing = number(p, "BrushInterval", 15.0) / 100.0;
    s.spacingEffector = effector(p, "BrushIntervalEffector");
    s.thickness = number(p, "BrushThickness", 100.0) / 100.0;
    s.thicknessEffector = effector(p, "BrushThicknessEffector");
    s.verticalThickness = flag(p, "BrushVerticalThicknes");
    s.rotation = number(p, "BrushRotation");
    s.rotationMode = std::uint32_t(number(p, "BrushRotationEffector"));
    s.rotationRandom = number(p, "BrushRotationRandomScale") / 100.0;

    s.tipCount = tipCount;
    const int order = int(number(p, "BrushPatternOrderType"));
    s.tipOrder = order >= 0 && order <= 4 ? brush::TipOrder(order) : brush::TipOrder::Cycle;
    s.tipFlip = std::uint32_t(number(p, "BrushPatternReverseHorizontal"))
                | (std::uint32_t(number(p, "BrushPatternReverseVertical")) << 4);

    s.spray = flag(p, "BrushUseSpray");
    s.spraySizeSynced = flag(p, "BrushSpraySizeSyncBrushSize");
    // Synced: percent of the brush size; otherwise a diameter in pixels.
    s.spraySize = s.spraySizeSynced ? number(p, "BrushSpraySize", 100.0) / 100.0 : number(p, "BrushSpraySize") / 2.0;
    s.spraySizeEffector = effector(p, "BrushSpraySizeEffector");
    s.sprayDensity = number(p, "BrushSprayDensity", 1.0);
    s.sprayDensityEffector = effector(p, "BrushSprayDensityEffector");
    s.sprayBias = number(p, "BrushSprayBias") / 100.0;
    return s;
}

} // namespace

QImage tipCoverageFromThumbnail(const QImage &thumbnail)
{
    const QImage img = thumbnail.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width();
    const int h = img.height();
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    if (w == 0 || h == 0)
        return out;

    std::vector<int> alpha(std::size_t(w) * std::size_t(h));
    int maxAlpha = 0;
    for (int y = 0; y < h; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            alpha[std::size_t(y) * std::size_t(w) + std::size_t(x)] = qAlpha(line[x]);
            maxAlpha = std::max(maxAlpha, qAlpha(line[x]));
        }
    }
    if (maxAlpha == 0)
        return out;

    // Crisp, opaque lines are line art: use them as drawn. A soft glow is a
    // halo drawn around the (empty) shape: fill what it encloses.
    std::size_t visible = 0, opaque = 0;
    for (int a : alpha) {
        visible += a > 8 ? 1 : 0;
        opaque += a >= 230 ? 1 : 0;
    }
    const bool lineArt = opaque * 2 >= visible;

    // Regions: clear pixels reachable from the border are outside, the other
    // clear pixels are inside; halo pixels go to whichever region reaches
    // them first (a two-front flood), so each side keeps half the halo.
    enum : std::uint8_t { Unset, Outside, Inside };
    const int threshold = std::max(1, maxAlpha * 15 / 100);
    std::vector<std::uint8_t> region(alpha.size(), Unset);
    auto clear = [&](std::size_t i) { return alpha[i] < threshold; };
    auto index = [w](int x, int y) { return std::size_t(y) * std::size_t(w) + std::size_t(x); };

    std::deque<std::pair<int, int>> queue;
    auto flood = [&](std::uint8_t label, bool clearOnly) {
        while (!queue.empty()) {
            const auto [x, y] = queue.front();
            queue.pop_front();
            const int nx[] = {x - 1, x + 1, x, x};
            const int ny[] = {y, y, y - 1, y + 1};
            for (int k = 0; k < 4; ++k) {
                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h)
                    continue;
                const std::size_t j = index(nx[k], ny[k]);
                if (region[j] != Unset || (clearOnly && !clear(j)))
                    continue;
                region[j] = clearOnly ? label : region[index(x, y)];
                queue.emplace_back(nx[k], ny[k]);
            }
        }
    };
    for (int x = 0; x < w; ++x)
        for (int y : {0, h - 1})
            if (clear(index(x, y)) && region[index(x, y)] == Unset) {
                region[index(x, y)] = Outside;
                queue.emplace_back(x, y);
            }
    for (int y = 0; y < h; ++y)
        for (int x : {0, w - 1})
            if (clear(index(x, y)) && region[index(x, y)] == Unset) {
                region[index(x, y)] = Outside;
                queue.emplace_back(x, y);
            }
    flood(Outside, true);

    bool anyInside = false;
    for (std::size_t i = 0; i < alpha.size(); ++i)
        if (region[i] == Unset && clear(i)) {
            region[i] = Inside;
            anyInside = true;
        }

    if (lineArt || !anyInside) { // use it as drawn
        // Tips are grayscale: dark is ink, white is paper. Opaque white areas
        // (the inside of a drawn shape) leave the canvas untouched.
        for (int y = 0; y < h; ++y) {
            const auto *src = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            auto *line = reinterpret_cast<QRgb *>(out.scanLine(y));
            for (int x = 0; x < w; ++x)
                line[x] = qRgba(0, 0, 0, alpha[index(x, y)] * (255 - qGray(src[x])) / 255);
        }
        return out;
    }

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (region[index(x, y)] != Unset)
                queue.emplace_back(x, y);
    flood(Unset, false);

    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const std::size_t i = index(x, y);
            int a = 0;
            if (region[i] == Inside)
                a = clear(i) ? 255 : std::clamp(255 - alpha[i] * 255 / maxAlpha, 0, 255);
            line[x] = qRgba(0, 0, 0, a);
        }
    }
    return out;
}

std::optional<SutImport> importSut(const QString &path, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return std::nullopt;
    };
    if (!QFileInfo(path).isFile())
        return fail(QObject::tr("File non trovato: %1").arg(path));

    const QString connection = QStringLiteral("sut-import-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString name;
    Params params;
    QHash<QString, QImage> thumbnails; // by material OriginalPath
    QString failure;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open()) {
            failure = QObject::tr("Impossibile aprire il file: %1").arg(db.lastError().text());
        } else {
            QSqlQuery q(db);
            if (!q.exec(QStringLiteral("select NodeName, NodeVariantID from Node")) || !q.next()) {
                failure = QObject::tr("Non è un sotto-strumento di Clip Studio Paint (manca Node).");
            } else {
                name = q.value(0).toString();
                const qlonglong variant = q.value(1).toLongLong();
                QSqlQuery v(db);
                v.prepare(QStringLiteral("select * from Variant where VariantID = ?"));
                v.addBindValue(variant);
                if (!v.exec() || !v.next()) {
                    failure = QObject::tr("Variante %1 non trovata.").arg(variant);
                } else {
                    const QSqlRecord rec = v.record();
                    for (int i = 0; i < rec.count(); ++i)
                        if (!v.isNull(i))
                            params.insert(rec.fieldName(i), v.value(i));
                }
                if (failure.isEmpty() && db.tables().contains(QStringLiteral("MaterialFile"))) {
                    QSqlQuery m(db);
                    m.exec(QStringLiteral("select OriginalPath, FileData from MaterialFile"));
                    while (m.next()) {
                        const auto files = untar(m.value(1).toByteArray());
                        for (auto it = files.cbegin(); it != files.cend(); ++it)
                            if (it.key().endsWith(QLatin1String("thumbnail.png")))
                                thumbnails.insert(m.value(0).toString(), QImage::fromData(it.value(), "PNG"));
                    }
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    if (!failure.isEmpty())
        return fail(failure);

    auto definition = std::make_shared<BrushDefinition>();
    definition->name = name;
    SutImport result;
    if (flag(params, "BrushUsePatternImage")) {
        const QStringList tipPaths = patternPaths(params.value(QStringLiteral("BrushPatternImageArray")).toByteArray());
        for (const QString &tipPath : tipPaths) {
            const QImage thumb = thumbnails.value(tipPath);
            if (thumb.isNull())
                continue;
            result.tipThumbnails << thumb;
            definition->tips << tipCoverageFromThumbnail(thumb);
        }
        if (!definition->tips.isEmpty())
            result.warnings << QObject::tr("punte ricavate dalle anteprime (provvisorie)");
        else if (!tipPaths.isEmpty())
            result.warnings << QObject::tr("punte non leggibili: uso un timbro tondo");
    }
    definition->style = styleFrom(params, int(definition->tips.size()));
    result.warnings << unsupported(params);

    BrushPreset &preset = result.preset;
    preset.name = name;
    preset.tool = ToolId::Brush;
    preset.size = std::clamp(number(params, "BrushSize", 10.0), 1.0, 2000.0);
    preset.opacity = std::clamp(number(params, "Opacity", 100.0) / 100.0, 0.0, 1.0);
    preset.curve = PressureCurve(); // the brush's own dynamics carry its pressure response
    preset.smoothing = std::clamp(number(params, "FlickerReduction") * 0.05, 0.0, 0.6);
    preset.brush = std::move(definition);
    return result;
}

} // namespace mothion::tools
