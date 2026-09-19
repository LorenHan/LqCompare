#ifndef LQCOMPARE_LOGGING_H
#define LQCOMPARE_LOGGING_H

#include <QString>

namespace LqCompare {
namespace Log {

///
/// 分级日志（PRD: ENG-006）。
///
/// 设计约束：级别未启用时**不得求值参数**，因此调用点必须使用
/// LQCOMPARE_DEBUG() 之类的宏，而不是 Log::debug(expensiveToString())。
///
enum class Level {
    Error = 0,
    Warning = 1,
    Info = 2,
    Debug = 3,
    Trace = 4,
};

/// 设置当前级别；低于该级别的日志被丢弃。
void setLevel(Level level);
Level level();

/// 把日志同时写入文件（追加）。传空字符串表示关闭文件输出。
bool setLogFile(const QString &filePath);

/// 记录一条日志。category 用于按模块过滤，如 "session"、"dir"、"vcs"。
void write(Level level, const QString &category, const QString &message);

} // namespace Log
} // namespace LqCompare

// 参数在级别未启用时不会被求值。
//
// 注意：宏形参刻意命名为 lvl 而不是 level —— 形参名会参与全宏体的令牌替换，
// 若叫 level，宏体里的 LqCompare::Log::level() 也会被替换掉，编译直接失败。
#define LQCOMPARE_LOG(lvl, category, message)                                                      \
    do {                                                                                          \
        if (static_cast<int>(lvl) <= static_cast<int>(LqCompare::Log::level())) {                  \
            LqCompare::Log::write(lvl, QStringLiteral(category), message);                         \
        }                                                                                         \
    } while (false)

#define LQCOMPARE_ERROR(category, message)                                                         \
    LQCOMPARE_LOG(LqCompare::Log::Level::Error, category, message)
#define LQCOMPARE_WARN(category, message)                                                          \
    LQCOMPARE_LOG(LqCompare::Log::Level::Warning, category, message)
#define LQCOMPARE_INFO(category, message)                                                          \
    LQCOMPARE_LOG(LqCompare::Log::Level::Info, category, message)
#define LQCOMPARE_DEBUG(category, message)                                                         \
    LQCOMPARE_LOG(LqCompare::Log::Level::Debug, category, message)

#endif // LQCOMPARE_LOGGING_H
