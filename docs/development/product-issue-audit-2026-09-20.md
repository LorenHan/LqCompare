# LqCompare 产品与 Issue 全量审查（2026-09-20）

**结论：原有 issue 并非全部符合产品设计。** 功能主线覆盖充分，但存在内部矛盾、越过产品范围、混淆显示与写入、无法成立的验收承诺，以及会造成错误比较或覆盖的风险。本次逐项审查全部 369 个 ACTION-ID 和对应 GitHub issue，保留所有 ID 与历史，直接修正规格源中的 **92 项**，随后重新生成 PRD 与 issue 索引；没有删除 issue，也没有把规格修订当作功能已实现。

## 审查基线与可核查范围

- 产品裁决：Beyond Compare 为功能主参考，TortoiseGit diff/merge 为交互补充；Ribbon 多页、只做比较与合并、先本地文本/目录闭环均遵循 `docs/PRD.md` 和用户已确认顺序。
- 已读：`tools/spec/` 全部数据、`docs/PRD-actions.md`、`docs/research/`、`docs/design/architecture.md`、`docs/development/current-handoff.md`，以及 `.codex-work/issues-all.json` 的 369 条完整 issue 正文/状态/标签。
- 映射核对：规格 369、远端快照 369，ACTION-ID 无重复、无缺失、无多余；修订前所有 issue 验收条目文本与规格逐项一致。15 条正文存在勾选及实现证据扩充，属于进度记录，不能被重新发布抹除。优先级标签与生成策略一致（P0 59，P1 310）。
- 状态快照：369 条均 OPEN；354 待实现、12 部分完成、2 待审核、1 已完成。唯一「已完成」为 SESS-006 / #39，OPEN 与终态标签不一致；没有已关闭 issue，因此不存在可据此判定的“错误关闭”。
- 本报告审查需求设计和状态证据边界，**不是 369 个功能的端到端验收证书**。当前会话其它代理正在实现代码，后续进度以新的测试、提交和 issue 更新为准；表内远端状态特意保留审查开始快照。

## 官方依据与明确裁决

