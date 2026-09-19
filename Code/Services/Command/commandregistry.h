#ifndef LQCOMPARE_COMMANDREGISTRY_H
#define LQCOMPARE_COMMANDREGISTRY_H

#include <QKeySequence>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace LqCompare {

///
/// \brief The Command struct
/// 一条可执行命令的完整声明。
///
/// 命令注册中心是 Ribbon 按钮、菜单项、快速访问工具栏、快捷键与搜索栏的**唯一出口**
/// （PRD: UI-024）。界面元素不允许直接连接业务槽函数。
///
/// description 对应 PRD UI-023 的两段式 tooltip 规范：第二段不允许为空，
/// 缺失时 CommandRegistry::validate() 会报出来。
///
struct Command
{
    QString id;          ///< 稳定命令 ID，形如 "session.new"，发布后不可改名
    QString actionId;    ///< 对应的 PRD ACTION-ID，形如 "SESS-005"，便于回溯规格
    QString module;      ///< 功能域，取值见 tools/spec/__init__.py 的 MODULES
    QString text;        ///< 界面显示文本
    QString description; ///< 一句话说明（tooltip 第二段）
    QString icon;        ///< 资源路径，形如 ":/Pictures/ribbon_open.svg"，可为空
    QKeySequence shortcut; ///< 默认快捷键，可为空
    std::function<void()> handler; ///< 执行体；为空表示「尚未实现」

    bool isImplemented() const { return static_cast<bool>(handler); }
};

///
/// \brief The CommandRegistry class
/// 全量命令的注册与查询。
///
class CommandRegistry : public QObject
{
    Q_OBJECT

public:
    static CommandRegistry &instance();

    /// 注册一条命令。
    ///
    /// 返回 false 的情况：id 不符合 <域>.<动作> 规范，或 id 已经存在（不覆盖已有条目）。
    bool add(const Command &command);

    bool contains(const QString &id) const;

    /// 查询命令；不存在时返回 nullptr。
    const Command *find(const QString &id) const;

    /// 按注册顺序返回全部命令。
    QVector<Command> all() const;

    /// 执行命令。命令不存在或未实现时返回 false。
    bool trigger(const QString &id) const;

    /// 自检：返回问题清单（空列表表示通过）。
    ///
    /// 检查项：id 命名规范、缺少 actionId / module / text / description、
    /// 缺少图标、重复快捷键。调试构建下启动时调用，见 App/main.cpp。
    QStringList validate() const;

    /// 清空注册表（仅测试使用）。
    void clear();

private:
    explicit CommandRegistry(QObject *parent = nullptr);

    QMap<QString, Command> m_commands;
    QStringList m_order;
};

} // namespace LqCompare

#endif // LQCOMPARE_COMMANDREGISTRY_H
