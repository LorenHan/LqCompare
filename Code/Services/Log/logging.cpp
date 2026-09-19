#include "logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <cstdio>

namespace LqCompare {
namespace Log {

namespace {

QMutex &mutex()
{
    static QMutex instance;
    return instance;
}

Level &currentLevel()
{
    static Level level = Level::Warning;
    return level;
}

QString &logFilePath()
{
    static QString path;
    return path;
}

const char *levelName(Level level)
{
    switch (level) {
    case Level::Error:
        return "ERROR";
    case Level::Warning:
        return "WARN ";
    case Level::Info:
        return "INFO ";
    case Level::Debug:
        return "DEBUG";
    case Level::Trace:
        return "TRACE";
    }
    return "?????";
}

} // namespace

void setLevel(Level level)
{
    QMutexLocker locker(&mutex());
    currentLevel() = level;
}

Level level()
{
    QMutexLocker locker(&mutex());
    return currentLevel();
}

bool setLogFile(const QString &filePath)
{
    QMutexLocker locker(&mutex());
    logFilePath() = filePath;
    if (filePath.isEmpty()) {
        return true;
    }
    const QFileInfo info(filePath);
    QDir directory = info.absoluteDir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        logFilePath().clear();
        return false;
    }
    QFile probe(filePath);
    if (!probe.open(QIODevice::Append | QIODevice::Text)) {
        logFilePath().clear();
        return false;
    }
    probe.close();
    return true;
}

void write(Level levelValue, const QString &category, const QString &message)
{
    const QString line = QStringLiteral("%1 [%2] [%3] %4")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  QString::fromLatin1(levelName(levelValue)), category, message);
    QMutexLocker locker(&mutex());
    std::fputs(qPrintable(line), stderr);
    std::fputc('\n', stderr);

    if (logFilePath().isEmpty()) {
        return;
    }
    QFile file(logFilePath());
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream stream(&file);
    stream << line << '\n';
}

} // namespace Log
} // namespace LqCompare
