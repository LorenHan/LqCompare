#ifndef LQCOMPARE_TST_PROBESESSION_H
#define LQCOMPARE_TST_PROBESESSION_H

#include "comparesession.h"

namespace LqCompare {
namespace TestSupport {

///
/// \brief 探针会话：记录每一次实现点被调用的次数，并可注入失败。
///
/// 本文件在测试里的角色是**「将来新增的一种会话类型」**：它只实现基类契约，
/// 完全不碰已有会话类型的代码。SESS-001 第 2 条（新增类型不需要改动已有代码）
/// 就是靠这两个类型一起验证的。
///
/// 刻意不做成 mock 框架那种「按字符串匹配调用」的东西：需要断言的只有
/// 「哪个实现点被调了几次」，几个整数就够了——多一层抽象会让「断言写错了」
/// 与「实现写错了」变得难以区分。
///
class ProbeSession : public CompareSession
{
    Q_OBJECT

public:
    explicit ProbeSession(QObject *parent = nullptr);

    // --- 可注入的行为 -------------------------------------------------------
    bool openResult = true;
    bool reloadResult = true;
    bool saveResult = true;
    /// 置 false 让 createView 返回 nullptr，用于验证基类的回退与上报。
    bool viewResult = true;
    /// 失败原因；留空则基类应给一句兜底文案（而不是一个没有内容的错误对话框）。
    QString openReason;
    /// 让 doOpen 里再调一次 open()，验证重入被挡住。
    bool reentrantOpen = false;
    /// 让 doReload 里把会话标脏，模拟「重建视图时视图自己报了改动」。
    /// 用来验证「成功重载之后会话一定不脏」这条由基类保证的收尾。
    bool markDirtyInDoReload = false;
    /// 覆写 canSaveNow()，让「只读会话」这类情形可测。
    bool alwaysSavable = false;
    /// 覆写 createSettings()，验证子类可以换掉设置实现。
    bool customSettings = false;

    // --- 调用计数 -----------------------------------------------------------
    int viewCalls = 0;
    int openCalls = 0;
    int reloadCalls = 0;
    int saveCalls = 0;
    int closeCalls = 0;
    int settingsFactoryCalls = 0;

    /// 最近一次 createView 收到的父对象，用于断言父对象被透传。
    QWidget *lastViewParent = nullptr;

protected:
    QWidget *createView(QWidget *parent) override;
    bool doOpen(QString *error) override;
    bool doReload(QString *error) override;
    bool doSave(QString *error) override;
    void doClose() override;
    bool canSaveNow() const override;
    SessionSettings *createSettings() override;
};

///
/// \brief 最低限度的会话类型：只实现 createView()，其余全走基类默认实现。
///
/// 它回答的是 SESS-001 第 2 条里「只需实现基类契约」那半句：
/// 一个只有视图、没有数据源的新类型，不需要写 doOpen/doReload/doSave/doClose
/// 就能被打开、拿到视图、被关闭。
///
class MinimalSession : public CompareSession
{
    Q_OBJECT

public:
    explicit MinimalSession(QObject *parent = nullptr);

    int viewCalls = 0;

protected:
    QWidget *createView(QWidget *parent) override;
};

///
/// \brief 测试用的设置实现，用来证明 `createSettings()` 的覆写真的生效。
///
/// 只继承内存实现、不加任何东西：这里要验证的是**工厂被换掉了**，
/// 而不是再写一份存储。写第二份存储等于在同一件事上放第二个事实来源。
///
class ProbeSettings : public MemorySessionSettings
{
    Q_OBJECT

public:
    explicit ProbeSettings(QObject *parent = nullptr);
};

} // namespace TestSupport
} // namespace LqCompare

#endif // LQCOMPARE_TST_PROBESESSION_H
