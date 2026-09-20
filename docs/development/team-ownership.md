# 本轮团队分工（2026-09-20 夜间）

所有人在 `/Users/loren/Desktop/Work/LqCompare` 同一工作区。禁止覆盖其他目录的未提交改动，禁止 git reset/clean。各自只修改独占目录，跨模块共享API先通知协调对话。

| 工作 | 负责目录 | 集成方式 |
| --- | --- | --- |
| 主协调 | App、Views/Shell、Services/Session、集成文档与总构建 | 总体验收 |
| 文本子代理 | Services/Text、Views/Text、Tests/Text* | text.pri/textview.pri |
| 文件夹子代理 | Services/Folder、Views/Folder、Tests/Folder* | folder.pri/folderview.pri |
| 产品审查子代理 | tools/spec、生成PRD/索引、审查报告 | 主协调同步issue |
| 三方合并独立对话 | Services/Merge、Views/Merge、Tests/Merge* | merge.pri/mergeview.pri |
| Git差异独立对话 | Services/Vcs、Views/Vcs、Tests/Vcs* | vcs.pri/vcsview.pri |
| 专用视图独立对话 | Services/Special、Views/Special、Tests/Special* | specialviews.pri（两层各一份） |
| 报表补丁独立对话 | Services/Report、Services/Patch、Tests/Report*、Tests/Patch* | report.pri/patch.pri |

独立团队不得改 App、Services/Session、工具/规格及任何不属于自己的共享文件。顶层 services.pri/views.pri 已有 exists include 自动接入。尽早提供公共接口与集成方法给协调对话；构建使用每路独立目录与 make -j4，避免争抢。

GitHub issue 与共享文档由主协调统一更新。不要自行发布、关闭issue或推送。每路成果写各自 `docs/development/team-<模块>.md` 交付记录，标出哪些验收通过及哪些仍未完成。

## 独立对话索引

- 三方文本合并：01a0bf70-4000-7022-b4b1-69005a4ef513
- Git 差异集成：01a0bf70-41fd-7e12-9e59-4bfef1c0d0dd
- 十六进制与图片比较：01a0bf70-43c2-7c33-a2b4-7aeb8906d92f
- 报表与补丁：01a0bf70-4569-7012-991b-fdf689414c07
- 命令行与脚本：01a0bf71-8f8d-7a83-aec4-db16cdaf2535（Services/Cli、Script、Tests/Cli*、Script*）
- 目录同步与快照：01a0bf71-9188-7b30-8aec-518c5263ad56（Services/Sync、Snapshot、Views/Sync、Tests/Sync*、Snapshot*）
- 单实例与平台修复：01a0bf71-9354-75b1-95a1-421362a6a3e8（Platform/instanceprotocol、singleinstance、Tests/SingleInstance）
- 选项与设置界面：01a0bf71-9502-7982-b22c-c7e80c07941a（Services/Settings、Views/Options、Tests/Options*）
- 文件格式与过滤接入：01a0bf71-96f7-7e02-939f-d6c3f3435669（Services/Format、Views/Filter、Tests/Format*、FilterView*）
- 压缩包比较：01a0bf71-98fb-7c33-a1b6-f3339513596c（Services/Archive、Views/Archive、Tests/Archive*）
- 表格数据比较：01a0bf71-9ac9-7392-94d8-341af484b635（Services/Table、Views/Table、Tests/Table*）
- 快捷键与命令状态：01a0bf71-9c9e-7401-b9c4-fb2b63d7286f（Services/Command、Views/Page、Tests/Command*）

新增目录的 .pri 由主协调接入。编译每个任务最多 make -j2。不要用全量 run-tests.sh 同时重复构建其它团队测试。

- 版本/PE：01a0bf7a-2025-7983-9682-67779d1222e9（Services/Version、Views/Version、Tests/Version*）
- 媒体标签：01a0bf7a-2291-7c52-8a9d-811bfe148a6d（Services/Media、Views/Media、Tests/Media*）
- 注册表：01a0bf7a-24c6-7381-a4de-7766adbea14d（Services/Registry、Views/Registry、Tests/Registry*）
- 三方文件夹：01a0bf7a-26d4-7c83-89c4-7adfd5f6fee3（Services/FolderMerge、Views/FolderMerge、Tests/FolderMerge*）

整合修复追加授权：Command团队可修改Tests/SessionType旧Home护栏及新增Tests/HomeRegistry；Format团队可修Services/Filter/filterstack.cpp解释式与Tests/FilterStack。
