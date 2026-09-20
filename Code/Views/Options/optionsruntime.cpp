#include "optionsruntime.h"

#include "optionsrepository.h"
#include "logging.h"

#include <QApplication>
#include <QDir>
#include <QFontDatabase>

namespace LqCompare {
namespace Options {
namespace {
QPalette themePalette(bool dark)
{
    QPalette palette;
    const QColor window = dark ? QColor(39, 42, 48) : QColor(245, 246, 248);
    const QColor base = dark ? QColor(29, 32, 37) : QColor(Qt::white);
    const QColor text = dark ? QColor(234, 237, 242) : QColor(28, 31, 36);
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, window);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, window);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::ToolTipBase, base);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, QColor(44, 111, 206));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, dark ? QColor(123, 183, 255) : QColor(25, 92, 181));
    palette.setColor(QPalette::Light, dark ? QColor(75, 79, 88) : QColor(Qt::white));
    palette.setColor(QPalette::Midlight, dark ? QColor(58, 63, 72) : QColor(228, 231, 235));
    palette.setColor(QPalette::Mid, dark ? QColor(102, 109, 121) : QColor(121, 127, 138));
    palette.setColor(QPalette::Dark, dark ? QColor(17, 19, 22) : QColor(92, 98, 108));
    palette.setColor(QPalette::Shadow, dark ? QColor(Qt::black) : QColor(72, 76, 85));
    const QColor disabled = dark ? QColor(141, 147, 157) : QColor(131, 136, 146);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, disabled);
    return palette;
}
}

OptionsRuntime::OptionsRuntime(Settings::OptionsRepository *repository, QObject *parent)
    : QObject(parent), m_repository(repository),
      m_systemFont(QApplication::font()), m_systemPalette(QApplication::palette())
{
    if (repository)
        connect(repository, &Settings::OptionsRepository::changed,
                this, [this](const QStringList &keys) { applyKeys(keys); });
    applyCurrent();
}

bool OptionsRuntime::applyCurrent()
{
    return applyKeys({QStringLiteral("display.theme"), QStringLiteral("logging.level")});
}

bool OptionsRuntime::applyKeys(const QStringList &keys)
{
    if (!m_repository || !qApp) {
        m_lastError = QStringLiteral("无法应用选项：设置仓库或应用程序不可用。");
        emit runtimeError(m_lastError);
        return false;
    }
    bool displayChanged = false;
    bool loggingChanged = false;
    for (const QString &key : keys) {
        displayChanged |= key.startsWith(QStringLiteral("display."));
        loggingChanged |= key.startsWith(QStringLiteral("logging."));
    }
    if (displayChanged) {
        const QString theme = m_repository->value(QStringLiteral("display.theme")).toString();
        qApp->setPalette(theme == QStringLiteral("system") ? m_systemPalette
                                                         : themePalette(theme == QStringLiteral("dark")));
        QFont uiFont = m_systemFont;
        const QString uiFamily = m_repository->value(QStringLiteral("display.uiFontFamily")).toString();
        if (!uiFamily.isEmpty()) uiFont.setFamily(uiFamily);
        const int uiSize = m_repository->value(QStringLiteral("display.uiFontSize")).toInt();
        if (uiSize > 0) uiFont.setPointSize(uiSize);
        if (QApplication::font() != uiFont) QApplication::setFont(uiFont);

        QFont content = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        const QString family = m_repository->value(QStringLiteral("display.contentFontFamily")).toString();
        if (!family.isEmpty()) content.setFamily(family);
        content.setPointSize(m_repository->value(QStringLiteral("display.contentFontSize")).toInt());
        if (content != m_contentFont) {
            m_contentFont = content;
            emit contentFontChanged(content);
        }
    }

    if (!loggingChanged) return true;
    Log::Level level = Log::Level::Info;
    Log::levelFromName(m_repository->value(QStringLiteral("logging.level")).toString(), &level);
    Log::setLevel(level);

    // 详细性能计时与轮转策略都是「读到就生效」的纯状态，没有失败路径，
    // 因此不像日志文件那样需要回滚。顺序上先设策略再换文件：换文件之后会立刻
    // 按新策略检查一次轮转，策略还没设的话那次检查用的就是上一个策略。
    Log::setPerformanceTimingEnabled(
            m_repository->value(Log::performanceTimingKey()).toBool());
    Log::setRotationPolicy(Log::RotationPolicy::fromValues(m_repository->values()));

    QString path;
    if (m_repository->value(QStringLiteral("logging.fileEnabled")).toBool()) {
        path = m_repository->value(QStringLiteral("logging.filePath")).toString();
        if (path.isEmpty())
            path = QDir(m_repository->location().directory).filePath(QStringLiteral("logs/lqcompare.log"));
    }
    const QString previousPath = Log::logFile();
    if (previousPath != path && !Log::setLogFile(path)) {
        const bool restored = Log::setLogFile(previousPath);
        m_lastError = QStringLiteral("无法打开日志文件：%1。").arg(path);
        m_lastError += restored
                ? QStringLiteral("已保留原日志输出位置：%1。").arg(previousPath.isEmpty() ? QStringLiteral("未启用文件输出") : previousPath)
                : QStringLiteral("原日志输出位置也无法重新打开：%1；当前仅保留其他日志输出。").arg(previousPath);
        emit runtimeError(m_lastError);
        return false;
    }

    // 换完文件立刻按策略查一次轮转：新路径上可能已经躺着一份超限的旧日志
    // （上一次运行留下的），等「下一条日志」才处理会让用户刚打开程序就看到
    // 一个超标文件。失败**如实上报**，但日志继续照记——轮转没做成的后果
    // 只是旧日志暂时偏大，不该让它变成「设置无法应用」。
    QString rotationError;
    if (!path.isEmpty()) {
        Log::RotationDecision decision;
        if (!Log::rotateIfNeeded(QDateTime::currentDateTime(), &decision, &rotationError))
            rotationError = QStringLiteral("日志轮转未能完成：%1").arg(rotationError);
    }
    m_lastError = rotationError;
    if (!m_lastError.isEmpty())
        emit runtimeError(m_lastError);
    return m_lastError.isEmpty();
}

} // namespace Options
} // namespace LqCompare