1. **文本行尾默认值**：BC 默认忽略行尾风格差异，可独立启用比较。原 TXT-010 要求默认不忽略，已修；保存仍保留原字节，末尾换行独立控制。[BC Text Compare Importance](https://www.scootersoftware.com/v5help/sessiontextimportance.html)
2. **快速测试不能无条件截断内容结论**：BC 区分快速测试/内容测试，提供内容结果覆盖快速测试的选项。已修 DIR-004/010；CRLF 与 LF 导致原始大小不同仍可规则相同。[BC Folder Compare Comparison Settings](https://www.scootersoftware.com/v5help/sessiondircomparison.html)
3. **格式转换并非任意可逆**：BC 明确分加载转换与保存转换，编辑需有相应保存能力。已修 FMT-004，不允许显示编辑后的转换内容却把旧原文写回并报告成功。[BC Text Format Conversion](https://www.scootersoftware.com/v5help/formattextconversion.html)
4. **快照不是内容备份**：BC 快照存目录信息，可带 CRC；不含原文件就不能做二进制/规则比较。已修 SNAP-001/002。[BC Snapshots](https://www.scootersoftware.com/v5help/snapshots.html)
5. **命令行次序与返回码必须自洽**：BC 原生三/四位置参数是左/右/中心/输出，BC 本身返回码也并非常见 diff 的 0/1/2。LqCompare 明确采用自己的 CLI-004 表，Git mergetool 通过命名绑定适配，不能冒充逐字兼容 BC。[BC Command Line Reference](https://www.scootersoftware.com/v5help/command_line_reference.html)
6. **阅读命令不是编辑权限**：TortoiseGitMerge 的选择、复制、查找、跳转和差异导航均是基本阅读路径。UI-010/SESS-017 已保留这些能力，写入命令才受只读约束。[TortoiseGitMerge Keyboard Shortcuts](https://tortoisegit.org/docs/tortoisegitmerge/tme-keyboard.html)
7. **保留自有安全取舍**：未知不当相同、无基线双向差异先冲突、删除不静默降级永久、写前预演/备份属于本产品明确安全设计。它们应标为有意差异，不能宣称完全复刻 BC。BC 的同步脚本有 update/mirror 行为，不能据此推导本产品的基线同步协议。[BC Scripting Reference](https://www.scootersoftware.com/v5help/scripting_reference.html)

`docs/research/` 的功能点计数代表本地测绘条目数，不是官方认证的功能数。TortoiseGit 文档 C.3 明写“建议的 Ribbon 页面”，UI-007～016 引用它表示本项目布局方案，不能说是 TortoiseGit 现有十页界面。词法算法名称、结构化解析、画笔、多帧、波形、复杂脚本及完整历史图都需与自有增强区分。

## 必须优先纠正的设计问题

| 优先级 | 原问题及影响 | 已落实的规格裁决 |
| --- | --- | --- |
| 立即 | DIR-004/010 大小或时间不同就短路，规则比较失真；DIR-011 重叠状态且无基线推断左右均改 | 快速/内容证据分开，覆盖显式，主状态/时间/完整性分维度 |
| 立即 | SYNC-002 首次同步按时间选边；预演后目标变化仍执行；不完整扫描可能被解释为删除 | 无基线保守冲突，计划失效重新预演，错误/取消禁止镜像删除 |
| 立即 | FMT-004 转换保存无法逆转；TXT-014 重解码丢稿/替换字符写坏源文件 | 保留原文模型，不可逆转换只读，重解码先处理脏内容，有损解码不默认覆盖 |
| 立即 | UI-010 只读整页禁用；UI-026 调Tab宽度也标dirty；TXT-004 切算法清撤销 | 阅读与写入能力分离，显示状态不修改文件，不丢内容撤销历史 |
| 高 | VCS-014 要做提交，VCS-015 自动暂存，违反PRD“提交交外部工具” | 改为变更审阅与外部提交入口；merge只写MERGED并回报调用方 |
| 高 | CLI/ARC/RPT 各自定义不相容返回码；Git四参数被当原生位置顺序 | CLI-004单一表，合并成功/取消单独说明，命名参数适配Git |
| 高 | SESS-007四层，OPT-003/014五层；FILT-010白名单推翻排除优先 | 统一四层设置链；显式停用排除才可扩大范围 |
| 高 | SNAP说不是备份又保存内容；PE承诺缺失字段；管理员可读全部键；归档可还原平均压缩级别 | 删除不可能/越界承诺，未知数据如实未知，格式能力显式化 |
| 高 | DATA-010/IMG-007 无条件承诺原格式保真保存；归档 .bak 覆盖历史备份 | 无法保真时只读/另存，备份唯一命名，保存能力按格式矩阵声明 |
| 高 | EDV-003/004与PAT重复；发布规格覆盖已有勾选/提交证据 | EDV仅UI，PAT统一服务；发布须保留独立实现证据并重验变更项 |

## 各模块结论与依赖

| 模块 | 数量 | 结论 | 主要依赖 |
| --- | ---: | --- | --- |
| UI | 35 | 基础框架符合；已修只读/显示状态/命令权限冲突。多页Ribbon来自本项目信息架构，不是TortoiseGit原样复刻。 | UI-024、SESS-001/017/018 |
| SESS | 20 | 总体分层合理；四层设置链统一，剪贴板编码与便携存储已修。先接真实会话工厂和关闭/重载脏保护。 | PLAT-002、UI-024、FMT-001 |
| TXT | 40 | 核心方向正确，已修EOL默认、重要性、规范化组合、重解码与撤销语义。性能/布局需明确验收预算。 | SESS、FILT、FMT；TXT-002→TXT-001/022/029/030 |
| MRG | 18 | 保守三路合并合适，输入/输出和Git调用契约已统一。先纯归并模型，再决策、统一撤销、落盘。 | TXT、SESS-018、CLI-004 |
| DIR | 38 | 已修快速测试短路、状态维度、扫描深度、预演安全。目录真实浏览及子视图往返优先于写操作。 | PLAT-002/007/008、FILT、TXT、FMT |
| SYNC | 12 | 先预演后执行符合；双向基线是自有安全增强，应与BC按时间更新模式分开说明。已补计划失效与范围保护。 | DIR-010/011/019/027、SNAP、FILT-006 |
| FMG | 10 | 计划与落盘分离符合；已统一覆盖前默认备份。进入开发前须完成可执行操作计划与文本合并。 | DIR、MRG、SYNC操作计划 |
| HEX | 10 | 基础偏移比较可用；结构解析是额外范围。先读、找、导航，再可撤销编辑。 | 虚拟字节源、原子保存 |
| DATA | 12 | CSV/TSV可先交付；已去一对多映射歧义和Excel无损写回假承诺，复杂格式后置。 | FMT-005、文本编码、RPT |
| IMG | 10 | 像素/元数据分开合理；大图和颜色空间待明确。画笔编辑、多帧属于后续增强。 | 解码器能力、内存预算、RPT |
| MED | 6 | 标签定位符合；播放/波形不是相等判定且后置。支持格式与写回能力需按依赖库验收。 | 格式识别、标签库、操作备份 |
| REG | 6 | Windows限定与HKCU默认符合；管理员万能读取及无方向修复导出已修。 | 注册表服务、Windows真实测试、备份 |
| VER | 5 | 版本回退符合工程/运维定位；已修PE不可提供的字段承诺。 | 平台无关PE解析、Windows入口、DIR |
| ARC | 8 | 归档目录源合理；已补受控缓存、备份防覆盖和解压资源预算，统计分析后置。 | 统一数据源、DIR、格式读写能力矩阵 |
| EDV | 5 | 编辑/补丁视图合理；已区分EDV界面与PAT服务职责，保留ID避免丢历史。 | TXT组件、PAT服务 |
| FILT | 12 | 已实现服务方向正确；排除/白名单冲突已修。真实正则资源上限仍要复验。 | 扫描与显示两层模型、设置作用域 |
| FMT | 12 | 数据化定义符合；已修不可逆转换保存和掩码重叠模型。优先常用文本与视图路由。 | FILT、SESS类型、进程服务 |
| RPT | 12 | 模型驱动合理；已修未知摘要、HTML引用及退出码。先TXT/CSV/离线HTML，打印/模板后置。 | 稳定比较结果模型、CLI |
| PAT | 6 | 标准补丁与真实工具验收合理；已补原始内容补丁语义。需务实定义多文件回滚失败路径。 | TXT、DIR文件操作、事务备份 |
| SNAP | 6 | 已收敛为元数据/摘要快照，不再承担内容备份；比较能力必须按所含证据限制。 | DIR元数据、CRC、SYNC基线协议 |
| VCS | 19 | 只读比较与外部工具协作符合；已删内置commit/自动暂存范围。日志/图/Blame排在比较闭环之后。 | 进程服务、虚拟只读源、TXT/MRG/CLI |
| CLI | 12 | 已统一位置参数/类型/退出码，禁止通配截断。先真实两路径打开和只读，再静默/等待/报表。 | 会话工厂、解析表、进程IPC |
| SCR | 10 | 批量比较合理；已定义静默写授权与预演。控制流/录制为后续自有扩展，避免伪称BC兼容。 | CLI、文件操作计划、RPT |
| OPT | 14 | 统一设置仓库合理；已消除第五层覆盖，远程明确后续。先把已有服务接入真实设置。 | SESS-007/008、各领域设置声明 |
| PLAT | 10 | 平台抽象和可测试边界合理；Windows目标验收仍需真实编译/运行，macOS通过不可替代。 | Qt5.15.2/MinGW8.1 32位、系统API |
| ENG | 15 | 质量护栏符合；已修静态Qt政策冲突和issue状态/发布证据丢失。性能与覆盖率需定量。 | 构建脚本、CI、实际套件 |
| DOC | 6 | 必要且合理；修正13/14类型、未核实竞品断言和与实际实现漂移，持续维护。 | 每批实现与工具生成输出 |

## 实现顺序与完成口径

1. **可打开、看懂并保存文本**：会话工厂/参数入口 → 解码与EOL/BOM → Myers/Patience结果 → 双窗格/真实行号/差异导航/规则切换 → 查找 → 独立撤销、块搬运、原子保存、脏关闭与重载保护。覆盖 TXT-001/002/003/008～010/014～017/019/022/024/025/028～030/036/040，并接 SESS-003/010/017/018。先让真实按钮处理真实文件。
2. **可浏览目录并返回上下文**：扫描/取消/未知错误 → 配对与正确状态证据 → 显示过滤/选择/排序 → 双击子视图及返回定位 → 刷新。这是 DIR-001～018/028/029/034/037/038 的首个可用切片；扫描范围过滤不可被显示隐藏项覆盖。
3. **安全文件操作**：统一操作计划、备份、回收站、原子写、失败重试 → 复制/移动/重命名/删除 → 镜像和同步预演 → 计划过期与不完整扫描保护。DIR-019～027、FILT-006、SYNC 必须依赖同一个操作服务。
4. **三路合并与Git工具契约**：纯归并模型 → 输入只读/输出编辑 → 冲突决策统一撤销 → 保存与退出码 → 真 Git mergetool 验证；文件夹合并在文本合并与操作计划都稳定后做。
5. **可复用工作流**：会话文件/默认值/最近记录、完整格式定义与规则、TXT/CSV/HTML报表、补丁与只读快照、CLI静默/等待/脚本。补丁以原始字节为基线，与显示忽略严格分开。
6. **专用视图与扩展**：先Hex与CSV，再图片/版本/只读归档/媒体；结构解析、XLSX写回、画笔/多帧、波形、复杂VCS历史、脚本控制流/录制、原生远程连接依次后置。后置不是删除需求，未实现按钮仍依PRD展示明确状态。

完成必须逐项满足该 issue 的所有适用验收项、真实入口可到达、失败/取消路径有证据，并通过对应平台和回归。纯服务实现可标部分完成；框架型issue若验收本就只到可独立构造的框架，可以进入待审核，但不能把14个类型的登记当14种会话都可用。P0/P1是优先级，不是依赖排序；例如编辑、保存、脏关闭原来部分为P1，仍属于首个文本闭环。

## 尚须细化的验收与共享文档

- 多处写“不卡顿”“有上界”“立即”“内存不爆增”而没有硬件/语料/构建模式/预算。由 PLAT-009 统一基准档：32位目标的峰值内存、文本100MB/超长行、8000×8000图片、目录10万项、报表50万项；CI不固定性能时区分功能断言与性能趋势，不能用任意机器2秒作绝对门槛。
- PAT-002 的全成全退需要定义备份/回滚本身失败、空间耗尽与掉电恢复；不能宣称文件系统多文件事务绝不会失败。FMG/SYNC 对部分成功采取明示清单，与补丁事务要区分。
- FILT-002 的超时结果返回和后台计算真正停止是两件事。现有文档承认遗弃工作线程，应针对真实恶意正则验证并发/内存/CPU总预算，而不是只验证200ms调用返回。
- `docs/development/current-handoff.md` 里“Qt 6.0才有 QRegularExpression::setMatchTimeout()”不应保留：官方成员表并无此API。只应陈述当前Qt接口缺可中断超时并说明已用保护机制及限制。[Qt QRegularExpression](https://doc.qt.io/qt-6/qregularexpression.html)
- `docs/PRD.md` 把4种文本+3种文件夹+4种数据+3种系统写成13种，实际14；BC原生13种加本项目Archive独立类型1种。类型ID已发布，不应为修统计而删已有类型。
- `docs/design/architecture.md` 的模块清单只是当时基线，需随本轮 Text/Folder/IPC 新服务同步；本报告不手改其他代理正维护的共享文档。
- `docs/research/reference-projects.md` 同时称全部四库GPL又称FreeFileSync许可未核实，属于证据层自相矛盾；采用仓库“不引入未核实依赖”的政策即可，不需编造已核实的结论。
- 不为本次收敛新增重复issue：Histogram先作为TXT-004明确后续增量；远程数据源服务在OPT-009开工前再拆依赖；补丁复用、字节数据源、文件操作计划先列跨模块实现依赖。新增issue应带明确独立验收，避免仅为增加数量而拆。

## 逐条判定

“符合”表示定位与边界可接受，**不表示已实现**；“已修订”表示本次修改了规格源；“需细化”是可保留目标但开工前需明确预算/语义；“后续增强”表示不阻挡文本/目录核心交付。每行列出具体判定点，远端状态为审查初始快照。

设计判定分布：符合 233、已修订 92、需细化 27、后续增强 17；合计 369。

### UI

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `UI-001` [#2](https://github.com/LorenHan/LqCompare/issues/2) | 待实现 | 符合 | 窗口装配、空参Home和脏关闭符合；需SESS-018贯通。 |
| `UI-002` [#1](https://github.com/LorenHan/LqCompare/issues/1) | 待实现 | 符合 | 样式即时切换与最小化合理；验证会话状态不丢。 |
| `UI-003` [#3](https://github.com/LorenHan/LqCompare/issues/3) | 待实现 | 已修订 | 已区分默认QAT与用户主动加入专属命令。 |
| `UI-004` [#4](https://github.com/LorenHan/LqCompare/issues/4) | 待实现 | 符合 | 搜索共用注册表；禁用命令仍可搜索但不可越权执行。 |
| `UI-005` [#5](https://github.com/LorenHan/LqCompare/issues/5) | 待实现 | 符合 | 后台菜单与Home重复入口合理；统一启用条件。 |
| `UI-006` [#6](https://github.com/LorenHan/LqCompare/issues/6) | 待实现 | 符合 | 上下文菜单副作用可验；依赖UI-024和快捷访问存储。 |
| `UI-007` [#7](https://github.com/LorenHan/LqCompare/issues/7) | 待实现 | 符合 | Home入口符合；VCS组先禁用未实现能力，避免挡住文本闭环。 |
| `UI-008` [#8](https://github.com/LorenHan/LqCompare/issues/8) | 待实现 | 符合 | 规则/路径/对齐分组符合；控件须按会话能力启用。 |
| `UI-009` [#10](https://github.com/LorenHan/LqCompare/issues/10) | 待实现 | 符合 | Merge为上下文页符合；输入只读由MRG-001约束。 |
| `UI-010` [#9](https://github.com/LorenHan/LqCompare/issues/9) | 待实现 | 已修订 | 已保留只读模式的查找、选择与跳转。 |
| `UI-011` [#11](https://github.com/LorenHan/LqCompare/issues/11) | 待实现 | 符合 | 显示与数据操作分离符合；布局模式需明确互斥。 |
| `UI-012` [#12](https://github.com/LorenHan/LqCompare/issues/12) | 待实现 | 符合 | 过滤入口符合；扫描过滤和显示过滤不能混用。 |
| `UI-013` [#13](https://github.com/LorenHan/LqCompare/issues/13) | 待实现 | 符合 | 会话定义脏标记合理；与文件内容脏状态分开。 |
| `UI-014` [#15](https://github.com/LorenHan/LqCompare/issues/15) | 待实现 | 符合 | 报表入口符合；共用RPT异步生成与取消。 |
| `UI-015` [#14](https://github.com/LorenHan/LqCompare/issues/14) | 待实现 | 需细化 | 外部进程服务集中合理；Capture组尚缺具体功能定义。 |
| `UI-016` [#16](https://github.com/LorenHan/LqCompare/issues/16) | 待实现 | 符合 | 离线帮助/诊断符合；诊断导出接OPT-010。 |
| `UI-017` [#17](https://github.com/LorenHan/LqCompare/issues/17) | 待实现 | 已修订 | 已区分能力禁用与单纯布局隐藏。 |
| `UI-018` [#20](https://github.com/LorenHan/LqCompare/issues/20) | 待实现 | 需细化 | 补明确最小支持宽度与缩放矩阵，不能承诺所有宽度无裁切。 |
| `UI-019` [#18](https://github.com/LorenHan/LqCompare/issues/18) | 待实现 | 符合 | 主动作/变体独立启用合理；使用同一注册命令。 |
| `UI-020` [#19](https://github.com/LorenHan/LqCompare/issues/19) | 待实现 | 符合 | 多选/互斥模式合理；开菜单前从真实状态刷新。 |
| `UI-021` [#21](https://github.com/LorenHan/LqCompare/issues/21) | 待实现 | 后续增强 | 可视预设属后续便利能力；悬停预览必须可回退且不重扫大目录。 |
| `UI-022` [#22](https://github.com/LorenHan/LqCompare/issues/22) | 待实现 | 符合 | 四种路径输入同源合理；编辑路径切换需SESS-018保护。 |
| `UI-023` [#23](https://github.com/LorenHan/LqCompare/issues/23) | 待实现 | 符合 | 两段提示与禁用理由合理；不得只靠tooltip暴露未实现状态。 |
| `UI-024` [#24](https://github.com/LorenHan/LqCompare/issues/24) | 待实现 | 符合 | 命令ID、能力和回调中心符合架构；各入口必须统一调度。 |
| `UI-025` [#25](https://github.com/LorenHan/LqCompare/issues/25) | 待实现 | 符合 | 单套SVG资源符合；声明尺寸与DPI物理像素区分。 |
| `UI-026` [#26](https://github.com/LorenHan/LqCompare/issues/26) | 待实现 | 已修订 | 已分离读取编码、保存格式和仅显示的Tab宽度。 |
| `UI-027` [#27](https://github.com/LorenHan/LqCompare/issues/27) | 待实现 | 符合 | 快捷键冲突可验；互斥会话上下文可明确允许复用。 |
| `UI-028` [#28](https://github.com/LorenHan/LqCompare/issues/28) | 待实现 | 符合 | 主题与差异语义分离合理；图标/文字补充颜色。 |
| `UI-029` [#30](https://github.com/LorenHan/LqCompare/issues/30) | 待实现 | 符合 | 布局持久化合理；异屏校正须考虑可用屏幕区域。 |
| `UI-030` [#29](https://github.com/LorenHan/LqCompare/issues/29) | 待实现 | 符合 | 运行时翻译可验；英文回退要求源文案或英文目录一致。 |
| `UI-031` [#33](https://github.com/LorenHan/LqCompare/issues/33) | 待实现 | 已修订 | 已补同一文件对重复打开的标题序号。 |
| `UI-032` [#32](https://github.com/LorenHan/LqCompare/issues/32) | 待实现 | 符合 | 统一对话框合理；默认取消和受影响数量符合安全原则。 |
| `UI-033` [#31](https://github.com/LorenHan/LqCompare/issues/31) | 待实现 | 符合 | 拖放闭环合理；单文件的二进制识别需复用FMT兜底。 |
| `UI-034` [#34](https://github.com/LorenHan/LqCompare/issues/34) | 待实现 | 需细化 | 补实际目标Windows分数DPI矩阵及验收截图，offscreen不能替代。 |
| `UI-035` [#35](https://github.com/LorenHan/LqCompare/issues/35) | 待实现 | 符合 | 全键盘路径合理；Alt数字与QAT默认快捷键须避免冲突。 |

### SESS

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `SESS-001` [#36](https://github.com/LorenHan/LqCompare/issues/36) | 待审核 | 符合 | 抽象生命周期符合分层；待审核不等于用户操作闭环已完成。 |
| `SESS-002` [#37](https://github.com/LorenHan/LqCompare/issues/37) | 待审核 | 符合 | 数据描述与工厂注册合理；14类型包含自有归档类型，需修PRD计数。 |
| `SESS-003` [#38](https://github.com/LorenHan/LqCompare/issues/38) | 待实现 | 符合 | Home入口符合；首要接可用文本/目录工厂。 |
| `SESS-004` [#41](https://github.com/LorenHan/LqCompare/issues/41) | 待实现 | 符合 | 会话树仅索引合理；删除定义文件须分开确认。 |
| `SESS-005` [#40](https://github.com/LorenHan/LqCompare/issues/40) | 待实现 | 符合 | 向导取消无残留合理；需虚拟数据源类型而非仅路径字符串。 |
| `SESS-006` [#39](https://github.com/LorenHan/LqCompare/issues/39) | 已完成 | 符合 | 框架设计符合；OPEN+已完成状态不合法，实际入口验收另记录。 |
| `SESS-007` [#42](https://github.com/LorenHan/LqCompare/issues/42) | 部分完成 | 符合 | 四层解析及三种写作用域符合；已消除OPT额外第五层。 |
| `SESS-008` [#43](https://github.com/LorenHan/LqCompare/issues/43) | 待实现 | 符合 | 会话文件兼容合理；与视图临时设置持久化边界保持一致。 |
| `SESS-009` [#46](https://github.com/LorenHan/LqCompare/issues/46) | 待实现 | 符合 | 历史两列表合理；路径凭据脱敏与失效修复可验。 |
| `SESS-010` [#44](https://github.com/LorenHan/LqCompare/issues/44) | 待实现 | 符合 | 独立标签/脏关闭符合；独立窗口明确留后续，先完成标签闭环。 |
| `SESS-011` [#45](https://github.com/LorenHan/LqCompare/issues/45) | 待实现 | 需细化 | 工作区引用会话合理；临时匿名会话应内嵌最小定义或先保存。 |
| `SESS-012` [#48](https://github.com/LorenHan/LqCompare/issues/48) | 待实现 | 已修订 | 已删除探测Unicode剪贴板源编码的不可能要求。 |
| `SESS-013` [#47](https://github.com/LorenHan/LqCompare/issues/47) | 待实现 | 符合 | 格式路由符合；历史默认掩码与FMT用户优先级须同源。 |
| `SESS-014` [#49](https://github.com/LorenHan/LqCompare/issues/49) | 待实现 | 符合 | Compare Using符合；换视图必须处理脏状态并保留可迁移设置。 |
| `SESS-015` [#50](https://github.com/LorenHan/LqCompare/issues/50) | 待实现 | 已修订 | 已统一标准/便携模式默认值存储。 |
| `SESS-016` [#51](https://github.com/LorenHan/LqCompare/issues/51) | 待实现 | 符合 | CLI初值不污染默认符合；共享CLI参数模型。 |
| `SESS-017` [#53](https://github.com/LorenHan/LqCompare/issues/53) | 待实现 | 已修订 | 已保留只读光标和阅读能力；文件写保护仍在模型层实施。 |
| `SESS-018` [#52](https://github.com/LorenHan/LqCompare/issues/52) | 待实现 | 需细化 | 必须补状态机Error/取消/关闭中分支；文本保存与定义保存分别追踪。 |
| `SESS-019` [#55](https://github.com/LorenHan/LqCompare/issues/55) | 待实现 | 已修订 | 已区分主比较状态、时间维度和基线派生统计。 |
| `SESS-020` [#54](https://github.com/LorenHan/LqCompare/issues/54) | 待实现 | 符合 | 原子保存与可定位失败符合；未知、错误不可当不存在。 |

### TXT

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `TXT-001` [#56](https://github.com/LorenHan/LqCompare/issues/56) | 待实现 | 需细化 | 双窗格共享结果符合；补最大规模/取消预算，不承诺任意全异输入恒快。 |
| `TXT-002` [#57](https://github.com/LorenHan/LqCompare/issues/57) | 待实现 | 需细化 | Myers与空间上界符合；2秒断言需固定硬件、语料与构建模式。 |
| `TXT-003` [#58](https://github.com/LorenHan/LqCompare/issues/58) | 待实现 | 已修订 | 已划分Patience服务与选择UI职责，不再隐含第三算法实现。 |
| `TXT-004` [#59](https://github.com/LorenHan/LqCompare/issues/59) | 待实现 | 已修订 | 已去掉未实现第三算法硬要求与切算法清空撤销栈。 |
| `TXT-005` [#60](https://github.com/LorenHan/LqCompare/issues/60) | 待实现 | 已修订 | 已用候选分值边界代替不成立的全局修改块数单调。 |
| `TXT-006` [#62](https://github.com/LorenHan/LqCompare/issues/62) | 待实现 | 符合 | 手动对齐符合；保存锚点需内容指纹，文件变化时提示失效。 |
| `TXT-007` [#61](https://github.com/LorenHan/LqCompare/issues/61) | 待实现 | 后续增强 | 按掩码强制文本行范围属扩展；先明确锚点失效和多段不交叉约束。 |
| `TXT-008` [#63](https://github.com/LorenHan/LqCompare/issues/63) | 待实现 | 已修订 | 已改为规范化链，不再按逻辑与组合或错误断言差异块计数。 |
| `TXT-009` [#64](https://github.com/LorenHan/LqCompare/issues/64) | 待实现 | 已修订 | 已将空白规则定义为互斥枚举。 |
| `TXT-010` [#65](https://github.com/LorenHan/LqCompare/issues/65) | 待实现 | 已修订 | 已按BC默认忽略行尾风格，严格预设可重新启用。 |
| `TXT-011` [#66](https://github.com/LorenHan/LqCompare/issues/66) | 待实现 | 符合 | 注释规则复用格式语法合理；必须保留字符串字面量。 |
| `TXT-012` [#69](https://github.com/LorenHan/LqCompare/issues/69) | 待实现 | 符合 | 噪声替换属显式规则；默认关闭宽泛数字/日期规则避免误忽略。 |
| `TXT-013` [#67](https://github.com/LorenHan/LqCompare/issues/67) | 待实现 | 已修订 | 已区分重要性分类、忽略不重要显示与整行过滤。 |
| `TXT-014` [#68](https://github.com/LorenHan/LqCompare/issues/68) | 待实现 | 已修订 | 已补重解码脏保护和有损解码禁止静默覆盖。 |
| `TXT-015` [#70](https://github.com/LorenHan/LqCompare/issues/70) | 待实现 | 已修订 | 已区分规则相同和字节完全一致。 |
| `TXT-016` [#71](https://github.com/LorenHan/LqCompare/issues/71) | 待实现 | 符合 | 保持混合EOL与末尾换行合理；保存按字节核对。 |
| `TXT-017` [#72](https://github.com/LorenHan/LqCompare/issues/72) | 待实现 | 已修订 | 已明确显示宽度遵循作用域，不强制污染其它会话。 |
| `TXT-018` [#73](https://github.com/LorenHan/LqCompare/issues/73) | 待实现 | 需细化 | 10万行流畅需给帧时/后台预算；关闭高亮不承诺固定速度提升。 |
| `TXT-019` [#75](https://github.com/LorenHan/LqCompare/issues/75) | 待实现 | 符合 | 按逻辑行同步符合；超长行换行与TXT-034共用映射。 |
| `TXT-020` [#76](https://github.com/LorenHan/LqCompare/issues/76) | 待实现 | 已修订 | 已保留上下布局的同步开关与行对齐。 |
| `TXT-021` [#74](https://github.com/LorenHan/LqCompare/issues/74) | 待实现 | 需细化 | 单栏与全部内联概念尚重叠，实施前给两种模式差异示例及编辑能力表。 |
| `TXT-022` [#77](https://github.com/LorenHan/LqCompare/issues/77) | 待实现 | 符合 | 导航跳过过滤结果符合；无差异/边界要可解释。 |
| `TXT-023` [#78](https://github.com/LorenHan/LqCompare/issues/78) | 待实现 | 需细化 | 采样概览合理；明确渲染预算与极端密集差异聚合。 |
| `TXT-024` [#79](https://github.com/LorenHan/LqCompare/issues/79) | 待实现 | 符合 | 真实行号不被显示过滤重编号，符合定位预期。 |
| `TXT-025` [#80](https://github.com/LorenHan/LqCompare/issues/80) | 待实现 | 需细化 | 码点不拆分是底线；补组合字符/emoji字素边界和最长行预算。 |
| `TXT-026` [#353](https://github.com/LorenHan/LqCompare/issues/353) | 待实现 | 符合 | 显示过滤不改变保存内容符合；组合逻辑须统一命令模型。 |
| `TXT-027` [#352](https://github.com/LorenHan/LqCompare/issues/352) | 待实现 | 符合 | 书签作为视图状态合理；编辑后用锚点迁移而非固定行号。 |
| `TXT-028` [#354](https://github.com/LorenHan/LqCompare/issues/354) | 待实现 | 已修订 | 已补替换/全部替换、撤销与只读禁用验收。 |
| `TXT-029` [#355](https://github.com/LorenHan/LqCompare/issues/355) | 待实现 | 符合 | 增量重算/独立撤销/原子写符合；保存前校验外部改动。 |
| `TXT-030` [#81](https://github.com/LorenHan/LqCompare/issues/81) | 待实现 | 符合 | 块搬运为可撤销编辑符合；只读目标必须服务侧拦截。 |
| `TXT-031` [#82](https://github.com/LorenHan/LqCompare/issues/82) | 待实现 | 符合 | 文本转换范围可见且可撤销符合；超大全文操作需确认/取消。 |
| `TXT-032` [#83](https://github.com/LorenHan/LqCompare/issues/83) | 待实现 | 需细化 | 32位100MB文本需明确峰值内存与降级预算；不能只写有上界。 |
| `TXT-033` [#84](https://github.com/LorenHan/LqCompare/issues/84) | 待实现 | 符合 | 不可见字符显示不改内容符合；搜索仍针对原字符。 |
| `TXT-034` [#85](https://github.com/LorenHan/LqCompare/issues/85) | 待实现 | 需细化 | 1MB单行限制可声明；补实际布局时间及取消阈值。 |
| `TXT-035` [#86](https://github.com/LorenHan/LqCompare/issues/86) | 待实现 | 后续增强 | 矩形选择属后续编辑增强；Tab/宽字符列定义先明确。 |
| `TXT-036` [#87](https://github.com/LorenHan/LqCompare/issues/87) | 待实现 | 符合 | 真实行列导航符合；两侧同一逻辑行应依对齐而非同数字行号。 |
| `TXT-037` [#88](https://github.com/LorenHan/LqCompare/issues/88) | 待实现 | 符合 | 报表复用RPT；TXT/HTML编码不必都因Excel强制BOM。 |
| `TXT-038` [#90](https://github.com/LorenHan/LqCompare/issues/90) | 待实现 | 已修订 | 已统一外部HTML复制与内部纯文本粘贴。 |
| `TXT-039` [#89](https://github.com/LorenHan/LqCompare/issues/89) | 待实现 | 已修订 | 已用内容锚点替代不稳定差异块序号恢复。 |
| `TXT-040` [#91](https://github.com/LorenHan/LqCompare/issues/91) | 待实现 | 符合 | 固定语料/纯引擎/offscreen符合；快照变化需人工解释而非盲目更新。 |

### MRG

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `MRG-001` [#92](https://github.com/LorenHan/LqCompare/issues/92) | 待实现 | 已修订 | 已限定输入只读，输出唯一内容写目标。 |
| `MRG-002` [#93](https://github.com/LorenHan/LqCompare/issues/93) | 待实现 | 符合 | Base只读和显隐不影响结果符合。 |
| `MRG-003` [#94](https://github.com/LorenHan/LqCompare/issues/94) | 待实现 | 需细化 | 保守三路归并符合；冲突粒度/相邻插入/删除修改需固定语料。 |
| `MRG-004` [#95](https://github.com/LorenHan/LqCompare/issues/95) | 待实现 | 需细化 | 始终按原文识别冲突是安全取舍，应明确区别BC忽略不重要冲突选项。 |
| `MRG-005` [#96](https://github.com/LorenHan/LqCompare/issues/96) | 待实现 | 符合 | 整块使用左/右且可撤销符合；空侧代表删除不能禁用。 |
| `MRG-006` [#98](https://github.com/LorenHan/LqCompare/issues/98) | 待实现 | 符合 | 先左后右为显式拼接符合；避免凭时间自动选顺序。 |
| `MRG-007` [#97](https://github.com/LorenHan/LqCompare/issues/97) | 待实现 | 符合 | 整文件替换强确认及一次撤销符合；计数口径需来自编辑日志。 |
| `MRG-008` [#99](https://github.com/LorenHan/LqCompare/issues/99) | 待实现 | 符合 | 人工块保护符合；再次自动合并列出覆盖范围。 |
| `MRG-009` [#100](https://github.com/LorenHan/LqCompare/issues/100) | 待实现 | 符合 | 工作流标记与内容分开符合；已解决不代表一定成功落盘。 |
| `MRG-010` [#101](https://github.com/LorenHan/LqCompare/issues/101) | 待实现 | 符合 | 未解决冲突导航符合；大于视口的冲突只能显示起点而非完整可见。 |
| `MRG-011` [#102](https://github.com/LorenHan/LqCompare/issues/102) | 待实现 | 符合 | 显示过滤不改变输出符合；上下文复用TXT过滤。 |
| `MRG-012` [#103](https://github.com/LorenHan/LqCompare/issues/103) | 待实现 | 符合 | 单侧相对Base查看合理；复用只读文本比较视图。 |
| `MRG-013` [#104](https://github.com/LorenHan/LqCompare/issues/104) | 待实现 | 已修订 | 已补未解决保存的候选内容保全及非成功返回码。 |
| `MRG-014` [#105](https://github.com/LorenHan/LqCompare/issues/105) | 待实现 | 符合 | 输出只读复验合理；来源统计需要MRG-015决策来源记录。 |
| `MRG-015` [#106](https://github.com/LorenHan/LqCompare/issues/106) | 待实现 | 符合 | 合并与编辑统一撤销栈符合；人工修改应保留来源历史。 |
| `MRG-016` [#107](https://github.com/LorenHan/LqCompare/issues/107) | 待实现 | 已修订 | 已明确Git命名绑定与原生位置顺序，成功必须已保存且无冲突。 |
| `MRG-017` [#108](https://github.com/LorenHan/LqCompare/issues/108) | 待实现 | 已修订 | 已限定编码/行尾统一仅影响输出，不重写输入。 |
| `MRG-018` [#109](https://github.com/LorenHan/LqCompare/issues/109) | 待实现 | 符合 | 纯引擎与真实Git集成测试符合；无Git仅跳集成子集。 |

### DIR

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `DIR-001` [#110](https://github.com/LorenHan/LqCompare/issues/110) | 待实现 | 符合 | 目录作为主视图及文件子视图符合；允许显式独立标签。 |
| `DIR-002` [#112](https://github.com/LorenHan/LqCompare/issues/112) | 待实现 | 符合 | 后台扫描/取消保留部分结果符合；部分结果须保持不完整标记。 |
| `DIR-003` [#111](https://github.com/LorenHan/LqCompare/issues/111) | 待实现 | 已修订 | 已消除不递归与第一层重复，并显示未扫描节点。 |
| `DIR-004` [#113](https://github.com/LorenHan/LqCompare/issues/113) | 待实现 | 已修订 | 已禁止大小快速差异短路规则比较。 |
| `DIR-005` [#114](https://github.com/LorenHan/LqCompare/issues/114) | 待实现 | 符合 | UTC/容差合理；时区/DST忽略必须是显式规则不能猜测时间。 |
| `DIR-006` [#115](https://github.com/LorenHan/LqCompare/issues/115) | 待实现 | 已修订 | 已分离文件系统身份与大小写配对，并报告一对多歧义。 |
| `DIR-007` [#117](https://github.com/LorenHan/LqCompare/issues/117) | 待实现 | 已修订 | 已补元数据缓存失效边界和强制重算。 |
| `DIR-008` [#116](https://github.com/LorenHan/LqCompare/issues/116) | 待实现 | 符合 | 前N字节仅能证不同比或部分相同，不能升级为全部相同。 |
| `DIR-009` [#118](https://github.com/LorenHan/LqCompare/issues/118) | 待实现 | 符合 | 复用格式/文本规则符合；与打开子视图结论交叉验证。 |
| `DIR-010` [#119](https://github.com/LorenHan/LqCompare/issues/119) | 待实现 | 已修订 | 已加入内容覆盖快速测试，去掉任意准则差异就短路。 |
| `DIR-011` [#120](https://github.com/LorenHan/LqCompare/issues/120) | 待实现 | 已修订 | 已拆主状态、时间关系、内容证据及完整性。 |
| `DIR-012` [#122](https://github.com/LorenHan/LqCompare/issues/122) | 待实现 | 符合 | 颜色加文字图标符合；换主题不需要重新扫描。 |
| `DIR-013` [#121](https://github.com/LorenHan/LqCompare/issues/121) | 待实现 | 符合 | 列配置按视图持久化合理；工作区布局与用户全局默认分开。 |
| `DIR-014` [#124](https://github.com/LorenHan/LqCompare/issues/124) | 待实现 | 符合 | 稳定自然序与缺失值规则符合；并行扫描结果要先建立稳定身份序。 |
| `DIR-015` [#126](https://github.com/LorenHan/LqCompare/issues/126) | 待实现 | 符合 | 展开/刷新位置保持合理；身份用路径与数据源标识。 |
| `DIR-016` [#123](https://github.com/LorenHan/LqCompare/issues/123) | 待实现 | 符合 | 大目录不自动全展开合理；阈值可配置且告知用户。 |
| `DIR-017` [#125](https://github.com/LorenHan/LqCompare/issues/125) | 待实现 | 已修订 | 已区分显示隐藏与扫描范围排除。 |
| `DIR-018` [#127](https://github.com/LorenHan/LqCompare/issues/127) | 待实现 | 符合 | 左右独立选择符合；命令明确活动源侧。 |
| `DIR-019` [#128](https://github.com/LorenHan/LqCompare/issues/128) | 待实现 | 已修订 | 已去重复复制策略并补备份、原子替换、预演时效。 |
| `DIR-020` [#131](https://github.com/LorenHan/LqCompare/issues/131) | 待实现 | 符合 | 跨系统元数据保留降级合理；复制内容失败与元数据失败分开报告。 |
| `DIR-021` [#129](https://github.com/LorenHan/LqCompare/issues/129) | 待实现 | 已修订 | 已按同卷/跨卷声明撤销能力。 |
| `DIR-022` [#130](https://github.com/LorenHan/LqCompare/issues/130) | 待实现 | 符合 | 回收站优先及根目录保护符合；执行时重新验证身份。 |
| `DIR-023` [#132](https://github.com/LorenHan/LqCompare/issues/132) | 待实现 | 已修订 | 已允许单侧重命名后重新配对，不隐改对侧。 |
| `DIR-024` [#133](https://github.com/LorenHan/LqCompare/issues/133) | 待实现 | 符合 | NewOnly符合；同时新建双侧的部分失败要可解释。 |
| `DIR-025` [#134](https://github.com/LorenHan/LqCompare/issues/134) | 待实现 | 已修订 | 已补扫描不完整/根不可达时禁止镜像删除。 |
| `DIR-026` [#136](https://github.com/LorenHan/LqCompare/issues/136) | 待实现 | 符合 | 属性左右对照符合；无权限应为不可读而非空。 |
| `DIR-027` [#137](https://github.com/LorenHan/LqCompare/issues/137) | 待实现 | 符合 | 操作日志与恢复能力符合；备份额度、生命周期及覆盖恢复要先落地。 |
| `DIR-028` [#135](https://github.com/LorenHan/LqCompare/issues/135) | 待实现 | 符合 | 统一平台定位/打开符合；虚拟源导出后才可系统打开。 |
| `DIR-029` [#138](https://github.com/LorenHan/LqCompare/issues/138) | 待实现 | 符合 | 返回位置与子视图修改刷新符合；优先验证此闭环。 |
| `DIR-030` [#139](https://github.com/LorenHan/LqCompare/issues/139) | 待实现 | 符合 | 参数列表与可替换进程服务符合；不用shell拼接处理路径。 |
| `DIR-031` [#142](https://github.com/LorenHan/LqCompare/issues/142) | 待实现 | 符合 | 模糊配对显式开启且报告歧义合理。 |
| `DIR-032` [#140](https://github.com/LorenHan/LqCompare/issues/140) | 待实现 | 符合 | 强制配对符合；补一对一约束及循环映射拒绝。 |
| `DIR-033` [#141](https://github.com/LorenHan/LqCompare/issues/141) | 待实现 | 已修订 | 已允许有预算的受控临时缓存，归档服务独立。 |
| `DIR-034` [#143](https://github.com/LorenHan/LqCompare/issues/143) | 待实现 | 已修订 | 已增加跳过缓存的强制内容复验。 |
| `DIR-035` [#144](https://github.com/LorenHan/LqCompare/issues/144) | 待实现 | 符合 | 报表记录比较准则符合；内容未知保留原因。 |
| `DIR-036` [#145](https://github.com/LorenHan/LqCompare/issues/145) | 待实现 | 已修订 | 已限定本期为OS挂载网络位置，原生远程连接后续。 |
| `DIR-037` [#146](https://github.com/LorenHan/LqCompare/issues/146) | 待实现 | 符合 | 默认不跟随链接合理；硬链接按卷+file id/inode识别。 |
| `DIR-038` [#147](https://github.com/LorenHan/LqCompare/issues/147) | 待实现 | 已修订 | 已更新测试断言到新准则/状态语义。 |

### SYNC

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `SYNC-001` [#148](https://github.com/LorenHan/LqCompare/issues/148) | 待实现 | 符合 | 三栏预演先行符合；执行入口须绑定具体计划版本。 |
| `SYNC-002` [#149](https://github.com/LorenHan/LqCompare/issues/149) | 待实现 | 已修订 | 已修两侧都不存在复制、默认取新和无基线冲突定义。 |
| `SYNC-003` [#150](https://github.com/LorenHan/LqCompare/issues/150) | 待实现 | 符合 | 单项否决合理；反转方向必须重新校验来源及规则。 |
| `SYNC-004` [#153](https://github.com/LorenHan/LqCompare/issues/153) | 待实现 | 已修订 | 已补预演后变化检查和逐项恢复能力，不宣称全部不可逆。 |
| `SYNC-005` [#151](https://github.com/LorenHan/LqCompare/issues/151) | 待实现 | 已修订 | 已区分回收站还原与显式永久删除不可恢复。 |
| `SYNC-006` [#152](https://github.com/LorenHan/LqCompare/issues/152) | 待实现 | 符合 | 基线驱动且无基线保守合理；与BC取新同步不同属安全增强。 |
| `SYNC-007` [#154](https://github.com/LorenHan/LqCompare/issues/154) | 待实现 | 符合 | 校验独立可配合理；复制后校验失败保留原源和备份。 |
| `SYNC-008` [#155](https://github.com/LorenHan/LqCompare/issues/155) | 待实现 | 已修订 | 已防报告写入目标污染后续同步。 |
| `SYNC-009` [#156](https://github.com/LorenHan/LqCompare/issues/156) | 待实现 | 符合 | 预设不含目录内容合理；静默执行须符合SCR-003显式授权。 |
| `SYNC-010` [#157](https://github.com/LorenHan/LqCompare/issues/157) | 待实现 | 已修订 | 已去可关闭的范围排除保护，显示隐藏与扫描排除分开。 |
| `SYNC-011` [#159](https://github.com/LorenHan/LqCompare/issues/159) | 待实现 | 已修订 | 已禁止失败/取消/未知范围写成已同步基线。 |
| `SYNC-012` [#357](https://github.com/LorenHan/LqCompare/issues/357) | 待实现 | 符合 | 临时目录与删除阈值测试合理；补计划过期/源根消失用例。 |

### FMG

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `FMG-001` [#158](https://github.com/LorenHan/LqCompare/issues/158) | 待实现 | 符合 | 计划结果四栏与输出目录明确符合；依赖目录/文本合并引擎。 |
| `FMG-002` [#160](https://github.com/LorenHan/LqCompare/issues/160) | 待实现 | 符合 | 决策与落盘分离符合；跳过与明确删除输出应区分。 |
| `FMG-003` [#356](https://github.com/LorenHan/LqCompare/issues/356) | 待实现 | 符合 | 父决策不覆盖人工子决策合理；部分选择要展示覆盖范围。 |
| `FMG-004` [#358](https://github.com/LorenHan/LqCompare/issues/358) | 待实现 | 符合 | 基线内容判冲突合理；删除/修改、文件/目录冲突需语料。 |
| `FMG-005` [#359](https://github.com/LorenHan/LqCompare/issues/359) | 待实现 | 符合 | 默认保留未涉及输出项合理；原地模式备份依FMG-009。 |
| `FMG-006` [#161](https://github.com/LorenHan/LqCompare/issues/161) | 待实现 | 符合 | 预演同源及外部变化失效符合。 |
| `FMG-007` [#163](https://github.com/LorenHan/LqCompare/issues/163) | 待实现 | 符合 | 批量范围与人工决策保护符合。 |
| `FMG-008` [#162](https://github.com/LorenHan/LqCompare/issues/162) | 待实现 | 符合 | 决策与执行分开报告合理；不把预演当执行成功。 |
| `FMG-009` [#164](https://github.com/LorenHan/LqCompare/issues/164) | 待实现 | 已修订 | 已改覆盖默认备份，失败停止，显式放弃才不可撤销。 |
| `FMG-010` [#166](https://github.com/LorenHan/LqCompare/issues/166) | 待实现 | 符合 | 临时三路数据语料符合；应覆盖文件/目录互换与部分执行恢复。 |

### HEX

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `HEX-001` [#165](https://github.com/LorenHan/LqCompare/issues/165) | 待实现 | 已修订 | 已对齐绝对偏移默认与虚拟填充模式。 |
| `HEX-002` [#167](https://github.com/LorenHan/LqCompare/issues/167) | 待实现 | 符合 | 偏移跳转合理；VA映射依HEX-006解析，未解析时禁用。 |
| `HEX-003` [#168](https://github.com/LorenHan/LqCompare/issues/168) | 待实现 | 符合 | 字节块与计数合理；缺失字节的总数口径需固定。 |
| `HEX-004` [#171](https://github.com/LorenHan/LqCompare/issues/171) | 待实现 | 符合 | 编辑与原子保存符合；长度变化需明确告知。 |
| `HEX-005` [#170](https://github.com/LorenHan/LqCompare/issues/170) | 待实现 | 符合 | 字节通配查找合理；解析错误应指出具体token。 |
| `HEX-006` [#169](https://github.com/LorenHan/LqCompare/issues/169) | 待实现 | 后续增强 | 结构解释器属于额外开发范围，先完成字节比较再做五格式解析。 |
| `HEX-007` [#172](https://github.com/LorenHan/LqCompare/issues/172) | 待实现 | 符合 | 搬运进撤销且只读拦截符合。 |
| `HEX-008` [#173](https://github.com/LorenHan/LqCompare/issues/173) | 待实现 | 符合 | 绝对偏移保守默认合理；结构对齐依解释器且需唯一字段对应。 |
| `HEX-009` [#174](https://github.com/LorenHan/LqCompare/issues/174) | 待实现 | 符合 | 着色不覆盖差异合理；ASCII高位字节不要猜测编码。 |
| `HEX-010` [#175](https://github.com/LorenHan/LqCompare/issues/175) | 待实现 | 符合 | 边界语料与字节保存测试符合。 |

### DATA

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `DATA-001` [#178](https://github.com/LorenHan/LqCompare/issues/178) | 待实现 | 需细化 | CSV/TSV先行；XLS/XLSX/HTML依读入库许可、公式/缓存值口径。 |
| `DATA-002` [#176](https://github.com/LorenHan/LqCompare/issues/176) | 待实现 | 需细化 | 按键/内容对齐合理；重复键必须定义保序、歧义提示或组合键策略。 |
| `DATA-003` [#177](https://github.com/LorenHan/LqCompare/issues/177) | 待实现 | 已修订 | 已改一对一并允许显式序号映射，避免无表头不可用。 |
| `DATA-004` [#179](https://github.com/LorenHan/LqCompare/issues/179) | 待实现 | 已修订 | 已消除重复主状态的双重计数。 |
| `DATA-005` [#180](https://github.com/LorenHan/LqCompare/issues/180) | 待实现 | 符合 | 类型容差合理；NaN、空值、无效日期及区域格式需明确。 |
| `DATA-006` [#183](https://github.com/LorenHan/LqCompare/issues/183) | 待实现 | 需细化 | 左右独立解析合理；正则分隔与引号语法冲突须专门定义。 |
| `DATA-007` [#181](https://github.com/LorenHan/LqCompare/issues/181) | 待实现 | 符合 | 多表按名/位置配对符合；晚于单表解析闭环。 |
| `DATA-008` [#182](https://github.com/LorenHan/LqCompare/issues/182) | 待实现 | 符合 | 网格虚拟化与冻结合理；具体大表预算依PLAT-009。 |
| `DATA-009` [#184](https://github.com/LorenHan/LqCompare/issues/184) | 待实现 | 后续增强 | TSV复制/差异导出合理；XLSX导出属于后续写入能力。 |
| `DATA-010` [#185](https://github.com/LorenHan/LqCompare/issues/185) | 待实现 | 已修订 | 已限定Excel原地保存保真条件，默认只读/另存CSV。 |
| `DATA-011` [#186](https://github.com/LorenHan/LqCompare/issues/186) | 待实现 | 符合 | 并排与列映射记录符合；复用RPT引擎。 |
| `DATA-012` [#187](https://github.com/LorenHan/LqCompare/issues/187) | 待实现 | 符合 | 脏CSV/重复键/类型边界语料符合。 |

### IMG

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `IMG-001` [#188](https://github.com/LorenHan/LqCompare/issues/188) | 待实现 | 需细化 | 支持解码与尺寸差异合理；8000方图须给32位峰值预算和色彩空间口径。 |
| `IMG-002` [#189](https://github.com/LorenHan/LqCompare/issues/189) | 待实现 | 符合 | 互补可视模式合理；闪烁有暂停入口。 |
| `IMG-003` [#190](https://github.com/LorenHan/LqCompare/issues/190) | 待实现 | 需细化 | 容差常显合理；需明确ICC归一、预乘alpha和透明RGB比较口径。 |
| `IMG-004` [#191](https://github.com/LorenHan/LqCompare/issues/191) | 待实现 | 符合 | 联动视角合理；固定偏移只改对齐不能伪装原图像素。 |
| `IMG-005` [#192](https://github.com/LorenHan/LqCompare/issues/192) | 待实现 | 需细化 | 连通域导航合理；4邻/8邻及碎点聚合上限须固定。 |
| `IMG-006` [#193](https://github.com/LorenHan/LqCompare/issues/193) | 待实现 | 符合 | 元数据与像素结论分离符合。 |
| `IMG-007` [#194](https://github.com/LorenHan/LqCompare/issues/194) | 待实现 | 已修订 | 已限定有损/未知元数据保存保证，编辑列后续增强。 |
| `IMG-008` [#195](https://github.com/LorenHan/LqCompare/issues/195) | 待实现 | 后续增强 | 多帧属后续增强；先明确按帧序号还是时间轴对齐。 |
| `IMG-009` [#196](https://github.com/LorenHan/LqCompare/issues/196) | 待实现 | 符合 | 包含差异图证据符合；HTML资源内嵌同RPT。 |
| `IMG-010` [#197](https://github.com/LorenHan/LqCompare/issues/197) | 待实现 | 符合 | 程序生成边界图合理；性能预算与IMG-001统一。 |

### MED

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `MED-001` [#198](https://github.com/LorenHan/LqCompare/issues/198) | 待实现 | 符合 | 标签与内容相等分离符合；扩展格式依解码库能力显式展示。 |
| `MED-002` [#202](https://github.com/LorenHan/LqCompare/issues/202) | 待实现 | 符合 | 写标签备份合理；具体格式只读/可写能力矩阵需先确定。 |
| `MED-003` [#199](https://github.com/LorenHan/LqCompare/issues/199) | 待实现 | 符合 | 封面/歌词/章节对照合理；缺失字段不是空字符串。 |
| `MED-004` [#201](https://github.com/LorenHan/LqCompare/issues/201) | 待实现 | 后续增强 | 波形/播放为后续辅助，不计入媒体相等结论。 |
| `MED-005` [#200](https://github.com/LorenHan/LqCompare/issues/200) | 待实现 | 符合 | 批量写标签先预演逐个备份合理；复用操作计划。 |
| `MED-006` [#203](https://github.com/LorenHan/LqCompare/issues/203) | 待实现 | 符合 | 无音频设备可测标签解析符合。 |

### REG

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `REG-001` [#204](https://github.com/LorenHan/LqCompare/issues/204) | 待实现 | 符合 | Windows入口与导出文件树合理；加载只读不应先要求写权限。 |
| `REG-002` [#205](https://github.com/LorenHan/LqCompare/issues/205) | 待实现 | 符合 | 类型与内容都参与比较符合；不可读状态补充REG-005。 |
| `REG-003` [#206](https://github.com/LorenHan/LqCompare/issues/206) | 待实现 | 符合 | HKCU默认和写前备份符合；创建新键的回滚需记录不存在状态。 |
| `REG-004` [#208](https://github.com/LorenHan/LqCompare/issues/208) | 待实现 | 已修订 | 已明确修复片段方向和删除预演。 |
| `REG-005` [#209](https://github.com/LorenHan/LqCompare/issues/209) | 待实现 | 已修订 | 已取消管理员可读全部键的不可能承诺。 |
| `REG-006` [#207](https://github.com/LorenHan/LqCompare/issues/207) | 待实现 | 符合 | 真实HKCU测试限Windows合理；纯.reg解析应尽量跨平台测。 |

### VER

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `VER-001` [#210](https://github.com/LorenHan/LqCompare/issues/210) | 待实现 | 已修订 | 已统一Windows入口与跨平台解析测试。 |
| `VER-002` [#211](https://github.com/LorenHan/LqCompare/issues/211) | 待实现 | 已修订 | 已改为PE实际可获取字段，取消版本/完整签名假承诺。 |
| `VER-003` [#212](https://github.com/LorenHan/LqCompare/issues/212) | 待实现 | 符合 | 版本号语义合理；语义版本预发布规则与任意版本字符串区分。 |
| `VER-004` [#213](https://github.com/LorenHan/LqCompare/issues/213) | 待实现 | 符合 | 批量回退检测符合运维场景；XLSX导出后置。 |
| `VER-005` [#214](https://github.com/LorenHan/LqCompare/issues/214) | 待实现 | 符合 | 最小PE固定语料与跨平台解析符合。 |

### ARC

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `ARC-001` [#216](https://github.com/LorenHan/LqCompare/issues/216) | 待实现 | 符合 | 按需读取符合；固实压缩和多层嵌套需受预算约束。 |
| `ARC-002` [#215](https://github.com/LorenHan/LqCompare/issues/215) | 待实现 | 已修订 | 已允许显式有界私有临时缓存，保证关闭/异常清理。 |
| `ARC-003` [#217](https://github.com/LorenHan/LqCompare/issues/217) | 待实现 | 符合 | 目录服务复用符合；归档读写能力按格式分别声明。 |
| `ARC-004` [#218](https://github.com/LorenHan/LqCompare/issues/218) | 待实现 | 已修订 | 已避免固定.bak覆盖旧备份；不支持写的格式应只读。 |
| `ARC-005` [#219](https://github.com/LorenHan/LqCompare/issues/219) | 待实现 | 已修订 | 已修压缩级别不可推断和CRC疑似重复；分析属后续增强。 |
| `ARC-006` [#220](https://github.com/LorenHan/LqCompare/issues/220) | 待实现 | 已修订 | 已增压缩炸弹资源预算，路径校验要执行时阻止符号链接替换。 |
| `ARC-007` [#221](https://github.com/LorenHan/LqCompare/issues/221) | 待实现 | 已修订 | 已统一CLI退出码。 |
| `ARC-008` [#223](https://github.com/LorenHan/LqCompare/issues/223) | 待实现 | 符合 | 安全语料符合；增加大小谎报、路径碰撞与压缩炸弹。 |

### EDV

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `EDV-001` [#222](https://github.com/LorenHan/LqCompare/issues/222) | 待实现 | 符合 | 复用文本编辑器符合；打开单个二进制应安全路由。 |
| `EDV-002` [#224](https://github.com/LorenHan/LqCompare/issues/224) | 待实现 | 符合 | 补丁查看先行符合；解析服务归PAT-003。 |
| `EDV-003` [#225](https://github.com/LorenHan/LqCompare/issues/225) | 待实现 | 已修订 | 已明确仅承担PAT-002的视图/确认，不重复引擎。 |
| `EDV-004` [#226](https://github.com/LorenHan/LqCompare/issues/226) | 待实现 | 已修订 | 已明确生成UI与PAT服务的职责。 |
| `EDV-005` [#227](https://github.com/LorenHan/LqCompare/issues/227) | 待实现 | 符合 | 编辑UI与补丁往返测试可分；不用重复实现同一测试引擎。 |

### FILT

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `FILT-001` [#228](https://github.com/LorenHan/LqCompare/issues/228) | 部分完成 | 符合 | 统一掩码与排除优先符合；扫描目录的**跨层语义需固定。 |
| `FILT-002` [#229](https://github.com/LorenHan/LqCompare/issues/229) | 部分完成 | 需细化 | 超时返回不等于停止匹配；需验证遗弃线程/资源上限和真实恶意正则。 |
| `FILT-003` [#230](https://github.com/LorenHan/LqCompare/issues/230) | 部分完成 | 符合 | 名称与属性交集合理；不可读元数据须不确定，不能静默消失。 |
| `FILT-004` [#231](https://github.com/LorenHan/LqCompare/issues/231) | 待实现 | 符合 | 显式内容过滤合理；排除整行不得改变保存模型。 |
| `FILT-005` [#233](https://github.com/LorenHan/LqCompare/issues/233) | 部分完成 | 符合 | 跨层交集已由实现明确；临时视图过滤不能变成删除范围权限。 |
| `FILT-006` [#232](https://github.com/LorenHan/LqCompare/issues/232) | 待实现 | 符合 | 显示隐藏显式选择符合；范围排除项不因该选项重新纳入。 |
| `FILT-007` [#234](https://github.com/LorenHan/LqCompare/issues/234) | 待实现 | 符合 | 预设数据化与用户覆盖合理。 |
| `FILT-008` [#235](https://github.com/LorenHan/LqCompare/issues/235) | 待实现 | 符合 | 临时过滤生命周期符合；切子视图保留、关标签丢弃。 |
| `FILT-009` [#236](https://github.com/LorenHan/LqCompare/issues/236) | 待实现 | 符合 | 提前剪枝合理；仅能在确认子树不会含强制包含命中时剪枝。 |
| `FILT-010` [#237](https://github.com/LorenHan/LqCompare/issues/237) | 待实现 | 已修订 | 已解决白名单强制绕过排除优先的矛盾。 |
| `FILT-011` [#238](https://github.com/LorenHan/LqCompare/issues/238) | 待实现 | 符合 | 帮助示例与测试同源符合。 |
| `FILT-012` [#239](https://github.com/LorenHan/LqCompare/issues/239) | 待实现 | 符合 | 固定语料合理；性能更快不可对每种输入硬断言，使用稳定扫描量证据。 |

### FMT

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `FMT-001` [#240](https://github.com/LorenHan/LqCompare/issues/240) | 待实现 | 符合 | 格式模型与用户覆盖符合；先把常见文本/二进制路由接上。 |
| `FMT-002` [#360](https://github.com/LorenHan/LqCompare/issues/360) | 待实现 | 符合 | 优先级与覆盖副本合理；内置不可删不等于用户覆盖不可编辑。 |
| `FMT-003` [#361](https://github.com/LorenHan/LqCompare/issues/361) | 待实现 | 符合 | 词法共享符合；嵌套注释和字符串状态需跨行正确。 |
| `FMT-004` [#363](https://github.com/LorenHan/LqCompare/issues/363) | 待实现 | 已修订 | 已分开比较规范化/不可逆转换/反向保存，避免假保存丢编辑。 |
| `FMT-005` [#362](https://github.com/LorenHan/LqCompare/issues/362) | 待实现 | 符合 | 列定义按名/序号均合理；与DATA-003已统一。 |
| `FMT-006` [#241](https://github.com/LorenHan/LqCompare/issues/241) | 待实现 | 符合 | 图片默认设置只影响新会话合理。 |
| `FMT-007` [#242](https://github.com/LorenHan/LqCompare/issues/242) | 待实现 | 符合 | 归档嗅探与扩展冲突解释合理；加载器要验证内容而非信任扩展。 |
| `FMT-008` [#244](https://github.com/LorenHan/LqCompare/issues/244) | 待实现 | 符合 | 会话关联覆盖合理；不得修改全局优先级。 |
| `FMT-009` [#243](https://github.com/LorenHan/LqCompare/issues/243) | 待实现 | 符合 | 兜底可选合理；检测NUL需先识别UTF-16 BOM避免误判二进制。 |
| `FMT-010` [#245](https://github.com/LorenHan/LqCompare/issues/245) | 待实现 | 已修订 | 已允许有意重叠，改测意外遮蔽/不可达。 |
| `FMT-011` [#246](https://github.com/LorenHan/LqCompare/issues/246) | 待实现 | 符合 | 导入预览与冲突合并符合；非法导入应整体不生效并明确原因。 |
| `FMT-012` [#247](https://github.com/LorenHan/LqCompare/issues/247) | 待实现 | 符合 | 优先级、继承和损坏恢复测试符合。 |

### RPT

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `RPT-001` [#248](https://github.com/LorenHan/LqCompare/issues/248) | 待实现 | 符合 | 模型驱动报表符合；四布局按会话能力选择而非伪造无意义数据。 |
| `RPT-002` [#249](https://github.com/LorenHan/LqCompare/issues/249) | 待实现 | 符合 | 文件原子输出和剪贴板限额符合。 |
| `RPT-003` [#250](https://github.com/LorenHan/LqCompare/issues/250) | 待实现 | 已修订 | 已统一HTML默认内嵌CSS离线契约。 |
| `RPT-004` [#251](https://github.com/LorenHan/LqCompare/issues/251) | 待实现 | 符合 | 打印分页合理；系统PDF与内置PDF分别说明可用性。 |
| `RPT-005` [#252](https://github.com/LorenHan/LqCompare/issues/252) | 待实现 | 符合 | 统计口径一致符合；过滤口径和总数必须并列说明。 |
| `RPT-006` [#253](https://github.com/LorenHan/LqCompare/issues/253) | 待实现 | 已修订 | 已加入结果不完整/错误，避免假无差异结论。 |
| `RPT-007` [#254](https://github.com/LorenHan/LqCompare/issues/254) | 待实现 | 已修订 | 已区分真实资源引用与文本URL并要求转义，10万行浏览属后续增强。 |
| `RPT-008` [#257](https://github.com/LorenHan/LqCompare/issues/257) | 待实现 | 符合 | 按格式/平台默认编码合理；ANSI不可表示字符需提示不丢字。 |
| `RPT-009` [#255](https://github.com/LorenHan/LqCompare/issues/255) | 待实现 | 需细化 | 流式输出合理；50万条60秒须固定硬件和典型路径长度。 |
| `RPT-010` [#256](https://github.com/LorenHan/LqCompare/issues/256) | 待实现 | 已修订 | 已统一CLI退出码并保证输出失败非成功。 |
| `RPT-011` [#258](https://github.com/LorenHan/LqCompare/issues/258) | 待实现 | 后续增强 | 自定义模板属后续增强；渲染失败默认应停止自动化而非假成功回退。 |
| `RPT-012` [#261](https://github.com/LorenHan/LqCompare/issues/261) | 待实现 | 符合 | 输出快照合理；应测试HTML注入字符和CSV公式前缀的明确导出策略。 |

### PAT

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `PAT-001` [#259](https://github.com/LorenHan/LqCompare/issues/259) | 待实现 | 已修订 | 已明确原始内容与显示忽略分开，局部补丁不能承诺完整还原。 |
| `PAT-002` [#260](https://github.com/LorenHan/LqCompare/issues/260) | 待实现 | 需细化 | 全量回滚需要事务日志和可恢复失败状态；磁盘故障时不能保证无条件成功回滚。 |
| `PAT-003` [#262](https://github.com/LorenHan/LqCompare/issues/262) | 待实现 | 符合 | 多格式兼容合理；不支持的二进制/重命名/权限段不得悄悄忽略。 |
| `PAT-004` [#263](https://github.com/LorenHan/LqCompare/issues/263) | 待实现 | 符合 | 仅补丁不代替VCS符合；更新索引留外部工具。 |
| `PAT-005` [#264](https://github.com/LorenHan/LqCompare/issues/264) | 待实现 | 符合 | 路径穿越/删除确认符合；要阻止符号链接跳出根目录。 |
| `PAT-006` [#265](https://github.com/LorenHan/LqCompare/issues/265) | 待实现 | 符合 | 真实工具往返符合；多文件新增/删除与回滚失败需覆盖。 |

### SNAP

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `SNAP-001` [#266](https://github.com/LorenHan/LqCompare/issues/266) | 待实现 | 已修订 | 已去全内容备份扩张，回到元数据/摘要快照。 |
| `SNAP-002` [#268](https://github.com/LorenHan/LqCompare/issues/268) | 待实现 | 已修订 | 已限制CRC快照只能摘要比较，不能逐字节/规则比较或恢复文件。 |
| `SNAP-003` [#267](https://github.com/LorenHan/LqCompare/issues/267) | 待实现 | 符合 | 时间变化报告合理；缺摘要时只能内容未知不能断言内容变化。 |
| `SNAP-004` [#269](https://github.com/LorenHan/LqCompare/issues/269) | 待实现 | 符合 | 快照库管理合理；删除快照与解绑同步基线需联动提示。 |
| `SNAP-005` [#270](https://github.com/LorenHan/LqCompare/issues/270) | 待实现 | 符合 | 绑定与降级合理；基线更新服务由SYNC-011统一实现。 |
| `SNAP-006` [#271](https://github.com/LorenHan/LqCompare/issues/271) | 待实现 | 符合 | 只读/损坏恢复测试符合。 |

### VCS

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `VCS-001` [#272](https://github.com/LorenHan/LqCompare/issues/272) | 待实现 | 已修订 | 已删除不准确的库许可推论，保留外部Git架构选择。 |
| `VCS-002` [#274](https://github.com/LorenHan/LqCompare/issues/274) | 待实现 | 符合 | HEAD比较符合；虚拟源只读且工作副本不被写入。 |
| `VCS-003` [#273](https://github.com/LorenHan/LqCompare/issues/273) | 待实现 | 符合 | 修订选择合理；参数安全、歧义引用与--路径分隔需验证。 |
| `VCS-004` [#276](https://github.com/LorenHan/LqCompare/issues/276) | 待实现 | 已修订 | 已区分直接头比较与共同祖先→目标分支，不再称直接差异为伪差异。 |
| `VCS-005` [#275](https://github.com/LorenHan/LqCompare/issues/275) | 待实现 | 符合 | 批量status符合；每文件索引/工作树双状态可同时存在。 |
| `VCS-006` [#277](https://github.com/LorenHan/LqCompare/issues/277) | 待实现 | 后续增强 | 只读历史有用途但非首期必需；优先外部工具入口。 |
| `VCS-007` [#278](https://github.com/LorenHan/LqCompare/issues/278) | 待实现 | 后续增强 | 图形列属于后续Git阅读增强，依VCS-006分页边界。 |
| `VCS-008` [#279](https://github.com/LorenHan/LqCompare/issues/279) | 待实现 | 后续增强 | 日志检索属于后续增强；全量精确计数与增量加载需权衡。 |
| `VCS-009` [#280](https://github.com/LorenHan/LqCompare/issues/280) | 待实现 | 符合 | 历史变更列表合理；合并提交需让用户选父提交。 |
| `VCS-010` [#281](https://github.com/LorenHan/LqCompare/issues/281) | 待实现 | 符合 | 虚拟历史对比符合；根提交相对空树，合并提交明确父提交。 |
| `VCS-011` [#282](https://github.com/LorenHan/LqCompare/issues/282) | 待实现 | 后续增强 | 完整修订图属于后续增强，不应阻挡比较核心。 |
| `VCS-012` [#283](https://github.com/LorenHan/LqCompare/issues/283) | 待实现 | 后续增强 | Blame属于后续增强；未提交行和重命名来源需明确。 |
| `VCS-013` [#284](https://github.com/LorenHan/LqCompare/issues/284) | 待实现 | 后续增强 | Blame着色属于后续增强；颜色不能代替作者/提交文字。 |
| `VCS-014` [#285](https://github.com/LorenHan/LqCompare/issues/285) | 待实现 | 已修订 | 已改提交前审阅和外部工具入口，移除内置commit与暂存。 |
| `VCS-015` [#286](https://github.com/LorenHan/LqCompare/issues/286) | 待实现 | 已修订 | 已去自动git add，merge只写MERGED并交调用者处理索引。 |
| `VCS-016` [#287](https://github.com/LorenHan/LqCompare/issues/287) | 待实现 | 符合 | 外部协作符合定位；Git配置片段优先于内置完整历史界面。 |
| `VCS-017` [#288](https://github.com/LorenHan/LqCompare/issues/288) | 待实现 | 符合 | 子模块/worktree归属合理；晚于单仓库只读比较。 |
| `VCS-018` [#289](https://github.com/LorenHan/LqCompare/issues/289) | 待实现 | 符合 | 可用性降级合理；关闭VCS后保留选项恢复入口。 |
| `VCS-019` [#291](https://github.com/LorenHan/LqCompare/issues/291) | 待实现 | 已修订 | 已限定缺Git只跳真实集成，不跳假后端与降级测试。 |

### CLI

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `CLI-001` [#290](https://github.com/LorenHan/LqCompare/issues/290) | 待实现 | 已修订 | 已统一左/右/base/output顺序并消歧POSIX绝对路径。 |
| `CLI-002` [#292](https://github.com/LorenHan/LqCompare/issues/292) | 待实现 | 已修订 | 已补全部注册类型，避免遗漏合并/编辑/补丁。 |
| `CLI-003` [#293](https://github.com/LorenHan/LqCompare/issues/293) | 待实现 | 符合 | 覆盖只作用本次运行符合；需要和SESS-016共享模型。 |
| `CLI-004` [#295](https://github.com/LorenHan/LqCompare/issues/295) | 待实现 | 已修订 | 已统一比较、输出错误和mergetool模式退出码。 |
| `CLI-005` [#294](https://github.com/LorenHan/LqCompare/issues/294) | 待实现 | 符合 | 只读服务端约束符合；输出目标不是读取侧权限的例外漏洞。 |
| `CLI-006` [#296](https://github.com/LorenHan/LqCompare/issues/296) | 待实现 | 符合 | 结构输出与日志分流合理；进度写stderr不可污染JSON。 |
| `CLI-007` [#297](https://github.com/LorenHan/LqCompare/issues/297) | 待实现 | 符合 | 转发/独立/等待合理；wait须返回任务结果而非仅收到参数。 |
| `CLI-008` [#299](https://github.com/LorenHan/LqCompare/issues/299) | 待实现 | 符合 | 无界面报告合理；不得依赖QWidget或显示服务器。 |
| `CLI-009` [#298](https://github.com/LorenHan/LqCompare/issues/298) | 待实现 | 符合 | 脚本相对路径基于脚本位置是有意设计；与调用工作目录分别记录。 |
| `CLI-010` [#300](https://github.com/LorenHan/LqCompare/issues/300) | 待实现 | 已修订 | 已去通配多项静默截前两个的危险行为。 |
| `CLI-011` [#301](https://github.com/LorenHan/LqCompare/issues/301) | 待实现 | 符合 | 帮助与参数表同源符合；只列已实现/明确标示计划参数。 |
| `CLI-012` [#303](https://github.com/LorenHan/LqCompare/issues/303) | 待实现 | 符合 | 真实进程契约测试符合；无DISPLAY与offscreen路径分别验证。 |

### SCR

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `SCR-001` [#302](https://github.com/LorenHan/LqCompare/issues/302) | 待实现 | 符合 | 静态检查与共享命令语义合理；BC兼容子集和自有语法要明确。 |
| `SCR-002` [#304](https://github.com/LorenHan/LqCompare/issues/304) | 待实现 | 符合 | load/compare/select闭环符合；数据源变更清掉过期选择。 |
| `SCR-003` [#305](https://github.com/LorenHan/LqCompare/issues/305) | 待实现 | 已修订 | 已补静默执行显式授权与同源预演计划。 |
| `SCR-004` [#306](https://github.com/LorenHan/LqCompare/issues/306) | 待实现 | 符合 | 变量转义和未定义检查合理；路径值不能重新解释成命令token。 |
| `SCR-005` [#309](https://github.com/LorenHan/LqCompare/issues/309) | 待实现 | 后续增强 | if/循环/goto是后续脚本语言扩展，BC兼容要求应单列而非冒充支持。 |
| `SCR-006` [#307](https://github.com/LorenHan/LqCompare/issues/307) | 待实现 | 符合 | 错误默认停止合理；continue后最终仍非零。 |
| `SCR-007` [#308](https://github.com/LorenHan/LqCompare/issues/308) | 待实现 | 符合 | 日志脱敏符合；不要记录输入文件原文。 |
| `SCR-008` [#310](https://github.com/LorenHan/LqCompare/issues/310) | 待实现 | 后续增强 | OS调度入口为后续辅助；进程ID复用不能误删有效锁。 |
| `SCR-009` [#311](https://github.com/LorenHan/LqCompare/issues/311) | 待实现 | 后续增强 | 录制/会话导脚本为后续便利能力；只录可重现操作。 |
| `SCR-010` [#312](https://github.com/LorenHan/LqCompare/issues/312) | 待实现 | 符合 | 语法/干跑/超时用例合理；实际文件写只在临时目录。 |

### OPT

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `OPT-001` [#313](https://github.com/LorenHan/LqCompare/issues/313) | 待实现 | 符合 | 设置仓库与搜索符合；逐分类切换提示可后续优化为统一草稿。 |
| `OPT-002` [#314](https://github.com/LorenHan/LqCompare/issues/314) | 待实现 | 已修订 | 已把关闭最后标签的空白窗口改为Home回落。 |
| `OPT-003` [#315](https://github.com/LorenHan/LqCompare/issues/315) | 待实现 | 已修订 | 已删除额外程序比较默认层，与SESS-007统一。 |
| `OPT-004` [#316](https://github.com/LorenHan/LqCompare/issues/316) | 待实现 | 符合 | UI字体与内容字体分离符合；统一UI-028主题存储。 |
| `OPT-005` [#317](https://github.com/LorenHan/LqCompare/issues/317) | 待实现 | 符合 | 安全默认值符合；批量预演不能因全局覆盖默认而被跳过。 |
| `OPT-006` [#318](https://github.com/LorenHan/LqCompare/issues/318) | 待实现 | 符合 | 目录显示默认合理；后台扫描开关不能阻塞GUI线程。 |
| `OPT-007` [#319](https://github.com/LorenHan/LqCompare/issues/319) | 待实现 | 符合 | Tab显示/编辑预期一致合理；纯显示设置不能标内容dirty。 |
| `OPT-008` [#320](https://github.com/LorenHan/LqCompare/issues/320) | 待实现 | 符合 | 报表默认入口符合；统一RPT预设存储。 |
| `OPT-009` [#364](https://github.com/LorenHan/LqCompare/issues/364) | 待实现 | 已修订 | 已明确远程后续范围与数据源服务依赖。 |
| `OPT-010` [#365](https://github.com/LorenHan/LqCompare/issues/365) | 待实现 | 符合 | 日志/诊断可操作合理；导出前路径脱敏可选择。 |
| `OPT-011` [#367](https://github.com/LorenHan/LqCompare/issues/367) | 待实现 | 符合 | 自定义命令与快捷键管理符合；不重复UI-027绑定存储。 |
| `OPT-012` [#366](https://github.com/LorenHan/LqCompare/issues/366) | 待实现 | 符合 | 便携/标准独立合理；只读安装目录要提示更换位置。 |
| `OPT-013` [#321](https://github.com/LorenHan/LqCompare/issues/321) | 待实现 | 符合 | 原子导入且默认不含隐私符合；凭据引用不能迁移成伪可用凭据。 |
| `OPT-014` [#323](https://github.com/LorenHan/LqCompare/issues/323) | 待实现 | 已修订 | 已将设置链测试统一四层。 |

### PLAT

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `PLAT-001` [#322](https://github.com/LorenHan/LqCompare/issues/322) | 待实现 | 符合 | 目标工具链明确；macOS构建成功不能算Windows交付通过。 |
| `PLAT-002` [#325](https://github.com/LorenHan/LqCompare/issues/325) | 部分完成 | 符合 | 平台抽象与假实现符合；目前Windows真实编译验收仍缺。 |
| `PLAT-003` [#324](https://github.com/LorenHan/LqCompare/issues/324) | 部分完成 | 符合 | 回收站不可用不静默永久删符合；平台不能自动撤销要给系统恢复入口。 |
| `PLAT-004` [#329](https://github.com/LorenHan/LqCompare/issues/329) | 部分完成 | 符合 | 图标异步缓存合理；应用程序/文件夹自定义图标按需例外缓存。 |
| `PLAT-005` [#326](https://github.com/LorenHan/LqCompare/issues/326) | 部分完成 | 符合 | Shell安装可逆合理；真实Windows验证和CLI接入完成才算完成。 |
| `PLAT-006` [#327](https://github.com/LorenHan/LqCompare/issues/327) | 待实现 | 符合 | 单实例合理；超时后防止请求已接收又新实例重复执行写操作。 |
| `PLAT-007` [#328](https://github.com/LorenHan/LqCompare/issues/328) | 部分完成 | 符合 | 无效UTF8与Unicode字节保真符合；平台支持差异实机验证。 |
| `PLAT-008` [#330](https://github.com/LorenHan/LqCompare/issues/330) | 部分完成 | 符合 | 错误分类/批量重试符合；预检不能代替实际I/O失败处理。 |
| `PLAT-009` [#331](https://github.com/LorenHan/LqCompare/issues/331) | 待实现 | 需细化 | 性能门槛需固定机型/样本/模式和容差，避免不稳定共享CI误报。 |
| `PLAT-010` [#332](https://github.com/LorenHan/LqCompare/issues/332) | 待实现 | 符合 | 跨平台矩阵符合；offscreen不能替代原生Shell/DPI/无障碍实测。 |

### ENG

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `ENG-001` [#333](https://github.com/LorenHan/LqCompare/issues/333) | 待实现 | 符合 | 分层自检符合；首次clone构建须写清LqRibbon外部路径依赖。 |
| `ENG-002` [#334](https://github.com/LorenHan/LqCompare/issues/334) | 待实现 | 符合 | 模块pri自足合理；避免测试隐式借主程序INCLUDEPATH。 |
| `ENG-003` [#335](https://github.com/LorenHan/LqCompare/issues/335) | 待实现 | 符合 | 独立QtTest与失败复现命令符合。 |
| `ENG-004` [#336](https://github.com/LorenHan/LqCompare/issues/336) | 待实现 | 符合 | 离线缓存策略合理；首次缓存准备和失效时失败原因需清楚。 |
| `ENG-005` [#337](https://github.com/LorenHan/LqCompare/issues/337) | 待实现 | 符合 | 格式检查和第三方排除符合；静态检测工具能力需真实匹配。 |
| `ENG-006` [#338](https://github.com/LorenHan/LqCompare/issues/338) | 部分完成 | 符合 | 懒日志和结构载体符合；界面输出面板接入后才完成三目标。 |
| `ENG-007` [#339](https://github.com/LorenHan/LqCompare/issues/339) | 待实现 | 需细化 | 致命信号处理需异步信号安全，UI导出只能下次启动展示。 |
| `ENG-008` [#341](https://github.com/LorenHan/LqCompare/issues/341) | 待实现 | 符合 | 翻译流水线合理；与UI-030白名单口径统一。 |
| `ENG-009` [#340](https://github.com/LorenHan/LqCompare/issues/340) | 待实现 | 符合 | 资源可解析/引用护栏符合；语义重复不能只靠文件名绝对判断。 |
| `ENG-010` [#342](https://github.com/LorenHan/LqCompare/issues/342) | 待实现 | 需细化 | 覆盖率阈值需给具体模块目标；安全分支不以整体数字替代。 |
| `ENG-011` [#343](https://github.com/LorenHan/LqCompare/issues/343) | 待实现 | 已修订 | 已删除违背动态Qt策略的静态单文件承诺。 |
| `ENG-012` [#344](https://github.com/LorenHan/LqCompare/issues/344) | 待实现 | 符合 | 构建标识与可关闭更新合理；网络失败不打断用户。 |
| `ENG-013` [#345](https://github.com/LorenHan/LqCompare/issues/345) | 待实现 | 符合 | 依赖清单与闭源许可政策符合；只核对政策一致性，不代替许可审查。 |
| `ENG-014` [#368](https://github.com/LorenHan/LqCompare/issues/368) | 待实现 | 已修订 | 已要求规格区更新保留实现证据并重验变更标准。 |
| `ENG-015` [#369](https://github.com/LorenHan/LqCompare/issues/369) | 待实现 | 已修订 | 已明确OPEN非终态与关闭终态，补重新打开状态同步。 |

### DOC

| ACTION-ID / Issue | 远端状态 | 设计判定 | 判定与依赖说明 |
| --- | --- | --- | --- |
| `DOC-001` [#346](https://github.com/LorenHan/LqCompare/issues/346) | 待实现 | 符合 | 文档随功能交付合理；不能用规划按钮截图冒充已实现。 |
| `DOC-002` [#347](https://github.com/LorenHan/LqCompare/issues/347) | 待实现 | 符合 | 命令表生成参考符合；示例必须可真实运行。 |
| `DOC-003` [#348](https://github.com/LorenHan/LqCompare/issues/348) | 待实现 | 符合 | 真实架构/交接符合；本次标出的旧事实需随实现同步。 |
| `DOC-004` [#349](https://github.com/LorenHan/LqCompare/issues/349) | 待实现 | 符合 | 快捷键/掩码速查同源符合；按会话能力过滤。 |
| `DOC-005` [#350](https://github.com/LorenHan/LqCompare/issues/350) | 待实现 | 符合 | ACTION-ID与issue链接变更日志符合。 |
| `DOC-006` [#351](https://github.com/LorenHan/LqCompare/issues/351) | 待实现 | 符合 | 对标裁决表必须区别官方事实、自有增强与延后范围。 |

## 规格修订清单与远端同步

规格审查阶段先修改规格数据、生成物及此报告；随后按用户授权完成远端同步（结果见文末）。每项变更的完整前后快照与原因位于 `.codex-work/audit-spec-changes.json`。同步保留旧实现记录；变更验收项重新置为待验证，没有沿用旧勾选。以下列出全部本次同步条目：

| ACTION-ID / Issue | 修订原因 |
| --- | --- |
| `UI-003` [#3](https://github.com/LorenHan/LqCompare/issues/3) | 默认QAT与用户自定义范围矛盾 |
| `UI-010` [#9](https://github.com/LorenHan/LqCompare/issues/9) | 只读不能禁用阅读导航 |
| `UI-017` [#17](https://github.com/LorenHan/LqCompare/issues/17) | 布局显隐与业务能力混淆 |
| `UI-026` [#26](https://github.com/LorenHan/LqCompare/issues/26) | 显示设置不能假装编辑，编码重载须防丢稿 |
| `UI-031` [#33](https://github.com/LorenHan/LqCompare/issues/33) | 相同文件对无法靠路径保证标题唯一 |
| `SESS-012` [#48](https://github.com/LorenHan/LqCompare/issues/48) | Unicode剪贴板不含源文件字节编码 |
| `SESS-015` [#50](https://github.com/LorenHan/LqCompare/issues/50) | 默认值位置应遵循便携模式 |
| `SESS-017` [#53](https://github.com/LorenHan/LqCompare/issues/53) | 只读保留阅读光标和导航 |
| `SESS-019` [#55](https://github.com/LorenHan/LqCompare/issues/55) | 基础状态与基线/时间派生状态不可混为互斥集合 |
| `TXT-003` [#58](https://github.com/LorenHan/LqCompare/issues/58) | 算法实现与选择UI重复归属 |
| `TXT-004` [#59](https://github.com/LorenHan/LqCompare/issues/59) | 查看策略不得丢失编辑历史 |
| `TXT-005` [#60](https://github.com/LorenHan/LqCompare/issues/60) | 全局差异块数单调不是算法不变量 |
| `TXT-008` [#63](https://github.com/LorenHan/LqCompare/issues/63) | 规则组合是顺序规范化而非各自判等的逻辑与 |
| `TXT-009` [#64](https://github.com/LorenHan/LqCompare/issues/64) | 互斥开关与全开语义自相矛盾 |
| `TXT-010` [#65](https://github.com/LorenHan/LqCompare/issues/65) | 官方BC默认忽略行尾风格，原规格相反 |
| `TXT-013` [#67](https://github.com/LorenHan/LqCompare/issues/67) | 重要性分类与行过滤混淆 |
| `TXT-014` [#68](https://github.com/LorenHan/LqCompare/issues/68) | 替换字符不能静默覆盖原始字节 |
| `TXT-015` [#70](https://github.com/LorenHan/LqCompare/issues/70) | 规则相同不等于字节相同 |
| `TXT-017` [#72](https://github.com/LorenHan/LqCompare/issues/72) | 跨会话记忆须尊重作用域 |
| `TXT-020` [#76](https://github.com/LorenHan/LqCompare/issues/76) | 布局改变不应强制取消逻辑行同步 |
| `TXT-028` [#354](https://github.com/LorenHan/LqCompare/issues/354) | 标题含替换但验收遗漏 |
| `TXT-038` [#90](https://github.com/LorenHan/LqCompare/issues/90) | 内部粘贴优先富文本与无格式粘贴相互矛盾 |
| `TXT-039` [#89](https://github.com/LorenHan/LqCompare/issues/89) | 差异块索引不稳定 |
| `MRG-001` [#92](https://github.com/LorenHan/LqCompare/issues/92) | 唯一写目标与输入可解锁自相矛盾 |
| `MRG-013` [#104](https://github.com/LorenHan/LqCompare/issues/104) | 未解决冲突不可保存成貌似成功的无标记结果 |
| `MRG-016` [#107](https://github.com/LorenHan/LqCompare/issues/107) | 成功保存/冲突/取消的集成退出码需单独定义 |
| `MRG-017` [#108](https://github.com/LorenHan/LqCompare/issues/108) | 统一格式不能误改合并输入 |
| `DIR-003` [#111](https://github.com/LorenHan/LqCompare/issues/111) | 只比较第一层与不递归定义重复 |
| `DIR-004` [#113](https://github.com/LorenHan/LqCompare/issues/113) | 大小短路破坏规则比较 |
| `DIR-006` [#115](https://github.com/LorenHan/LqCompare/issues/115) | 文件系统身份与比较配对规则混淆 |
| `DIR-007` [#117](https://github.com/LorenHan/LqCompare/issues/117) | 同大小同mtime缓存不保证内容未变 |
| `DIR-010` [#119](https://github.com/LorenHan/LqCompare/issues/119) | 任意快速差异短路与BC内容覆盖规则不符 |
| `DIR-011` [#120](https://github.com/LorenHan/LqCompare/issues/120) | 互斥集合混入重叠类别和无基线双侧改动 |
| `DIR-017` [#125](https://github.com/LorenHan/LqCompare/issues/125) | 显示过滤与扫描排除边界不同 |
| `DIR-019` [#128](https://github.com/LorenHan/LqCompare/issues/128) | 覆盖备份和预演失效检查缺失 |
| `DIR-021` [#129](https://github.com/LorenHan/LqCompare/issues/129) | 跨卷一律可撤销与DIR-027相反 |
| `DIR-023` [#132](https://github.com/LorenHan/LqCompare/issues/132) | 禁止名称失配与只改一侧验收冲突 |
| `DIR-025` [#134](https://github.com/LorenHan/LqCompare/issues/134) | 不完整扫描绝不能触发镜像删除 |
| `DIR-033` [#141](https://github.com/LorenHan/LqCompare/issues/141) | 禁止所有临时文件与32位大归档内存目标冲突 |
| `DIR-034` [#143](https://github.com/LorenHan/LqCompare/issues/143) | 元数据缓存存在同大小同时间失效边界 |
| `DIR-036` [#145](https://github.com/LorenHan/LqCompare/issues/145) | 原生WebDAV属于延后远程范围 |
| `DIR-038` [#147](https://github.com/LorenHan/LqCompare/issues/147) | 测试同步修复后的比较语义 |
| `SYNC-002` [#149](https://github.com/LorenHan/LqCompare/issues/149) | 无基线冲突不能仅看相同时间戳 |
| `SYNC-004` [#153](https://github.com/LorenHan/LqCompare/issues/153) | 预演到执行间文件变化未定义 |
| `SYNC-005` [#151](https://github.com/LorenHan/LqCompare/issues/151) | 永久删除不能承诺回收站还原 |
| `SYNC-008` [#155](https://github.com/LorenHan/LqCompare/issues/155) | 把同步日志写入目标会自污染 |
| `SYNC-010` [#157](https://github.com/LorenHan/LqCompare/issues/157) | 可关闭保护开关与硬性排除边界矛盾 |
| `SYNC-011` [#159](https://github.com/LorenHan/LqCompare/issues/159) | 部分失败不能污染下一次基线 |
| `FMG-009` [#164](https://github.com/LorenHan/LqCompare/issues/164) | 默认不备份与PRD覆盖前备份冲突 |
| `HEX-001` [#165](https://github.com/LorenHan/LqCompare/issues/165) | 默认自动插删对齐与HEX-008绝对偏移矛盾 |
| `DATA-003` [#177](https://github.com/LorenHan/LqCompare/issues/177) | 一对多缺值组合语义 |
| `DATA-004` [#179](https://github.com/LorenHan/LqCompare/issues/179) | 重叠行状态造成计数不一致 |
| `DATA-010` [#185](https://github.com/LorenHan/LqCompare/issues/185) | 通用XLSX写回可能静默丢公式和结构 |
| `IMG-007` [#194](https://github.com/LorenHan/LqCompare/issues/194) | 所有格式元数据无损保存不可保证 |
| `REG-004` [#208](https://github.com/LorenHan/LqCompare/issues/208) | 只含差异不足以定义修复方向与删除含义 |
| `REG-005` [#209](https://github.com/LorenHan/LqCompare/issues/209) | 管理员无法保证读取全部注册表键 |
| `VER-001` [#210](https://github.com/LorenHan/LqCompare/issues/210) | 平台支持声明与PRD、注册表实现不一致 |
| `VER-002` [#211](https://github.com/LorenHan/LqCompare/issues/211) | PE导入导出表不提供DLL版本和完整函数签名 |
| `ARC-002` [#215](https://github.com/LorenHan/LqCompare/issues/215) | 绝禁临时缓存与32位大文件读取不可兼得 |
| `ARC-004` [#218](https://github.com/LorenHan/LqCompare/issues/218) | 固定.bak会覆盖已有备份 |
| `ARC-005` [#219](https://github.com/LorenHan/LqCompare/issues/219) | CRC碰撞不能作为确定重复证明 |
| `ARC-006` [#220](https://github.com/LorenHan/LqCompare/issues/220) | 缺解压资源耗尽保护 |
| `ARC-007` [#221](https://github.com/LorenHan/LqCompare/issues/221) | 与CLI-004返回码契约冲突 |
| `EDV-003` [#225](https://github.com/LorenHan/LqCompare/issues/225) | EDV与PAT重复验收未区分视图/服务 |
| `EDV-004` [#226](https://github.com/LorenHan/LqCompare/issues/226) | EDV与PAT重复验收未区分视图/服务 |
| `FILT-010` [#237](https://github.com/LorenHan/LqCompare/issues/237) | 强制白名单越过排除优先，危及同步范围 |
| `FMT-004` [#363](https://github.com/LorenHan/LqCompare/issues/363) | 转换视图编辑无法无条件还原原内容，BC有反向保存转换 |
| `FMT-010` [#245](https://github.com/LorenHan/LqCompare/issues/245) | 禁止重叠与优先级/兜底模型矛盾 |
| `RPT-003` [#250](https://github.com/LorenHan/LqCompare/issues/250) | CSS可选外链与单文件强制冲突 |
| `RPT-006` [#253](https://github.com/LorenHan/LqCompare/issues/253) | 不完整结果不能二选一宣称无差异 |
| `RPT-007` [#254](https://github.com/LorenHan/LqCompare/issues/254) | 无http字面量误伤被比较内容，且缺HTML转义 |
| `RPT-010` [#256](https://github.com/LorenHan/LqCompare/issues/256) | 与CLI-004返回码契约冲突 |
| `PAT-001` [#259](https://github.com/LorenHan/LqCompare/issues/259) | 显示忽略规则不能破坏补丁往返契约 |
| `SNAP-001` [#266](https://github.com/LorenHan/LqCompare/issues/266) | 不是备份与可存内容作为备份矛盾 |
| `SNAP-002` [#268](https://github.com/LorenHan/LqCompare/issues/268) | CRC摘要不能支持全部内容比较方式 |
| `VCS-001` [#272](https://github.com/LorenHan/LqCompare/issues/272) | libgit2许可证理由不准确且无必要 |
| `VCS-004` [#276](https://github.com/LorenHan/LqCompare/issues/276) | 直接分支头比较不是伪差异，merge-base需要方向 |
| `VCS-014` [#285](https://github.com/LorenHan/LqCompare/issues/285) | 内置提交与PRD明确不做提交冲突 |
| `VCS-015` [#286](https://github.com/LorenHan/LqCompare/issues/286) | 自动标记解决实际上修改索引，与只比较/合并定位冲突 |
| `VCS-019` [#291](https://github.com/LorenHan/LqCompare/issues/291) | 整体skip会跳过无git降级用例 |
| `CLI-001` [#290](https://github.com/LorenHan/LqCompare/issues/290) | /开关可能误吞POSIX绝对路径 |
| `CLI-002` [#292](https://github.com/LorenHan/LqCompare/issues/292) | 全部类型清单遗漏文本合并/编辑/补丁 |
| `CLI-004` [#295](https://github.com/LorenHan/LqCompare/issues/295) | 输出错误/合并模式不可与比较模式混淆 |
| `CLI-010` [#300](https://github.com/LorenHan/LqCompare/issues/300) | 通配过多取前两项会比较错误对象 |
| `SCR-003` [#305](https://github.com/LorenHan/LqCompare/issues/305) | 交互强制预演与静默执行授权边界缺失 |
| `OPT-002` [#314](https://github.com/LorenHan/LqCompare/issues/314) | 保留空白窗口与SESS-003 Home回落冲突 |
| `OPT-003` [#315](https://github.com/LorenHan/LqCompare/issues/315) | 设置五层与已实现四层链冲突 |
| `OPT-009` [#364](https://github.com/LorenHan/LqCompare/issues/364) | 远程范围延后且缺数据源依赖 |
| `OPT-014` [#323](https://github.com/LorenHan/LqCompare/issues/323) | 设置五层与已实现四层链冲突 |
| `ENG-011` [#343](https://github.com/LorenHan/LqCompare/issues/343) | 静态Qt选项与动态链接政策冲突 |
| `ENG-014` [#368](https://github.com/LorenHan/LqCompare/issues/368) | 发布器全量重写会抹除既有验收记录 |
| `ENG-015` [#369](https://github.com/LorenHan/LqCompare/issues/369) | 开闭事实不能推导所有中间状态，OPEN+已完成应修复 |

状态处理（审查时建议）：#39（SESS-006）应在验收确认后关闭为 completed，或保持 OPEN 并改为待审核；不应仅为标签整齐而宣称框架已经接通所有产品入口。#36/#37现为待审核，需区别“框架验收通过”和“真实会话工厂全部可用”。其它12条部分完成与交接文档的缺口说明相符，未发现可据当前证据自动升级完成的理由。

## 本轮验证

- 全量解析全部规格和远端快照：369个唯一ACTION-ID，369个唯一映射，无缺漏；修订前每项验收文字与远端一致，进度备注单独识别。
- `python3 tools/publish_issues.py prd` 重新生成 `docs/PRD-actions.md`、`docs/github/issue-index.md`、`docs/github/prd-issues.json`，保留issue映射。
- `python3 tools/check_spec.py` 通过：369规格 / 27功能域 / P0 59。
- 本次未为文案修订新增无意义的代码测试；后续实现必须按新规格跑真实引擎、文件写入和端到端用例。本报告不替其它并行实现宣称通过。

## GitHub 同步结果（审查后续执行）

执行时间：2026-09-20 15:44 UTC（上海时间 23:44）。已通过新工具 [`tools/sync_issue_revision.py`](../../tools/sync_issue_revision.py) 将全部92个修订 issue 同步至 GitHub，成功92、失败0。其中仅 DIR-036 / #145 与 VCS-014 / #285 修改标题，其余更新规范正文。没有创建、删除、关闭 issue，没有发送评论。

先 dry-run 读取全部远端最新正文，确认与审查初始快照无新增正文/标题变化，再逐个保存 before/after 正文、JSON 与差异。以最多3并发执行，每次 PATCH 前重读并防止覆盖并发编辑，PATCH 只带 title/body。每条完成后再次 GET，验证92个 issue 的 labels、state、assignees均未变化。116项改写或新增验收标准保持未勾选；本次修订集合中原来没有已勾选验收，已有15个进度issue的正文不在本次改写范围。

#39（SESS-006）作为单独授权的状态修复，仅将“已完成”标签改为“待审核”，保留 OPEN、原正文、原标题、负责人和其它标签，不以框架完成替代产品入口验收。

最终重新拉取全部369个 issue 核对：92个修订正文和计划逐字一致，范围外277个正文/标题均未改变；再次运行纯合并渲染器，92个全部为无变化，证明可重入。工具内置8项保护测试覆盖保留勾选/行内证据、变更标准重置并存档、保留未知章节/代码围栏、非标准正文完整存档、ID不匹配拒绝和幂等。

证据：

- 初始计划、每项 before/after/diff、逐项回读结果：`.codex-work/issue-revision-sync-2026-09-20/`。
- 逐项成功/失败汇总：`.codex-work/issue-revision-sync-2026-09-20/result.json`。
- 最终全量核对：`.codex-work/issue-revision-sync-2026-09-20/final-verification.json`。
- #39 独立标签变更前后：`.codex-work/issue-revision-sync-2026-09-20/SESS-006-state/`。
- 最终369个 issue 完整快照：`.codex-work/issues-after-revision.json`。

工具默认 dry-run，必须显式 `--apply` 才按已保存计划执行；若最新远端正文/标题与计划不一致则拒绝覆盖，需重新 dry-run 合并最新证据。旧发布器仍负责首次创建，本工具只修订已有issue，二者不创建重复条目。
