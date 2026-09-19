"""LqCompare 产品规格数据源。

本包是「issue 即规格书」的单一事实来源（single source of truth）：
`tools/publish_issues.py` 读取本包全部条目，生成 `docs/PRD-actions.md`
并在 GitHub 上批量创建 issue。

设计约束
--------
1. 一个条目 = 一个可独立实现、可独立验收的 issue，条目之间不重叠。
2. 条目的 `entry` 必须写清入口、作用对象与行为边界（成功 / 取消 / 禁用 / 失败）。
3. 条目的 `criteria` 必须是可核对的完成标准，不写「体验良好」这类不可验证的话。
4. `ref` 指向竞品测绘文档里的原始条目，便于回溯对标对象。
5. **优先级不在条目里声明。** P0/P1 由 `tools/publish_issues.py` 的 `P0_RANGES`
   按前缀区间统一推导。若条目自带一个 prio 字段，它就会和策略里的区间互相矛盾，
   同一件事出现两个事实来源——这是必须避免的。

条目字段
--------
id       : ACTION-ID，全局唯一，形如 `TXT-012`
module   : 功能域，对应 GitHub 模块标签之一（见 MODULES）
title    : issue 标题正文（不含 ACTION-ID 前缀）
entry    : 入口、作用对象与行为边界
criteria : 完成标准列表
ref      : 竞品对标出处
"""

MODULES = [
    "界面",
    "会话",
    "文本比对",
    "三方合并",
    "文件夹比对",
    "文件夹同步",
    "文件夹合并",
    "十六进制",
    "表格比对",
    "图片比对",
    "媒体比对",
    "注册表",
    "版本比对",
    "压缩包",
    "编辑视图",
    "过滤规则",
    "文件格式",
    "报表导出",
    "补丁",
    "快照",
    "版本控制",
    "命令行",
    "脚本自动化",
    "选项外观",
    "平台性能",
    "工程质量",
    "文档",
]

# Ailecium 同款状态标签 + 本仓库状态机的合法取值。
STATE_LABELS = ["待实现", "进行中", "部分完成", "待审核", "待定", "已完成", "已取消"]

# 一个条目所属的功能域 -> 该域在规格书与 issue 中使用的模块标签。
# 键为 ACTION-ID 前缀，值为 MODULES 中的一项。
PREFIX_MODULE = {
    "UI": "界面",
    "SESS": "会话",
    "TXT": "文本比对",
    "MRG": "三方合并",
    "DIR": "文件夹比对",
    "SYNC": "文件夹同步",
    "FMG": "文件夹合并",
    "HEX": "十六进制",
    "DATA": "表格比对",
    "IMG": "图片比对",
    "MED": "媒体比对",
    "REG": "注册表",
    "VER": "版本比对",
    "ARC": "压缩包",
    "EDV": "编辑视图",
    "FILT": "过滤规则",
    "FMT": "文件格式",
    "RPT": "报表导出",
    "PAT": "补丁",
    "SNAP": "快照",
    "VCS": "版本控制",
    "CLI": "命令行",
    "SCR": "脚本自动化",
    "OPT": "选项外观",
    "PLAT": "平台性能",
    "ENG": "工程质量",
    "DOC": "文档",
}


def A(aid, module, title, entry, criteria, ref=""):
    """构造一条规格条目。优先级不在这里给，见模块文档第 5 条约束。"""
    if module not in MODULES:
        raise ValueError("未知功能域 %r（条目 %s）" % (module, aid))
    if "_" in aid or "-" not in aid:
        raise ValueError("ACTION-ID 必须形如 PREFIX-NNN：%r" % aid)
    prefix = aid.split("-", 1)[0]
    if PREFIX_MODULE.get(prefix) != module:
        raise ValueError(
            "条目 %s 的前缀 %s 与功能域 %s 不一致" % (aid, prefix, module)
        )
    if not title.strip():
        raise ValueError("条目 %s 缺少标题" % aid)
    if not entry.strip():
        raise ValueError("条目 %s 缺少行为边界" % aid)
    if isinstance(criteria, str):
        criteria = [criteria]
    criteria = [c.strip() for c in criteria if c and c.strip()]
    if not criteria:
        raise ValueError("条目 %s 缺少完成标准" % aid)
    return {
        "id": aid,
        "module": module,
        "title": title.strip(),
        "entry": entry.strip(),
        "criteria": criteria,
        "ref": ref.strip(),
    }


def load_actions():
    """导入全部规格模块并返回按 ACTION-ID 排序的条目列表。"""
    from . import a_ui, b_text, b_merge, c_folder, d_views, e_engine, f_system
    from . import g_quality

    actions = []
    for mod in (a_ui, b_text, b_merge, c_folder, d_views, e_engine, f_system,
                g_quality):
        actions.extend(getattr(mod, "ACTIONS", []))

    seen = {}
    for item in actions:
        if item["id"] in seen:
            raise ValueError("ACTION-ID 重复：%s" % item["id"])
        seen[item["id"]] = item

    # 顺序 = 规格模块的声明顺序，模块内按 ACTION-ID 的自然序号。
    # 不按前缀字典序排列，否则 FILT/FMT 等会与规格书的阅读顺序脱节。
    order = {name: index for index, name in enumerate(
        dict.fromkeys(item["id"].split("-")[0] for item in actions))}

    def sort_key(item):
        prefix, number = item["id"].split("-", 1)
        try:
            numeric = int(number)
        except ValueError:
            numeric = 10 ** 6
        return (order.get(prefix, 10 ** 6), numeric, item["id"])

    return sorted(actions, key=sort_key)
