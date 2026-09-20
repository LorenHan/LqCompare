#include "formatdetector.h"
#include "sessiontype.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QtEndian>

namespace LqCompare {
namespace Format {
namespace {
bool supplied(const FileProbe &probe)
{
    return !probe.path.isEmpty() || probe.contentAvailable || probe.directory || !probe.error.isEmpty();
}

Filter::MaskSubject subjectFor(const FileProbe &probe)
{
    // Filter's language always uses '/' separators. Only normalize native
    // separators: backslashes are legal literal filename bytes on POSIX.
    return Filter::MaskSubject::forPath(QDir::fromNativeSeparators(probe.path));
}

bool available(const SessionTypeRegistry &types, const QString &id)
{
    const auto *type = types.find(id);
    return type && type->hasFactory() && type->isAvailableHere();
}

QString unavailability(const SessionTypeRegistry &types, const QString &id)
{
    const auto *type = types.find(id);
    if (!type)
        return QStringLiteral("视图类型 %1 未注册。").arg(id);
    if (!type->isAvailableHere())
        return type->unavailableReason();
    return QStringLiteral("视图 %1 尚未提供可用实现。").arg(type->type.displayName);
}

bool hasSignature(const FormatDefinition &definition, const FileProbe &probe)
{
    if (!probe.contentAvailable)
        return false;
    for (const auto &signature : definition.signatures) {
        if (signature.offset <= probe.prefix.size() &&
            signature.bytes.size() <= probe.prefix.size() - signature.offset &&
            probe.prefix.mid(signature.offset, signature.bytes.size()) == signature.bytes)
            return true;
    }
    return false;
}

bool canCheckSignature(const FormatDefinition &definition, const FileProbe &probe)
{
    if (!probe.contentAvailable || definition.signatures.isEmpty())
        return false;
    // A truncated prefix cannot disprove a signature beyond the sampled region.
    for (const auto &signature : definition.signatures)
        if (!probe.complete && signature.offset + signature.bytes.size() > probe.prefix.size())
            return false;
    return true;
}

QString safeFallback(const ContentAssessment &left, const ContentAssessment &right,
                     const SessionTypeRegistry &types)
{
    const bool binary = left.kind == ContentKind::Binary || right.kind == ContentKind::Binary;
    if (!binary && available(types, QStringLiteral("text")))
        return QStringLiteral("text");
    if (available(types, QStringLiteral("hex")))
        return QStringLiteral("hex");
    return {};
}

// Validate scalars ourselves so a truncated UTF-8 tail is not mistaken for a
// broken whole file. Overlong forms, surrogate scalars and > U+10FFFF are rejected.
bool readUtf8(const QByteArray &bytes, bool complete, QVector<uint> *scalars)
{
    int i = 0;
    while (i < bytes.size()) {
        const auto first = uchar(bytes.at(i));
        if (first < 0x80) {
            scalars->append(first);
            ++i;
            continue;
        }
        int length = 0;
        uint scalar = 0;
        uint minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { length = 2; scalar = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { length = 3; scalar = first & 0x0f; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { length = 4; scalar = first & 0x07; minimum = 0x10000; }
        else return false;
        const int remaining = bytes.size() - i;
        for (int j = 1; j < qMin(length, remaining); ++j) {
            const auto next = uchar(bytes.at(i + j));
            if ((next & 0xc0) != 0x80)
                return false;
            if (j == 1 && ((first == 0xe0 && next < 0xa0) || (first == 0xed && next >= 0xa0) ||
                           (first == 0xf0 && next < 0x90) || (first == 0xf4 && next >= 0x90)))
                return false;
            scalar = (scalar << 6) | (next & 0x3f);
        }
        if (remaining < length)
            return !complete;
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
            return false;
        scalars->append(scalar);
        i += length;
    }
    return true;
}

bool readUnicode(const QByteArray &bytes, int offset, bool little, int width,
                 bool complete, QVector<uint> *scalars)
{
    const auto *data = reinterpret_cast<const uchar *>(bytes.constData());
    while (offset + width <= bytes.size()) {
        uint scalar = width == 4
                ? (little ? qFromLittleEndian<quint32>(data + offset) : qFromBigEndian<quint32>(data + offset))
                : (little ? qFromLittleEndian<quint16>(data + offset) : qFromBigEndian<quint16>(data + offset));
        offset += width;
        if (width == 2 && scalar >= 0xd800 && scalar <= 0xdbff) {
            if (offset + width > bytes.size())
                return !complete;
            const uint low = little ? qFromLittleEndian<quint16>(data + offset) : qFromBigEndian<quint16>(data + offset);
            if (low < 0xdc00 || low > 0xdfff)
                return false;
            scalar = 0x10000 + ((scalar - 0xd800) << 10) + low - 0xdc00;
            offset += width;
        }
        if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
            return false;
        scalars->append(scalar);
    }
    return offset == bytes.size() || !complete;
}
} // namespace

FileProbe probeFile(const QString &path, qint64 limit)
{
    FileProbe result;
    result.path = path;
    if (path.isEmpty())
        return result;
    const QFileInfo info(path);
    if (!info.exists()) {
        result.error = QStringLiteral("文件不存在：%1").arg(path);
        return result;
    }
    if (info.isDir()) {
        result.directory = true;
        return result;
    }
    if (!info.isFile()) {
        result.error = QStringLiteral("只支持探测普通文件：%1").arg(path);
        return result;
    }
    if (limit < 1 || limit > 1024 * 1024) {
        result.error = QStringLiteral("内容探测上限须在 1–1048576 字节范围内。");
        return result;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("无法读取 %1：%2").arg(path, file.errorString());
        return result;
    }
    result.prefix = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError) {
        result.error = QStringLiteral("读取 %1 失败：%2").arg(path, file.errorString());
        result.prefix.clear();
        return result;
    }
    result.complete = result.prefix.size() <= limit;
    if (!result.complete)
        result.prefix.truncate(int(limit));
    result.contentAvailable = true;
    return result;
}

ContentAssessment assessContent(const FileProbe &probe)
{
    ContentAssessment result;
    if (!probe.contentAvailable) {
        result.explanation = probe.error.isEmpty() ? QStringLiteral("未读取内容。") : probe.error;
        return result;
    }
    const auto &bytes = probe.prefix;
    QVector<uint> scalars;
    scalars.reserve(bytes.size());
    bool valid = false;
    if (bytes.startsWith(QByteArray::fromHex("fffe0000"))) {
        result.encoding = QStringLiteral("UTF-32LE");
        valid = readUnicode(bytes, 4, true, 4, probe.complete, &scalars);
    } else if (bytes.startsWith(QByteArray::fromHex("0000feff"))) {
        result.encoding = QStringLiteral("UTF-32BE");
        valid = readUnicode(bytes, 4, false, 4, probe.complete, &scalars);
    } else if (bytes.startsWith(QByteArray::fromHex("fffe"))) {
        result.encoding = QStringLiteral("UTF-16LE");
        valid = readUnicode(bytes, 2, true, 2, probe.complete, &scalars);
    } else if (bytes.startsWith(QByteArray::fromHex("feff"))) {
        result.encoding = QStringLiteral("UTF-16BE");
        valid = readUnicode(bytes, 2, false, 2, probe.complete, &scalars);
    } else {
        result.encoding = QStringLiteral("UTF-8");
        valid = readUtf8(bytes, probe.complete, &scalars);
    }
    bool nul = false;
    int controls = 0;
    for (const auto scalar : scalars) {
        nul |= scalar == 0;
        if ((scalar < 0x20 && scalar != '\t' && scalar != '\n' && scalar != '\r' && scalar != '\f') || scalar == 0x7f)
            ++controls;
    }
    if (!valid || nul || controls > scalars.size() / 32) {
        result.kind = ContentKind::Binary;
        result.explanation = !valid ? QStringLiteral("内容不是有效 %1；建议十六进制视图。").arg(result.encoding)
                                   : nul ? QStringLiteral("解码内容含 NUL 字符；建议十六进制视图。")
                                         : QStringLiteral("内容含较多控制字符；建议十六进制视图。");
    } else {
        result.kind = ContentKind::Text;
        result.explanation = bytes.isEmpty() ? QStringLiteral("空文件，按文本处理。")
                                           : QStringLiteral("内容符合 %1 文本%2。").arg(result.encoding,
                                               probe.complete ? QString() : QStringLiteral("（仅检查文件前缀）"));
    }
    return result;
}

FormatDetector::FormatDetector()
{
    setDefinitions(builtInFormatDefinitions());
}

FormatDetector::FormatDetector(const QVector<FormatDefinition> &definitions)
{
    setDefinitions(definitions);
}

bool FormatDetector::setDefinitions(const QVector<FormatDefinition> &definitions, QString *error)
{
    if (error)
        error->clear();
    QSet<QString> ids;
    QVector<QVector<Filter::Mask>> compiled;
    for (const auto &definition : definitions) {
        auto errors = validateDefinition(definition);
        if (ids.contains(definition.id))
            errors << QStringLiteral("格式 ID 重复：%1").arg(definition.id);
        if (!errors.isEmpty()) {
            if (error)
                *error = errors.join(QLatin1Char('\n'));
            return false;
        }
        ids.insert(definition.id);
        QVector<Filter::Mask> masks;
        for (const auto &mask : definition.masks)
            masks.append(Filter::Mask::compile(mask).mask);
        compiled.append(masks);
    }
    m_definitions = definitions;
    m_masks = compiled;
    return true;
}

DetectionResult FormatDetector::detectFiles(const QString &leftPath, const QString &rightPath,
                                           const SessionTypeRegistry &types, const DetectionOptions &options) const
{
    return detect(probeFile(leftPath, options.probeLimit), probeFile(rightPath, options.probeLimit), types, options);
}

DetectionResult FormatDetector::detect(const FileProbe &left, const FileProbe &right,
                                      const SessionTypeRegistry &types, const DetectionOptions &options) const
{
    DetectionResult result;
    result.leftContent = assessContent(left);
    result.rightContent = assessContent(right);
    const FileProbe *sides[] = {&left, &right};
    for (const auto *side : sides)
        if (!side->error.isEmpty())
            result.diagnostics << side->error;
    if (!result.diagnostics.isEmpty()) {
        result.explanation = QStringLiteral("无法完成文件探测：%1").arg(result.diagnostics.join(QStringLiteral("；")));
        return result;
    }
    if (!supplied(left) && !supplied(right)) {
        result.explanation = QStringLiteral("未提供要识别的文件。");
        result.diagnostics << result.explanation;
        return result;
    }
    if (left.directory || right.directory) {
        if ((supplied(left) && !left.directory) || (supplied(right) && !right.directory)) {
            result.explanation = QStringLiteral("文件与目录不能作为同一对输入。");
            result.diagnostics << result.explanation;
            return result;
        }
        result.source = DetectionSource::Directory;
        result.requestedSessionTypeId = QStringLiteral("folder");
        if (available(types, result.requestedSessionTypeId)) {
            result.sessionTypeId = result.requestedSessionTypeId;
            result.explanation = QStringLiteral("输入是目录，使用文件夹比较。");
        } else {
            result.explanation = unavailability(types, result.requestedSessionTypeId);
            result.diagnostics << result.explanation;
        }
        return result;
    }

    int chosen = -1;
    for (const auto &override : options.overrides) {
        if (!override.enabled)
            continue;
        const auto parsed = Filter::Mask::compile(override.mask);
        if (!parsed.ok() || override.mask.trimmed().isEmpty()) {
            result.diagnostics << QStringLiteral("已跳过无效关联覆盖「%1」：%2").arg(override.mask, parsed.error.describe());
            continue;
        }
        int definitionIndex = -1;
        for (int i = 0; i < m_definitions.size(); ++i)
            if (m_definitions.at(i).id == override.formatId)
                definitionIndex = i;
        if (definitionIndex < 0) {
            result.diagnostics << QStringLiteral("已跳过关联覆盖：格式 %1 不存在。").arg(override.formatId);
            continue;
        }
        for (int side = 0; side < 2; ++side) {
            if (supplied(*sides[side]) && parsed.mask.matches(subjectFor(*sides[side]), options.maskCaseSensitivity)) {
                chosen = definitionIndex;
                result.source = DetectionSource::AssociationOverride;
                result.matchedSide = side;
                result.matchedRule = override.mask;
                break;
            }
        }
        if (chosen >= 0)
            break;
    }
    if (chosen < 0) {
        for (int i = 0; i < m_definitions.size() && chosen < 0; ++i) {
            for (const auto &mask : m_masks.at(i)) {
                for (int side = 0; side < 2; ++side) {
                    if (supplied(*sides[side]) && mask.matches(subjectFor(*sides[side]), options.maskCaseSensitivity)) {
                        chosen = i;
                        result.source = DetectionSource::FileMask;
                        result.matchedRule = mask.pattern();
                        result.matchedSide = side;
                        break;
                    }
                }
                if (chosen >= 0)
                    break;
            }
        }
    }
    if (chosen < 0) {
        for (int i = 0; i < m_definitions.size() && chosen < 0; ++i) {
            for (int side = 0; side < 2; ++side) {
                if (hasSignature(m_definitions.at(i), *sides[side])) {
                    chosen = i;
                    result.source = DetectionSource::ContentSignature;
                    result.matchedSide = side;
                    result.matchedRule = QStringLiteral("magic bytes");
                    break;
                }
            }
        }
    }

    if (chosen >= 0) {
        const auto &definition = m_definitions.at(chosen);
        result.formatId = definition.id;
        result.formatName = definition.name;
        result.requestedSessionTypeId = definition.sessionTypeId;
        result.explanation = QStringLiteral("当前使用 %1 格式；来源：%2（%3，%4侧）。")
                .arg(definition.name, detectionSourceLabel(result.source), result.matchedRule,
                     result.matchedSide == 0 ? QStringLiteral("左") : QStringLiteral("右"));
        // Mask/association rules remain authoritative. Report contradicting data
        // explicitly; silently switching would defeat intentional overrides.
        if (result.source != DetectionSource::ContentSignature) {
            const auto &matchingProbe = *sides[result.matchedSide];
            if (canCheckSignature(definition, matchingProbe) && !hasSignature(definition, matchingProbe))
                result.diagnostics << QStringLiteral("掩码与内容签名冲突：%1 未检测到 %2 的签名；仍按规则选择格式。")
                                      .arg(matchingProbe.path, definition.name);
            if (definition.sessionTypeId == QStringLiteral("text") &&
                (result.leftContent.kind == ContentKind::Binary || result.rightContent.kind == ContentKind::Binary))
                result.diagnostics << QStringLiteral("格式规则选择文本视图，但内容探测发现二进制数据；可手动改用十六进制视图。");
        }
    } else {
        result.source = DetectionSource::UnknownFallback;
        switch (options.unknownFallback) {
        case UnknownFallback::Automatic:
            result.requestedSessionTypeId = result.leftContent.kind == ContentKind::Binary || result.rightContent.kind == ContentKind::Binary
                    ? QStringLiteral("hex") : QStringLiteral("text");
            break;
        case UnknownFallback::Text: result.requestedSessionTypeId = QStringLiteral("text"); break;
        case UnknownFallback::Hex: result.requestedSessionTypeId = QStringLiteral("hex"); break;
        case UnknownFallback::Picture: result.requestedSessionTypeId = QStringLiteral("picture"); break;
        case UnknownFallback::Ask:
            result.explanation = QStringLiteral("没有格式规则命中；兜底设置要求先选择视图，因此尚未打开。");
            return result;
        }
        QStringList reasons;
        if (supplied(left))
            reasons << QStringLiteral("左侧：%1").arg(result.leftContent.explanation);
        if (supplied(right))
            reasons << QStringLiteral("右侧：%1").arg(result.rightContent.explanation);
        result.explanation = QStringLiteral("没有格式规则命中；%1 兜底选择 %2。%3")
                .arg(options.unknownFallback == UnknownFallback::Automatic ? QStringLiteral("内容探测") : QStringLiteral("用户设置"),
                     result.requestedSessionTypeId, reasons.join(QStringLiteral("；")));
    }

    if (available(types, result.requestedSessionTypeId)) {
        result.sessionTypeId = result.requestedSessionTypeId;
    } else {
        result.usedAvailabilityFallback = true;
        result.diagnostics << unavailability(types, result.requestedSessionTypeId);
        result.sessionTypeId = safeFallback(result.leftContent, result.rightContent, types);
        result.explanation += result.sessionTypeId.isEmpty()
                ? QStringLiteral(" 没有可用的安全兜底视图，未打开。")
                : QStringLiteral(" 所选视图不可用，已按内容回退到 %1。").arg(result.sessionTypeId);
    }
    return result;
}

QString detectionSourceLabel(DetectionSource source)
{
    switch (source) {
    case DetectionSource::AssociationOverride: return QStringLiteral("会话关联覆盖");
    case DetectionSource::FileMask: return QStringLiteral("格式掩码");
    case DetectionSource::ContentSignature: return QStringLiteral("内容签名");
    case DetectionSource::UnknownFallback: return QStringLiteral("未知格式兜底");
    case DetectionSource::Directory: return QStringLiteral("目录类型");
    case DetectionSource::None: return QStringLiteral("未识别");
    }
    return {};
}

} // namespace Format
} // namespace LqCompare
