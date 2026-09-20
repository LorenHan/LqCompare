#include "probesession.h"

#include <QLabel>

namespace LqCompare {
namespace TestSupport {

ProbeSession::ProbeSession(QObject *parent) : CompareSession(QStringLiteral("probe"), parent)
{
}

QWidget *ProbeSession::createView(QWidget *parent)
{
    ++viewCalls;
    lastViewParent = parent;
    if (!viewResult) {
        return nullptr;
    }
    // 用一个最朴素、不含任何比对面板的控件：本套件验证的是基类的契约，
    // 不是某个视图长什么样；视图本身的测试属于各自的会话类型条目。
    return new QLabel(QStringLiteral("probe"), parent);
}

bool ProbeSession::doOpen(QString *error)
{
    ++openCalls;
    if (reentrantOpen) {
        QString ignored;
        // 重入调用必须被基类挡住。这里刻意忽略返回值：本用例断言的是
        // 「doOpen 没有被再调一次」，而不是这次重入返回了什么。
        open(&ignored);
    }
    if (!openResult) {
        if (error) {
            *error = openReason;
        }
        return false;
    }
    return true;
}

bool ProbeSession::doReload(QString *error)
{
    ++reloadCalls;
    // 模拟「重建视图的过程中视图自己报了改动」。基类在成功之后必须把脏标记清掉，
    // 否则重载完的会话会一直是脏的，用户每次点保存都在写同一份内容。
    if (markDirtyInDoReload) {
        setDirty(true);
    }
    if (!reloadResult) {
        if (error) {
            *error = openReason;
        }
        return false;
    }
    return true;
}

bool ProbeSession::doSave(QString *error)
{
    ++saveCalls;
    if (!saveResult) {
        if (error) {
            *error = openReason;
        }
        return false;
    }
    return true;
}

void ProbeSession::doClose()
{
    ++closeCalls;
}

bool ProbeSession::canSaveNow() const
{
    return alwaysSavable || CompareSession::canSaveNow();
}

SessionSettings *ProbeSession::createSettings()
{
    ++settingsFactoryCalls;
    if (customSettings) {
        return new ProbeSettings(this);
    }
    return CompareSession::createSettings();
}

MinimalSession::MinimalSession(QObject *parent) : CompareSession(QStringLiteral("minimal"), parent)
{
}

QWidget *MinimalSession::createView(QWidget *parent)
{
    ++viewCalls;
    return new QLabel(QStringLiteral("minimal"), parent);
}

ProbeSettings::ProbeSettings(QObject *parent) : MemorySessionSettings(parent)
{
}

} // namespace TestSupport
} // namespace LqCompare
