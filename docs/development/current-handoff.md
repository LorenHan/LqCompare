# 当前进度与接手说明

> 更新时间：2026-09-21。**新开一个对话接手本项目时，先读这一页。**
> 详细的并行划分见 [parallel-workstreams.md](parallel-workstreams.md)。
>
> **2026-09-20 夜间的并行开发（24 个工作流）已整体落地**，每个工作流各有一份
> `docs/development/team-*.md` 交付记录，分工与验收原则见
> [night-progress.md](night-progress.md)。其中「报表与补丁」「Git 差异集成」
> 两条被额度中断，已由本文件更新当轮补完（见 §1 末尾与 §4）。

## 1. 一句话现状

规格（369 条）与 GitHub issue 已全部铺好；Qt 工程骨架已在 macOS 上编译通过、
主程序可启动、测试全绿。**服务层开始有真实功能**：文件系统抽象层（PLAT-002）、
回收站服务（PLAT-003）、名称处理（PLAT-007）、批量操作的失败处置（PLAT-008）、
系统图标服务（PLAT-004）与 Shell 集成（PLAT-005）已落地。其中回收站在本机是
**真的能删进废纸篓再还原回来**的；名称处理连「无效 UTF-8 的文件名」这种只在 Linux 上
出现的输入都写好了测试（CI 上会真实执行）；错误路径现在携带**原始系统错误码**
（errno / Win32 / Cocoa），批量操作会给出按原因分组的失败清单并支持只重试失败项；
系统图标是按**类型**缓存 + 后台解析 + 去重的，在本机能真的拿到 Finder 那一套图标；
Shell 集成的注册表计划、安装回滚、卸载还原与残留检查全部在**内存注册表**上真实执行，
因此这台 macOS 上跑的是完整流程，而不只是编译过；分级日志（ENG-006）现在
级别过滤对宏与直接调用一视同仁、输出行带线程 id、并支持挂任意接收者与 RAII 计时；
掩码语法与过滤声明（FILT-001）也落地了——21 条语法速查条目连同它们的样本
都是**可执行**的，因而「帮助里写的行为」和「程序的行为」不可能分家；
**会话这把钥匙（SESS-001）也插进去了**：会话抽象基类给出了统一契约
（视图 / 打开 / 关闭 / 重载 / 保存 / 脏标记 / 设置）与三个公共出口
（状态栏文本、错误上报、进度上报），并且「基类不依赖任何具体视图」是一条
**编译期**护栏——基类一旦 include 具体视图头文件，`Tests/Session` 会直接构建失败。
它是本仓库第一个链接 QtWidgets 的模块，也是第一个跑在 offscreen 上的测试套件。
会话的设置接口单独落在 `Services/Session/`：作用域链与落盘还没做，但接口先定下来了，
因此后续的过滤器与选项页不必各自发明一套「会话设置」。
**会话类型的登记处（SESS-002）也补上了**：14 种内置类型（ID / 显示名 / 英文原名 /
图标键 / 默认文件掩码 / 分组 / Pro 归属 / 平台限定）落成一张**纯数据**的表，注册表按
「注册顺序 = 优先级」回答「这个文件该用哪个视图打开」，并按分组枚举出 Home 页与新建
向导需要的入口。它的默认文件掩码**复用** FILT-001 那套掩码语言，而不是再写一份
「看扩展名」的匹配；「创建工厂」的返回类型只前向声明了 `CompareSession`，
因此这个模块是纯 QtCore 的——本仓库第一次有「服务层的会话框架测试」（`QT -= gui`）。
界面尚未接它：那批代码目前的生产调用方只有启动自检，把条目喂给 Home 页与新建向导
属于 SESS-003 / SESS-005。
**「一个会话类型有哪些设置项」这件事也定下来了（SESS-006）**：设置项的声明模型
（标题、说明、控件类型、默认值、校验规则）落在 `Services/Session/settingschema.{h,cpp}`，
连同**草稿**（读写、脏判定、全有或全无的应用、恢复默认）、**未保存改动的询问策略**
与**声明目录**一起，全是纯 QtCore 的——因此本仓库又多了第一个「服务层的会话设置测试」
（`Tests/Settings`，同样刻意不写 `QT += gui`）。对话框外壳
`Views/Session/settingsdialog.{h,cpp}` 只做三件事：把声明画成控件、把用户的操作译成对
草稿的读写、把草稿的结论译成按钮的可用状态与错误行。**框架里一项具体设置都没有**——
「文本比对该有哪些设置」是 TEXT-* / FOLD-* 的产品决定，有一条用例把「目录当前为空」
钉住，让下一个人看到它时必须做那次决定。
界面上仍是 169 个按钮里 31 条带处理器，其余点击后提示对应 ACTION-ID；
会话基类、类型注册表与设置对话框都还没有被容器拿去接起来（Rules 按钮尚未开这个对话框）。
**「一次改动生效到哪一层」也定下来了（SESS-007）**：三层作用域（视图 / 会话 / 类型）
由 `ScopedSessionSettings` 串成一条链，**读**按 视图 → 会话 → 类型 → 出厂默认 逐层解析，
**写**只落到下拉指定的那一层、绝不碰另外两层；「本次修改将保存到 X」、切换作用域的提示、
以及「关闭标签会丢弃哪些视图级设置」三处文案都是服务层的**可测数据**而不是对话框里的字符串。
它仍然是纯 QtCore 的（`Tests/SettingsScope` 同样刻意 `QT -= gui`），因此
「视图级改动不得污染会话默认值」这条边界是在**没有界面**的情况下被断言住的。
界面还没接：下拉目前只是「能选」，对话框也还没有作用域链的实例可用。
**过滤的三层也定下来了（FILT-005）**：文件格式定义内建过滤 → 会话设置过滤 →
视图临时过滤，**层与层取交集**（一个条目必须被每一条生效的层放行），层内仍是
FILT-001 的「排除优先」。一句话概括取舍：并集的意思是「任一层放行即可见」，
于是新增一层过滤会让结果**变多**——用户加一个「只看 `.cpp`」反而看到了原本被排除的
文件，而他只会认为过滤器坏了。这里还有一个**同形不同义的陷阱**：`SettingScope`
的三层是**覆盖**（`value()` 只返回胜出的那一层），`FilterLayer` 的三层是**叠加**
（全部生效），拿前者的读法取过滤声明会静默丢掉会话层的过滤——因此三层的声明一律
**按存储分别读**，`FilterLayerBinder::loadInto()` 是唯一实现。每一层的**落点**也是数据
（格式层只读、会话层随会话保存、**视图层仅当前视图**），写入按它路由，于是
「临时过滤不小心存成永久过滤」在结构上不可能发生。它同样是纯 QtCore 的
（`Tests/FilterStack` 刻意 `QT -= gui`），并且是本模块第一个带**表自检**的条目：
层级表是唯一的事实来源，`validateFilterLayerTable()` 查顺序与落点，
其中「视图层的落点是否仍然是仅当前视图」单独占一条——它错了没有任何运行期现象，
用户只会觉得「我删掉的临时过滤又回来了」。
（⚠️ 这条自检**没有生产调用点**，早先写的「启动时打印」已不成立，见 §2「主程序运行」
那一行的说明与 `architecture.md` §5 的决定。）
**单实例与进程间通信也落地了（PLAT-006，见 §1.16）**：第二次启动不再开第二个窗口，
而是把参数（连同**第二个实例的当前目录**——相对路径要按他的目录解析）通过本地套接字
转交给已在运行的那个实例，转交结果翻译成退出码（**刻意错开 CLI-004 已占用的 0~4**，
否则脚本会把「没转交成功」读成「比较过了且没有差异」）；首个实例不应答时按边界条款
**照常启动新实例**、绝不弹错误框；「要不要把窗口抢到前台」是一个三值策略。
它的规则（标识符推导、线协议的编解码与分帧、退出码表、开关表、置前判定）全在
纯 QtCore 的 `instanceprotocol` 里，动作只有一层；测试是本仓**第一个起真子进程**的套件。
**一条限制必须一起记住**：本机 `QSharedMemory` 建不起来，实际跑的是
「进程锁 + 本地套接字」这条回退路径，共享内存那条腿与它的遗留段回收分支在本机不可达。
界面还没接：设置页的过滤 Tab、Filters 页与「查看最终生效过滤」面板都还没有挂上去，
面板的内容目前是服务层的**数据 + 文本**。
**属性条件也定下来了（FILT-003）**：大小（1024 进制单位，含中文「字节」）、修改时间
（绝对区间与「最近 N 天」）、属性位（只读 / 隐藏 / 系统 / 归档）、所有者与组四类，
**与名称过滤取「与」**（取或会让「只看 10 MB 以上的文件」把名称过滤排除掉的文件放回来）。
它守住了三条不显眼但很容易踩坏的约定：**元数据读不到时放行并如实报「结论不确定」**
（`ConditionOutcome` 同时带 `accepted` 与 `evaluated` —— 把「不知道」当成「不满足」会让
条目凭空消失）；**条件写错永远不缩小结果集、但一定被报出来**（带行号/列号/建议）；
**「现在」由调用方传入**，模块自己不取当前时间，否则「最近 7 天」的用例只能靠跑得快
避免跨秒失败。它同样是纯 QtCore 的（`Tests/AttributeFilter` 刻意 `QT -= gui`），
并且**模块里没有任何读文件的代码**——这条约束由一条**读源码的护栏**钉住
（出现 `QFile`/`readAll`/`QTextStream`/`QDataStream` 即红），因为「以后有人顺手加一读」
正是这类约束的典型死法。它也有自己的条件表自检（同样**没有生产调用点**，见 §2）。
**名称过滤器也定下来了（FILT-002）**：三种模式（精确 / 通配 / 正则，**由每条表达式的
行首前缀 `=` / `*` / `~` 自己决定**，不是整个过滤器一个全局开关）、三种组合语义
（任一命中即保留 / 不含任何一条 / 必须全部命中）、**三态结论**（命中 / 不命中 / 不确定），
以及**默认 200ms/条的超时保护**。这里有一个 Qt 版本现实：`QRegularExpression` 直到
Qt 6.0 才有 `setMatchTimeout()`，本仓钉在 5.15，因此超时是自己做的——把匹配放到一个
工作线程上等截止时间，超时就把**整个线程丢弃换新的**（不用 `QThread::terminate()`，
它会把互斥量留在未定义状态），同时把结论记成「不确定 + 一条超时问题」；连续超时
达到阈值（默认 2）时断路器打开，后续条目直接按「不确定」放行、不再派活。
**超时与元数据缺失一律放行、但必须被报出来**——这和 FILT-003 的取值方式同源：
把「不知道」当成「不满足」，用户会看到文件凭空消失却查不到原因。另有两层更便宜的
护栏在它前面：`analyzeRegexPatternRisk()` 在解析时就静态认出 `(a+)+$` 这类嵌套量词并
给出警告（**只在确实会退化时报**——`(a{2})+` 与 `(a+){2}` 不报），避免把「可能很慢」
变成「一写就被拦」。它同样是纯 QtCore 的（`Tests/NameFilter` 刻意 `QT -= gui`），
并且**解析、界面的实时校验、复验共用同一个 `analyzeNameFilterLine()`**——
有读源码的护栏钉住这条，防止将来有人为了「界面快一点」再写第二份解析。
界面还没接：名称过滤框与错误就地标红要等设置页 `OPT-*`。
**内容过滤也定下来了（FILT-004）**，这是本模块里唯一**要读文件内容**的一段：
一条**行过滤器**（整行 / 通配 / 正则三种模式，同样由行首前缀自己决定；`LineFilterResult`
把留下的行与被丢掉的行连同行号一起交回，便于界面给「为什么这一行没了」一个答案），
一条**关键字节过滤器**（`\xHH` 与 `\n \r \t \0` 的词法转义、任一 / 全部两种组合方式、
结论是**三态**：接受 / 拒绝 / **不适用**——被指定按关键字节过滤而条目其实是文本时，
正确结论是「这条规则与它无关，放行并报出来」，判成「不含那个序列所以拒绝」会让一批文本
文件凭空消失）。它的代价与其余几段不同阶，因此**必须显式启用**：
`ContentFilterEnablement::active()` 是「启用**且**有规则」，与 `FilterLayerState::active()`
同形——「配过但关掉了」的过滤器必须是不生效的，而界面上「已配置未启用」与「未配置」
要是两句不同的话。两条轴的**先后顺序**也定死在数据里（先过滤行、再应用忽略规则），
`LineFilter::excludes()` 只接受**原始行**，因此「先过滤」在类型上就成立——反过来做会让
参与比较的行集随忽略规则变化。它同样是纯 QtCore 的（`Tests/ContentFilter` 刻意 `QT -= gui`），
并且**模块内不读任何文件**，这条由一条**成对**的读源码护栏钉住（护栏先去掉注释再判，
另有「植入一行 `QFile(...).readAll()` 后必须报错」的反向用例）。
界面还没接：状态栏那句「内容过滤已启用，比较速度会降低」、Filters 页的启用开关，
以及把行过滤接进比对引擎的那一步（`Services/Text` 侧）都还没有做，见 §1.15。
**行对齐引擎现在有两种算法了（TXT-003）**：Myers 之外补上 Patience——先按
「两侧都只出现一次的行」计数出候选，再用**严格递增最长子序列**挑锚点，
锚点之间递归，**没有唯一行的一段平滑回退到 Myers**（回退是常规路径而不是异常路径：
真实文件里唯一行常常只占少数，把「没有唯一行」当成「没有对齐」会让这些文件比 Myers
还差，整份显示成「全删 + 全插」）。两种算法共用同一个区间收集器，因此块边界、
预算与 `alignmentLimited` 都只有一个来源——「切换算法不改变视图契约」这句话
于是是结构上的事，不是约定。算法清单也落了地（`availableAlignments()` 只返回
已实现的算法，`validateAlignmentTable()` 报出「规格点名的算法被登记为未实现」），
**但界面上的算法下拉与它的持久化属 TXT-004，本轮没有做**，见 §1.26。

### 1.0.1 夜间并行落地的模块（2026-09-20/21）

一夜之间有 24 个工作流并行推进，各模块的**确切接口与边界**写在对应的
`docs/development/team-*.md` 里（那里也逐条列了「尚未覆盖的规格」）。
与本研究任务直接相关的两条是：

- **报表与补丁（`Services/Report/`、`Services/Patch/`）**：`report.h` 出
  HTML/TXT 两种格式 × 并排/交错/摘要/统计四种布局（离线单文件、全部非信任字段
  做 HTML 转义、内嵌 CSP、分块写入 + `QSaveFile` 原子保存）；`patch.h` 出
  unified 补丁的生成、解析与**只读预演**；`patchapply.h` 出**补丁应用**——
  预演→备份→暂存→提交→校验的事务，失败自动回滚且 `RecoveryRequired` 如实上报，
  注入器 `ApplicationFailureInjector` 是它的测试缝。边界是**一个已存在普通文件的替换**，
  创建 / 删除 / 多目标在写盘前就拒绝。
- **Git 差异集成（`Services/Vcs/`、`Views/Vcs/`）**：`vcsbackend.h` 是只读后端
  （工作副本 / HEAD / 任意修订 / 索引 / 冲突三阶段 / 日志 / 引用 / blame），
  `GitBackend` 通过用户安装的 Git 执行，安全独立参数 + 环境清洗 + 超时与输出上限；
  `vcsview.h` 有 HEAD / 索引 / 修订 / 日志四种模式；`blameview.h` 是逐行追溯视图
  （按作者 / 按日期龄 / 按提交块三种着色）。两侧都是 **`QTemporaryDir` 里的只读快照**，
  接比较会话时必须保留 `Comparison` 的值或它的 `lifetime`。

### 1.0.2 补完两条被中断的工作流（2026-09-21）

额度中断时这两条各停在一个「接线已写好、实现/验证没做完」的点上：

- **补丁应用**：`patch.pri` 已经声明了 `patchapply.cpp`，而该文件并不存在——
  因为 `services.pri` 会 include `patch.pri`，**整个应用工程当时是编译不过的**。
  本轮补出 `patchapply.cpp`（约 690 行）与 `Tests/PatchApply`（58 个用例函数 / QTest 合计 60），
  应用工程恢复可构建。
- **Blame 视图**：`blameview.cpp` 不只是没进构建，它本身是**编译不过的半成品**
  （文件写到某个 `}` 就结束，连 `Private` 的收尾与构造函数都没有）；`vcsview.pri`
  也没列它。本轮补齐尾部约 230 行并接进 `vcsview.pri`，`Tests/VcsBlameView`
  （23 个用例）跑绿。

两条的验证结论、变异命中情况与**未落地的规格条目**见
`team-report-patch.md` 与 `team-vcs.md` 的「续做」小节。

### 1.0.3 收尾一条「已实现但从未闭环」的条目（2026-09-21 03:3x）

夜间那批产出里，`PLAT-006`（单实例与进程间通信）的实现与 104 条测试都在，
但 **issue #327 一直是「待实现 / 0 个勾 / 0 条评论」**，而 §4 的推荐还在说「下一轮做它」。
这一轮没有写新的生产代码，做的是**验证并把闭环补上**，另外补了一条被漏掉的用例：

- 单套件 105 通过 → 全量 **3394 passed / 0 failed / 2 skipped（64 套件）**；
  五道护栏全过；`make -B -j8` 全量重编 0 条本仓 warning；离屏启动正常
  （新增的日志行见 §2「主程序运行」）。
- 12 处变异**检出 11 处**：唯一未检出的「不再回收遗留标识」受本机环境限制
  （`QSharedMemory` 建不起来，那条分支不可达），已在 §1.16 与 issue 里写明。
- **补的那条用例是实质产出**：变异 M9 证明 `splitFrame()` 里「声明长度 == 实际长度」
  这一比较被去掉后零条用例变红（原有两条都被「载荷必须读尽」兜住了）。
  新增 `decodeRejectsAFrameShorterThanItsDeclaredPayload()` 之后该变异被检出。
- 结论与接口细节见 §1.16；「本机跑的是回退路径」这条也记进了 §6 的坑表。

### 1.0.4 落定 OPT-010 日志与诊断选项（2026-09-21 06:3x）

夜间那批「把界面接通当一批来做」的产出里，`OPT-010` 是**唯一一个连服务层都没有的条目**
——全仓 grep `rotate` / `diagnos` / `performanceTiming` 零命中，issue #365 五条完成标准
全是「日志与诊断选项」这一页该有的东西。这一轮把它整条落到底：

- 新增 `Services/Log/logfiles.{h,cpp}`（滚动与清空）与 `Services/Log/diagnostics.{h,cpp}`（诊断包），
  两个模块都**纯 QtCore**，并沿用 OPT-005 的做法——把「策略」做成可自检的纯数据对象、
  把「判定」做成注入时间的纯函数。
- 新增 `Tests/LogDiagnostics`（**89 个用例函数**，九组 A~I）；`Tests/Logging` 34 → **43**、
  `Tests/Options` 46 → **50**、`Tests/OptionsDialog` 17 → **23**。
- 全量 **3596 passed / 0 failed / 2 skipped（66 套件）**；五道护栏全过；
  `make -B -j8` 全量重编 0 条本仓 warning；离屏启动正常。
- **17 处变异 17 处检出、0 处漏检**。
- 代码提交 `d084060`（提交号由本提交之后的纯文档提交回填，见 §3.1）。
- 过程中**发现并修掉一个真实的隐私 bug**：脱敏的边界判定原本要求「路径后面必须跟分隔符」，
  于是 `/Users/loren `（后面是空格）这种最常见的写法**不会**被替换掉——导出的诊断包里
  就带着用户的真实家目录。判据已反转（见 §6）。

### 1.0.5 纠正一条被误判为「未落地」的模块：FMT-001（2026-09-21 07:3x）

**本轮没有写新的生产代码。** 做的是一件更值钱的事：发现 `FMT-001`（文件格式定义模型与存储）
的实现**早就在磁盘上**，而 issue `#240` 与本文档都还写着「文件格式定义模块未落地，没有宿主」。

- `Code/Services/Format/formatdefinition.{h,cpp}`、`formatdetector.{h,cpp}` 与
  `Code/Tests/Format`（798 行、**80** 个用例）都在，`Services/Format/format.pri` 已由
  `services.pri` 的 `exists()` include 接进构建。
- 本文档此前**至少四处**声称它不存在，并被当作阻塞理由引用：§1.15 的 FILT-004 第 5 条、
  §4.0 的「内容过滤三件事」、§4.1 的 FILT-004 行与 FILT-005 行。这些说法**互相同源、
  一起错了**——一处写错，后面全在复制它。
- 交付记录在 `docs/development/team-format.md`（夜间那批工作流的产出，81 行）：
  它明确列出「未完成且不计为本轮实现」的是**格式管理器 UI、语法高亮引擎、格式转换执行、
  归档解压器、图片/表格语义执行、关联覆盖导入界面、应用级接线**——这些**都不是 FMT-001 的
  完成标准**，它们对应 `FMT-002`（#360）及其后的条目。**不要把「该模块后续条目没做」
  读成「该模块没做」。**

本轮为它补了**唯一真的缺的东西**（完成标准 4「定义有唯一稳定的 ID」此前零覆盖）：

- 新增 `duplicateIdsAreRejectedWithADiagnostic()`——同一文件里重复 ID 只保留**第一个**，
  后续跳过并报「格式 ID 重复」。
- 新增 `idFormatRulesRejectUnstableIdentifiers()`——ID 必须是稳定的小写短横线标识
  （大写、空格、空串、以 `-` 开头、含 `.` 一律拒绝）。
- 反向验证：**5 处变异 5 处检出、0 处漏检**（去掉重复检查、不再记录已见 ID、
  放宽 ID 格式、有错也不跳过、放宽版本校验）。
- 结论见 §1.19；验证数据见 §2。提交 `a1cd3ee`（提交号由紧随其后的纯文档提交回填）。

**本轮因此把四条 `FILT-*` / `FMT-*` 依赖链上的一句错话删掉了**：`FILT-004` 第 5 条
（「内容过滤条件可保存为文件格式定义的一部分」）此前被写成「要等文件格式定义」，
现在**宿主已经存在**，它缺的只是**把自己的两个声明键挂上去**这一步，与 `FMT-001` 无关。

### 1.0.6 修好一条从来没跑起来过的流水线：ENG-004（2026-09-21 07:4x）

**这条流水线自建立起就没有产出过任何关于「构建」与「测试」的信息。** `gh run list`
上最近 6 次运行全是 `failure`，而且都停在同一步：

```
Build and test (ubuntu-latest)  获取 LqRibbon 依赖  fatal: could not read Username
  for 'https://github.com': No such device or address
##[error]Process completed with exit code 128.
```

`gh run view` 把这件事说得更清楚：

```
Y Build and test (ubuntu-latest) in 27s
  Y 安装 Qt
  X 获取 LqRibbon 依赖
  - 构建主程序          <- 「-」= 根本没开始
  - 运行测试套件        <- 「-」= 根本没开始
Y Repository checks in 13s
```

也就是说：**五道静态护栏在跑（13 秒全绿），而编译与测试从来没跑过**。根因是
`LqRibbon` 在**私有**仓 `MyClass` 里，匿名 clone 必然以 128 退出，而这一步被写成了
硬失败。这一轮做了四件事：

1. **把「拿不到私有依赖」改成降级开关**，而不是整条流水线的死因：拿不到时跳过主程序
   构建与打包，测试照跑。
2. **降级范围用实测而不是推断**：66 个测试工程里真正依赖 LqRibbon 的只有
   `Tests/AppIntegration` 与 `Tests/CommandActions`（其余 64 个不依赖），判据是对每个
   `.pro` 跑一次 `LQCOMPARE_MYCLASS_ROOT=/nonexistent qmake` 数出来的。**上一轮写在
   workflow 注释里的「一个都不 include Views/，所以全部套件都不依赖 LqRibbon」是错的**，
   而且它的依据只是「没 grep 到」。
3. **补齐 ENG-004 的五条完成标准里能落地的部分**：四个阶段（静态检查 → 构建 → 测试 →
   打包）、三平台矩阵、失败时上传日志与 JUnit 用例清单、可执行产物上传、Qt 走缓存
   并有缓存失效/依赖缺失两种降级说明。
4. **修掉 `run-tests.sh` 上「写的时候以为能跨平台」的问题**：第一提交修了前四类
   （输出格式用了 Qt 私有的 `xml` 而不是 CI 能消费的 `junitxml`；Windows（Git Bash）上
   没有 `make`（MinGW 装的是 `mingw32-make`）；Windows 上可执行文件带 `.exe` 而
   `[[ -x foo ]]` 不会补后缀；可选依赖缺失时缺一个「显式排除且必须被看见」的机制）。
   **第一次真实 CI 跑完之后又发现并修掉三类**（见 §1.20 第五~七项）：`-o -,txt`
   在 Windows 上一行都不输出、崩溃的套件不写 `Totals:` 行导致合计与红套件数对不上、
   构建输出被丢进 `/dev/null` 让 17/26 个构建失败**一个字的原因都没有**。

**本轮验证数据**：全量 **3598 / 0 / 2（66 套件，2 分 59 秒）**（用的就是改过的 `run-tests.sh`）；
五道护栏全绿（winapi 源文件 332，本轮没变）；主程序增量构建 0 条本仓 warning、离屏启动正常；
**8 处变异 8 处检出、0 处漏检**。本轮**不碰任何 C++ 代码**，所以上面的功能数字与上一轮一致
是预期的，不是「没测」。

**本轮诚实记账**：三条腿的真实结果**已经拿到**（run `35546164218`）——流水线按设计跑完了
四个阶段并上传了 6 个 artifact，但三条腿都还是红的，红的是被测对象而不是流水线本身。
原因、已修的部分与下一轮要做的，见 §1.20 与 §4.0.1。
代码提交 `786cf46`（提交号由紧随其后的纯文档提交回填），推送见 §3.1。

## 1.1 已落地的服务层模块

| 模块 | 条目 | 状态 | 测试 |
| --- | --- | --- | --- |
| `Services/Command/` | UI-024 | 骨架 | `Tests/CommandRegistry`（14 用例） |
| `Services/Log/` | ENG-006 | **部分完成**（界面输出面板尚未接线） | `Tests/Logging`（43 个用例函数，本轮从 32 加到 43） |
| `Services/Log/`（滚动与诊断） | OPT-010 | **已完成**（五条标准全落；滚动 / 清空 / 诊断包三个服务层模块齐备，设置项登记在 `logging.*` 分类下，设置页有按钮与文案，界面走的是与测试同一条公开方法） | `Tests/LogDiagnostics`（89 个用例函数，**纯 QtCore**） |
| `Services/Filter/` | FILT-001 | **部分完成**（界面上的速查与实时预览尚未接线） | `Tests/Filter`（87 个用例函数）；`FILT-005` 另见下一行 |
| `Services/Filter/`（三层叠加与落点） | FILT-005 | **部分完成**（第 1、4、5 条的服务层已完整落地并有启动自检；第 2、3 条的服务层部分——启用状态与四态文案、面板数据与全文——也已落地，**缺的只是显示控件**，住所是设置页 OPT-*） | `Tests/FilterStack`（84 个用例函数，**纯 QtCore**） |
| `Services/Filter/`（属性条件） | FILT-003 | **部分完成**（第 1~4 条已落；第 5 条只落了能独立验证的那一半——「与内容比对解耦」由读源码的护栏钉住，「扫描阶段提前丢弃」要等 `Folder/` 扫描器） | `Tests/AttributeFilter`（89 个用例函数，**纯 QtCore**） |
| `Services/Filter/`（名称过滤） | FILT-002 | **部分完成**（第 1、2、4、5 条已落；第 3 条只落服务层一半——组合语义与文案都已落地并被测试，「界面明确显示当前语义」要等界面接入） | `Tests/NameFilter`（93 个用例函数，**纯 QtCore**） |
| `Services/Filter/`（内容过滤） | FILT-004 | **部分完成**（第 1、2 条已落；第 3 条落了一半——顺序定死成「先过滤行、再应用忽略规则」并有测试，但把行过滤接进比对引擎的那一步在 `Services/Text` 里，尚未接；第 4 条要状态栏、第 5 条要 Filters 页给它入口——**它的宿主「文件格式定义」已经落地了**，见 §1.19，此前记成「要文件格式定义」是错的） | `Tests/ContentFilter`（85 个用例函数，**纯 QtCore**） |
| `Services/Format/`（格式定义与识别） | FMT-001 | **已完成**（五条标准全落；模型、存储与识别都在，见 §1.19。**注意本文档此前误记为「模块未落地」——代码与 80 条用例一直都在 `Code/Services/Format/` 与 `Code/Tests/Format/`，交付记录在 `docs/development/team-format.md`**。未在 Windows 上构建过，但 FMT-001 的五条标准都不含平台相关行为） | `Tests/Format`（82 个用例函数 / QTest 合计 82，**纯 QtCore**） |
| `Services/Text/`（行对齐引擎） | TXT-002 / TXT-003 | **TXT-002 已完成；TXT-003 已完成**（Myers 与 Patience 两种算法 + 回退 + 可选算法表；界面上的算法下拉与持久化属 TXT-004，本轮**未做**，见 §1.26） | `Tests/Text`（25 个用例函数）+ `Tests/Alignment`（14 个用例函数，**纯 QtCore**） |
| `Services/Text/`（行内容规范化链） | TXT-008 | **已完成**（四条标准全落；链提到公开接口 `normalizedLine()`，土耳其语取舍与 simple folding 的两条边界都以注释 + 用例两面记录，见 §1.27。**注意本轮没有新建套件**：实现与测试都在既有的 `textdiff` / `Tests/Text` / `Tests/TextView` 里） | `Tests/Text`（31 个用例函数，本轮 25 → 31）+ `Tests/TextView`（19 个用例函数，本轮 18 → 19） |
| `Services/Text/`（空白模式表） | TXT-009 | **部分完成**（5 条里 4 条已落：两级语义的分水岭、Tab/空格混排语料、单一枚举 + 模式表、忽略行的可视标记；第 4 条后半句「标记可由 **View 页**开关关闭」**没有住所**——View 页属 `OPT-007` / `OPT-003`，尚未落地。见 §1.28） | `Tests/Text`（34 个用例函数，本轮 31 → 34）+ `Tests/TextView`（20 个用例函数，本轮 19 → 20） |
| `Services/Text/`（相似行对齐与阈值） | TXT-005 | **已完成**（四条标准全落；新模块 `linesimilarity.{h,cpp}`：分值 / 阈值判定 / 单调配对 / 两个工作量上限；引擎新增「一处改动」归并 `DifferenceRun` / `differenceRuns()`，状态栏、上一处/下一处、复制这一处、命令行摘要统一改用它。**顺带修掉一个连带缺陷**：三方合并按块粒度解读会让基线行从结果里消失，见 §1.29） | **新套件** `Tests/Similarity`（15 条用例函数，**纯 QtCore**）；另 `Tests/Text`、`Tests/TextView`、`Tests/Report`、`Tests/AppIntegration`、`Tests/Cli`、`Tests/Merge`、`Tests/MergeView` 均有断言改动 |
| `Views/Text/`（文本比对会话与双窗格视图） | TXT-001 | **已完成**（四条标准全落；会话与双窗格从会话框架落地起就是这套形状，issue 上却一直是「待实现」——本轮**核对并闭环**，产出几乎全在断言上，生产改动只有一处 `TextPane::lineNumbers()` 访问器。**注意本文档此前从头到尾一次都没提过 TXT-001**，这正是它长期停在「待实现」的原因之一，见 §1.30） | `Tests/TextView`（24 个用例函数，本轮 20 → 24） |
| `Services/Text/` + `Views/Text/`（行尾规则与状态栏严重度） | TXT-010 | **已完成**（四条标准全落。两条开关 `ignoreEol` / `ignoreFinalNewline` **早就在同一条键函数里**，缺的是「哪一条管哪件事」的组合断言；**本轮唯一新增的能力是第 4 条后半句那个「警告图标」**：会话多出一条独立的严重程度通道 `StatusSeverity`（文本与严重度**各自去重**）、容器转发并在切标签时重播、窗口在状态栏放一个永久控件 `statusWarningIcon`。另把 `Document::hasMixedEndings()` 做成谓词并与文案共用 `countEndings()`。**顺带删掉一处冗余路径**：图标原本有两条刷新路，导致两处变异互相遮蔽、双双漏检，见 §1.31） | `Tests/Text`（38 个用例函数，本轮 34 → 38）+ `Tests/TextView`（26，24 → 26）+ `Tests/Session`（52，50 → 52）+ `Tests/AppIntegration`（13，12 → 13） |
| `Services/Text/`（替换规则链） | TXT-012 | **部分完成**（第 1、3、4 条已勾；第 2 条只落了服务层那一半——四条规则都能单独开关，且「正则 / 说明」都是服务层的可测数据，**画到界面上的那一半**要等设置页 `OPT-*`，见 §1.32） | **新套件** `Tests/TextRules`（19 个用例函数，**纯 QtCore**） |
| `Services/Folder/`（二进制逐字节比对） | DIR-008 | **部分完成**（第 1、3、4、5 条已落；第 2 条落了「只比较前 N 字节」的引擎、设置键与视图往返，**缺的是画到界面上的那个输入框**——`FolderCompareView` 目前用与 `maximumDepth` 同样的办法原样保留该值而不显示它。见 §1.33） | `Tests/Folder`（45 个用例函数；DIR-008 那一轮 30 → 35，DIR-011 那一轮 35 → 45，链接 QtWidgets）+ `Tests/Report`（37，36 → 37） |
| `Services/Folder/`（条目状态模型与判据） | DIR-011 | **已完成**（五条标准全落；新模块 `entrystatus.{h,cpp}`：9 档主状态表 + 内容证据 / 时间关系两维 + 存在性派生视图 + 基线校验与「只在叶子上细化」+ 引擎与用例共用的父子汇总 + 「为什么是这个状态」三节理由 + 不可能组合自检；视图侧新增状态图标列与右键菜单、报表与命令行改用同一张表；9 个中性灰描边状态图标。**一处必须说清的边界**：`RuleIdentical` / `CrcIdentical` 两档已进类型模型、证据表与理由链，但引擎暂时只产得出字节档——规则比对与 CRC 比对分别属 DIR-009 / DIR-007，尚未开工；第 1 条要求的是证据这一维**能区分**这些档，不是要求兄弟条目先落地。见 §1.37） | **新套件** `Tests/EntryStatus`（27 个用例函数，**纯 QtCore**）+ `Tests/Folder`（45，35 → 45） |
| `Services/Folder/`（递归子目录策略） | DIR-003 | **已完成**（五条标准全落；新模块 `recursionstrategy.{h,cpp}`：三档表 + 表自检、档位 ↔ `Options` 双向映射（反查**按行为**归类，不新增字段）、深度边界唯一一份解释文案、循环符号链接判据与文案；视图侧把两态复选框换成档位下拉 + 深度上限数字框，换档即 `rescanRequested()` → 会话重扫。**一处必须说清的边界**：`applyTierToControls` 原先带一个 `fromUser` 参数，实测是**遮蔽防线**（程序性路径下那条写入恒被覆盖或恒等），删掉它没有任何用例变红，本轮收成「深度控件只有两个写入者」并让变异能单独打红。见 §1.38） | `Tests/Folder`（51 个用例函数，本轮 45 → 51；链接 QtWidgets） |
| `Services/Folder/`（状态着色与图标） | DIR-012 | **已完成**（五条标准全落；新模块 `statuspalette.{h,cpp}`：三套配色（`default` / `high-contrast` / `color-blind-safe`）各带深浅两套色值 + 两个参考背景 + **自报的对比度门槛**，一张表 `colorSchemeTable()` 与一个**把表当参数**的校验函数；`relativeLuminance()` / `contrastRatio()` 走 WCAG 相对亮度、非法输入返回 `-1`；`colorBlindSeparation()` 走 Viénot/Brettel 模拟，**「色盲友好」这一位是被判据守着的**而不是装饰标签；配色导出 / 导入 `.lqcolors`，导入**复用同一个校验函数**且失败时一个字段都不改。视图侧：`themedStatusIcon()` 用 `CompositionMode_SourceIn` 给中性灰描边图标着色以适配深浅主题、按表铺的配色下拉与「配色…」菜单、`setColorScheme()` 只逐行 `dataChanged` 重绘**绝不重扫**、导入的自定义配色只活在本次会话。见 §1.39） | **新套件** `Tests/StatusPalette`（29 个用例函数，**纯 QtCore**）+ `Tests/Folder`（55，51 → 55；链接 QtWidgets） |
| `Services/Files/`（文件系统） | PLAT-002 | **部分完成**（Windows 实现未编译验证） | `Tests/FileSystem`（50 用例） |
| `Services/Files/`（回收站） | PLAT-003 | **部分完成**（Windows 实现未编译验证） | `Tests/Trash`（35 用例） |
| `Services/Files/`（名称与 Unicode） | PLAT-007 | **部分完成**（长路径只写在 Windows 侧，未编译验证） | `Tests/PathName`（40 用例 + 1 个仅 Linux 执行） |
| `Services/Files/`（错误携带与批量处置） | PLAT-008 | **部分完成**（界面动作尚未接上） | `Tests/Batch`（36 用例） |
| `Services/Files/`（文件操作策略与安全契约） | OPT-005 | **已完成**（五条标准全落；策略本身不执行文件操作，只回答「该怎么做」） | `Tests/FileOpsOptions`（92 个用例函数，**纯 QtCore**） |
| `Services/Platform/`（系统图标） | PLAT-004 | **部分完成**（Windows / Linux 实现未在目标平台验证；界面尚未取用） | `Tests/PlatformIcon`（46 个用例函数，含 3 条走真实图标源） |
| `Services/Platform/`（Shell 集成） | PLAT-005 | **部分完成**（Windows 注册表薄层未编译过；界面尚未接入；本机平台能力置灰说明已落地） | `Tests/ShellIntegration`（101 个用例函数） |
| `Services/Platform/`（单实例与进程间通信） | PLAT-006 | **部分完成**（第 1、2、4、5 条已勾；第 3 条只落地「把已有窗口激活到前台」那一半——「创建会话」已由 `MainWindow::openRequest()` 接上，但「不抢焦点可配置」只有服务层 API（`ActivationPolicy` 三值），**没有任何设置项或命令行开关暴露它**，故第 3 条留空。另有一条环境限制：**共享内存这条腿在本机不可用**，实际走的是进程锁 + 本地套接字，`recoverStaleIdentifier()` 分支在本机不可达） | `Tests/SingleInstance`（103 个用例函数 / QTest 合计 105，含真实子进程） |
| `Services/Session/`（会话设置接口） | SESS-001 | **部分完成**（只有接口与内存实现；作用域链与落盘留给 SESS-006 / SESS-007） | `Tests/Session`（42 个用例函数，与下一行同一套件） |
| `Views/Session/`（会话基类） | SESS-001 | **部分完成**（第 2 条里「并注册」那半句依赖 SESS-002，已落地但基类尚未接上注册表；界面尚未取用） | `Tests/Session`（42 个用例函数） |
| `Services/Session/`（类型注册表） | SESS-002 | **部分完成**（第 2 条里「并注册」的前半句——按类型 ID 造会话——要等各会话类型实现出来；界面尚未取用） | `Tests/SessionType`（57 个用例函数，**纯 QtCore**） |
| `Services/Session/`（设置声明与草稿） | SESS-006 | **已完成**（第 2、3、4 条的主体；第 1 条是界面，第 3 条在界面上真正被调用另由 `Tests/SettingsDialog` 证明。框架刻意不含具体设置项） | `Tests/Settings`（65 个用例函数，**纯 QtCore**） |
| `Views/Session/`（会话设置对话框） | SESS-006 | **已完成**（第 1 条的对话框外壳；界面尚未接入 Rules 按钮） | `Tests/SettingsDialog`（44 个用例函数，链接 QtWidgets） |
| `Services/Session/`（作用域链与写入路由） | SESS-007 | **部分完成**（第 1、4 条已勾；第 2、3 条的服务层逻辑与文案已落地并被测试，缺的是界面接通：下拉还没显示去向、切换还没问一句、关标签路径还没有持有设置的会话） | `Tests/SettingsScope`（40 个用例函数，**纯 QtCore**） |
| `Services/Report/` | RPT-* | **部分完成**（只交付 HTML/TXT；CSV/XML、模板、预设持久化、CLI 入口、打印机/PDF 与 UI 异步装配未做） | `Tests/Report`（36 个用例，含真实 Text 解码→报告映射与转义回归） |
| `Services/Patch/`（生成 / 解析 / 只读预演） | PAT-001 / PAT-003 | **部分完成**（已通过真实 `git apply` 与系统 `patch` 逐字节验收；二进制补丁、传统 context diff、重命名、权限修改未做） | `Tests/PatchRegression`（75 个用例） |
| `Services/Patch/`（应用与事务） | PAT-002 / PAT-005 | **部分完成**（**本轮补完**。五条完成标准里的预演、逐 hunk、回滚、反向均落地并有测试；「操作日志」由 `ApplicationResult::audit` 承担、「报表」由 `applicationAuditJson` 承担，全局 `Services/Log` 未接通——`patch.pri` 不可改，include `logging.h` 会形成声明不出来的隐式依赖。界面（确认对话框、受影响文件清单、hunk 勾选）未做） | `Tests/PatchApply`（58 个用例函数 / QTest 合计 60，**纯 QtCore**） |
| `Services/Vcs/`（只读后端） | VCS-001 ~ VCS-019 的共同底座 | **部分完成**（HEAD / 索引 / 任意修订 / 冲突三阶段 / 日志 / 引用 / blame / 修订图数据均可用；无引用选择浏览器、merge-base、日志正则搜索、图形列） | `Tests/Vcs`（31 个用例，其中 23 个依赖真实 Git） |
| `Views/Vcs/`（四种模式的比较视图） | VCS-002 ~ VCS-010 | **部分完成**（无变更增删行数、无变更列表批量导出、无图形列） | `Tests/VcsView`（16 个用例） |
| `Views/Vcs/`（逐行追溯） | VCS-012 / VCS-013 | **部分完成**（**本轮补完**。三种着色、悬停四字段、同提交连续行合并为块、只看某作者、对比度校验已落地；**未实现**：「忽略空白改动」与「跨重命名追溯」两个选项（服务层 `Backend::blame()` 也没有承载它们的参数，要扩接口）、「只看某提交的区间」（只有单个提交）、进度是不定长的没有百分比） | `Tests/VcsBlameView`（23 个用例） |

PLAT-002 的详细说明与其「第 2 条完成标准为何不勾选」见
[issue #325](https://github.com/LorenHan/LqCompare/issues/325)；
PLAT-003 见 [issue #324](https://github.com/LorenHan/LqCompare/issues/324)；
PLAT-007 见 [issue #328](https://github.com/LorenHan/LqCompare/issues/328)；
PLAT-008 见 [issue #330](https://github.com/LorenHan/LqCompare/issues/330)；
PLAT-004 见 [issue #329](https://github.com/LorenHan/LqCompare/issues/329)；
PLAT-006 见 [issue #327](https://github.com/LorenHan/LqCompare/issues/327)；
PLAT-005 见 [issue #326](https://github.com/LorenHan/LqCompare/issues/326)；
ENG-006 见 [issue #338](https://github.com/LorenHan/LqCompare/issues/338)；
FILT-001 见 [issue #228](https://github.com/LorenHan/LqCompare/issues/228)；
SESS-001 见 [issue #36](https://github.com/LorenHan/LqCompare/issues/36)；
SESS-002 见 [issue #37](https://github.com/LorenHan/LqCompare/issues/37)；
SESS-007 见 [issue #42](https://github.com/LorenHan/LqCompare/issues/42)；
FILT-005 见 [issue #233](https://github.com/LorenHan/LqCompare/issues/233)；
FILT-003 见 [issue #230](https://github.com/LorenHan/LqCompare/issues/230)；
FILT-002 见 [issue #229](https://github.com/LorenHan/LqCompare/issues/229)；
FILT-004 见 [issue #231](https://github.com/LorenHan/LqCompare/issues/231)；
OPT-005 见 [issue #317](https://github.com/LorenHan/LqCompare/issues/317)；
OPT-010 见 [issue #365](https://github.com/LorenHan/LqCompare/issues/365)；
DIR-003 见 [issue #111](https://github.com/LorenHan/LqCompare/issues/111)。

### 1.2 回收站（PLAT-003）落地到了什么程度

| 平台 | 实现 | 在本机验证过 |
| --- | --- | --- |
| macOS | `trash_mac.mm`，`NSFileManager trashItemAtURL:` | **是**——真实往返（移进废纸篓 → 断言文件确实在废纸篓里 → 还原 → 断言回到原处） |
| Linux | `trash_linux.cpp`，XDG 规范（`~/.local/share/Trash` + `.trashinfo`） | 部分——路径与格式规则在 `trash.cpp` 里，**已在 macOS 上真实执行**；系统调用部分未在 Linux 上跑过 |
| Windows | `trash_win.cpp`，`SHFileOperationW` + `FOF_ALLOWUNDO` | **否**——从未编译过 |

删除的入口从 `FileSystem` 移到了 `TrashService`（`Code/Services/Files/trash.h`）。
**迁移的理由值得记住**：`FileSystem` 是无状态的，而「撤销最近一次删除」需要
一个长期存在的撤销点。若在 `FileSystem` 上留一个便捷转发，实现必然是
「每次调用现场 new 一个 TrashService」，于是撤销点随对象一起被丢掉，
用户点撤销永远报「没有可还原的删除」——而删除本身是成功的，只有撤销不工作。
这类缺陷很难查。所以删除只保留一条入口。

`undoLastDelete()` 在 Windows 上返回 `NotSupported`，这是规格明确允许的
「受平台能力限制时说明」：回收站里的条目是一对 `$R`/`$I` 文件，不是普通文件，
还原要走 Shell 命名空间扩展。`displayLocation()` 在 Windows 上返回
`shell:RecycleBinFolder`，界面可以用它提供「打开回收站」入口让用户手工还原。

### 1.3 PLAT-008 落地到了什么程度

规格的五条完成标准对应到代码：

| 完成标准 | 落在哪里 | 状态 |
| --- | --- | --- |
| 四类错误分别识别并给出不同建议 | `errorAdvice()`（PLAT-002 已就位），`Tests/FileSystem` 里有一条用例断言四条建议互不相同 | **已落** |
| 错误信息包含原始系统错误码 | `ErrorCode`（分类 + 域 + 原始值）、`errorDetail()`、`errorReport()`；`fromSystemError` / `fromWindowsError` / `fromCocoaError` 是唯一正确出口 | **已落** |
| 批量中失败的条目汇总为失败清单，可单独重试 | `BatchReport::failureGroups()`、`FailureGroup`、`BatchOperation::retryFailed()` | **已落** |
| 「重试失败项」与「跳过并继续」两条出路 | `retryFailed()` / 直接读取报告接受当前进度；`BatchFailurePolicy` 显式声明 | **已落** |
| 长任务中途错误不中断整体，保持已完成进度 | 默认策略下循环不停；`retryFailed()` 把结果**合并**回整批报告而不是替换 | **已落** |

**还没有做的**：界面上的动作还没接上。也就是说，`batch.h` 提供的失败清单、
两条出路与进度回调目前只有测试在用，Ribbon 上还没有一个按钮会走进去。
这一步要等 SESS（会话）与视图层就位，因为「失败清单」需要一个可停留的对话框，
而「重试失败项」需要一次批量操作作为上下文。

### 1.4 PLAT-004 落地到了什么程度

规格的五条完成标准对应到代码：

| 完成标准 | 落在哪里 | 在本机验证过 |
| --- | --- | --- |
| 按扩展名获取系统关联图标（Win `SHGetFileInfoW` / mac `UTType`+`NSWorkspace` / Linux 主题图标） | `iconservice_mac.mm` / `iconservice_win.cpp` / `iconservice_linux.cpp` | **macOS 是**（三条真机用例拿到真实像素，且文字文件与文件夹的图不同）；Windows 侧**从未编译过**；Linux 侧未在 Linux 上跑过 |
| 图标缓存按扩展名而非按文件，缓存命中率高 | `IconKey::cacheKey()`（键 = `f|txt` / `d|<dir>`）、`IconCache`（有界 LRU，带 `Stats::hitRate()`） | **是**——`serviceCachesByExtensionNotByFile` 断言同一类型的多个文件只解析一次，`serviceDoesNotLetDirectoryPoisonFileKey` 断言带扩展名的目录不会污染同名类型的文件 |
| 图标获取在后台线程，缺失时回退到内置的通用图标 | `IconService::requestIcon()` + 专属单线程 `QThreadPool` + `IconRequestQueue` 去重；`IconSource::Builtin` 是回退 | **是**（用可替换的假提供者断言调用次数与去重；另有真机用例） |
| 系统图标不可用时（无桌面环境）回退到内置图标集，不崩溃 | `createNativeIconProvider()` 在无可用图标源时返回 `HeadlessIconProvider`（Linux）；macOS 老系统走 `@available` 之外的分支；提供者抛异常由 `WorkItem` 吞掉 | **部分**——回退路径（`serviceFallsBackToBuiltinWhenProviderHasNothing`）与「有在途请求时析构」（`serviceDestructsWithPendingWork`）有覆盖；「真的没有桌面环境」只能靠注入假提供者模拟，本机没法真跑 |
| 图标大小随 DPI 与界面缩放正确获取 | `iconPixelSize(baseSize, devicePixelRatio)`、`Win32IconSize::nearest()`、`IconEntry::actualPixelSize` 如实报出真实尺寸 | **部分**——缩放算法与档位收拢是纯逻辑、已覆盖；Windows 上真实拿到的尺寸未验证 |

**还没有做的**：界面还没取用。`IconService` 目前只有测试在用，文件夹树与列表
还没接上（会话与视图层就位后一起做）。另外 Windows 只接了 16/32 两档，
48/256 需要 `IImageList` COM——代码里已注明，`actualPixelSize` 会如实报出
「其实只拿到了 32」，不会假装请求的尺寸就是拿到的尺寸。

### 1.5 PLAT-005 落地到了什么程度

规格的五条完成标准对应到代码：

| 完成标准 | 落在哪里 | 在本机验证过 |
| --- | --- | --- |
| Windows：右键菜单项（比较 / 与…比较 / 作为左右侧比较），通过注册表实现 | `buildShellIntegrationPlan()` 生成三类目标（`*` / `Directory` / `Directory\Background`）下的 `shell\LqCompare.<动作>` 动词键（`MUIVerb` / `MultiSelectModel` / `Position` / `Icon` + `shell\command`）；`install()` 写入 | **是（在内存注册表上）**——菜单项内容、加速键唯一性、单选/多选约束、位置与图标选项、命令行的引号与动作开关都有用例；但**没有**在真实 Windows 资源管理器里看过 |
| 「选择第二个文件后比较」的两步式交互（占位菜单项） | `ShellAction::CompareSecondStep` + `shellActionIsPlaceholder()`；菜单文字带省略号，命令行走 `%V`（第二个选中项） | **是**——`onlySecondStepIsPlaceholder` 断言只有它是占位；`includedByOptionsRespectsTwoStepSwitch` 断言关掉后「与…比较」与占位项一起消失 |
| 文件关联可注册 `.patch` / `.diff`，可单独关闭 | 计划里 `.patch` / `.diff` 写 `Software\Classes\.<ext>` 的默认值指向 `LqCompare.PatchFile` / `LqCompare.DiffFile`；扩展名键是 **Shared**（用户的既有值先备份），ProgID 是 **Owned** | **是**——`planWithoutPatchOmitsPatchEntries` 断言关掉 `.patch` 后 `.diff` 完好，反之亦然；`uninstallRestoresPreviousAssociation` 断言还原成用户原来的 ProgID 而不是删掉 |
| 一键安装与一键卸载；卸载后注册表无残留（有校验） | `install()`（含步骤 0「已安装则先按记录的选项拆掉」、失败整体回滚）、`uninstall()`、`verify()`、`findResidue()`；`InstalledState` 记录版本、路径、选项与备份数 | **是**——101 个用例覆盖安装/回滚（`failedInstallLeavesStoreEmpty` 断言回滚后存储**一条不剩**）、卸载还原（含 `Unsupported` 值类型逐字节还原）、重复安装沿用最初那份备份、外来内容保留并说明、校验能区分「缺失」与「值不对」、残留四类发现 |
| 非 Windows 平台该能力置灰并说明 | `registrystore_stub.cpp` 的 `UnsupportedRegistryStore` + `platformRegistryUnsupportedReason()` / `platformRegistryUnsupportedAdvice()`；macOS 建议走「自动操作 / 访达扩展」、Linux 建议走「Dolphin 服务菜单 / Nautilus 脚本」 | **是**——本机就是非 Windows，`capability().available` 为假、原因与建议都断言非空；读给出「什么都没有」（不报错）、写返回 `NotSupported` 而不是「假成功」 |

**还没有做的**：

1. `registrystore_win.cpp` **从未被编译过**（与 `trash_win.cpp` 等同一个道理）。
   它里面有一处必须留意的选择：所有访问都带 `KEY_WOW64_64KEY`。交付目标是
   32 位 MinGW 构建，不加这个标志时会写进 `WOW6432Node` 影子副本，
   而 64 位资源管理器看不见那里——安装、校验、卸载全都报成功，菜单里却什么都没有。
2. **界面还没接上**。`ShellIntegration` 目前只有测试在用，Ribbon 上还没有
   「安装 Shell 集成 / 卸载 / 检查残留」的入口，也没有一个对话框展示
   `ShellIntegrationReport::lines()`。这一步与 PLAT-004 的界面接入是同一件事的两半，
   一起做更省事（都需要 OPT 设置页）。
3. **命令行入口只解析、不执行**。`parseShellInvocation()` 已经能把
   `--shell-action=compare "a.txt" "b.txt"` 解成结构化的 `ShellInvocation`
   （含「同一个路径选两次」这类无效输入），但 `main.cpp` 还没有按它去开会话——
   那要等 SESS 会话框架就位（CLI-001）。
4. `Icon` 注册表值用的是可执行文件的图标索引 0。想要一个专用图标需要把图标
   编进 exe 的资源节（`.rc` 文件），本项目还没有加——索引指向不存在的资源时
   资源管理器显示**空白占位**而不是报错，所以这个值必须与打包方式一起改。

### 1.6 ENG-006 落地到了什么程度

规格的五条完成标准对应到代码：

| 完成标准 | 落在哪里 | 在本机验证过 |
| --- | --- | --- |
| 分级日志（五级）与分类标签 | `Level`（Error/Warning/Info/Debug/Trace）、`LQCOMPARE_ERROR/WARN/INFO/DEBUG/TRACE`，分类是宏的第一个参数 | **是**——五个级别逐个走过「名字 → 级别 → 输出」，且断言级别标识互不相同、都不为空、都不等于兜底的 `unknown` |
| 级别未启用时参数不求值 | 宏先调 `isEnabled()` 再拼消息 | **是**——用带计数副作用的表达式验证：关掉时求值 0 次，打开时**恰好 1 次**（2 次说明宏体里出现了两遍参数） |
| 输出到控制台、文件、界面输出面板三个目标 | 控制台（`stderr`）与文件已接；界面输出面板通过 `addSink()` / `removeSink()` / `clearSinks()` 接入 | **两个半**——控制台与文件有覆盖（追加而非截断、不可写路径要返回失败、关掉之后不再写入）；接收者机制有覆盖（结构化字段、多接收者顺序、移除、重入不死锁、跨线程调用）；**输出面板本身尚未接线**，原因见下 |
| 格式含时间戳、级别、分类、线程 id、消息 | `Record`（结构化载体）+ `Record::line()`（规范文本行）。线程名非空时也带上 | **是**——时间戳可还原成合理时刻（不是「行里有数字」）、级别是定宽短名、分类、`[t:<十六进制>]`、消息；并断言两个目标拿到**逐字相同**的文本 |
| 「记录耗时」辅助（进入/退出自动计时） | `Log::Stopwatch`（RAII）+ `LQCOMPARE_SCOPE_TIMER` 宏 | **是**——析构时记一条「X 耗时 N ms」、级别关掉时静默、备注写在同一行、中途可查 `elapsedMs()`、`finish()` 可重复调用只记一条、**级别在析构时判断**（先放计时器再调级别也能出结果） |

**顺带修掉的一个真实缺陷**：`Log::write()` 原先**完全不做级别过滤**，
只有宏里那个 `if` 在过滤。于是 `main.cpp` 里 5 处直接调用（含启动横幅）
在 `--log-level error` 下照样打印，与 `logging.h` 写的「低于该级别的日志被丢弃」
相反，日志文件也没法靠调级别瘦身。现在 `isEnabled()` 是唯一的判断处，
宏、`write()`、`Stopwatch` 三处都走它——各自写一遍 `<=` 的话，把方向弄反
只会发生在其中一处，而现象是「某个级别偶发不输出」。

**行为上的一个可见变化**：默认级别是 `warning`，而启动横幅是 `info`，
所以**不带参数启动时日志里不再有启动横幅**（原先有，因为当时 `write()` 不过滤）。
交接文档里的验证命令本来就带 `--log-level info`，照常工作。
如果希望「用户什么都不说时日志里也有一条启动锚点」，那是 OPT-010 的默认值问题，
不是日志模块的问题——不要用「让 `write()` 不过滤」去解决它。

**还没有做的**：界面输出面板尚未接线。接收者机制（第三个目标）已经就位并有跨线程用例，
但把 `MainWindow` 的输出面板挂上去时要**加一次排队跳转**：
接收者在「记录日志的那个线程」上被调用，而图标解析跑在后台线程上，
从那里碰控件会崩。正确姿势是让接收者只 `emit` 一个信号，
再以 `Qt::QueuedConnection` 连到面板的槽。这一步与 PLAT-004 / PLAT-005 的
界面接入是同一批活（都需要 OPT 设置页），一起做更省事。

### 1.7 FILT-001 落地到了什么程度

规格的五条完成标准对应到代码：

| 完成标准 | 落在哪里 | 在本机验证过 |
| --- | --- | --- |
| 支持 `*`（任意字符）、`?`（单字符）、`[...]`（字符集）、`**`（跨目录）等语义 | `Mask::compile()` / `Mask::matches()`；`*` 不跨 `/`、`?` 恰好一个字符、字符集含 `[!...]` / `[^...]` 取反与 `[a-z]` 区间、`**` **独占一段**时跨目录 | **是**——87 个用例函数里，A/B/C 三组共 33 个用例专测语义边界：`*` 吃零个字符、区间上下界含、`[a-]` 与 `[-a]` 里的 `-` 退化、`]` 写在最前面是字面量、`a?b` 不命中 `a/b`、`build/**` 也命中 `build` 本身、`a**b` 不跨目录 |
| 支持排除掩码（前导 `-`）与包含掩码同时声明，排除优先 | `MaskFilter::parse()` 逐行解析；`MaskFilter::decide()` 先扫排除再扫包含 | **是**——`excludeWinsOverInclude` 断言 `*.cpp` + `-*_test.cpp` 下 `main_test.cpp` 不被接受；另有「只写排除 → 其余全保留」「只写包含 → 白名单」「空声明 → 全保留」三条 |
| 掩码匹配在 Windows 上默认大小写不敏感、Unix 上默认大小写敏感，且可显式覆盖 | `defaultCaseSensitivity(MaskPlatform)`（**平台是显式参数**）+ `MaskPlatform` 枚举 + `setCaseSensitivity()` / `clearCaseSensitivityOverride()` | **是**——**两种平台的默认值都在本机被断言**（Windows 那条不靠 `#ifdef` 分支绕过去）；另有「显式覆盖生效」「清掉覆盖回到平台默认」「`[a-z]` 与 `[A-Z]` 在不敏感下双向成立」「不敏感不会让 `[A-_]` 的区间反转」 |
| 提供掩码语法速查与实时预览（输入掩码后显示「匹配 N 项 / 共 M 项」） | 速查：`maskSyntaxReference()` 给出 21 行、每行都带可执行样本，另有 `maskSyntaxReferenceText()` 生成纯文本表；预览：`preview()` / `previewNames()` / `MaskFilterPreview::summary()` | **速查是**（测试会遍历这 21 行、把 53 条样本**真的跑一遍**，并断言纯文本速查里含每一个掩码与样本）；**计数是**（`summaryUsesTheWordingFromTheSpec` 断言文案逐字等于「匹配 2 项 / 共 3 项」）；**界面上的速查面板与实时预览不是**，见下 |
| 解析器为纯函数并有完整单元测试（含恶意与畸形输入） | `Mask::compile()` 无全局状态、不碰文件系统；`Tests/Filter` 的 I 组专测恶意输入 | **是**——`compileIsPureAndRepeatable` 断言同一输入两次得到同一结果（含失败位置）；I 组 6 条覆盖 `**/**/…` 对上 40 段路径的 2^N 回溯、`*a*a*…` 的段内指数退化、400 段路径、5000 字符掩码、500 成员字符集 |

**顺带定下来的两条对外约定**（都不是规格要求的，但不定下来会各自乱长）：

1. **掩码里的分隔符恒为 `/`，`\` 是转义字符**，与平台无关。于是
   `build\out` 会得到一条带建议的错误（「`\` 只能用来转义 `* ? [ ] - # \`，
   如果这是 Windows 路径分隔符请改写成 `/`」），而不是静默变成 `buildout`。
2. **不含 `/` 的掩码按名字匹配**（`*.txt` 在任意目录下都命中），
   **含 `/` 的按相对路径从起点匹配**（`src/*.txt` 不命中 `x/src/a.txt`）。

**还没有做的**：界面上的速查面板与实时预览尚未接线。`preview()` 与
`maskSyntaxReferenceText()` 目前只有测试在用——「输入掩码后显示匹配 N / 共 M」
需要一个 Filters 页或会话设置页来承载，而那要等 SESS/OPT 的工作流；
与 PLAT-004 / PLAT-005 / ENG-006 的界面接入是同一批活。另外
FILT-002（正则与超时保护）还没开始，它会**复用**这一份掩码实现；
FILT-005（三层叠加与作用域）与 FILT-003（属性过滤）已在后续两轮落地，
分别见 §1.12 与 §1.13；FILT-003 还顺势把声明切行 `splitDeclarationLines()` 提成了
本模块的公共 API，FILT-002 可直接复用。

### 1.8 SESS-001 落地到了什么程度

规格的四条完成标准对应到代码：

| 完成标准 | 落在哪里 | 状态 |
| --- | --- | --- |
| 基类定义统一契约：createWidget、open、close、reload、save、isDirty、canSave、sessionSettings | `CompareSession`（`Views/Session/comparesession.h`）。`sessionSettings` 的返回类型是 `Services/Session/session.h` 里的 `SessionSettings` 接口 | **已落** |
| 新增一种会话类型只需实现基类契约并注册，不需要改动已有会话代码 | 「实现基类契约」那半句：`Tests/Session` 里的 `MinimalSession` 只写了 `createView()` 一个实现点，就走通了「打开 → 拿视图 → 标脏 → 保存 → 重载 → 关闭」全部动作。「并注册」那半句在 SESS-002 落地后补上了：G 组把一个只实现基类契约的类型的工厂登记进 `SessionTypeRegistry`，再从注册表把会话**造出来**并走完同一个生命周期——全程没有改动 `CompareSession` | **已落** |
| 基类提供状态栏文本、错误上报、进度上报三个公共出口 | `setStatusText()` / `reportError()` / `reportProgress()` 三个 public 出口，配 `statusTextChanged` / `errorReported` / `progressChanged` 三个信号；载体是 `SessionError`（message + detail）与 `SessionProgress`（current + total + what + `percent()`） | **已落** |
| 基类不依赖任何具体视图头文件（编译期校验） | 编译期那一半：`SessionTests.pro` 的 INCLUDEPATH 里只有 `Views/Session` 与 `Services/Session`，基类一旦 include 具体视图头文件，**该测试工程直接构建失败**。源码级那一半：`Tests/Session` 的 F 组用例把两个源文件的 `#include "…"` 与白名单比对，并另有一条用例对**故意写坏的源码**做反向验证 | **已落** |

**第 2 条为什么分了两轮才算完成**：「新增一种会话类型」在这个仓库里是两步——
实现基类契约，然后在**类型注册表**里登记（类型 ID、显示名、图标、默认掩码、
创建工厂）。SESS-001 那一轮只有第一步可做：注册表是 SESS-002，还不存在时没有
第二个地方可以登记，而临时造一个「只登记类型 ID 的小表」等于把 SESS-002 的契约
提前定死一份，两份类型表必然分歧。SESS-002 落地后，G 组把第二步补上，
**因此这一条现在可以勾了**。

**诚实地说清这条的证据边界**：目前「已有会话代码」只有基类本身与两个测试替身，
因此这条标准证明的是「新增类型不需要改动基类与既有类型的代码」。
真正的第二种会话类型要等 `TEXT-*` / `FOLD-*` 落地，届时它只是「再加一个类型」——
如果那时发现还要改基类，说明这一条的结论当时下早了。SESS-002 本身是一次
正向数据点：它是新模块 + 新测试，**没有改动 `comparesession.h` 一行**。

**还没有做的**：

1. **界面还没取用**。`CompareSession` 与 `SessionTypeRegistry` 目前只有启动自检
   与测试在用，`SessionArea::addSession()` 仍然创建占位页。接上要等 SESS-003
   （Home 视图）——Home 页要按注册表的分组枚举生成入口，容器要按类型 ID 造会话。
2. **设置的落盘与作用域链还没有**。`MemorySessionSettings` 只保证「本次运行期间
   读得到、写得进」，重启即丢。它**不谎称已落盘**：既然是内存实现，就没有
   「保存失败」这种状态需要上报。作用域三层（视图 > 会话 > 类型）是 SESS-007。
3. **三个公共出口还没有接收方**。信号已经在了，但状态栏、错误对话框与进度条
   都还没有连上去——那与 PLAT-004 / PLAT-005 / ENG-006 / FILT-001 的界面接入
   是同一批活（都要等 OPT 设置页与真正的会话视图）。

**两条刻意的边界**（改之前先读这三段理由，否则很容易把它们「修」回去）：

- `close()` **不销毁视图**。视图的父子关系属于容器；基类无法知道这个视图有没有
  被别处引用（分离窗格会把同一个视图挂到另一个窗口下），替容器删就是越权。
- 「未保存改动时拒绝重载」与「没有改动时拒绝保存」都放在**基类**，不在各会话类型里。
  忘了判的后果分别是「几十处编辑无声消失」与「同步目录里一串无意义的版本」，
  两条都不该由每个新增的会话类型各自记得。
- `open()` 幂等且**第二次不调 `doOpen()`**。界面在恢复标签时会重复调用它，
  重跑一遍会把滚动位置与编辑状态全部重置。

### 1.9 SESS-002 落地到了什么程度

规格的四条完成标准对应到代码：

| 完成标准 | 落在哪里 | 状态 |
| --- | --- | --- |
| 注册表条目包含：类型 ID、显示名、图标、默认文件掩码、创建工厂、是否 Pro 特性、是否平台限定 | `SessionType`（七个字段齐备）+ `SessionTypeEntry`（描述子 + 工厂）。`add()` 逐条校验：ID 格式、ID 重复、显示名非空、每条掩码能编译，**任一条不过就整条拒绝** | **已落** |
| 类型 ID 有稳定性测试：已发布 ID 的字符串值被快照断言锁定 | `Tests/SessionType` 的 B 组：14 个 ID 的精确值**与顺序**被逐字比对；6 个非法 ID 形态被拒；`idsAreStableAcrossRebuilds` 盯住「表是每次重建的」这件事 | **已落** |
| 按掩码查询匹配类型时按注册顺序（优先级）返回首个命中 | `findByFileMask()` 顺序扫描、首个命中即返回；`findByMaskFollowsRegistrationOrderNotTableOrder` 用**反序登记**证明它按注册顺序而不是插入位置；内置表里 `*.html` 那处重叠是真实数据上的用例 | **已落** |
| 注册表可枚举，供 Home 页与新建向导直接生成入口 | `entries()` / `byGroup()` / `groups()`（`onlyAvailable` 可关，供界面置灰展示）+ `findByName()`。枚举顺序与注册顺序一致，因此「分组枚举的并集」可以整表比对 | **已落** |

**还没有做的**：

1. **界面还没取用**。`SessionArea::addSession()` 仍然创建占位页；Home 页仍在用
   自己那份硬编码的类型表。让 HomePage 改成读注册表是 SESS-003 的范围——
   本轮改了会与它撞车，因此先用一条**源码级用例**把两边钉在一起（见下）。
2. **创建工厂还没有真正的实现方**。内置 14 种类型的 `factory` 全为空——
   各会话类型（`TEXT-*` / `FOLD-*` / `HEX-*` …）还没实现，没有东西可注册进工厂。
   「类型已登记」与「这一版还没有这个视图」由 `hasFactory()` 分开，
   G 组用测试替身证明这条路是通的。
3. **默认文件掩码只是一组种子**，不是 Beyond Compare 那张完整的文件格式关联表。
   真正的权威在 `FORMAT-*`（文件格式定义）。现在如实只放无歧义的种子
   （`.csv` 给表格、`.png` 给图片、`.diff` 给补丁视图），好过抄一份假装完整的表。

**四条刻意的取舍**（改之前先读，否则很容易把它们「修」回去）：

- **`add()` 整条拒绝，不做部分接受**。一个「ID 进去了但掩码全丢了」的条目会让
  按掩码的自动选择**静默失效**——「掩码写错了」能在启动时发现，「某些文件双击
  没反应」不能。
- **内置表里 `*.html` 被文本比对与表格比对同时声明，这是有意留的**。合成的小表
  只能证明实现自洽，证明不了这张表落在什么结论上；留着它，`onlyHtmlOverlaps`
  与 `allByMaskKeepsTheOnlyRealOverlap` 就把「文本胜出」这个当前结论钉死了。
  要让表格胜出是一次产品决定，改的时候这两条会红，从而强制做那次决定。
- **`describe()` 与 `validate()` 是两件事**。前者给人看（`main.cpp` 与诊断输出用），
  后者是自检。`validate()` **只查登记时没有把住的**几件事（英文原名缺失、图标键
  写法、掩码写成大写）——把 `add()` 已经拦下的再查一遍，那些分支永远走不到，
  而一条永远不会红的护栏比没有护栏更糟。
- **注册表不做成单例**（与 `CommandRegistry::instance()` 不同）。表要被反复构造：
  合成的小表验证优先级、内置的大表验证快照。单例会让「这一次测试往表里加了什么」
  泄漏到下一处。

**过渡期的源码级护栏**：`homePageHardcodedIdsMatchTheRegistry` 会去读
`Views/Shell/homepage.cpp`，从 `sections()` 的函数体里抠出全部
`QStringLiteral("…")` 与注册表**逐个比对（含顺序）**。注册表落地前那张硬编码的表
是事实上的第一批「已发布 ID」，两边不一致会让 Home 页点出来的入口指向不存在的类型
（现象是「点卡片没反应」）。另有一条 `homePageIdCheckCanFailOnBrokenSource`
对一段**故意写错**的源码跑同一个判定流程，证明这条护栏不是恒真的。

### 1.10 SESS-006 落地到了什么程度

规格的四条完成标准对应到代码：

| 完成标准 | 落在哪里 | 状态 |
| --- | --- | --- |
| 对话框左侧为 Tab 列表（按会话类型变化），右侧为当前 Tab 内容，底部为作用域下拉 + 确定/取消/应用/恢复默认 | `Views/Session/settingsdialog.{h,cpp}`。Tab 列表由 `SettingsSchema::tabs` 驱动（`editorsShowTheSessionsCurrentValuesNotTheDefaults` 与 `twoDifferentSchemasProduceTwoDifferentDialogs` 证明换一份声明就换一套界面）；底部四按钮的对象名固定为 `settingsOkButton` / `settingsCancelButton` / `settingsApplyButton` / `settingsRestoreDefaultsButton`，作用域下拉为 `settingsScopeCombo`，取值为 `allSettingScopes()` 的三项（含各自的一句说明）。几何断言把「左列表、右内容、底部条」钉住 | **已落**（界面尚未接入 Rules 按钮，见下） |
| 设置项分组声明式定义（标题、说明、控件类型、默认值、校验规则），框架据此生成界面 | `Services/Session/settingschema.{h,cpp}` 的 `SettingItem` / `SettingGroup` / `SettingsTab` / `SettingsSchema`。六种控件（Bool / Integer / Text / MultilineText / Choice / MaskList）各有一条数据驱动用例走「造声明 → 造草稿 → 读默认 → 写新值 → 应用 → 目标里拿到归一后的值」；`SettingsSchema::validate()` 与 `selfCheck()` 覆盖键重复、分组 ID 重复、说明缺失、区间反了、枚举取值表与控件类型不匹配、默认值过不了自己的校验等写法错误 | **已落** |
| 存在未保存改动时切换 Tab 或关闭对话框给出确认 | 策略在服务层：`inquiryForUnsavedChanges(reason, dirtyKeys, targetTabTitle)` 返回「要不要问、标题、正文、给出哪几个出口、默认项」。界面层 `SessionSettingsDialog::confirmPendingChanges()` 是切 Tab 与关闭**唯一**的入口（`reject()` 与 Tab 列表的 `currentRowChanged` 都走它），并由 `clickingTheTabListGoesThroughTheSameInquiry` / `escapeGoesThroughTheSameInquiry` 证明它真的被调到 | **已落** |
| 任一会话设置 Tab 均可在无界面测试中被单独构造与读写 | `Tests/Settings` 刻意 `QT -= gui`——`settingschema.cpp` 一旦引入 QtGui，该工程直接构建失败。用例全程不碰控件：读默认值、按 Tab 与按项查脏、`applyTo()` 写到目标设置上再读回来 | **已落** |

**还没有做的**：

1. **界面没有接 Rules 按钮**。`MainWindow` 里那批命令还没有一条去构造这个对话框——
   接通它的前置是「当前会话能交出设置目标」与「设置声明有人登记」，前者要各会话类型
   落地，后者是 TEXT-* / FOLD-* 的决定。因此对话框目前的生产调用方是**零**，
   只有测试构造函数在用它。这一条与 SESS-002 的处境相同（注册表也只有启动自检在用），
   不是本轮遗漏。
2. **框架里一项具体设置都没有**。「文本比对该有哪些设置」是产品决定，先编一份
   看起来完整的表，等各类型落地时会被逐条质疑；留白不妨碍任何人——登记一份声明，
   对话框立刻就有内容。`frameworkShipsNoHardcodedSettingItems` 把「目录当前为空」
   这个状态钉死：下一个人看到它红了，说明有人做了那次决定。
3. **恢复默认只作用于当前 Tab**，不是整表重置。整表重置是更大范围的动作，
   该有一个说清范围的入口（那是 OPT 设置页的事）。理由写在 §4 决策表里。
4. **作用域下拉目前只是「能选」**，选了之后「本次修改将保存到 X」与三层覆盖链
   是 SESS-007 的范围。`SettingScope` 的三个取值与各自的说明这一轮已经定下来，
   SESS-007 直接复用。

**五条刻意的取舍**（改之前先读，否则很容易把它们「修」回去）：

- **校验规则是枚举字段，不是一段正则**。正则看着更通用，但界面没法回答「这一项在等什么」，
  测试也没法逐条覆盖边界。更要紧的是 Qt 5.15 的 `QRegularExpression` **没有匹配超时**
  （`setMatchTimeout()` 是 Qt 6.0 才有的，FILT-002 为此被卡住），把用户输入喂给一条
  不可中断的匹配等于留了一个不可控的卡顿入口。现用的规则都是 O(字符串长度) 的。
- **掩码清单项复用 `MaskFilter::parse()`**，不自制「看扩展名」的匹配。与 SESS-002 复用
  掩码语言同源：自制写法会把 `[abc`（未闭合字符集）与 `build\out`（`\` 后是不能转义的
  字符）静静放行，而它们正是 FILT-001 明确要求报错的形态——用户在设置页看到「合法」，
  扫描时那一行却被丢掉。
- **用户改的是草稿，不是会话本身**。于是「取消」= 丢草稿，「恢复默认」= 重置草稿且可撤销。
  直接改会话的话，取消要把每一项改回去（一百种做错的方式，错的后果是「点了取消、设置却变了」），
  恢复默认还会变成不可撤销的动作——本仓库的纪律是破坏性操作必须可逆。
- **「脏」是与「载入时读到的那份值」比，不是与出厂默认比**。什么都没改就不该被问；
  而「改回默认值」是一次真实的改动，必须算作改动（否则按了恢复默认再关窗口，
  程序认为什么都没发生，设置其实该变）。`isDefault()` 才是与出厂默认比的那一个，
  两个概念分开。
- **询问的出口按场景给**：切换 Tab 只给「应用 / 取消」，关闭才给「放弃改动」。
  切 Tab 时草稿原样带到下一张 Tab，没有任何东西会丢，给一个用不上的破坏性按钮等于
  凭空造出一条丢工作的路径。默认项恒为「取消」（手快的回车不应丢掉刚敲进去的东西）。

### 1.11 SESS-007 落地到了什么程度

规格的四条完成标准对应到代码：

| 完成标准 | 落在哪里 | 状态 |
| --- | --- | --- |
| 三种作用域：仅当前视图、当前会话默认值、该类型全部新会话默认值（三层优先级 视图 > 会话 > 类型） | `settingScopePriorityOrder()`（**解析顺序**，与 `allSettingScopes()` 的**展示顺序**是两个函数）、`settingScopeRank()`；`Type` 的显示文案本轮对齐成规格原文「该类型全部新会话默认值」。A 组 5 条用例把顺序钉死（反转发现在 7 条红） | **已落**（逻辑层。下拉能选、选择真的决定写入位置；对话框尚未接通作用域链） |
| 下拉中明确显示「本次修改将保存到 X」，并在切换作用域时提示已有改动将改写到何处 | `writeDestinationText(scope)` 与 `ScopedSessionSettings::writeDestinationText()`；`scopeSwitchNotice(from, to, pendingKeys)` 给出 `ask / title / text`。三条文案的**字面值**被逐字断言；另有两条用例钉住「没有待定改动就不问」「不改作用域就不问」，以及「文案不得声称会把**已应用**的改动搬走」 | **逻辑已落，界面未接**：对话框底部还没有显示这句话的控件，切换下拉也没有去问 `scopeSwitchNotice()`。见下 |
| 作用域为「仅当前视图」时关闭标签即丢弃，且关闭前有提示 | `viewScopeKeys()`（只报视图层，不会把另外两层算进来）、`discardViewScope()`（返回真被丢弃的条数）、`planViewScopeClose()`（没有视图级设置时一个字都不问） | **逻辑已落，关闭路径未接**：`SessionArea` 目前只有占位页、没有真正的会话，因此没有「关闭一个持有设置的标签」这件事可接。见下 |
| 读取设置时按 视图 → 会话 → 类型 → 出厂默认 的覆盖链解析，且该链有单元测试 | `ScopedSessionSettings::value()` / `resolvedValue()` / `factoryDefault()`；B 组 6 条用例逐环验证，含「出厂默认排在调用方的 `fallback` 之前」「出厂默认按 `normalized()` 归一后交出」「显式设成空值不算没设置」 | **已落** |

**还没做的**（三条都只是「界面尚未接通」，不是逻辑缺失）：

1. **对话框底部没有显示「本次修改将保存到 X」**。`settingsdialog.cpp` 的下拉目前
   只有三个作用域各自的文案与说明（SESS-006 落的），没有这一行去向提示。
   接它需要对话框持有一个 `ScopedSessionSettings`，而那是「各会话类型交出设置目标」
   之后的事——与 SESS-006 第 1 条「界面未接 Rules 按钮」是同一个前置。
2. **切换下拉的提示没有接**。`scopeSwitchNotice()` 已经被测好了，但对话框的
   `settingsScopeCombo` 还没有在 `currentIndexChanged` 里问它一句。
3. **关标签的提示没有接**。`SessionArea::closeCurrentSession()` 关的是占位页，
   没有任何东西持有视图层设置。等第一个真正的会话类型落地、并且会话的
   `sessionSettings()` 换成 `ScopedSessionSettings` 之后，这一处才有人可接。
   规格第 3 条「关闭前有提示」的**判定与文案**已经在 `planViewScopeClose()` 里，
   届时只是把它的 `ask / text` 交给 `SessionClosing` 那条路径。

**五条刻意的取舍**（改之前先读，否则很容易把它们「修」回去）：

- **读按覆盖链、写只落一层**。读写方向不对称是有意的：写的时候「顺手把下面几层也写一遍」，
  会让用户一次临时调整（视图级）变成这个会话甚至这个类型的默认值，而他从没同意过。
- **判断「这一层有没有」用 `contains()`，不看值是否为空**。用户把编码清空是一次**真实的**
  设置；按值判断会越过这一层取到下面那层，于是用户发现自己清不掉这一项。
- **写入目标层缺失时返回 false，不退而写入别的层**。悄悄写到视图层会让一次
  「保存成新会话默认值」活不过关标签，而用户以为存下来了。
- **`changed` 按有效值是否变化发，不按「有没有写这一层」发**。所以本类**不转发**
  三层的 `changed`——转发会让一次写入发出两条，且层的 `clear()` 用空键表示「全变了」，
  直接转发会把「某一层清空了」说成「所有设置都变了」。代价是：**外部绕过本类
  直接改某一层时，本类不会察觉**。因此约定三层存储只经本类读写。
- **`clear()` / `remove()` 只作用于写入目标层**。类型层是所有新会话共用的默认值，
  被一个会话的「清空」带走等于一次跨会话、不可逆的破坏性操作；
  另有 `clearLayer(scope)` 把范围写在函数名上。

### 1.12 FILT-005 落地到了什么程度

规格的五条完成标准对应到代码：

| 完成标准 | 落在哪里 | 状态 |
| --- | --- | --- |
| 三层叠加：文件格式定义内建过滤 → 会话设置过滤 → 视图临时过滤 | `FilterLayer`（顺序即叠加顺序）、`FilterStack::decide()`（层间取**交集**、层内排除优先）、`FilterLayerBinder::loadInto()`（**按存储分别读**三层） | **已落** |
| 每层可单独启用/禁用，并显示该层是否当前生效 | `setLayerEnabled()` / `isLayerEnabled()`、`FilterLayerState::active()`（= 启用**且**有规则）、`layerStatusText()`（四种状态各一句话） | **服务层已落**；「显示」的控件属设置页，因此**未勾** |
| 提供「查看最终生效过滤」面板，展示三层合并后的表达式与匹配计数 | `buildEffectiveFilterPanel()` → `EffectiveFilterPanel`（标题 / 层级语义 / 一行表达式 / 逐层行 / 计数 / 全文 `describe()`）；`FilterStack::combinedExpression()` | **数据 + 文本已落**；面板的控件宿主属设置页（OPT-*），因此**未勾** |
| 视图临时过滤不写入会话，关闭标签即丢弃 | `filterLayerStorage(View) == ViewMemory`、`FilterLayerBinder::saveLayer()` 按落点路由且**目标存储缺失时报失败、不退而写入另一个存储**、`discardViewLayer()`；视图存储**借用**（归视图所有，随视图一起消失） | **已落** |
| 层级语义有单元测试（每层的包含/排除关系） | `Tests/FilterStack` 的 B 组（20 个用例覆盖交集、层内排除优先、起决定作用的层、跨层排除清单） | **已落** |

**还没做的**（都只是「界面尚未接通」，不是逻辑缺失）：

1. **设置页的过滤 Tab 与 Filters 页**。三层声明的输入框、每层的启用开关、
   语法错误就地标红、实时预览共用同一个 `FilterStack`，但页面本身属 OPT-* /
   `FILT-*` 的界面批次。
2. **「查看最终生效过滤」面板**。内容现在是数据 + 纯文本，
   还没有控件宿主（同一批设置页）。
3. **格式层的真实来源**。`FilterLayer::Format` 的声明由
   `FilterLayerBinder::setBuiltinDeclaration()` 从外面给进来；文件格式定义（`FMT-*`）
   还没落地，因此这条目前只在测试与启动自检里被填过。格式层**永远不可写**
   （`canSaveLayer(Format)` 恒为假），这一点已经钉住了。
4. **各层声明的落盘**。会话层写进 `SessionSettings` 之后的文件格式是 SESS-008 的事；
   本条目只保证「写到哪个存储」。

**五条刻意的取舍**（改之前先读）：

- **层间取交集，不取并集**。并集会让「加一层过滤」使结果变多；用户不会怀疑是
  层间语义问题，只会认为过滤器坏了。
- **`FilterLayer` 与 `SettingScope` 是两套同形不同义的三层**，过滤必须**按存储分别读**，
  不能借 `ScopedSessionSettings::value()`。→ 会静默丢掉会话层的过滤。
- **`active()` 是「启用且有规则」**，不是「启用」。三层默认都启用，只按 `enabled` 判的话
  「某一层生效中」恒为真。
- **落点是数据，写入按它路由**。调用点没有「往另一个存储写」的入口，
  所以「临时过滤存成永久过滤」在结构上不可能发生。
- **层级表是唯一的事实来源 + 启动自检**。自检**把表当参数**，因此测试能拿一份
  故意写坏的表证明它真的会报（一条永远不会红的护栏比没有护栏更糟）。

### 1.13 FILT-003 落地到了什么程度

规格的五条完成标准对应到代码（模块在 `Services/Filter/attributefilter.{h,cpp}`）：

| 完成标准 | 落在哪里 | 状态 |
| --- | --- | --- |
| 按文件大小过滤（含单位与范围） | `SizeCondition`（`parseSizeText()` 认 1024 进制单位、`setRangeText()`、`accepts()`）、`EntryMetadata::withSize()` | **已落** |
| 按修改时间过滤（绝对区间 / 相对「最近 N 天」） | `TimeCondition`（`TimeRangeKind::{Absolute,Relative}`、`parseDateTimeText()`、`parseRelativeDaysText()`、`referenceTime()` 由调用方给、`accepts()`） | **已落** |
| 按文件属性过滤（只读 / 隐藏 / 系统 / 归档 / 所有者 / 组） | `AttributeBitsCondition`（`Requirement::{Ignore,Required,Forbidden}`）、`OwnerCondition`（`ListMatchMode::{AnyOf,NoneOf}`、按平台决定大小写） | **已落** |
| 属性条件与名称过滤是**与**关系 | `decideEntry()` 里 `accepted = nameAccepted && attributes.accepted`，并保留两侧的结论与起决定作用的那一侧 | **已落** |
| 属性过滤不读文件内容（扫描时能提前丢弃） | 模块内**没有任何读文件的代码**（输入是 `EntryMetadata`）。第 5 条的「扫描阶段提前丢弃」那半句要等 `Folder/` 扫描器接上，所以**这一条未勾**；能独立验证的那一半（「与内容比对解耦」）由 `Tests/AttributeFilter` 的 I 组**读源码的护栏**钉住 | **部分落**（服务层一半已落） |

**还没做的**：

1. **扫描阶段的提前丢弃**（第 5 条的后半句）。`Folder/` 目录扫描器还没落地，
   因此「在枚举时就用属性条件把条目丢掉、不进入后续比对」这条性能断言现在没有承接方。
   落地方式已定：扫描器拿到 `EntryMetadata` 后调 `decideEntry()`，`accepted == false`
   直接跳过；`undecided` 的条目照常进入，由界面在状态栏说明「部分结论不确定」。
2. **属性条件的界面输入**。声明的文本框、四类条件的控件、错误就地标红都属设置页
   （OPT-* / FILT-* 的界面批次）。服务层已经把 `AttributeFilter::parseDeclaration()`、
   `ConditionProblem`（带行号/列号/长度/建议）与 `toDeclarationText()` 准备好了。
3. **真实元数据的填充方**。`EntryMetadata` 现在由调用方构造；从 `QFileInfo` /
   平台 API 填 `hasSize`、`hasLastModified`、`attributeBits`、`knownAttributeBits`
   属于文件系统层与扫描器的活，本条目只定义契约。
4. **属性位与所有者/组在各平台上的可得性**。macOS/Linux 侧的信息源已确定，
   但 Windows 侧「归档位」之外还有一批属性位，得等目标平台真跑一次才能勾。

**五条刻意的取舍**（改之前先读）：

- **属性条件与名称过滤取「与」**，不是「或」。取或会让「只看 10 MB 以上的文件」
  把名称过滤排除掉的文件放回来；用户加过滤器的意图是更窄，他只会认为过滤器不可靠。
  同一条理由在 FILT-005 的「层间取交集」上重复出现。
- **元数据缺失走「放行 + 记不确定」**，不当作不满足。`ConditionOutcome` 同时带
  `accepted` 与 `evaluated` 两个字段就是这个意思；把「还不知道大小」当成「不满足」，
  条目会在元数据到达之前凭空消失，而用户不会想到去查过滤器的判定时机。
- **条件写错永远不缩小结果集，但一定被报出来**。`abc` 当大小不拦任何东西，
  同时给一条带位置与建议的问题；否则用户得到的是「过滤器生效了但结果不对」。
- **「现在」由调用方传入**（`TimeCondition::referenceTime()`），模块自己从不调
  `QDateTime::currentDateTime()`。否则「最近 7 天」的用例只能靠跑得快避免跨秒失败。
- **只填日期的上限算到当天最后一刻**（`2026-09-10` 含 9 月 10 日一整天），
  且用同一天的 `23:59:59` 构造而不是 `addSecs(86399)`——夏令时那天有 25 小时。

### 1.14 FILT-002 落地到了什么程度

规格的五条完成标准对应到代码（模块在 `Services/Filter/namefilter.{h,cpp}`）：

| 完成标准 | 落在哪里 | 状态 |
| --- | --- | --- |
| 支持精确名、通配符、以及正则三种匹配模式并可切换 | `NameMatchMode::{Exact,Wildcard,Regex}`、`NameFilterExpression::mode` / `setMode()`；`analyzeNameFilterLine()` 按**行首前缀**判模式（`=` 精确 / `*` 通配 / `~` 正则，无前缀按通配） | **已落** |
| 正则匹配有超时保护（默认 200ms/条），超时记录为错误条目并继续 | `NameMatchBudget`（`perEntryMs=200`、`consecutiveTimeoutLimit=2`）+ `NameMatchRunner` 抽象 + `ThreadNameMatchRunner`（工作线程 + 截止时间）+ `analyzeRegexPatternRisk()` 静态预检 + 连续超时断路器；超时**记成 `problems()` 里的 `NameFilterIssueKind::Timeout` 条目并继续**（该条结论为 `Undecided`，按放行处理） | **已落** |
| 支持「包含任一」「不包含任何」「全部满足」三种组合语义，且界面明确显示当前语义 | 语义已落：`NameCombineMode::{AnyOf,NoneOf,AllOf}`、`setCombineMode()`、`decide()` 的三态合成；文案也已备好（组合语义表里的「共用解释」）。**「界面明确显示当前语义」未接**——Filters 页还没挂上去 | **部分落**（服务层一半已落） |
| 过滤表达式的编辑有实时校验，非法正则就地报错且不生效 | `analyzeNameFilterLine(line, lineNumber, platform)` 返回 `NameFilterProblems`（带行号 / 列号 / 长度 / 严重度 / 建议）；有语法错、或静态风险预检命中的正则**不进入生效集合**（`disabledExpressionCount()` 可查）；它同时是解析 / 实时校验 / 复验的**唯一实现**（由读源码的护栏钉住，见 `Tests/NameFilter` 的 J 组） | **已落** |
| 表达式可命名保存为预设并导出 | `NameFilterPreset`、`setName()` / `name()`、`serializeNamedNameFilters()` / `parseNamedNameFilters()`——往返**逐字一致**，含大小写选项、组合语义与每条的启用状态 | **已落**（导出格式是文本；落盘/文件选择那一层属设置页批次） |

**还没做的**：

1. **界面的名称过滤框与「当前组合语义」显示**（第 3 条的界面半句，以及第 4 条的就地标红）。
   输入框、错误标红、组合语义的下拉与说明文字都属于界面批次（设置页 `OPT-*` /
   Filters 页）；服务层已把 `NameFilterProblems`（带行号/列号/长度/严重度）与
   `toDeclarationText()` 准备好了。
2. **预设的界面保存 / 导入导出入口**。数据层已经能命名、序列化、往返（`NameFilterPreset` +
   `serializeNamedNameFilters()` / `parseNamedNameFilters()`），缺的只是「保存为预设」与
   「导出到文件」两个动作的宿主控件；`QFileDialog` 那一层不属本模块。
3. **扫描阶段真正用上名称预筛**。与 FILT-003 同理，`Folder/` 扫描器未落地，
   「先用名称把条目丢掉、不进入内容比对」这条性能断言现在没有承接方。
   落地方式同上：扫描器拿到名字后调 `decide()`，`NotMatched` 跳过、`Undecided` 照常进入。
4. **运行期超时的平台侧验证**。真线程用例（E 组）在本机跑过，但「一台被别的进程压满的机器上
   200ms 这个默认值够不够」需要在目标平台真跑一次性能基线才能定；现在这个值是可配的。

**五条刻意的取舍**（改之前先读）：

- **模式是每条表达式自己的，不是整个过滤器一个全局开关**。一个过滤器里同时写
  `*.txt`（通配）和 `~(a+)+$`（正则）是常态；全局开关会逼用户把过滤器拆成两个。
  前写入 `namefilter.h` 顶部。
- **只有正则走超时保护，精确与通配不欠**。精确是 `QString` 比较，通配是线性回溯可控的
  手写匹配；给它们也套线程只会平白多一次线程往返。理由写在 `NameMatchBudget` 的注释里。
- **超时一律放行，并且必须被报出来**（`NameMatchOutcome::Undecided` + `NameFilterIssueKind::Timeout`）。
  这与 FILT-003 的 `ConditionOutcome` 同源：把「不知道」当成「不满足」，用户会看到文件
  凭空消失而查不到原因；放行 + 状态栏说明才是可理解的失败方式。
- **超时后丢弃整个工作线程换新的**，不用 `QThread::terminate()`。Qt 5.15 没有 API 能安全地
  中止一个正在跑 PCRE2 的线程；`terminate()` 会让互斥量与 `QMutex` 停在未定义状态。
  丢弃（`deleteLater`）+ 重建是唯一不留隐患的走法，代价是每个被放弃的线程泄漏一次
  线程栈——但达到断路器（默认连 2 次）就停止再派活，不会持续泄漏。
- **Qt 5.15 的现实**：`QRegularExpression::setMatchTimeout()` / `matchTimeout()` 是 **Qt 6.0**
  才有的。本仓既然钉在 5.15，超时只能自己做（已在
  `~/Qt/5.15.2/clang_64/lib/QtCore.framework/Versions/5/Headers/qregularexpression.h`
  逐字核对过没有这两个成员）。直调 PCRE2 match limit 需要把第三方库带进交付物，
  与「一个 exe 分发」相冲，因此不采纳；相关理由与取舍已同时写进 `architecture.md` §4。

### 1.15 FILT-004 落地到了什么程度

规格的五条完成标准对应到代码（模块在 `Services/Filter/contentfilter.{h,cpp}`，
是本模块里**唯一读文件内容**的一段）：

| 完成标准 | 落在哪里 | 状态 |
| --- | --- | --- |
| 支持「行过滤器」：排除匹配指定模式的行后再比对（如日志时间戳行） | `LineFilter::parseDeclaration()`（一行一条表达式、`#` 注释、`\n`/`\r\n`/`\r` 都认）、三种模式 `LineFilterMode::{Exact,Wildcard,Regex}` 由**行首前缀**决定（`= ` 精确 / 无前缀 通配 / `re:` 正则）、`LineFilter::excludes(rawLine)` 判单行、`matchingPatternIndexes()` 给命中计数、`LineFilterResult` 把留下的行与被丢掉的行（连同行号）一起交回 | **已落**（服务层） |
| 支持「关键字节过滤」：仅当二进制文件包含指定字节序列时才纳入比较 | `KeyByteFilter::parseDeclaration()`、`KeyByteCombineMode::{AnyOf,AllOf}`、`KeyByteFilter::decide(data, kind)` 返回**三态** `KeyByteOutcome::{Accepted,Rejected,NotApplicable}`；`\xHH` 与 `\n \r \t \0 \\` 的解码/编码（`decodeByteSequenceText()` / `encodeByteSequenceText()`）是往返逐字节一致的纯函数 | **已落**（服务层） |
| 内容过滤与忽略规则的作用顺序明确（先过滤行再应用忽略规则）且有测试 | 顺序表 `contentFilterStageTable()`（两段：行过滤 → 忽略规则，含标识符与说明）+ `contentFilterStageOrder()` / `contentFilterStageIndex()` / 自检；`LineFilter::excludes()` 的输入**只接受原始行**，因此「先过滤」在类型上就成立。测试用**真的** `Text::compare` 钉住两条路径的差别 | **部分落**：顺序已明确且被测试；把它接进比对引擎（`Services/Text` 侧按此顺序调用）**未做** |
| 启用内容过滤时状态栏提示「内容过滤已启用，比较速度会降低」 | 文案已落：`contentFilterEnablementNotice()` 对「未启用 / 已启用但没规则 / 已启用且有规则」三种状态各给一句不同的话，`contentFilterPerformanceNotice()` 是那句性能提示 | **部分落**：文案已落，**状态栏未接** |
| 内容过滤条件可保存为文件格式定义的一部分 | 键名 `lineFilterDeclarationKey()`（`"line-filter"`）、`keyByteFilterDeclarationKey()`（`"key-byte-filter"`）、`contentFilterEnabledKey()`（`"content-filter-enabled"`）已定；`toDeclarationText()` 是各解析器的逆（往返逐字一致，可直接写回设置项） | **部分落**：键名与往返已落，**宿主也已存在**（`FMT-001` 的 `FormatDefinition::settings` 就是原样往返的不透明袋，见 §1.19——本文档此前在**这一行**写「文件格式定义模块未落地」是错的，并传染到了 §1.15 与 §4）。真正缺的是**写入入口**：Filters 页得先有「把当前内容过滤存成格式定义」这个动作 |

**还没做的**：

1. **把「先过滤行、再应用忽略规则」接进比对引擎**（第 3 条剩下的那一半）。
   顺序已经定死在 `contentFilterStageTable()` 里、`LineFilter` 的入参也已经是原始行，
   缺的是 `Services/Text` 的比对入口在调用忽略规则**之前**先过一遍 `LineFilter`。
   本轮的取舍是**不去改 `Services/Text`**（那会把 FILT-004 变成一次跨模块改动，
   而本条目的其余部分还等着同一个位置的另外两处宿主），因此只在服务层把顺序与
   证据准备好：测试里用真的 `Text::compare` 分别跑「先过滤再忽略空白」与
   「只忽略空白」，前者差异为零、后者差异仍在。
2. **状态栏那一行提示**（第 4 条）。住所是主窗口的状态栏（`MainWindow::setupStatusBar()`）
   与 Filters 页，两者都要等 `OPT-*` 的界面批次；文案已经备好。
3. **把两个声明键写进文件格式定义**（第 5 条）。`FILT` 系列的「格式层」目前
   由 `filterstack` 的 `setBuiltinDeclaration()` 承载，而「文件格式定义」是
   `Format/` 模块——**它已经落地了**（`FMT-001`，见 §1.19；本文档此前在这几处
   误写成「尚未落地」，已被证伪）。键名与文本往返都已经定好，`FormatDefinition::settings`
   是原样保存与往返的不透明袋，所以这一步**只差把两个键挂上去**，不需要动 `Format/` 的模型。
   真正挡着它的是 Filters 页（`OPT-*` 界面批次），不是格式定义。
4. **关键字节过滤在真实文件上的样本**。判定本身已被 85 个用例覆盖（含 NUL 字节、
   代理对、`\xHH` 与词法转义混写），但「拿一个真的二进制文件（PNG / PDF）跑一遍」
   要等我们把内容读进来的那一层（同第 1 条的位置）。

**六条刻意的取舍**（改之前先读，前两条写在 `contentfilter.h` 顶部）：

- **内容过滤默认不启用，而且「配了规则」不等于「启用」**。它是本模块里唯一
  代价与文件大小同阶的一段（要把内容读进来）。`ContentFilterEnablement::active()`
  是「启用**且**有规则」，与 FILT-005 的 `FilterLayerState::active()` 同形。
- **行过滤器的输入是原始行**，不是在忽略规则处理之后的行。反过来做会让参与比较的
  行集随忽略规则变化——同一条「排除时间戳行」的规则在「忽略空白」开着时命中、
  关掉就不命中，而用户认为这两件事毫不相干。
- **内容过滤的正则是子串匹配**（grep 语义），而名称过滤器的正则是整名匹配。
  两侧问的问题不同：名称侧「这个名字符不符合一条命名规则」，
  内容侧「这一行里有没有那一段」。刻意不一致，理由写在模块头。
- **行首前缀要跳过行首缩进**，与 `namefilter.h` 刻意不一致。内容过滤的声明常常是
  从日志里抄下来的一行（带着缩进），不跳缩进会让 `  =  ...` 静默退化成通配模式：
  它照样解析成功、照样有一条表达式，只是永远不命中。
- **只有通配模式命中不了空行**（掩码要求名字非空），`re:^$` 是唯一能表达
  「去掉空行」的写法。曾经有一版在入口处对空行提前返回，把精确与正则也一起挡掉了。
- **说不清的结论一律放行并报出来**：文本条目遇到关键字节规则给 `NotApplicable`
  而不是 `Rejected`；一条规则都没写时给 `Accepted` 且不给理由（空规则集不构成约束）。
   与 FILT-002 / FILT-003 的三态结论同源。

### 1.16 PLAT-006 单实例与进程间通信落地到了什么程度

代码在 `Code/Services/Platform/instanceprotocol.{h,cpp}`（纯 QtCore 的数据与判定）
与 `singleinstance.{h,cpp}`（动作，需要 QtNetwork），测试在 `Tests/SingleInstance`
（103 个用例函数 / QTest 合计 105，十二组 A~L）。

**四条完成标准已勾、第 3 条只勾得动一半**，逐条对应：

| 完成标准 | 落地情况 |
| --- | --- |
| 1. 跨平台单实例机制（共享内存 + 本地套接字 / QLocalServer） | **已勾**。无一行平台 `#ifdef`：选主走 `QLockFile`、标识走 `QSharedMemory`、转交走 `QLocalServer`/`QLocalSocket`。**但有一条环境限制**：本机与本次执行环境里共享内存不可用，实际跑的是「进程锁 + 本地套接字」，见下面 |
| 2. 第二个实例把参数转发给首个实例并退出，退出码反映转发结果 | **已勾**。`RelayStatus` 五种结局 → `relayExitCodeTable()` → 退出码；转交与应答在**一个总截止时间**内完成，应答里带回首个实例实际解析出的参数个数供比对 |
| 3. 收到参数后首个实例创建会话并把窗口置前（不抢焦点…可配置） | **只勾一半，保持 `[ ]`**。「创建会话」已由 `main.cpp` 的 `relayReceived` → `MainWindow::openRequest()` 接上；「窗口置前」由 `activationRequested` 的结论驱动。缺的是**「可配置」**：`ActivationPolicy`（`Always` / `UnlessTypingRecently` / `Never`）与「距上次输入多久」的注入点在服务层齐备并有 6 条用例，但**没有任何设置项或命令行开关把策略暴露给用户**，生产路径只会用默认的 `Always` |
| 4. 首个实例无响应时（超时）第二个实例自行启动并记录日志 | **已勾**。默认 `RelayFallback::StartNewInstance` → `InstanceRole::Degraded`，并且 `m_report.detail` 在**成功路径上也非空**（「什么都没打印」会让人分不清「降级了」与「日志级别把它挡住了」），`main.cpp` 用 `LQCOMPARE_INFO("instance", …)` 落日志 |
| 5. 单实例行为可由选项关闭（见 OPT-002） | **已勾**。设置键 `general.singleInstance` 已在 `OptionsRepository::definitions()` 里登记（「复用已有实例」，说明里写明「下次启动生效」），`OptionsDialog::applyChanges()` 对它的改动会给出「单实例行为将在下次启动时生效」的状态文案，`main.cpp` 读它决定 `setEnabled()`；另有命令行开关表 `singleInstanceSwitches()`（`--single-instance` / `--no-single-instance`，表尾胜出）与 `--new-instance` / `--wait` |

**为什么第 3 条的「可配置」不做成硬做**：给 `ActivationPolicy` 加一个设置键是小事，
但它的宿主是「比较与界面」那一组选项（OPT-003 / OPT-007），而那一组现在还没有落地；
在 `general` 下另起一个键会让后续的选项分组与规格对不上。因此按 §4.1 的纪律
把它留空，并在这里写明依赖。

**按存储分别读的三层教训不适用于本模块**：这里的三层是「进程锁 / 共享内存 / 端点」，
它们都住在系统里，没有本仓的设置覆盖链。

**环境限制（接手必读，否则会误判成没写）**：`QSharedMemory` 在本机**跑不起来**。
实测：裸 `shmget()` 返回 `ENOMEM`（errno 12）、Qt 报 `OutOfResources`，
因此 `takePrimaryIdentifier()` 里 `memory->error() == AlreadyExists` 这个分支
**从不进入**，而 `recoverStaleIdentifier()` 只在该分支里被调用 → **它在本机不可达**，
「崩溃遗留标识被回收」的三条用例在本机是「跳过式通过」（`crash-primary` 子进程
`_exit()` 之后本机根本没有遗留段可回收）。生产日志如实报告了这一点：

```
[instance] 共享内存不可用（QSharedMemory::create: out of resources），
已由进程锁保证单实例；本进程是首个实例，已在端点 lqcompare-loren-org.lqcompare.d-4102bf82 上等待转交
```

因此：**「共享内存标识 + 崩溃遗留回收」这半条腿写好了、编译进了产物、但没有在本机执行过**，
与 Windows 薄层的处境同类。勾第 1 条的理由是「共享内存 / 本地套接字」在本条里是可替代的
两条实现路径，而套接字那条已被真子进程端到端验证；共享内存那条**未验证**这件事
在这里、`architecture.md` §3.5 与 issue #327 的落地说明里都写明了。

### 1.17 OPT-005 文件操作选项落地到了什么程度

策略与判定在 `Code/Services/Files/fileopsoptions.{h,cpp}`（纯 QtCore，不碰文件系统），
设置项登记在 `Code/Services/Settings/optionsrepository.cpp`（8 个 `fileops.*` 键），
界面在 `Code/Views/Options/optionsdialog.{h,cpp}`（`fileops` 分类页 + 常驻的安全提示行），
测试在 `Tests/FileOpsOptions`（92 个用例函数，九组 A~I）。

**五条完成标准全部勾上**，逐条对应：

| 完成标准 | 落地情况 |
| --- | --- |
| 1. 删除方式默认回收站 / 永久删除，并提示永久删除不可恢复 | **已勾**。设置键 `fileops.deleteMode` 出厂 `trash`；`deleteWarning()` 只在选到永久删除时给出「不可恢复」文案，界面上是随草稿刷新的提示行；自检会把「默认被改成永久删除」报成违规 |
| 2. 覆盖策略默认询问 / 覆盖 / 跳过，并区分「文件较新」的特殊提示 | **已勾**。`fileops.overwritePolicy` 出厂 `ask`；`OverwriteSituation` 四态（目标缺失 / 较旧 / 同时 / 较新）→ `overwriteDecision()` 给结论**并带上该情形专属的提示文案**；「目标缺失」这一态**不问**（见下） |
| 3. 复制时默认保留的元数据项（时间戳 / 属性 / 权限） | **已勾**。三个**互相独立**的布尔（`fileops.preserveTimestamps` / `preserveAttributes` / `preservePermissions`，出厂全为真）。刻意不做成枚举：枚举会把「只留时间戳」这类组合排除掉，而三者的失败后果各不相同（丢时间戳影响增量比对、丢属性影响只读位、丢权限影响可执行位） |
| 4. 大文件体积确认阈值（默认 100MB）与批量删除条数阈值（默认 20） | **已勾**。`fileops.largeFileConfirmMegabytes`（100，单位后缀 `" MB"`）与 `fileops.batchDeleteConfirmCount`（20，单位 `" 个"`）；`needsLargeFileConfirmation()` 把 MB 换算成字节后比较，**填 0 表示关掉该确认**；两个出厂值都以 `constexpr` 常量给出，自检比对的是常量而不是又抄一遍数字 |
| 5. 操作后可选的校验方式（无 / 大小 / CRC） | **已勾**。`fileops.verifyAfterCopy` 出厂 `none`；`VerifyMode` 三态，并且「枚举 → 中文标签」只有服务层一张表，选项页下拉的文本直接取它的 `*Label()`，不存在界面与逻辑各写一份文字 |

**为什么把「默认值必须保守」写成可执行的契约**：issue 的边界一栏写的是「安全相关默认值
必须保守（默认走回收站、默认不覆盖）」。这句话如果只停在注释里，就只能靠 review 用眼睛盯。
因此它被写成 `safetyContractViolations()`——一个**返回违规清单**的函数，于是「有人把默认
改成永久删除 / 直接覆盖」在测试里是红的，而且它也是本模块唯一一处「自我否定」式判定。

**为什么「目标缺失」不询问**：覆盖判定在目标不存在时直接返回 `Overwrite` 且**不给任何提示**。
把这一态也算成「需要询问」会让覆盖提示在最常见的情形（新文件落进空目录）里刷屏，
真正需要人看的「目标较新」反而被淹没。这条反直觉的选择在 `architecture.md` §4 里有一行。

**界面上的警告是动态算出来的**：`OptionsDialog::updateFileOpsHint()` 在草稿变化时用
`FileOperationPolicy::fromValues(m_draft)` 重新解释当前选择，把违规与提醒列成多行文案，
而不是写死一句。于是「界面警告」与「服务层判定」不可能分家——它们读的是同一个函数。

**未做（有意留空）**：本模块只回答**「该怎么做」**，不执行任何文件操作——真正的复制 / 移动 /
删除动作属 ENG-* 序列，落地时来读这份策略即可。本模块也**不读文件系统**（有源码级护栏：
`moduleNeverTouchesTheFileSystem` + `sourceGuardWouldCatchAnInjectedRead` 成对出现），
与 FILT-004 同一手法：先把「决定」和「执行」切开。

### 1.18 OPT-010 日志与诊断选项落地到了什么程度

三个服务层文件、一组设置键、一页界面、一套 89 个用例函数的新套件：

| 落点 | 文件 |
| --- | --- |
| 滚动与清空（服务层） | `Code/Services/Log/logfiles.{h,cpp}` |
| 诊断包与环境报告（服务层） | `Code/Services/Log/diagnostics.{h,cpp}` |
| 写路径上的滚动检查、性能计时开关 | `Code/Services/Log/logging.{h,cpp}`（改） |
| 四个 `logging.*` 设置键 | `Code/Services/Settings/optionsrepository.cpp`（改） |
| 设置生效（推到日志模块） | `Code/Views/Options/optionsruntime.cpp`（改） |
| 两个按钮 + 告知弹窗 | `Code/Views/Options/optionsdialog.{h,cpp}`（改） |
| 测试 | `Code/Tests/LogDiagnostics/`（新，89 个用例函数） |

**五条完成标准全部勾上**，逐条对应（用的是 issue #365 正文里的原话）：

| 完成标准 | 落地情况 |
| --- | --- |
| 1. 日志级别（错误/警告/信息/调试）与日志文件位置可配置 | **已勾**（这一条在 ENG-006 那一轮就已落地，本轮没有改动）。`logging.level`（出厂 `warning`，取值与标签直接来自 `Log::levelChoices()` / `levelLabel()`）、`logging.fileEnabled`、`logging.filePath`（非绝对路径会被校验拒掉；对话框里配「浏览…」按钮，空值提示语是「配置目录下 logs/lqcompare.log」）。界面上的控件由设置声明表自动生成，**没有第二份默认值** |
| 2. 日志文件轮转策略（按大小或按天） | **已勾**。`logging.rotationMode`（出厂 `none`，另两值 `size` / `daily`）+ `logging.rotationMaximumMegabytes`（出厂 5，1~1024，单位 `" MB"`）+ `logging.rotationKeepFiles`（出厂 5，0~100，单位 `" 份"`）；范围常量由**服务层**给出（`minimumRotationMaximumMegabytes()` 等），设置页不自己抄一遍。**检查在写路径上**（`appendRecord()` 里，`QElapsedTimer` 限流到每秒至多一次），所以「日志涨到上限」不需要用户去点一次设置。判据是**纯函数** `rotationDecision(policy, currentSize, fileDate, now)`——时间从外面注入，因此「跨天」这条分支能用假时间测，不需要等一天 |
| 3. 提供「打开日志目录」「清空日志」「导出诊断包（日志+版本+环境信息）」 | **已勾**。日志页三个按钮：`optionsOpenLogDirectory`（打开当前日志目录，走系统文件管理器；未启用文件日志时提示而不是打开一个不存在的目录）、`optionsClearLog`（清空日志文件）、`optionsExportDiagnostics`（导出诊断包…）。诊断包内含 `logs/`（当前日志 + 全部滚动历史）、`environment.txt`（应用名与版本 / Qt 版本 / 系统与架构 / 存储模式与目录 / 当前日志级别 / 滚动设置摘要 / 日志文件路径 / 导出时间）、`manifest.json`（文件清单 + 脱敏次数 + 是否含日志）。目录名冲突时退让成 `-2`、`-3` |
| 4. 提供「记录详细性能计时」开关，用于排查慢操作 | **已勾**。`logging.performanceTiming`（出厂 `false`）。关着时 `Stopwatch` 的耗时记录走正常的级别过滤；开着时**绕过级别过滤**但**保留计时器自己那条记录的级别**。开关在**析构时**读取（与级别同一处判断），所以「先放计时器、再开开关」这种排查顺序也能出结果 |
| 5. 诊断包导出前提示会包含路径信息，并允许脱敏 | **已勾**。导出前先弹一个确认框（`optionsDiagnosticNotice` + `optionsDiagnosticRedact`），文案随勾选**实时变化**且**取自服务层** `Log::diagnosticNoticeText(bool)`（两段刻意不同的文字：脱敏版说「已把家目录与配置目录替换成 `~` / `<配置目录>`」，不脱敏版明说「包内会包含你的家目录与配置目录的真实路径」）。前缀表 `defaultRedactionRules(home, storageDir)` 按**最长前缀优先**排序（否则 `/Users/loren/.qcompare` 会先被 `/Users/loren` 吃掉，剩下的半截反而不像路径）。结果里带 `redactedOccurrences`，清单里也记着 |

**「先告知再导出」的顺序是刻意的**：确认框在选目录**之前**弹出。反过来实现的话，
用户是在文件已经写出去之后才知道包里有路径——那时候「允许脱敏」就只是个安慰。

**为什么滚动检查必须在写路径上、而且写在写这一行之前**：只在「应用设置」时检查，
等于把「日志文件会涨到 500 MB」交给用户记性——而用户恰恰是被日志占满磁盘才来改这项设置的。
顺序也重要：先写这一行再检查的话，那条把文件顶过上限的记录永远留在旧文件里，
滚动之后立刻又超限，于是「上限」变成一个永远达不到的实现细节。

**为什么写路径上的滚动失败被吞掉**：`appendRecord()` 持着非递归互斥量，在里面再记一条
「滚动失败」会**死锁**，而现象是「程序卡住」，与日志模块看起来毫无关系。所以那里只尽力而为；
需要错误的地方（设置页的应用动作）走显式的 `Log::rotateIfNeeded()` 拿 `RotationDecision` 与错误串。
**日志系统的失败不该把用户的工作一起带走**——这是本模块唯一一处刻意不报错的判断。

**为什么清空是截断而不是删除**：删除之后日志系统手里那个 `QFile` 仍指向已被 unlink 的
inode，后续写入落在一个「谁也不认识的文件」上：磁盘不涨、文件也永远不出现，
用户看到的是「日志功能坏了」。截断保住了 inode 与打开的文件句柄。

**为什么脱敏的边界朝「多替换」一侧偏**：判据是「路径后面不能是名字的延续字符
（`_ - .` 或字母数字）」，而不是「后面必须是分隔符」。后者会漏掉 `/Users/loren `（后面是空格）
这类真实路径——那是一次隐私泄露，发出去的包里躺着用户的家目录；
前者多替换的顶多是 `/Users/lorenx` 这种恰好以家目录名开头的路径，只是看起来有点怪。
**两种错误的代价不对等**，所以规则朝不泄露的一侧偏。

**为什么枚举滚动历史不用 `QDir` 的名字通配**：日志文件名是用户可配的。
`entryList(name + ".*")` 在名字含 `[` `*` `?` 时既会**漏掉**真正的历史
（`a[1].log.1`），又会**误收**无关文件（`a1.log.12345`）。改成列全部文件、
按前缀 `startsWith` + 整数后缀解析来判断。这条是**测试先红、再去修实现**发现的。

**未做（有意留空）**：诊断包里不额外收集系统信息（不跑 `system_profiler`、
不读注册表、不抓控件树）——那会让「导出一个包」变成一件慢且有副作用的事；
环境报告里只有进程自己就能答出来的东西。也不做「自动上报」：包只落在用户选的目录里。

### 1.19 FMT-001 文件格式定义模型与存储：其实早已落地

本轮**没有新增生产代码**，做的是「核对 issue 与源码，发现它已经做完了」。
实现在 `Code/Services/Format/`，交付记录在 `docs/development/team-format.md`
（夜间那批工作流的产出之一），测试在 `Code/Tests/Format`。

**五条完成标准逐条对应**：

| 完成标准 | 落地情况 |
| --- | --- |
| 1. 格式定义字段：名称、掩码列表、默认视图类型、编码与行尾策略、语法定义、转换规则、行过滤器、重要性规则、列/字段定义、杂项选项 | **已勾**。前四类是一等字段（`name` / `masks` / `sessionTypeId` / `signatures`），其余全部落在 `settings` 这个**不透明 JSON 袋**里原样保存与往返。这是刻意的设计决定（见 `architecture.md` §4）：在没有任何消费者的情况下替每一类定死 schema，只会与第一个真正来读它们的语法高亮引擎 / 格式管理器互相妥协。关键约束是**解析它不执行任何程序**——有一条带 `externalConverter` 字段的往返用例钉住这点 |
| 2. 定义以文本格式存储（便于 diff 与版本管理），支持继承/覆盖内置定义 | **已勾**。version=1 的格式化 JSON（`serializeDefinitions` 输出缩进文本），支持 `baseId` 继承**与**同 ID 继承；省略字段继承、`settings` 按键合并、导出的是解析后的完整值 |
| 3. 用户定义与内置定义分离存放，升级不覆盖用户修改 | **已勾**。`builtIn` 标记 + `mergeDefinitions(builtIns, user, priorityIds)`：相同 ID 的用户项覆盖内置，**且从不修改传入的内置表**（有 `mergePriorityDoesNotMutateBuiltIns()` 钉住）。用户与内置由调用方分开保存，本模块不替调用方决定落盘位置 |
| 4. 定义有唯一稳定的 ID，重命名不影响关联 | **已勾**。ID 必须是稳定的小写短横线标识 `^[a-z0-9][a-z0-9-]*$`；改名走「同 ID + 新 name」，覆盖与继承因此都不断链。**本轮补上了此前零覆盖的两条用例**：重复 ID 只保留第一个并报诊断、非法 ID 逐条拒绝 |
| 5. 定义文件损坏时跳过该定义并报错，不影响其它定义加载 | **已勾**。**条目级**错误（ID 非法、掩码编译不过、签名越界、继承来源不存在、`baseId` 不是字符串）只跳过那一条并报出「第几个 / 哪个 ID / 为什么」；**文档级**错误（不是 JSON、版本不认识、没有 `definitions` 数组）整份拒绝 |

**本轮为此补的测试（唯一的代码改动）**：

- `duplicateIdsAreRejectedWithADiagnostic()`——同一份文件里两个同 ID 的定义，
  只保留**第一个**（先到先得，不是后者覆盖前者），第二条被跳过并报「格式 ID 重复」。
- `idFormatRulesRejectUnstableIdentifiers()`——大写、空格、空串、以 `-` 开头、含 `.`
  五种 ID 一律拒绝，且**每一条都单独报出「格式 ID 无效」**（不是只报第一条就停）。
- 反向验证 **5 处变异 5 处检出、0 处漏检**（见 §2）。

**明确没做的事（都不在 FMT-001 的完成标准里）**：`team-format.md` 结尾列的那一串——
格式管理器 UI（属 `FMT-002` / #360）、语法高亮引擎、格式转换执行、归档解压器、
图片/表格语义执行、关联覆盖的单独规则文件导入界面、应用级接线。**不要**把「该模块的后续条目没做」
读成「该模块没做」——本轮之前本文档就是这么错的。

**平台限制**：本轮未在 Windows 上构建（本机没有该环境）。FMT-001 的五条标准都不含平台相关
行为，平台限定的判定由 macOS 服务测试覆盖；原生 Windows 文件访问属识别侧，需目标平台验收。

**这条错话为什么会传染**：本文档至少四处写「文件格式定义未落地」（§1.15 FILT-004 第 5 条、
§4.0 的内容过滤三件事、§4.1 的 FILT-004 行与 FILT-005 行），而 `team-format.md` 一直躺在那儿
说它落了。错误写法被后面几轮**复制**，于是看起来像是「多处独立佐证」。
**接手时的做法**：看到「某模块未落地」时，先 `ls Code/Services/<模块>/` 与
`Code/Tests/<模块>/`，再看 `docs/development/team-*.md`——夜间那批工作流有 24 路，
本文档对它们的覆盖面并不完整。

### 1.20 ENG-004 持续集成流水线：修到了什么程度

**这条路以前一次都没通过，而且不是「红在测试上」，是红在测试之前。** 见 §1.0.6 的日志。
本轮的改动分三块。

**一、流水线阶段（完成标准 1、3、4、5）**

| 阶段 | 落地 | 拿不到 LqRibbon 时 |
| --- | --- | --- |
| 代码风格检查 | `checks` 作业：五道静态护栏（分层 / 图标 / 规格 / shell 可移植性 / 宽字符 API，含自测） | 照常跑（不依赖任何私有物） |
| 构建 | `构建主程序` 步骤：`qmake Code/LqCompare.pro` + `make -j$(getconf _NPROCESSORS_ONLN)` | **跳过**（用 `::notice` 显式说一声，不是静默） |
| 测试 | `运行测试套件`：`bash Code/Tests/run-tests.sh` | 照常跑，但排除 2 个依赖 LqRibbon 的套件 |
| 打包产物 | `打包主程序`：Linux 出 `LqCompare-linux-x64.tar.gz`、macOS 出 `LqCompare-macos-x64.zip`（`zip -y` 保符号链接）、Windows 出 zip | **跳过**（宁可跳过，也不产一个没有可执行文件的空包） |

上传的构建产物有两类，都是 `always()`/显式条件：**测试日志**（每个套件的纯文本 + JUnit XML）、
**可执行文件**（主程序包；以及 66 个测试套件二进制——没有 token 时 CI 唯一能编出来的可执行文件
就是它们，而这正是「下载下来手工重跑一遍」最需要的东西）。

**二、平台矩阵（完成标准 2）**：`ubuntu-latest` / `macos-15-intel` / `windows-latest` 三条腿，
`fail-fast: false`（一条腿红不能吃掉另一条腿的信息）。

- **macOS 必须用 `macos-15-intel`，不能用 `macos-latest`**：本项目 Qt 基线 5.15.2 只有
  macOS x86_64（clang_64）官方包，而 GitHub 托管的 arm64 运行器**没有预装 Rosetta 2**，
  x86_64 的 qmake 在上面跑不起来。`macos-13`（原来的 Intel 标签）已于 **2025-12-04 退役**，
  用了会直接失败。`macos-15-intel` 可用到 2027-08，**之后再没有 x86_64 托管运行器**，
  届时要整体迁 arm64 + 自建 Qt。
- **Windows 用 MinGW 8.1.0 32 位**（`arch: win32_mingw81` + `tools: tools_mingw,qt.tools.win32_mingw810`），
  与 `Code/LqCompare.pro` 头注释里的交付目标同一套工具链。
- **`defaults.run.shell: bash`**：Windows 上 `run:` 默认是 pwsh，而每一步都是 bash 语法
  （`case` / `[ -n ]` / `>> "$GITHUB_OUTPUT"`），不显式声明的话三条腿只会剩两条。
- 作业级 `timeout-minutes: 120`：不给超时的话一个卡住的测试会占着额度挂到 6 小时上限。

**三、`run-tests.sh` 的七项可移植性修复（都是「写的时候以为跨平台」；前四项来自写代码时的自查，后三项是第一次真实 CI 跑完之后才暴露出来的）**

1. **输出格式**：`-o results.xml,xml` 改成 `junitxml`。Qt 的 `xml` 是它自己的私有格式
   （根节点 `<TestCase>`），任何 CI 的测试报告解析器都不认；`junitxml` 才产出
   `<testsuite failures=... tests=...>`，失败用例清单才能被 CI 直接消费。
2. **`make` 探测**：Windows（Git Bash）上没有 `make`，MinGW 装的是 `mingw32-make`。
   按 `mingw32-make` → `make` → `gmake` 顺序探测，找不到就报「找不到 make/mingw32-make」
   并以 2 退出。**不这么做的话现场是**每个套件都「构建失败」，而真实报错
   （`make: command not found`）被 `>/dev/null 2>&1` 吞掉，看起来像代码编不过。
   `MAKE` 同时是 make 的内建变量，显式传入的值仍被尊重（用于覆盖）。
3. **`.exe` 后缀**：Windows 上可执行文件带 `.exe`，而 `[[ -x foo ]]` **不会**自动补后缀。
   显式试一次 `${binary}.exe`，不让它一路掉到 `find -perm -u+x` 兜底。
4. **可选依赖缺失时的显式排除**：新增 `LQCOMPARE_TEST_SKIP`（空格分隔的子串，匹配规则与
   位置参数一致）。它**刻意不做成静默**——开头打一行排除清单，末尾把「全部套件通过」换成
   「通过（已排除 N 个套件、未验证）：…」，全部被排除时仍以退出码 2 报「一个测试都没跑」。
   理由是「排除掉」与「静默跳过」在日志上只差一句话，却会把「64 个已验证」读成「全部都验证过」。
5. **不再用 `-o -,txt` 拿 stdout**（第一次 CI 跑出来的问题）：改成只写文件、脚本再 `cat` 回来。
   Windows 上 `-o -,txt` 一行都不输出而文件正常，于是日志里没有 `Totals:`、合计为 0。
6. **崩溃的套件要在合计之外单独点名**：不写 `Totals:` 的套件一条用例都不计入合计，
   于是「34 个套件红了」与「合计 26 failed」会看起来像矛盾。
7. **构建输出落盘**（`<套件>/build.log`），失败时打末尾 20/30 行，并加进上传产物。
   以前丢 `>/dev/null`，CI 上 17/26 个套件构建失败而**一个字的原因都没有**。

**反向验证：10 处变异 10 处检出、0 处漏检**（4 处打在 make 探测 / `.exe` / MAKE 覆盖上，
4 处打在排除机制上：忽略环境变量、命中不置位、边界判断失效、汇总掩盖排除，
2 处打在本轮新增的回读逻辑上：不删旧产物会让崩溃被上一轮的 `results.txt` 掩盖、
不回读产物会让合计归零——后者正是 Windows 腿第一次跑出来的现场）。
另有 8 项**行为断言**（崩溃点名、旧产物不被采信、合计不被伪造、build.log 落盘且带真实原因）。
驱动只在 `/tmp`，就地变异 + `finally` 无条件还原 + sha256 校验源文件。

**第二批改动的本机验证数据**：跑的就是改完的 `run-tests.sh`——
**3598 passed / 0 failed / 2 skipped、66 个套件全绿、退出码 0、3 分 16 秒**；
新机制的表现与预期一致：66 个套件各写出一份 `build.log`（构建全部成功），
「没有产出统计行」那一行**一次都没出现**（没有套件崩，分支没有误报），
2 条跳过仍来自 `PathName`（40/0/1）与 `Registry`（61/0/1），与上一轮一致。
五道护栏全过（winapi 扫 332 个源文件、spec 校验 369 条含 P0 59、icons 32 个、shell 16 个脚本、
layering 通过）。本轮**不碰任何 C++ 代码**，所以功能数字与上一轮相同是预期的，不是「没测」。

**CI 第一次真实运行的结果（run `35546164218`，2026-09-21 08:0x）**：流水线**跑完了四个阶段**，
三条腿都产出了测试日志与可执行产物（6 个 artifact）。这一步本身就是这次修复要的东西——
以前它红在第一跳，什么信息都没有。三条腿的实际结果：

| 腿 | 用时 | 合计 | 失败套件 | 失败类型 |
| --- | --- | --- | --- | --- |
| ubuntu-latest | 8m20s | 2473 passed / 18 failed / 1 skipped | 18 个 | 17 个**构建失败** + 1 个用例失败（`Archive`） |
| macos-15-intel | 15m5s | 3463 passed / 1 failed / 2 skipped | 1 个 | 构建全部成功；`Archive` 用例失败 |
| windows-latest | 16m31s | 0 passed / 26 failed / 0 skipped | 34 个 | 26 个构建失败 + 8 个套件**没产出 `Totals:` 行** |

三条腿都停在 `运行测试套件` 这一步；`构建主程序` 与 `打包主程序` 被**正确跳过**（拿不到
LqRibbon），`上传测试日志` 与 `上传测试套件可执行文件` **都成功**——降级开关与四阶段设计
按预期工作。

这一跑暴露了**两个真问题**（第 1、2 条，下一轮的活）和**两个工具问题**（第 3、4 条，
本轮已随 `run-tests.sh` 一起修掉并做了反向验证）：

1. **`Archive` 在 ubuntu 与 macOS 上都失败，同一个原因**——**已修，见 §1.21**。
   它的 `initTestCase()` 调 `generate_fixtures.py` 生成夹具并回读校验，在 CI 的
   **Python 3.14** 下抛 `UnicodeDecodeError: 'utf-8' codec can't decode byte 0x82
   in position 3`（本机 3.13 与 3.9 都不抛）。**当时这里写的诊断是错的**：
   原话是「夹具本身没错，是回读校验这一步在 3.14 上不再成立」，于是「修复方向」写成
   「把 `cp437.zip` 从回读名单里去掉」。真实原因是 **3.14 把夹具写坏了**——
   `zipfile._open_to_write()` 改成无条件把 `flag_bits` 置成 UTF-8 位，
   覆盖了生成器按 `raw_flags` 声明的意图，`cp437.zip` 因此变成「声称是 UTF-8、
   字节却不是」。回读校验没有错，它是**唯一发现这件事的机制**；
   照当时的「修复方向」做，等于把唯一能报警的仪表拆掉。详见 §1.21 与 §6。
2. **ubuntu 17 个、windows 26 个套件构建失败，原因当时看不到**：运行器把 qmake / make
   的输出丢进了 `/dev/null`。本轮改成写 `<套件>/build.log`、失败时打出末尾 20/30 行、
   并把 `build.log` 加进上传产物——**下一跑就有原因可看**。
3. **Windows 腿所有套件都「通过」而合计是 0**：Qt 的 `-o -,txt`（结果写 stdout）在 Windows
   **一行都不输出**，文件产物却正常。日志里因此既没有 `Totals:` 也没有失败用例。
   本轮改成「只写文件、脚本再 `cat` 回来」，不再依赖 stdout 约定。
4. **合计与「红了几个套件」对不上**（Windows：34 个套件红，合计只有 `26 failed`）：
   崩溃的套件不写 `Totals:` 行，于是**一条用例都不计入合计**。本轮在合计之外单独点名
   「另有 N 个套件没有产出统计行（其用例数不计入上面的合计）」。
5. `[!]` 注解提醒 `actions/checkout@v4` / `actions/upload-artifact@v4` 仍指向 Node 20，
   已被强制跑在 Node 24 上。现在是警告，将来会变成错误；顺手升到 v5 即可（未做）。

上面第 2 条是**下一轮**的活；第 1 条已在同一轮修掉（见 §1.21），第 3、4 条已随
`run-tests.sh` 一起修掉并做了反向验证（见 §6）。这些失败**都不是本次改动引入的**——
它们一直存在，只是以前没有任何机制把它们摆出来。

**明确还没做到的（不能勾）**：

- **三条腿没有一条是「完整通过」的**（第一次真跑的结果见上表）：macOS 腿离完整只差
  一个 `Archive` 用例，ubuntu 与 Windows 差在构建失败上。因此 ENG-004 第 2 条
  （「至少一个平台完整 + 另一平台构建」）**不能勾**——它要的是「完整」，而现在最好的一条腿
  也是 3463 passed / 1 failed。**这个结论是本轮唯一不能含糊的地方**：流水线修好了，
  但它如实报告出被测对象三条腿都还没绿。
  其中「macOS 只差一个用例」这一个缺口**已在同一轮修掉**（§1.21），但**要等 CI 再跑一次
  才能确认**——本机是 macOS + Python 3.13，而这一条红恰好只在 Python 3.14 上出现。
  **不要**在 CI 确认之前把第 2 条勾上。
- 没有 `MYCLASS_TOKEN`，所以「构建主程序 / 打包产物 / 上传主程序产物」三个步骤
  **在公开 CI 上永远走不到**，它们的正确性目前只在 macOS 本地被间接验证过
  （`dist/macos/LqCompare.app` 是本地构建出来的，打包命令按同一套路径写）。
- **Windows 腿的打包分支是未验证代码**：`7z a -tzip dist/windows/LqCompare.exe` 从没被执行过
  （需要 token 才走得到）。
- ENG-003 还差的两项（**单套件超时**、**并行执行套件**）本轮没做，它们不属于 ENG-004。

**平台限制**：本轮所有本地验证都在 macOS 上完成。Windows 侧的一切结论都必须来自 CI 的
Windows 腿或目标机实测，**不能从 macOS 的绿推出来**。

### 1.21 修掉三条腿里唯一「一改就能让某个平台变完整」的红：Python 3.14 改写了归档夹具

**这一节是本轮的第二项工作**，接着 §1.20 的清单做第 1 条。

### 现象

`Tests/Archive` 在 `ubuntu-latest` 与 `macos-15-intel` 两条腿上都红，`initTestCase()` 就挂，
一条用例都没跑。从 CI 上传的 `results.txt` 里拿到的是完整回溯（这正是 §1.20 第 4 项
「日志要落到产物里」的价值）：

```
FAIL!  : ArchiveTests::initTestCase() 'process.exitCode() == 0' returned FALSE.
(Traceback (most recent call last):
  File ".../Code/Tests/Archive/generate_fixtures.py", line 314, in generate
    with zipfile.ZipFile(output / valid) as verify:
  File "/opt/hostedtoolcache/Python/3.14.7/x64/lib/python3.14/zipfile/__init__.py",
       line 1578, in _RealGetContents
    filename = filename.decode('utf-8')
UnicodeDecodeError: 'utf-8' codec can't decode byte 0x82 in position 3: invalid start byte
)
```

注意它崩在 `generate()` 的**最后一行**（回读校验），也就是**所有夹具都已经写出来了**——
崩的是自检，不是生成。

### 真正的根因（与 §1.20 里当时写的诊断不同）

当时写的是「夹具没错，是回读校验那一步在 3.14 上不成立」。**错了。**
把本机 `zipfile` 与真实的 3.14 源码逐行对比（3.14 的 `Lib/zipfile/__init__.py`）：

| | `_open_to_write()` 里给 `flag_bits` 的初值 |
| --- | --- |
| Python ≤ 3.13 | `zinfo.flag_bits = 0x00` |
| Python ≥ 3.14 | `zinfo.flag_bits = _MASK_UTF_FILENAME`（**无条件置 UTF-8 位**） |

生成器用 `RawName` 覆盖私有方法 `_encodeFilenameFlags()` 来指定原始名字与通用位标记，
写的是 `self.flag_bits | self.raw_flags`。3.13 上 `flag_bits` 初值是 0，所以「谁的意图」说了算；
**3.14 上它已经被置成 UTF-8 位，于是 `cp437.zip` 被写成「置了 UTF-8 位、字节却不是 UTF-8」**——
它不再是「CP437 名字」那个夹具，而变成了另一个夹具（`invalid-utf8.zip`）的形态。

所以：**夹具确实写坏了，回读校验是唯一发现它的机制。**
按当时的「修复方向」把 `cp437.zip` 从回读名单里去掉，等于把唯一会报警的仪表拆掉。

### 顺手查出的第二个问题：整个夹具集都随解释器版本变

`_open_to_write()` 那一行影响的是**每一个**成员：3.14 上连 `z.txt` 这种纯 ASCII 名字
也带上 UTF-8 位。实测（本机 3.13 vs 模拟 3.14）生成的 **92 个产物文件（91 个夹具 + `manifest.json`）
里有 72 个字节不同**，差异全部是那一个 bit。夹具的全部价值在于可复现，所以这一条也一并归一了。

### 改了什么（只动一个文件：`Code/Tests/Archive/generate_fixtures.py`）

1. `RawName._encodeFilenameFlags()`：把 UTF-8 位**先抹掉再按 `raw_flags` 置回**
   （`(self.flag_bits & ~UTF8_NAME_FLAG) | self.raw_flags`），其余位（数据描述符位 0x08）
   照常保留。`raw_flags` 成为唯一的意图来源。
2. 新增 `PlainName`（普通字符串名字走它）+ `intended_utf8_flag()`：把判据显式钉成
   「名字能用 ASCII 表示就不置位，否则置位」——也就是 3.13 及更早的行为。
3. 新增 `assert_utf8_flag_as_intended(data, written)`：`archive()` 写完后**立刻**核对
   真正落盘的字节里每个成员的 UTF-8 位是否等于声明的意图。这是把「换版本时一句能读懂的话」
   和「一个目录都读不出来的异常」区分开的那一步（见下面的 M8）。
4. 中央目录的锚点用 `ZipFile` 自己的 `start_dir`，**不扫 EOCD**：夹具
   `comment-signatures.zip` 的注释里故意放了一个假的 `PK\x05\x06`，`rfind` 会先找到假的
   （这个坑是本轮实际踩出来的，见 §6）。

**9 个夹具的结论**：`cp437.zip` 的 flag 回到 `0x0000`，而两个**故意**置位的夹具
（`unicode-utf8-disagreement.zip`、`invalid-utf8.zip`）保持 `0x0800`——意图都被尊重了。

### 本机怎么在没有 3.14 的情况下验证的

本机只有 Python 3.13.12 与 3.9.6，装不上 3.14。做法是写一个**行为复现器**
（`/tmp/lqcompare-sim314.py`）：用 `inspect.getsource()` 取出本机 `_open_to_write` 的源码，
把 `zinfo.flag_bits = 0x00` 这一行**原样替换**成 `_MASK_UTF_FILENAME`（就是 3.14 的那一行），
`exec` 回 `zipfile` 模块后跑生成器。这样拿到的失败与 CI **逐字一致**：

```
UnicodeDecodeError: 'utf-8' codec can't decode byte 0x82 in position 3: invalid start byte
```

修完之后，两条不变式同时成立：

| 检查 | 结果 |
| --- | --- |
| 本机 3.13 跑生成器 | 91 个夹具，成功 |
| **模拟 3.14** 跑生成器 | 91 个夹具，成功（修之前：同一个 `UnicodeDecodeError`） |
| 两者产物**逐字节一致** | ✅ 一致（修之前：74 个文件不同） |
| 与仓库里**已提交**的 `fixtures/` 一致 | ✅ 一致 → 3.13 上是零改动，不需要重新生成 |

### 反向验证

**8 处变异，7 处检出、1 处按设计漏检**（驱动在 `/tmp/lqcompare-mutate-archive.py`，
就地变异 + `finally` 无条件还原 + sha256 校验）。判定是「本机 3.13 / 模拟 3.14 / 行为断言
脚本」三个组件里任意一个的结果相对基线发生变化：

| 变异 | 结果 |
| --- | --- |
| M1 `PlainName` 不再抹掉 UTF-8 位 | 检出（模拟 3.14 下断言变红） |
| M2 `RawName` 不再抹掉 UTF-8 位 | 检出（同上） |
| M3 `intended_utf8_flag` 恒返回 0 | 检出（`utf8.zip` 的中文名字被判成非 UTF-8） |
| M4 `intended_utf8_flag` 恒返回置位 | 检出（ASCII 名字被判成 UTF-8） |
| **M5 只把写入后的断言关掉** | **按设计漏检**——断言自己没有行为，改掉它不产生可观察差异 |
| M6 中央目录锚点改回扫 EOCD | 检出（`comment-signatures.zip` 直接 `struct.error`） |
| M7 `PlainName` 不再进入检查 | 检出（行为断言 B 组变红） |
| **M8 回归 + 断言失效（M2+M5）** | 检出，且**失败性质 = `UnicodeDecodeError`** |

**M5/M8 这一对是本节最值得带走的一条**：一个「只负责把别的失败讲清楚」的护栏，
单独变异它**必然漏检**（它没有自己的行为），所以它按设计漏检**不是**测试缺口；
要证明它的价值，得把**行为**和**护栏**一起变异（M8）——那时失败就从
「一句说明意图不符的话」退回成「一个目录都读不出来的 `UnicodeDecodeError`」。
以后遇到类似的报错型护栏，用这个成对变异来交代它，别指望单独变异能检出它。

另有 **13 项行为断言**（`/tmp/lqcompare-check-archive.py`）：判据的四个边界、
三种成员类型各自的意图、裸 `ZipInfo` 不表意要跳过、以及「两种解释器环境下
都成功且产物逐字节一致」这个核心不变式。

### 验证数据

- `Tests/Archive` **106 passed / 0 failed / 0 skipped**（与本套件 README 里记录的 106 一致）；
  `Tests/ArchiveView` 15 passed；两个套件合计 121 passed。
- 本轮只改一个 Python 文件，**不碰任何 C++ 代码**，所以其余功能数字不变。
- 已提交的 `fixtures/` **一个字节都没变**（`git status` 只有那一个 `.py` 是 M）。

### 还没做到的

- **CI 确认**：本机无法跑真正的 Python 3.14，所以「macOS 腿变成完整」这件事必须由
  下一次 CI 运行回答。在这之前 ENG-004 第 2 条**保持不勾**。
- Windows 的 26 个套件构建失败**仍未动**（不是「未定位」——已定位到 5 个文件，
  见 §1.22）。它需要一轮独立的工作，理由写在那一节。
- 3.14 的 `_open_to_write` 是本机读源码 + 复现器确认的，**没有在真 3.14 上跑过**。
  复现器的锚点（`zinfo.flag_bits = 0x00` 必须出现且只出现一次）会在本机 zipfile 变化时
  立刻报错退出，不会静默失效。

### 1.22 第二条红腿的账本：ubuntu 已修，Windows 是 5 个文件的工作清单

§1.20 第 2 项加的 `build.log` 上传在这一轮直接兑现了价值——**不用猜、不用复现**，
下载产物就能读到编译器原话。结论如下（原始证据：run `35547633279` 的
`test-logs-<os>/<套件>/build.log`）。

### ubuntu：一个文件造成 17 个套件失败（**已修**）

`Code/Services/Files/trash_linux.cpp` 用了 `PathUtils::Style::posix()` 与
`PathUtils::parentPath()` 却没有 `#include "pathutils.h"`，也没有别的头文件会捎带进来：

```
trash_linux.cpp:35:7:  error: 'PathUtils' does not name a type
trash_linux.cpp:69:32: error: 'PathUtils' has not been declared
trash_linux.cpp:78:32: error: 'PathUtils' has not been declared
```

17 个套件之所以全中，是因为它们都（直接或间接）把 `Services/Files` 编进去。
**一个只在非开发平台上编译的文件，只要缺一行 include，就能让半条腿红掉。**

这次的验证没有停在「等 CI」：本机 macOS SDK 恰好带 `sys/statvfs.h`，于是可以对整个
Linux 翻译单元做 `-fsyntax-only`——修后 `clang exit=0` 整份文件通过；把那行 include
去掉，同一条命令立刻报出同类的 4 处 `use of undeclared identifier 'PathUtils'`。
**以后修 `*_linux.cpp` 里的编译错误，先用这条命令在本机验一遍**：

```sh
QTDIR=~/Qt/5.15.2/clang_64
arch -x86_64 clang++ -std=c++17 -fsyntax-only -fPIC -F$QTDIR/lib \
  -I Code/Services/Files -I Code/Services/Log -I Code/Services \
  -I$QTDIR/include -I$QTDIR/include/QtCore \
  -I$QTDIR/lib/QtCore.framework/Headers \
  Code/Services/Files/trash_linux.cpp
```

### Windows：26 个套件失败，**5 个文件**，两类系统性原因 + 几个真 bug

这一节写清楚是为了**下一轮不用重新下载产物**。

| 文件 | 报错（原文节选） | 原因与修法 |
| --- | --- | --- |
| `Services/Files/filesystem_win.cpp` | `'REPARSE_DATA_BUFFER' does not name a type; did you mean 'REPARSE_GUID_DATA_BUFFER'?`（403 行）<br>`'FSCTL_GET_REPARSE_POINT' was not declared in this scope`（231 行） | 两类原因各一半：① 文件定义了 `WIN32_LEAN_AND_MEAN`，于是 `windows.h` **不会再带进 `winioctl.h`**，而 `FSCTL_GET_REPARSE_POINT` 与 `MAXIMUM_REPARSE_DATA_BUFFER_SIZE` 都在那里 → 显式 `#include <winioctl.h>`；② `REPARSE_DATA_BUFFER` 在 `winnt.h` 里被 `#if (_WIN32_WINNT >= 0x0600)` 包着，而 MinGW 8.1 的默认值比它低，所以整个结构体没声明（`REPARSE_GUID_DATA_BUFFER` 是无条件的，所以编译器能给出「你是不是想写它」的建议）→ 在**包含 windows.h 之前**把 `_WIN32_WINNT` 抬到 `0x0600` 或更高 |
| `Tests/MergeOutput/tst_mergeoutput.cpp` | `'SYMBOLIC_LINK_FLAG_DIRECTORY' was not declared`（50 行）<br>`'CreateSymbolicLinkW' was not declared`（53 行） | 同上第 ② 条：都是 Vista 起才在头文件里出现的符号，`_WIN32_WINNT` 不够高 |
| `Services/Merge/mergeoutput.cpp`、`Services/Text/textdocument.cpp` | `call of overloaded 'number(DWORD&)' is ambiguous`（`QByteArray::number(info.dwVolumeSerialNumber)` 等，共 51 处） | **不是平台问题，是「Windows 分支少写了 POSIX 分支写了的东西」**：同一个文件里 POSIX 分支是 `QByteArray::number(qulonglong(info.st_dev))`，显式转了 64 位；Windows 分支直接把 `DWORD` 交给 `QByteArray::number`，在 MinGW 的重载集上产生歧义 → 照 POSIX 分支的样子补显式转换（`qulonglong` / `qlonglong`）。**这条很值得记**：两边写法不对称时，先看「能编的那一边多做了一步什么」 |
| `Services/Platform/registrystore_win.cpp` | `conversion from 'QStringList' to non-scalar type 'QString' requested`（355 行）<br>`'class QString' has no member named 'removeAll'; did you mean 'remove'?`（356 行）<br>`'RegDeleteTreeW' was not declared in this scope`（484 行） | 前两条是**真代码 bug**（把 `QStringList::removeAll` 写到 `QString` 上了）；第三条是 MinGW 8.1 的头文件里没有 `RegDeleteTree`（它是 Vista 起的东西，MinGW 只部分提供），要么改成递归 `RegEnumKeyEx`+`RegDeleteKeyW` 自己实现，要么 `LoadLibrary`+`GetProcAddress` 动态取——**这两条都不能靠加 include 糊过去** |

**为什么这一轮不顺手改**：本机是 macOS，**完全无法编译 Windows 分支**（没有 Windows 工具链，
`HANDLE`/`BY_HANDLE_FILE_INFORMATION` 这类类型也没有可信的替身），所以上面每一条都只能靠
CI 的 Windows 腿回答，一次 15 分钟。5 个文件、两类系统原因加两个真 bug 一起盲改，
会把「哪一处改错了」的信号混在一起。**下一轮的正确做法**：先只做 `_WIN32_WINNT` 与
`winioctl.h` 这两条系统性原因（覆盖面最大且方向确定），跑一次 CI 看剩下什么，
再逐条处理 `number()` 歧义与 `registrystore_win.cpp` 的两个真 bug。
另外 `windows-latest` 用的是 MinGW 8.1（Qt 的 `win32_mingw81`），它的头文件比现代 MSVC 旧，
**判断某个 Windows API 能不能用，要按 MinGW 8.1 的头文件判，不能按 MSDN 判**。

### 1.23 第一次真实 CI 的结果：一条腿已完整，一条腿只剩 1 个崩溃，Windows 未动

证据：run **`35549157384`**（`HEAD=2bcf2f3`），产物 `test-logs-<os>/<套件>/{results.txt,results.xml,build.log}`。
上一轮的两处修复都被平台**真实确认**了：

| 腿 | 套件 | passed / failed / skipped | 结论 |
| --- | --- | --- | --- |
| `macos-15-intel` | 64（全部有 `Totals:` 行） | **3568 / 0 / 2** | ✅ **完整**——「至少一个平台完整」这条要求在这一轮达成 |
| `ubuntu-latest` | 63 有 `Totals:`，1 个崩溃 | **3538 / 0 / 2** | 构建失败 **17 → 0**；只剩 `Tests/Folder` 运行期崩溃 |
| `windows-latest` | 仍有 26 个套件**构建失败** | — | 与上一轮完全一致，本轮**故意没动**（见 §1.22） |

**`Tests/Archive` 在 CI 运行器真实的 Python 3.14 上 106 passed / 0 failed。**
这同时否掉了 §1.20 里那个「是回读校验太严」的诊断，并证明 §1.21 的修法方向正确
（本机只能模拟 3.14，这里才是第一次真的跑在 3.14 上）。

**ubuntu 的新红是另一回事**：`Tests/Folder` 跑完第 7 个用例
（`recursionLimitIsExplicitUnknown`）就没了——第 8 个用例
`linksAreComparedWithoutFollowing()` 在**符号链接比较**上把进程带走了：
它建了一个指回自己的目录符号链接 `cycle`，macOS 上正常返回、Linux 上进程直接消失。
没有 `FAIL`、没有 `Totals:`，只有前面 7 行 `PASS`。**下一轮的首要小事就是查它**，
现在已经有线索了（见下面 stderr 那条修复）。

**`Folder` 为什么一个字的原因都没留下**：脚本把子进程的 stderr 也丢了
（`>/dev/null 2>&1`）。而崩溃原因（`Received signal 11`、`ASSERT`、Qt 崩溃处理器的回溯）
**只走 stderr**——`results.txt` 里不可能有。于是 CI 只能告诉你「某个套件红了」。
本轮把它改成落盘 `<套件>/stderr.log` 并加进上传产物，失败时贴出末尾 15 行；
另外**分开说**「stderr 有内容」与「stderr 是空的（被 SIGKILL/段错误直接带走）」——
后者本身也是信息，都写成「崩了」等于把两种死法混成一种。

**清理旧产物的位置也一并挪到了本轮最前面。** 原来那句 `rm -f results.txt results.xml`
放在「构建成功之后、启动二进制之前」，于是**构建失败的那一轮会原样带上上一轮的产物**，
被当作这一轮的结果上传。`results.txt/xml` 由二进制的 `-o` 覆盖写、`stderr.log` 由 `2>` 截断，
所以在「二进制真的被启动」的路径上删不删是一回事，**区别只在失败路径上**——
而这正是这类假信号最难被发现的地方。

### 这处改动是怎么验证的（**5 处变异 5 处检出**）

变异驱动 `/tmp/lqcompare-mutate-stderr.py`（**不进仓库**），观测点是三个**精确**的可观察量，
不是「输出变了没有」：`silent_says_empty` / `stderr_shown` / `stdout_leaked`
（M4 另加 `stale_survived`）。前两版驱动只报 2/5、4/5，两次都是**探针太弱**而不是护栏没用：

| 变异 | 第一次 | 第二次 | 第三次 | 说明 |
| --- | --- | --- | --- | --- |
| M1 stderr 丢回 `/dev/null` | 检出 | 检出 | 检出 | 原始缺陷 |
| M2 落盘但不打印 | 检出 | 检出 | 检出 | |
| M3 不区分「空」与「有内容」 | **漏检** | 检出 | 检出 | 第一次只查「有没有出现 stderr 这个词」 |
| M4 开头不清理旧产物 | **漏检** | **漏检** | 检出 | 见下 |
| M5 stdout 并进 stderr.log | **漏检** | 检出 | 检出 | 第一次没断言 stdout **不该**泄漏 |

**M4 连漏两次的教训值得单独记**：
1. 第一次的观测点错了——`2>file` 自己就会截断文件，所以「二进制被启动」的路径上
   删不删**完全等价**；而「构建失败/找不到二进制」的路径上那句 `rm` 根本执行不到。
   于是这一句当时是**真的等价变异**，不是探针的问题。
2. 第二次：观测点从「控制台输出」换成了「陈旧文件是否残留」，但探针场景选成了
   **构建失败**——而陈旧产物的危害不在控制台，**在上传的产物里**。所以要看的是文件本身。
   把清理挪到本轮最前面之后，M4 才变成一个**真**变异（`stale_survived: False → True`）。
3. 通用结论：**「等价变异」有两类**——一类是被判据本身证明等价的（这时该改代码），
   一类是**观测点选错了**（这时该改探针）。在宣布「这是等价变异」之前，
   先把观测点换成三种不同粒度各试一遍，否则会把「测试写漏了」误判成「代码没问题」。

### 探针套件的做法（保留下来给以后改 `run-tests.sh` 用）

`run-tests.sh` 是 CI 基础设施，本身已经修过两轮。它**可以被端到端自测**，办法是临时造
几个假套件放进 `Code/Tests/`（跑完即删，**不要提交**）：

| 探针 | 内容 | 验什么 |
| --- | --- | --- |
| `ZZProbeStderr` | 往 stderr 写一行、往 stdout 写一行、`return 3`、不写 `results.txt` | 「stderr 有内容」分支；同时确认 stdout **没有**泄漏 |
| `ZZProbeSilent` | `return 3`，什么都不输出 | 「stderr 是空的」分支 |
| `ZZProbeBadBuild` | `.pro` 里 `include(/nonexistent/…)` | 失败路径上旧产物有没有被清掉 |

`.pro` 只要 `TARGET = <名字>` + `DESTDIR = $$OUT_PWD/bin`（二进制路径＝`.pro` 名），
`run-tests.sh ZZProbe` 就能只跑这几个。**注意**：这几个目录一旦留在仓库里，
全量运行会永远失败——所以只当临时脚手架，用完删掉，配方留在这里就够。

### 1.23.1 第三次运行（`6cd5831` / `7f1d802`）：stderr 那条路**第一次回报**

run **`35551066378`**。三条腿的结论与上一次完全一致（macOS 64 套件 **3568 / 0 / 2**、
ubuntu 63 套件 **3538 / 0 / 2** 且 `Tests/Folder` 仍是唯一崩溃、Windows 仍是 **26** 个构建失败），
所以真正的新信息只有一条——**`Tests/Folder` 的 `stderr.log` 里写着**：

```
*** buffer overflow detected ***: terminated
```

**这一行把问题定性换掉了**：原来只知道「进程没了」，现在知道它是
**glibc 的 `_FORTIFY_SOURCE` 抓到了一个缓冲区越界**（`__chk_fail` → 打印这句 → `abort()`）。
不是死循环、不是栈溢出、不是 OOM。

**为什么苹果机上永远发现不了它**：`_FORTIFY_SOURCE` 是 glibc 的机制，Ubuntu 默认开着
（24.04 上是 `_FORTIFY_SOURCE=3`，用 `__builtin_dynamic_object_size`，连一部分**堆**分配也查得出来），
而 **macOS 的 libc 根本没有这个机制**。也就是说：

> **同一个缓冲区越界，在 macOS 上大概率表现为「跑得好好的」，在 Linux 上直接 `abort`。**
> 这不是「Linux 更严」，而是「macOS 少了一道本来就该有的检查」。

同理可推：`ASan`（`-fsanitize=address`）也能在本机复现这类失败，而它**在 macOS 上可用**——
这是下一轮最值得先试的一步（见 §4.0.1）。

**另一个顺手拿到的事实**：`stderr.log` 在三平台上分别有
macOS 4 个、ubuntu 5 个、Windows 3 个套件是**非空**的（`Logging` / `Options` /
`OptionsDialog` / `SpecialPicture`，内容是正常的 `[WARN]`/`[ERROR]` 与 Qt 的平台提示），
**其余套件是 0 字节**。所以「失败时才打印 stderr」不会给日志带来噪音，
而「空 / 非空」这个区分也确实分开了两类情形。


### 1.24 ENG-003 测试框架与测试运行器：把运行器自己变成可验证的对象（2026-09-21 11:41）

**这一条不是「代码没写」，而是「五条完成标准里有三条没做」**。开工前先 `ls` 过
`Code/Tests/`（66 个套件）与 `run-tests.sh`，结论是：

| 完成标准 | 开工前的实际状态 |
| --- | --- |
| 1. Qt Test / 每个套件独立 `.pro` / 可单独构建与运行 | **已具备**（66 个套件，各 `TARGET = tst_*`、各 `DESTDIR = $$OUT_PWD/bin`） |
| 2. 脚本构建全部套件、**并行运行**、汇总统计 | 构建与汇总有；**串行**跑（66 个套件 3 分 53 秒） |
| 3. 支持过滤套件、**设置超时**、输出 JUnit XML | 过滤有、JUnit XML 有（`-o <file>,junitxml`）；**没有任何超时** |
| 4. 失败时打印可复制的复现命令（含平台参数） | 有（`复现：QT_QPA_PLATFORM=offscreen <二进制> -o <txt>,txt`） |
| 5. 测试不得写用户目录与仓库目录，全部用临时目录并清理 | **违反**：`Tests/OptionsDialog::saveReviewScreenshots()` 把截图写进 `QDir::current()`，而运行器的工作目录就是仓库根 |

本轮把 2、3 两处补齐、把 5 修掉，并给运行器加了 `--self-test`。

**一、并行（标准 2）**。调度不用 `wait -n`（bash 4.3+ 才有，macOS 自带的是 3.2），
改成「按 PID 队列等**最老**的那个」：每启动一个套件就把 `$!` 入队，队列长度达到
`JOBS` 时 `wait` 队首。等最老的而不是最早结束的，换来的是「打印顺序 = 启动顺序」，
日志可读。默认 `JOBS = min(4, 核数)`，并且**把「套件数 × 每个套件的 make 并行度」
控制在核数量级**（`make -j(核数/JOBS)`）——两者都取核数会超订成 N² 个编译进程。
`JOBS=1` 时每个套件仍用满核数，与并行化之前逐字一致。

**二、超时（标准 3）**。GNU `timeout` 在 macOS 上默认不存在，`perl -e alarm` 之类
的外挂又把「跑测试」变成「跑测试 + 一个解释器」，所以是自己写的轮询：`kill -0` 判
存活、到点先 `TERM` 再 `KILL`、并落一个 `timeout.marker`。用 `kill -0` 是可行的
（bash 会异步回收后台子进程，子进程退出后 `kill -0` 立即为假，而 `wait` 仍能取回
真实退出码——自测每轮都在实测这条）；**刻意不用「`wait` + 看门狗子 shell」**，
那会在每个套件上留一个孤儿 `sleep`（66 个套件就是 66 个）。超时在日志里是与
「构建失败」「用例失败」并列的第三种失败，单独一行 + 单独一份末尾清单。

**三、并发下的计数（并行化最容易踩坏的地方）**。每个套件在子 shell 里跑，**子 shell
对全局变量的赋值不会传回父 shell**——所以结果一律写进 `<套件>/summary.env`
（一行六个数字），父进程 `read` 回来再累加。写错了的症状是最难查的那一类：
66 个套件都真的跑了、单套件的 `Totals:` 一行不差，**而合计是 `0 passed`**。

**四、测试不许脏工作目录（标准 5）**。`saveReviewScreenshots()` 改名成
`screenshotsNeverLandInTheWorkingDirectory()`，落在 `QTemporaryDir` 里，并**额外断言
「工作目录里的 `options-*.png` 没有被动过」**。只改落点而不盯落点，下一个人把路径改回
`QDir::current()` 时不会有任何东西变红——那正是这类改动最容易被「修」回去的地方。

**这里踩了一个坑，值得单独记**：这条断言的第一版比的是**文件名清单**，而变异
（把落点改回工作目录）**首次没有被检出**——因为被防的那三个文件名是固定的
（`options-<分类>.png`），而仓库根历史上就躺着同名的三个文件，写回工作目录只是
**覆盖**它们，名字集合一模一样（实测：三张截图的 mtime 变成了 11:28:11，
而断言仍然全绿）。改成比**指纹**（名字 + 大小 + 修改时间）之后立刻检出：
变异时 `Totals: 22 passed, 1 failed`，失败信息是
`Compared lists differ at index 0.`，还原后回到 23 passed。
**教训：断言要盯「内容有没有变」，不是「名字集合有没有变」——覆盖写永远看不出名字差异。**

**五、`--self-test`：让运行器自己的行为也有红有绿**。按 §5 第 7 条「静态检查脚本
必须能自证会报错」的约定，`run-tests.sh --self-test` 在**临时目录**里现造五个探针
套件（通过 / 失败 / 挂死 / 硬退出 / 构建期失败），用**同一个运行器**跑两遍
（并行 4 与串行 1），断言 24 条。几个值得记的设计点：

- **探针绝不进仓库**。留在 `Code/Tests/` 下会让全量运行永远失败（上一轮已经用三个
  临时探针踩过一次，见 §1.23）。所以探针是用 heredoc 现写到 `mktemp -d` 里的。
- **「硬退出」探针刻意不用 SIGSEGV/SIGABRT**：Qt Test 对这两个致命信号自己装了处理，
  会补写一份结果文件并把用例记为失败——那走的是「有统计行」的路，验不到
  「套件在写完结果前就死了」这条最要命的路径（CI 上 `Tests/Folder` 被 glibc 的
  `_FORTIFY_SOURCE` abort 掉就是这一类）。`std::_Exit(3)` 不跑 atexit、不给任何
  handler 机会，稳定复现。
- **「挂死」探针只睡 25 秒**，不睡无限长：运行器的超时被改坏时（变异测试真的这么做了），
  自测要**断言失败**，而不是跟着一起永久挂住。
- **并发度只有一个测量口径**：探针把 `epoch毫秒 名字 start/end` 追加进一个共享轨迹
  文件，事后 `sort` + 前缀和算峰值。**只统计有头有尾的探针**——挂死与硬退出永远走不到
  `end`，把它们的 `start` 记进去会让计数只增不减，串行也会报出并发 2。
- **两遍的 `合计` 必须逐字相同**，这是「并行没有改变结果」唯一看得见的地方。

**六、验证数据**。全量 **3598 passed / 0 failed / 2 skipped（66 套件）**，
与并行化之前逐项相同，**墙钟从 3 分 53 秒降到 1 分 21 秒**（`JOBS=4`）；
自测 24 条断言全绿（并行峰值 3 / 串行峰值 1）；五道护栏全过；
主程序全量重编 0 warning、离屏启动正常。

**七、反向验证：13 处变异，最后 13 处全部检出**（其中**两处第一次是真的漏检**，
下面单说）。检出的是：并行被关掉、超时被摘掉、本轮开头的产物清理被摘掉、
父进程不累加计数、失败也报通过、失败时不给复现命令、超时套件不写进末尾汇总、
硬退出时不贴 stderr、JUnit 格式用错（`xml` 而不是 `junitxml`）、
「没有产出 Totals 行」不再报、超时不落标记文件、qmake 失败时不贴报错原文、
截图写回工作目录。

**第一次漏检（M4「父进程不再把用例数累加进合计」）**：自测当时只断言了**某个套件
自己的** `Totals:` 行里有没有那 2 个用例，而 M4 破坏的是**父进程的合计**——两者是
不同的量，前者一条都不会变红。补的断言是 `^合计：6 passed, 1 failed, 0 skipped$`。
**教训：断言要盯「被测行为真正改变的那一个输出」，不是它旁边那个看起来像的。**
（与 §1.23 里 M4 连漏两次是同一类错误：观测点选错。）

**第二次漏检（M13「截图又写回工作目录」）**：见上面第四节。两次漏检共同指向同一件事
——**断言写得「看起来对」是不够的，必须让变异真的去撞它**。这两处都不是实现有 bug，
而是**验证本身有洞**，也正是本条要求「每条 issue 都要有反向验证」的全部理由。

**八、顺手纠正的一处口径**。`qtestlib` 的 `Totals:` 行把 `initTestCase` 与
`cleanupTestCase` **也算作用例**，所以「2 个真正的用例」在日志与 JUnit XML 里都是 4
（`tests="4"`）。自测里原来那条断言写的是 `Totals: *2 passed`，它其实匹配上的是
**失败探针**那一行——一句「名字与实物不符但恰好为真」的断言。已改成对着通过探针对。

**九、还没做的 / 有意留空的**：

- **超时分支未在 Windows 上实测**。它靠 POSIX 信号打断，而 Git Bash 的 `kill`
  能不能终止一个原生 Windows 进程没有验证过。因此 CI 里 Windows 腿**显式跳过**
  `--self-test` 并打一条 `::warning::`（不做成静默跳过）。**注意并行那一半在
  Windows 上是真跑了的**——下面的测试步骤现在默认就在并行跑。
- Windows 的 26 个套件构建失败（§1.22 的清单）本轮**未动**，与上一条是两件事。
- `Tests/Folder` 在 ubuntu 上的缓冲区越界（§1.23.1）本轮**未动**，它仍是
  「下一轮第一件事」（见 §4.0.1 第 4 条）。

代码提交 `5c16735`（提交号由紧随其后的纯文档提交回填）。

### 1.25 TXT-002 基础行对齐算法（Myers）：四条标准里有两条没被守住（2026-09-21 11:53）

**开工前我的判断是错的，而且是「乐观方向的错」**：`textdiff.{h,cpp}`（192 行）与
`Tests/Text` 都在，我据此把它归成「核对 + 关闭」。逐条对着标准核完才发现，
算法本身没问题，**是验证不对等**——四条标准里有两条的验收点从来没有被断言过：

| 完成标准 | 开工前的实际状态 |
| --- | --- |
| 1. 实现 Myers 差分，输出相同 / 新增 / 删除 / 修改四类块 | **已具备**（`insertionDeletionReplacement` 覆盖 Insert/Delete/Replace；`randomAlignmentsAreMinimal` 用 O(nm) DP 对照 1200 组随机输入的最小性） |
| 2. 1 万行 × 1 万行完成时间有明确上界断言（不超过 2s） | **已具备**（`tenThousandUnrelatedLines` 里两条 `QVERIFY2(timer.elapsed() < 2000, …)`，一条走无关行、一条走典型输入） |
| 3. 全相同 → 单个相同块；全不同 → 单块替换；无边界特例崩溃 | **只做了一半**：全不同那半有（`blocks.size()==1 && Replace`）。**全相同那半没有**——既有用例只断言了 `differences` 为空，对「有几个块」没有任何约束 |
| 4. 算法为纯函数，输出逐字段相同（有快照测试） | **没有**。只比过两遍的 `rows[i].leftLine` / `rightLine`；`blocks` 的全部字段、`rows[i].block`、`rows[i].change`、`differences`、`ignoredBlocks`、`alignmentLimited` 都不在约束里。也没有任何快照 |

本轮新增三个用例（`Tests/Text` 从 22 条到 25 条，全量 3598 → 3601），**没有改一行生产代码**。

**一、「差异为空」不等于「只有一个块」（标准 3）**。新增
`identicalInputYieldsASingleEqualBlock()`。为什么值得单独写：一个把所有行都拆成独立块的
实现同样「没有差异」，而**块边界就是界面上分隔线的位置**——会被画成一整屏都改动过。
反向验证里把 `compare()` 的块合并去掉，只有这一条会红（M1），说明它是这条性质的唯一守门人。
同一条用例里还钉了两个容易被跳过的边界：单行输入、两个空输入（不许崩、也不许产出空块）；
以及**「全相同」的另一种来源**——两行原文不同、按选项等价（`Whitespace::IgnoreAll`）。
后者必须仍然合并成一个块，而且必须标成 `Ignored` 而不是 `Equal`：标成 `Equal` 会让界面
把「被忽略的差异」画成「真的相同」。

**二、快照要摊开「每一个字段」，不能只列自己想到的那几个（标准 4）**。新增
`resultIsPureAndMatchesSnapshot()`：把 `Result` 摊成一行行文本（`fingerprint()`），
对五个夹具同时断言「两遍运行逐字段相同」与「与写死的黄金串相同」。

- **为什么摊成文本**：逐字段 `QCOMPARE` 只会覆盖你记得写的字段。这条标准要的是
  「逐字段」，摊开之后**给 `Result` 加字段时会在黄金串里留下痕迹**，而不是悄悄少守一半。
- **为什么两个断言都要**：只比自己两次等于在测「我等于我自己」，一个把左右整体对调的
  bug 会两遍都一样地错；只比黄金串又抓不住不确定性。
- **黄金串是手推的**（在纸上枚举 Myers 的 `ranges` 再算块与行），不是把实现跑出来的结果
  抄回来。抄回来的快照只能证明「今天和昨天一样」，证明不了「今天是对的」。
  手推的五个夹具第一次跑就全绿——算是一次独立交叉验证。
- 夹具 2 是夹具 1 的**左右对调**：块类型必须整体从 `Insert` 变 `Delete`、行里的 `-1`
  必须换到另一侧。这比多写一个不相干的夹具更能钉住方向。

**三、`alignmentLimited` 是快照里唯一没人守的字段，所以要为它单独造输入**。新增
`boundedAdversarialInputIsReportedAsLimited()`。它是边界条款（「输入规模超过阈值时自动
切换到线性空间变体」）**唯一可观测的那一面**：预算用尽必须**被报告**，因为调用方要靠它
决定是画一个笼统的替换块，还是提示「这两份文件太大，只做了粗略对齐」。把
`result.alignmentLimited = myers.limited` 改成恒 `false`，其余用例一条都不会红
（它们全都在预算之内）——M6 就是这么被这一条抓住的。

夹具的构造有个前提：**两侧必须至少有一行是公共的**。若毫无公共行，`run()` 会走
「无公共行」的快速路径直接产出替换块，而那条路径**刻意不消耗预算、也不设 `limited`**
（它本来就精确，没有受限可言）。所以夹具是「各 4000 行、互不相同、只在正中间共享一行」。
另外这条用例顺带钉住了头文件里那句承诺——「the remaining range is an explicit replacement,
**never silently considered equal**」：用 `equalBlocksPairIdenticalLines()` 断言
**任何 `Equal` 块里的左右两行必须真的相同**，受限之后也必须完整覆盖两侧每一行。

**四、反向验证：9 处变异，最终 9 处全部检出**（其中 M5 第一次是真漏检）。驱动脚本
`/tmp/lqcompare-mutate-txt002.py`，**不进仓库**；不删任何文件（推源文件 mtime 即可），
并且每次变异与还原之后都**断言 `textdiff.o` 的 mtime 真的动了**——否则「没编进去」会被
读成「没检出」，是方向相反的两个结论。

检出的是：块不再合并（M1）、被忽略的行报成相同（M2）、Insert 与 Delete 对调（M3）、
块的显示行数写成两侧行数之和（M4）、`firstRow` 写成块序号（M5）、不报告对齐受限（M6）、
行号整体偏移一位（M7）、非相同块一律报成修改（M8）、去掉「无公共行」快速路径（M9，
被**既有**的 `tenThousandUnrelatedLines` 抓住——精确路径退化成受限路径之后
`alignmentLimited` 变真，而那条用例断言它是假）。

**M5「`firstRow` 写成块序号」第一次漏检，成因与前两轮的两处漏检属于同一族但表现不同**：
这次**断言是对的，是夹具让两个不同的量恰好相等**。原来的四个夹具里每个块都只占一行，
于是「前面已经累积的行数」恰好等于「块序号」——`firstRow = rows.size()` 与
`firstRow = index` 在那些数据上给出同一个值。补一个**中间块占两行**的夹具
（`"a\nb\nc\n"` vs `"a\nX\nY\nb\nc\n"`）之后立刻检出。
**教训：写夹具时要问一句「这里有没有两个量在当前数据上恰好相等」——它会让一个真正的
变异变成等价变异，而报出来的却是「测试有洞」。**（这一条已进 §6。）

**五、验证数据**。全量 **3601 passed / 0 failed / 2 skipped（66 套件）**，并行 4 时
1 分 26 秒，EXIT=0；五道护栏全过；主程序 `make -j8` 0 warning / 0 error，
ad-hoc 签名后离屏启动到「`LqCompare 0.1.0 启动完成`」，无残留进程。

**六、还没做的 / 有意留空的**：

- **标准 2 的 2 秒上界绑定本机**。`tools/spec/` 之外的
  `docs/development/product-issue-audit-2026-09-20.md` 早就点过这件事
  （「2 秒断言需固定硬件、语料与构建模式」）。本轮**没有动它**：把上界改成
  「相对基线」需要一个稳定的基线与语料快照，那是独立一件事（与 ENG-004 的 CI 账有关），
  硬塞进 TXT-002 只会让这条本来可以通过的断言变得不可解释。**它是已知的口径缺口，
  不是「已经安全」。**
- **`tests/Text` 里仍没有「提交消息带 `TXT-002`」的历史提交**：算法是夜间那批工作流
  一次落地的，本轮的提交只补验证。issue 里的关联提交因此只有本轮这一个。

代码提交 `83a1c8f`（提交号由紧随其后的纯文档提交回填）。

### 1.26 TXT-003 耐心对齐（Patience）：从零实现，四条标准都在本机闭环（2026-09-21 13:0x）

**一、为什么挑它**。它是 P0，`textdiff.{h,cpp}` 里**一行 Patience 都没有**
（`grep -i patience` 在 `Code/Services` 与 `Code/Tests` 下零命中），
所以是「真的没写」而不是「写了没守」。四条完成标准里**没有一条指向别的工作流**：
第 3 条甚至明确把「算法选择控件与可用算法清单」划给 TXT-004，
把本条的范围钉在「引擎 + Myers 回退」上。这与 §4.1 里那些被界面阻塞的条目形成对照
——判据仍然是「完成标准里有没有动词指向一个还不存在的模块」。

**二、做了什么**（`Code/Services/Text/textdiff.{h,cpp}`，公开面只加了算法枚举、
一行算法表和四个查询函数）：

- **`AlignmentBuilder`（内部）**：区间表、`budget` 与 `limited` 的共享容器。
  两种算法都往它里面追加区间。**共用不是为省几行**：分别攒完再拼，拼接处迟早出现
  两套合并口径，而现象是「同一个文件用两种算法看，分隔线位置差一格」。
  预算共用还有一个直接后果——`alignmentLimited` 只有一个来源，
  不会出现「同一输入在两种算法下受限度不同」这种无法解释的分歧。
- **`Patience`（内部）**：先吃公共前后缀；再对剩下的段统计「两侧都只出现一次的行」，
  按左侧顺序收集候选（左侧下标天然递增），对右侧下标求**严格递增最长子序列**
  （标准 patience sorting，`tails[k]` 记结尾最小的下标）；锚点之间递归，
  **没有唯一行的一段交给 Myers**。
- **深度到顶（64）也交给 Myers**，而不是自己产出一个大替换块：Myers 的深度与预算
  上限已经保证了最坏情况有界，直接产出替换块不会更快、只会更不准。
  于是 **Patience 自己不设 `limited`**，那个字段永远由 Myers 决定。
- **算法表与自检**：`AlignmentDescriptor{alignment, identifier, implemented}` 是唯一
  事实来源；`defaultAlignment()` 取「表里第一条已实现的条目」（**不写死枚举值**），
  `availableAlignments()` 只返回已实现的，`validateAlignmentTable()` 另报
  「规格点名的算法被登记为未实现」。**两道机制缺一不可**：只有前者时，
  把 Patience 标成未实现会是一次静默的合规操作，而 issue 第 3 条要求实现它。
- **枚举外取值的兜底**：`Alignment` 的取值可能来自会话文件 / 命令行 / 设置仓库
  （那里存的是整数），读回来可能落在枚举之外。这种值必须退到默认算法，
  **不能**落进「什么都不跑」的分支——那会返回零块零行的结果，
  在界面上表现为「两份文件完全一样」。

**三、验证数据**。新套件 `Tests/Alignment`（`AlignmentTests.pro` + `tst_alignment.cpp`，
**刻意 `QT -= gui`**）：**14 个用例函数全绿**；全量 **3615 / 0 / 2（67 套件）**，
并行 4 时约 1 分 20 秒；五道护栏全过；主程序 `make -j8` 0 warning / 0 error，
ad-hoc 签名后离屏启动到「`LqCompare 0.1.0 启动完成`」。**没有改一行既有用例。**

**四、反向验证：9 处变异 9 处检出、0 处漏检**（驱动 `/tmp/txt003_mutate.py`，
**不进仓库**）。九处分别打在：锚点永远为空（Patience 退化成 Myers）、
没有唯一行时不再回退而产出一个替换块、唯一性只查左侧、表里把 Patience 标成未实现、
可选清单不按 `implemented` 过滤、拿掉枚举外取值的兜底、块 `rowCount` 只按左侧算、
被忽略的等价行也标成 `Equal`、相邻同类区间不再合并。
还原之后基线重新确认 **14 / 0 / 0**。驱动按 §6 的三条纪律写：变异全落在 `.cpp` 上
（不涉及头文件 → 不必重建构建目录）、还原**不用 `shutil.copy2`**（它会把旧 mtime
一起还原，`make` 据此跳过重编）、**不删任何文件**（本机沙箱按一个 turn 内的累积
删除数计守卫，超限直接让 Python 进程退出；顶源文件 mtime 就够）。

**五、一个「按设计漏检」要先说清楚**：`longestStrictlyIncreasing` 里把 `<` 改成 `<=`
是**等价变异**，跑不出来。原因是候选的右侧下标**天然互不相同**（每个候选来自一个两侧
都唯一的键，唯一键在右侧只出现一次，两个不同的键不可能落在同一行），
严格与非严格在输入上无差别。仍然写 `<`，并把这条不变量与「上层放宽之后非严格会把
同一行用两次、锚点循环的下一段右侧长度会算成负数」写进了源码注释——
**下一个人如果按「这里有漏检」去改，会改错方向。**

**六、还没做的 / 有意留空的**：

- **界面上的算法下拉、它的持久化与界面可见的算法清单是 TXT-004 的**，
  本轮按完成标准第 3 条**刻意不做**：本套件连 QtGui 都没链接，
  测的是「引擎确实会算两种、并且只声明自己会算的那两种」。
- **Patience 没有做「唯一行冲突」的启发式**（例如按行的缩进 / 长度再挑一次）。
  BC5 只要求「唯一行优先」，多加一层启发式会让同一个人无法从语料推出手推的期望值，
  从而失去反向验证的抓手。要加的话先补一条能独立推出来的语料。
- **`CompareOptions` 新增的 `alignment` 字段落在末尾**（结构体按位置聚合初始化的
  老约定），报表与脚本引擎目前都不打印它——**「这份报告是用哪种算法算的」暂时看不出来**，
  这是有意留白：先把算法稳定下来再决定它进不进报表。

代码提交 `3b3a993`（提交号由紧随其后的纯文档提交回填）。

### 1.27 TXT-008 忽略大小写差异：实现早就在，缺的是「链」这个名字与四类断言（2026-09-21 14:2x）

**这一条是「实现明显在、测试只覆盖一部分标准」那一类的第一个**（§4.0.5 第 8 条点名过它）。
逐条核对完成标准之后的结论是：**四条标准里没有一条是「真的没写」**——
`CompareOptions::ignoreCase`、`Change::Ignored`、Rules 组的勾选框、
`colorFor(Change::Ignored)` 的弱化底色、`text.ignoreCase` 设置键全都已经在仓库里。
缺的是**断言**，以及「规范化链」这件事**没有被命名过**（它当时是匿名命名空间里的一个
`static normalized()`，外面无从指认，也就无从验证第 3、4 条）。

**本轮做了什么**（一条生产改动 + 两个既有套件里加用例，**没有新建套件**）：

- `textdiff.{h,cpp}`：把链提到公开接口 `normalizedLine(text, options)`，
  内部 `keys()` 直接调它（**只有一个实现**）。头文件里的注释同时是
  **第 4 条要求的那份「取舍记录」**：链的三条契约、为什么用折叠而不是小写化、
  土耳其语 i/İ 的四格对照表、simple folding 不展开 ß/ﬁ/İ 以及由此换来的长度守恒。
- `Tests/Text` 加 **6 个**用例函数（25 → 31）：逐行的重要性切换、混合语料里只有
  仅大小写不同的行失去差异身份、链「少做任何一步都不相等」的语料 + 左右对调、
  非 ASCII 折叠对照表（12 行样本，每行点名一条 Unicode 规则）、
  土耳其语四格取舍 + `QLocale::setDefault(Turkish)` 下的结果不变、折叠长度守恒。
- `Tests/TextView` 加 **1 个**用例函数（18 → 19）：被忽略的行**仍被标记**、
  与**从真实渲染里采出来**的 Replace / Insert / Delete 三种差异色都不同、
  且彩度更低（「弱化」的可比量）。

**为什么不去新建 `Code/Tests/TextCase/`**：完成标准里没有一条指向还不存在的模块，
而实现就住在 `textdiff.{h,cpp}` 里；新套件只会把同一个 `compare()` 再包一层。
与 TXT-002 那一轮同一处置方式（见 §1.25），也与「新套件解决的是**新模块**没有测试」这条区分开。

**四条标准的落地程度：**

| 标准 | 服务层 / 视图层证据 | 状态 |
| --- | --- | --- |
| 1 不再算差异 + 仍有弱化提示 | `Tests/Text::caseOnlyChangesTurnRowsIntoIgnoredButKeepThemMarked`（行的 `change` 从 `Replace` 变 `Ignored`、行数不变）+ `Tests/TextView::ignoredRowsStayMarkedAndWeakerThanRealDifferences` | **已勾** |
| 2 开关切换改变行的重要性；结果与明确的规范化规则一致 | 同上前者（逐行）+ `nonAsciiFoldingFollowsUnicodeSimpleCaseFolding`（期望值按 Unicode 规则**手推**，不是跑一遍抄回来） | **已勾** |
| 3 统一规范化链、两侧都过完再判等 | `normalizationChainAppliesEveryEnabledRuleToBothSides`（少做任一步都不相等 + 左右对调一致 + 直接问 `normalizedLine`） | **已勾** |
| 4 非 ASCII 按 Unicode 规则 + 土耳其语取舍已记录 | 对照表用例 + `turkicCasePairsFollowTheRecordedTradeoffAndStayLocaleIndependent` + `textdiff.h` 的注释 | **已勾** |

**验证数据**：单套件 31 / 0 / 0 与 19 / 0 / 0；全量 **3622 passed / 0 failed / 2 skipped**
（此前 3615，**没有删改任何既有用例**，+7 全部是本轮新增）；
五道护栏全过（`check_winapi` 仍报「扫描 333 个源文件」，本轮**没有新增源文件**，
这个数不该变，也没有变）；主程序增量构建 0 条本仓 warning，离屏启动
「单实例机制已由选项关闭」→「Ribbon 构建完成：10 页 / 45 组 / 169 个按钮」→
「LqCompare 0.1.0 启动完成」。

**反向验证**：**8 处变异 8 处符合预期**（7 处检出 + 1 处按设计漏检），
驱动 `/tmp/txt008_mutate.py`（**不进仓库**）。七处检出：整条折叠被摘掉、
折叠退化成 `toLower()`、退化成 `toUpper()`、链里掉一步（开了大小写就不做空白折叠）、
只对左侧应用规则（右侧按默认选项算）、忽略行改用 Replace 的底色、
视图干脆不给忽略行画标记。**每处都断言了「.o 确实重编了」**（比对 `.o` 的 mtime
与写入变异的时刻）——第 2 节那条纪律（「结论只在确认过构建确实发生时才可信」）
本轮用代码而不是肉眼确认。**按设计漏检的那一处是「把链的两步调换顺序」**：
折叠与空白处理**可交换**（折叠既不会造出空白、也不会吃掉空白），
所以它不该红，代码里也写明了「这个顺序不是契约的一部分」。**下一个人不要按
「这里有漏检」去改方向。**

**还没做的 / 有意留空的**：

- **`Change::Ignored` 目前不区分「为什么被忽略」**（大小写？空白？行尾？）。
  第 1 条只要求「有弱化标记」，做成一档就够；要分档得先有一个
  「哪条规则造成的等价」的返回结构，那是 TXT-013（重要性）的地盘。
- **土耳其语不做局部化折叠**是有意为之，见上面那张对照表。要改的话先想清楚
  「同一对文件在两台 locale 不同的机器上给出不同结论」这件事怎么向用户交代。
- **`ﬁ`/`ß` 不展开成多字符**同样是 simple folding 的既有边界，收益是长度守恒。
  真正需要 `STRASSE == straße` 的场合应当走文件格式级的转换规则（FMT-004）。

代码提交 `5af6b9e`（提交号由紧随其后的纯文档提交回填）。

### 1.28 TXT-009 忽略空白变化：两级语义早就写对了，缺的是「分水岭」的断言与模式表（2026-09-21 14:4x）

**为什么这一轮做 TXT-009**：§4.0.6 第 4 条点名过它，判据仍是「**先逐条读完成标准，
再去源码与测试里找对应的断言或实现**」。核对结果与 TXT-008 同类但更细：
**五条标准里 1、2、5 条的实现在，断言只覆盖了一部分；第 3 条的隐患根本不在枚举上；
第 4 条有一半没有住所**。

**五条标准的落地程度：**

| 标准 | 证据 | 状态 |
| --- | --- | --- |
| 1「忽略空白变化」只在内部连续空白数量或首尾空白上不同时判等 | 原先只有个别用例顺带碰过；本轮补 `whitespaceLevelsAreStrictlyDistinctOnAFixedCorpus`（9 行手推语料 × 3 个模式，含前导 / 尾随 / 首尾 / 多处）与 `tabAndSpaceMixturesFollowTheSameTwoLevelDefinitions`（10 行 Tab/空格混排语料） | **已勾** |
| 2「忽略全部空白」删掉全部空白后相同则判等 | 同上两张表里的「有无」类行（`ab` vs `a b`、`abc` vs `a b c`、`a\tb` vs `ab`）——这类行**只有** `IgnoreAll` 判等，正是两级的分水岭 | **已勾** |
| 3 以单一枚举呈现三个模式、互斥 | 枚举本来就互斥；真正的问题在**界面与枚举按序号对应**（见下）。本轮补 `WhitespaceDescriptor` / `whitespaceTable()` / `whitespaceIdentifier()` / `defaultWhitespace()` / `availableWhitespaces()` / `validateWhitespaceTable()`，界面改按表铺下拉与读回；用例 `whitespaceModesAreASingleExclusiveEnumBackedByOneTable`（含把**四份故意写坏的表**喂进同一个自检）与 `Tests/TextView::whitespaceComboFollowsTheModeTableAndMarksIgnoredRows`（文案 + 值两条链） | **已勾** |
| 4 被忽略的空白差异在视图中有可视标记，且标记可通过 **View 页**开关关闭 | **前半句已勾**：`Tests/TextView::whitespaceComboFollowsTheModeTableAndMarksIgnoredRows` 断言两侧第 0、2 行都留下标记、真正相同的第 1 行不留标记、且底色不等于编辑区底色。**后半句没做**：视图里**没有 View 页设置界面**，那句开关没有住所 | **部分落** |
| 5 Tab 与空格混排的行符合同一套定义（有固定语料测试） | `tabAndSpaceMixturesFollowTheSameTwoLevelDefinitions`：Tab/空格互换、数量相同、交替、Tab 做首尾空白，另钉住「哪些字符算空白」（NBSP / U+3000 算，U+200B **不算**） | **已勾** |

**第 3 条的隐患不在枚举上，而在「序号」上。** 原写法是下拉里写死三行文案 +
读回时 `static_cast<Whitespace>(currentIndex())`。序号对应是一份**隐式的第二事实来源**：
调换两条文案、或在枚举中间插一个取值，界面上的「忽略全部空白」会静默变成另一个模式，
而构建、运行、既有用例**全都不红**。本轮把两条链都钉住了：

- **值那条链**：`availableWhitespaces()` 的第 i 项 ↔ 下拉第 i 行 ↔ 会话选项 ↔ 落盘的设置值；
- **文案那条链**：下拉第 i 行**显示的字** ↔ 模式表第 i 项的文案。

**第二条链是本轮踩出来的。** 第一版只钉了值那条，变异「把下拉的铺法改成倒序」
**全绿**——因为值那条路是 `whitespaceAt(index)` 按表取的，倒序只改变了用户看到的字。
为了让用例能取到「期望文案」而不复制任何界面字符串，把标签函数提成了
`TextCompareView::whitespaceLabel()` 这个**公开静态接口**（理由写在头文件里）；
不这么做，断言只能把三行英文抄一份，那又成了第二份事实来源。

**为什么不去新建 `Code/Tests/TextWhitespace/`**：同 §1.27——完成标准没有一条指向
还不存在的模块，实现与既有套件都在，新套件只会把同一个 `compare()` 再包一层。

**验证数据**：单套件 `Tests/Text` 34 / 0 / 0（31 → 34）与 `Tests/TextView` 20 / 0 / 0（19 → 20）；
全量 **3626 passed / 0 failed / 2 skipped**（此前 3622，**没有删改任何既有用例**，+4 全部是本轮新增）；
五道护栏全过（`check_winapi` 仍报「扫描 333 个源文件」——本轮没新增源文件）；
主程序增量构建 **0 条本仓 warning**（唯一告警仍是第 3 方 `LqRibbon.cpp:569`），
`textdiff.o` / `textcompareview.o` 的 mtime 都证明**确实重编了**；离屏启动三行正常
（「单实例机制已由选项关闭」→「Ribbon 构建完成：10 页 / 45 组 / 169 个按钮」→「LqCompare 0.1.0 启动完成」）。

**反向验证**：**10 处变异 10 处检出、0 漏检**，驱动 `/tmp/txt009_mutate.py`（**不进仓库**）。
九处在服务层与视图的值路径上（两级各自退化、默认模式改取末项、表外取值编假名字、
自检漏一条规则、可选清单塌成一条、读回永远落第一行、忽略行不画标记、忽略色换成扎眼的红），
第十处正是上面那条**文案链**。每处都断言了「`.o` 确实重编了」（比对 `.o` 的 mtime 与写入变异的时刻）。

**驱动本身踩的两个坑（值得照抄）**：
① 批量删 `.o` 会撞上本机沙箱的**批量删除守卫**（阈值 50/轮），把驱动自己弄死；
改成 `os.utime` 把目标对象的时间戳推回一小时前——同样能逼 `make` 重编，且一次文件操作都不做。
② **`run-tests.sh` 在有套件失败时仍然退 0**，拿退出码当判据会把「用例全红」读成「变异漏检」；
判据必须解析合计行里的失败数。

**还没做的 / 有意留空的**：

- **第 4 条的后半句（标记可由 View 页开关关闭）没有住所**。它要的是 `OPT-007`
  「文本编辑与视图选项」那张设置页（或 `OPT-003` 比较规则默认值那张），
  两张都还没落地（`OptionsDialog::categories()` 现在只有
  `general` / `display` / `logging` / `storage` / `fileops` 五个分类，规格列了 12 个）。
  **不要**为了勾掉它而在 `general` 下另起一个键——与 §4.1 里 `PLAT-006` 第 3 条同一处置。
- **`textcomparesession.cpp` 里还有一处序号耦合**：
  `static_cast<Text::Whitespace>(qBound(0, value, 2))`——读回时的合法区间是从枚举
  写死的 `0..2`，而不是从模式表推出来的。本轮**没动它**：它不在五条标准的射程内，
  且改动会牵到命令行那条同样以整数传模式的路径，属另一个条目（更贴近 `TXT-004`
  的「算法/模式选择与持久化」）。**已记在这里，别当它不存在。**
- **`Change::Ignored` 仍不分档**（大小写？空白？），同 §1.27。
  同一对行同时命中大小写与空白两条规则时，界面只给一个弱化标记，说不出是哪条造成的。

代码提交 `15a6530`（提交号由紧随其后的纯文档提交回填；见 §3.1 那一行）。

### 1.29 TXT-005 相似度阈值与相似行对齐：四条标准全落，并顺带拔出一个「合并丢数据」的连带缺陷（2026-09-21 15:0x）

**为什么这一轮做 TXT-005**：它是同族里唯一「四条标准全都指向一个还不存在的模块」的条目
（`TXT-010` 的 `ignoreEol` / `ignoreFinalNewline` 两个开关已在同一条链里生效，属于「先核对」那类；
TXT-005 属于「从零写」那类）。判据照旧：`/opt/homebrew/bin/gh issue view 60` 逐条读完成标准，
再去 `Code/Services/Text/` 与 `Code/Tests/` 里 `ls` 找对应实现。

**核对结果**：`Services/Text/` 下只有 `textdocument.*` / `textdiff.*`，
**没有任何**相似度相关的源码，`Tests/` 下也没有对应套件——四条标准全部要新写。
（若只 grep `similarity` 会看到 `CompareOptions::similarityThreshold` 这个字段与它注释里
「本字段尚未被引擎读取」，那不是实现，别当成「已经有了」。）

**四条标准的落地程度：**

| 标准 | 证据 | 状态 |
| --- | --- | --- |
| 1 阈值可配置（0–100）并提供「相似行对齐」总开关 | `MaximumSimilarity` / `defaultSimilarityThreshold()` / `clampSimilarityThreshold()` 都在 `linesimilarity`；`CompareOptions::alignSimilarLines` 与 `similarityThreshold` 由引擎真正读取；会话键 `text.alignSimilarLines` / `text.similarityThreshold` 往返有 `Tests/TextView` 用例 | **已勾** |
| 2 固定候选行对的分值与阈值判定有边界测试（低于阈值不配对）；「全局重排后不要求修改块数量单调，改用固定语料验证对应关系与无丢行」 | `Tests/Similarity` 15 条：分值在用例里按定义 `2·LCS/(len左+len右)` **另算一遍**再比对（不抄实现里的数）；`isSimilarEnough` 钉住「含等号」；配对用三条结构不变量判（下标各自严格递增 / 每对都 `>= 阈值` / 配对数与总分值不得被更优解压过）。**「无丢行」由固定语料显式断言**：配对前后左右两侧被覆盖的行数必须相等 | **已勾** |
| 3 关闭相似行对齐后，相似行被呈现为删除 + 新增两条独立块 | `Tests/Text` / `Tests/Similarity` 同一组夹具跑两种开关，断言「开着 = 1 个替换块、关掉 = 删除块 + 新增块」；命令行另有 `--similar-lines` / `--no-similar-lines` 两种跑法的摘要比对 | **已勾** |
| 4 阈值与开关写入会话设置并可用于命令行 | 会话键在 `textcomparesession` 里读写往返；命令行 `--similar-lines` / `--no-similar-lines` / `--similarity-threshold <0-100>` 三条都进 `Tests/Cli`（含 6 行非法参数：101 / -1 / `abc` / `70.5` / 缺值 / 两个开关同时给） | **已勾** |

**新增模块**：`Code/Services/Text/linesimilarity.{h,cpp}`——分值 `lineSimilarityPercent()`、
阈值判定 `isSimilarEnough()`、单调配对 `pairSimilarLines()`、两个工作量上限
（`similarityPairingMaximumCells()` 512×512、`similarityPairingBudget()` 10^8）
与钳制 `clampSimilarityThreshold()`，全部是**纯函数**，因此新套件 `Tests/Similarity`
刻意 `QT -= gui`（与 `Tests/Text` / `Tests/Alignment` 同一条纪律）。

**「一处改动」这个新概念**：一段改动会被铺成**若干相邻块**，于是「差异块个数」
不再等于「界面上数得出的改动处数」。为此在 `textdiff` 加 `DifferenceRun` / `differenceRuns()`
（判据只用「块下标连续」——两种算法产出的区间本来就是「相同段 / 非相同段」交替的），
状态栏、上一处/下一处、复制这一处、命令行摘要四处**统一改用它**。
这一层不是顺手加的：不加的话现象是「状态栏说 3 处、按两次『下一处』就到头了」。

**顺带拔出的连带缺陷（本轮最值钱的一条）**：拆分「一处改写」之后，**三方合并引擎**
按**块**粒度解读两侧编辑，于是「左改 B→C、右改 B→D」在 `A B C` / `A D C` / 基线 `A B C` 上
被读成两组互不重叠的编辑，**基线行 `B` 从结果里消失**（输出 `A\nC\n` 而不是 `A\nB\nC\n`）。
这是**丢数据**，比多报一个冲突严重得多，而它只在 TXT-005 落地之后才可能出现。
修法：`mergeengine` 加 `changeRunLength()`，把「基点相接」的同侧相邻非 Equal 块归并成一处
再交给合并判定；`textmergesession` 的输出行→块映射同样改按「一处改动」口径算归属
（否则纯新增行会挂到错误的旧行上）。**判据是「基点相接」而不是块类型**。

**验证**：单套件全绿——`Text` 34/0、`TextView` 20/0、`Similarity` 15/0、`Merge` 45/0、
`MergeView` 26/0、`Report` 36/0、`AppIntegration` 12/0、`Cli` 116/0；全量
**3649 passed / 0 failed / 2 skipped**（68 个套件，此前 3626；增量的主体是新套件
`Tests/Similarity` 的 15 条，其余为 `Tests/Cli`（本轮新增两个用例函数，含 6 行非法参数数据行）
与 `Tests/TextView` 新增的 1 条同属本轮；未删改任何既有用例）；
五道护栏全过（`check_winapi` 正式扫描 **337** 个源文件，比上轮 333 多 4 个 = 本轮新增源码）；
主程序增量与 `make -B` 全量重编均为 **0 条本仓 warning**，ad-hoc 签名后离屏启动正常；
**12 处变异 12 处检出、0 漏检**（驱动 `/tmp/txt005_mutate.py`，不进仓库）。

**本轮的教训（值得带走）：一个「夹具与手推分值都错」的假红，会被误读成「实现错了」。**
TXT-005 落地后 `Tests/Text` / `Tests/TextView` / `Tests/Report` / `Tests/AppIntegration`
一起变红，第一反应是「引擎把块拆错了」。真实原因是那批夹具里既有「两个不同文件恰好
各有一处差异」也有「某对行在出厂阈值 50 下根本配不上对」（实测 `bravo Two` / `BRAVO two`
只有 33 分、`a` / `left` / `right` 只有 22 分），**分值是从定义算出来的，不是从直觉猜的**。
→ **改行为之前先用一个独立小程序把真实数值 dump 出来**（本轮用了 `/tmp/lqdbg/`），
凡是断言里出现具体数字，那个数字要么有独立来源、要么是实测的，不能是「看起来差不多」。

**两处刻意的反直觉取舍**（理由已写进注释，别被「修正」回去）：
① 命令行越界**报错**、会话文件越界**钳制**——两条路刻意不同（脚本作者需要知道参数没生效；
   手改坏的会话文件不该改成一个用户没设过的数）。
② 配对目标**先配对数、后总分值**——反过来会让「一对 90 分」压过「两对 60 分」，
   用户看到两行改动被缩成一处，与这个功能的目的正好相反。

代码提交 `1e7ce78`（提交号由紧随其后的纯文档提交回填；见 §3.1 那一行）。

### 1.30 TXT-001 文本比对会话与双窗格视图骨架：一条「本文档从没提过」的 P0（2026-09-21 17:3x）

**怎么选中它的**：本轮按「优先 P0」在 59 条 P0 里挑，判据仍是
「完成标准的动词指向的东西还在不在」。挑中 TXT-001 的决定性事实是——
**在这份 3366 行的接手文档里 `TXT-001` 一次都没出现过**（`grep -c` 为 0）。
TXT-002 / TXT-003 / TXT-005 / TXT-008 / TXT-009 都有各自的 §1.x 与 §4.0.x，
唯独这条骨架条目没有任何记录。它因此长期停在「待实现」标签上，
而 `TextCompareSession`（`Views/Text/textcomparesession.*`）与
`TextCompareView`（`Views/Text/textcompareview.*`）从会话框架落地那天起就在仓库里、
一直被 `Tests/TextView` 的 20 条用例跑着。

> **教训（比这一条本身更值钱）**：§4.1 那张「谁被谁阻塞」的表只登记了
> **被讨论过**的条目。一条从来没进过讨论的条目，标签停在「待实现」，
> 文档里也查不到它——两边互相印证同一个错误结论。
> 所以「按 §4.1 查一次」不足以证明某条没被做过，**必须再 `ls` 一次源码与测试目录**
> （本条已经写进 §4.1 的判据里）。

**四条完成标准的落地方式**（产出 4 个新用例函数，`Tests/TextView` 20 → 24）：

| 标准 | 本轮怎么让它可验 |
| --- | --- |
| 1 可打开两个本地文本文件并显示**并排**双栏视图 | `twoLocalFilesOpenSideBySide`。既有用例只断言「两个 `TextPane` 都找得到」，而**上下堆叠同样满足它**——所以这里断言的是**几何关系**：分栏方向必须是水平、两侧窗格的屏幕原点必须左右分开且**纵向齐平**、宽度可见，并且两侧读到的分别是各自那个文件。这条走的是「构造期就带两个路径」的入口（用户按的就是这个），既有用例覆盖的是「先开空会话再去界面里选文件」 |
| 2 两个窗格**共享同一个**比对结果模型，行号与行高严格对齐 | `panesShareOneModelAndKeepRowGeometryAligned`。夹具里**必须有填充行**（右第 1 行是插入）——没有填充行时，「两侧各显示自己的文件」与「两侧共用一个模型」给出完全一样的结果。断言四件事：两侧的视觉行数**都等于**模型行数；逐行核对「模型 → 两侧显示的文字」（填充行必须是空行）；**号码槽与正文两条链各钉一遍**；两侧字体、换行模式、首行与末行的像素行高全部相同 |
| 3 单侧为空（新文件）时另一侧全部行标记为新增/删除**而不是报错** | `emptySideMarksEveryOtherRowAsInsertOrDeleteWithoutError`。左空右有 → 每一行 `leftLine == -1` 且 `change == Insert`，整段是**一个** `Insert` 块（`leftCount == 0`——若是 `Replace`，界面上会显示成「左边原来有内容被删掉了」）；左有右空 → 全部 `Delete`。**两个方向都验**，否则把插入与删除写反不会有人发现。视图侧另断言两侧行数一致、左侧三行全是空行、**两侧每一行都被标记**。空侧还必须是「可编辑的新文件」（0 行且 `canEdit()`），不是「读不出来的文件」 |
| 4 两侧完全不相关时不发生算法退化（无超长耗时、无栈溢出） | `unrelatedFilesStayBoundedWithoutDegrading`，三个输入：① 4000 行完全无公共行（走不耗预算的精确快速路径，单个 `Replace` 块、**不报受限**），② 每隔一行共有一行（相邻相同段只有 1 行，最放大递归与工作量；断言 **`equalRows == 2000`**——两侧公共行只有那 2000 行 `shared i`，LCS 长度可推，不是照抄观测值），③ 两侧各 2000 行互不相同、正中间共享一行（**预算真的用尽**，断言受限被报告、**且会话把它说到了用户看得见的状态栏文字里**）。三段都断言块首尾相接覆盖满两侧（一行都不许在粗略对齐里消失） |

**生产改动只有一处**：`TextPane::lineNumbers()`（`textcompareview.h`）。
理由不是「顺手加个 getter」，而是这条标准里**正文与号码是两条链**——
正文由 `setPlainText()` 铺、号码由 `setLineNumbers()` 铺，取自同一个数组却各走各的路。
只断言正文时，「号码根本没传下去」或「号码传成了另一侧那份」都不会让任何用例变红，
而用户按号码读出来的结论会与屏幕上显示的内容对不上。
这与 TXT-009 的教训（「UI 值与 UI 文案同源不同路时两条链都要有断言」）是同一个来源。

**验证数据**：单套件 `Tests/TextView` **24 / 0 / 0**（20 → 24）；
全量 **3653 passed / 0 failed / 2 skipped**（此前 3649，**未删改任何既有用例**）；
五道护栏全过；主程序增量构建 **0 条本仓 warning**（`textcompareview.o` 的 mtime 17:41
晚于头文件 17:39，证明新访问器确实编进去了）且离屏启动三行正常。
**8 处变异 8 处检出、0 处漏检**（驱动 `/tmp/txt001_mutate.py`，**不进仓库**）。

**本轮的两条坑（已进 §6）**：
① 变异让进程**段错误**时，**排在它前面的用例会先把进程带走**，
于是「预期该红的那条」根本没机会运行——驱动会把它错报成**漏检**。
判据要用「预期用例变红」**或**「整个进程非正常退出」，但必须把那两条如实分开写。
② 用例里出现**具体数字**时，那个数字要么有独立推出来的来源（本轮的 2000 = 夹具的 LCS 长度），
要么必须是**实测的**。本轮第一版把「每隔一行共有」那个输入的期望行数写成 4000，
实测 4005——多出来的 5 行是相似度分值恰好落在出厂阈值两侧造成的，
与「实现错了」毫无关系（与 §1.29 里那条「先怀疑夹具」是同一条纪律的第二次出现）。

### 1.31 TXT-010 忽略行尾差异：两条开关早就在，缺的是「哪一条管哪件事」的断言与状态栏那半句（2026-09-21 18:0x）

**怎么选中它的**：接着 §4.0.9 排定的同族次序往下走（TXT-010 排在第一位）。
开工前按 §4.1 的三步核对：① 逐条读完成标准；② `ls` 源码与测试目录；
③ 在本文档里 `grep` 条目号。`grep TXT-010` 此时**有命中**（§4.1 与 §1.29 都提过它），
所以这次不是 TXT-001 那种「本文档从没提过」的情形，而是**逐条核对后才发现缺口在哪**。

**核对结果**（与 TXT-002 / TXT-005 / TXT-008 / TXT-009 同一个模式——**实现明显在，验收点没被断言**）：

| 完成标准 | 实现 | 本轮之前的断言 |
| --- | --- | --- |
| 1 开启后仅行尾序列不同的行不判为差异 | ✅ `textdiff.cpp` 的键函数里 `if (options.ignoreEol && ending) ending = 1;` | ⚠️ 只覆盖 CRLF ↔ LF 一对；**CR 那一档零覆盖**，把 `ending = 1` 写成「非零即 CRLF」不会有任何东西变红 |
| 2 末尾换行独立于行尾类型、单独可控 | ✅ `ignoreFinalNewline` 借另一侧末行的行尾实现 | ⚠️ 只有 `eolRulesIndependent()` 里三行顺带，**没有 2×2 组合**（3 种风格 × 开/关 `ignoreEol`），也没有「另一侧为空时没有可借的东西」这条边界 |
| 3 开启时状态栏明确提示「已忽略行尾差异」 | ✅ `textcomparesession.cpp` 里 `status += tr(" • Line endings ignored")` | ❌ **一句断言都没有**（`grep "Line endings ignored" Code/Tests/` 零命中） |
| 4 行尾类型在状态栏显示为 LF/CRLF/CR/混合，**混合时给出警告图标** | ⚠️ `eolDescription()` 早就报四种取值；**「警告图标」没有任何机制** | ❌ 只有一句 `startsWith("Mixed")`，另外三支与「没有行尾」那支零覆盖；**图标这一半完全不存在** |

**做了什么**：

1. **补上真正缺的那半句：状态栏警告图标**（这是本轮唯一新增的能力）。
   做法不是在文本里贴符号，而是给会话加一条**独立的严重程度通道**：
   `CompareSession::StatusSeverity`（只有 `Normal` / `Warning` 两档）+ `statusSeverityChanged`
   信号 + `setStatusText(text, severity)`。**两条通道各自去重**——文本没变不发文本信号
   （状态栏刷新是高频路径），但严重度不能被文本的去重一起吞掉。
   容器 `SessionArea` 把当前会话的这一路转发出去（原地变化时转发，切标签时与既有的
   `statusTextChanged` 一样**重播**）；窗口在状态栏的**永久控件**位置放一个
   `statusWarningIcon` 标签，装配时设一次 pixmap、之后只切可见性。
2. **`Document` 侧把「混合」做成谓词**：`hasMixedEndings()`，并**刻意不把 `Eol::None`
   算作一种风格**（算了的话，任何不以换行收尾的文件都会被报成混合，图标会一直亮着）。
   同时把 `eolDescription()` / `preferredEol()` / `hasMixedEndings()` 三处的计数循环
   收敛到同一个 `countEndings()`，并加一条「谓词必须与文案的 `Mixed` 分支一致」的用例。
3. **断言补齐**（4 个套件，共 +9 个用例函数，**没有删改任何既有用例**）：
   `Tests/Text` 34 → 38（四种取值 + 谓词 + 三种风格两两组合 + 2×2 与空侧边界）、
   `Tests/TextView` 24 → 26（第 3 条的两句提示 + 严重度在「左混合 / 右混合 / 都不混合」三种情形下的升降）、
   `Tests/Session` 50 → 52（**基线**级别的两条：严重度的默认值、以及「文本一字不差、只有严重度变」时两条通道各自的行为）、
   `Tests/AppIntegration` 12 → 13（**真窗口**上验图标：先亮后灭、切标签跟着变、会话全关掉后不残留）。

**本轮最值钱的一击：两处变异互相遮蔽，且这正是「同一件事两条路」的代价。**
第一版把图标的刷新同时挂在两条路上（会话的严重度信号 + `refreshStatusBar()`），理由是
「刷新状态栏时顺手把图标也重算」。18 处变异跑完，**M13 与 M14 双双漏检**——
去掉其中任何一条，图标的表现**一点变化都没有**，另一条把它兜住了：
切标签时 `activeSessionStateChanged → refreshStatusBar()` 与容器的严重度重播**总是同时发生**。
→ 处置不是「再补一条用例」（补不出来：两条路必然同进同出），而是**删掉冗余的那一条**，
把图标收敛成**一个写入点**。删的是 `refreshStatusBar()` 里那次调用，保留容器重播
（理由：它与 `statusTextChanged` 在切标签时同样重播的既有约定对称）。
删完之后两处变异**各自都被检出**（`/tmp/txt010_mutate_b.py`，2/2）。
**判据**：一个「删掉之后没有任何用例变红」的分支不是纵深防御，是没人知道的死代码。

**验证数据**：单套件 `Tests/TextView` **26 / 0 / 0**、`Tests/Text` **38 / 0 / 0**、
`Tests/Session` **52 / 0 / 0**、`Tests/AppIntegration` **13 / 0 / 0**；
全量 **3662 passed / 0 failed / 2 skipped（68 个套件）**（此前 3653，本轮的 9 条见上），
连跑两次一致；五道护栏全过（`check_winapi.py` 扫 **337** 个源文件 / `check_spec.py` 369 条 P0 59 /
`check_icons.py` 32 个图标 / `check_shell.py` 16 个脚本 / 分层检查通过）；
主程序增量构建 **0 条本仓 warning**、`MainWindow.o`（18:03）晚于源文件（18:01）证明确实重编，
离屏启动三行正常（单实例关闭 → Ribbon 10 页 / 45 组 / 169 个按钮 → 启动完成）。
**18 处变异 18 处检出、0 处漏检**（驱动 `/tmp/txt010_mutate.py` + 复验 `/tmp/txt010_mutate_b.py`，
**都不进仓库**）。每处都断言了目标 `.o` 的 mtime 确实变了（证明变异真的编进去了），
并按「**期望的那条用例**变红」判定，不是「有东西红」。

**顺手修掉的两处 markdown 表格断行**：`architecture.md` §4 决策表里
`& | ! ( )` 与 `(a|b)*` 两处代码串里的裸 `|` 会把单元格截断（GFM 表格即使在反引号里
也不豁免 `|`，必须写 `\|`），已转义。

**两条坑（已进 §6）**：
① **两条路并存会让变异测试互相遮蔽**，见上；判据是「每一处都要能被单独打红」。
② **断言里出现具体数字/具体字符串时，必须去读实现印出来的那一句**。
本轮第一版把状态栏期望值写成 `Mixed (LF 1 / CRLF 1)`，实测是
`Mixed (LF 1 / CRLF 1 / CR 0)`——三个计数**总是**都印（用不到的那档是 0）。
这已经是同一条纪律在本文档里的第三次出现（前两次见 §1.29 与 §1.30），
**它不是在说「实现错了」，而是在说「我没去看实现」**。

### 1.32 TXT-012 内置替换规则：四条规则、一张表，以及一条「让顺序可观察」的语料（2026-09-21 19:2x）

**做了什么**：新增 `Services/Text/linereplacements.{h,cpp}`，把「比对前先改写行内容」这件事
做成一层独立的东西。四条内置规则（行首编号 / 日期时间 / GUID / 十六进制地址）登记在一张
`ReplacementRuleDescriptor` 表里，表是唯一的事实来源：`availableReplacementRules()` 的集合、
`ReplacementSet` 的应用顺序、`validateReplacementRuleTable()` 的判定全部从它推导。
`CompareOptions` 末尾新增一个 `replacements` 字段（**新字段只能加在末尾**，这个结构体被按位置
初始化过），`normalizedLine()` 在链的**最前面**调它。

**四个设计选择，每一个都是踩过类别的坑之后才这么定的**：

1. **命中片段换成固定占位符，不是删掉**。删除会让「整行原本就是一个地址」的行塌成**空行**，
   与文件里真的空行不可区分——在差异视图里表现为「凭空多出／少掉一个空行段落」，
   而用户对着自己的编辑历史找不到这处变化。占位符保留「这里原本有内容」这一信息，
   两侧是同一个占位符时照样判等为相同，「忽略」的意图一点没少。
2. **出厂是空集**（`defaultReplacementRules()` 返回空）。`Whitespace` / `Alignment` 都有
   「表里第一条已实现」的默认值，这一条**刻意没有**：忽略空白/大小写**不改变**用户看到的
   那一行，替换规则**会改**——出厂就开着等于替用户做了内容层面的决定，而他第一次打开两个
   文件时根本不知道有过这个决定。空集时 `apply()` 是一次**短路**，所以「不启用任何规则」与
   「没有这一层」逐字节相同，既有语料的结论一条都没变。
3. **替换排在规范化链的最前面**（先于大小写折叠与空白模式）。规则的正则是对**原文**写的：
   用户看着原文写正则、也在界面上看原文；把替换放到大小写折叠之后，一条照原文写的
   大小写敏感正则会变成「界面上的文本明明匹配、引擎却匹配不上」。
4. **规则表里的 `pattern` 一份两用**：既是引擎编译的正则，也是界面上给用户看的那一串
   （完成标准第 2 条要「显示其正则或匹配说明」）。分成两份就会立刻出现第二份事实来源——
   改了规则忘了改说明时，界面上仍然写着旧行为，而没有任何测试会红。

**反误伤是这一条的半个验收面**（每一条都在用例里成对断言）：

- 行首编号**要求分隔符 + 分隔符后紧跟空白**，所以 `42 apples` / `1 引言` / `版本 1. 发布`
  都不动——否则散文里的每个数量词都会消失，表现是「一堆本该不同的行变成了相同」。
- 日期要求 4 位年份开头且两端有词边界，所以 `20240101`（八位数字）与 `x2024-01-01`
  （前缀是标识符的一部分）都不动。
- GUID 两侧都有「不能是十六进制字符」的边界，所以嵌在更长十六进制串里的 `…aabb` + GUID
  不会被吃掉；花括号**连壳一起**换掉（只吃中间 36 个字符会让 `{…}` 与不带花括号的写法
  在判等上仍然不同，而它们表达的是同一个标识）。
- 十六进制地址**必须带 `0x` 前缀**：`add` / `beef` / `face` / `cafe` 都是普通英文单词，
  裸串规则会在散文中大开杀戒。`0x` 后没有十六进制数字时保持原样（不留半截 `0x`）。

**「按声明顺序依次应用」这句话原本是不可观察的**——这是本轮最值得记住的一件事。
两条规则的匹配区间一旦不相交，谁先谁后都给出同一个结果，于是一个把顺序写反的实现也能让
全套用例变绿。因此专门构造了一条**重叠语料** `0x12345678-1234-1234-1234-123456789abc`：
表里 Guid 排在 HexAddress 之前，于是先按 GUID 认出 8-4-4-4-12、`0x` 前缀留在原地
（`0x<GUID>`）；反过来先跑 HexAddress，`0x12345678` 被当作地址吃掉，剩下的 4-4-4-12
再也凑不出 GUID（`<HEX>-1234-…`）。两种顺序的结果不同，顺序这件事这才算被钉住了。
（`x` **不在**十六进制字符集里，所以 GUID 的左边界允许它，重叠点正是靠这一点成立的。）

**命中之后是 `Change::Ignored`，不是 `Equal`**：两行的键相同、原文却不同，走的正是
TXT-008 留下的那条通道（`textdiff.cpp` 收尾循环里那句 `left[..] == right[..] ? Equal : Ignored`）。
视图层因此**不需要**为替换规则新增一种状态，而用户仍然能看到「这一行被规则吃掉了」。
第一版用例把它断言成「只有一个 `Equal` 块」，跑出来是 `[Ignored(左2/右2), Equal(左1/右1)]`
——**红的不是实现，是我的断言**；这条已经写进 §6 的坑表。

**验证数据**：单套件 `Tests/TextRules` **19 / 0 / 0**（**纯 QtCore**，刻意 `QT -= gui`）；
全量 **3681 passed / 0 failed / 2 skipped（69 个套件）**（此前 3662 / 68 个套件，
+19 全部是本轮新增，**未删改任何既有用例**）；五道护栏全绿（winapi 源文件数 341）；
主程序 `make -j8` 本仓 0 warning、离屏启动正常（三行日志齐全）；
**12 处变异 12 处检出、0 漏检**——其中 M1（把两行对调）被**表自检**拦下（同一条规则登记两次），
「顺序断言有效」由 M7（`enabledRules()` 改成倒序）单独证明。

**还没做到的**：完成标准第 2 条只有服务层那一半。四条规则都能单独开关（`setEnabled`）、
「正则 / 说明」也都是可读数据，但**界面上那个开关**没有住所——Compare 页 Rules 组的
「忽略注释 / 忽略替换规则」那一批要等设置页落地（`OPT-*`，与 `TXT-009` 第 4 条后半句
是同一处置：**不要**为了勾掉它在 `general` 下另起一个键）。

### 1.33 DIR-008 二进制逐字节比对：「只比较前 N 字节」与它的三条边界（2026-09-21 19:4x）

**这一轮的开局方式与前几轮不同**：第 22 轮定时会话在 19:12 发现另一个会话正在改
`Code/Services/Text/`，按约定退出并把半成品存成 `.workbuddy/wip/DIR-008-partial-bytes.patch`
+ `RESUME-DIR-008.md`（**没有整仓 reset**，逐个文件精确回滚）。本轮的**第一件事就是接手它**：
`git apply` 补丁 → 逐处复核（它**从未被编译过**）→ 补断言 → 走完整闭环。这也是本节存在的理由：
上一轮的现场与结论全部落在 `.workbuddy/`（不进 git），如果只看 git 历史，下一轮会以为 DIR-008 从没动过。

**逐条核对（先核对再动手，结论与补丁里的记账一致）**：

| 完成标准 | 核对结论 |
| --- | --- |
| 1. 逐字节、遇首个不同字节即提前判不同 | **实现早就在**（分块循环内 `a != b` 时算出首个差异下标并 `break`），但既有用例只断言了偏移量——**把这个 `break` 删掉，全部 3687 条用例一条都不会红**（夹具都是「大小相同、只有一处差异」，命中之后剩下的块两侧全同）。本轮补上断言，见下 |
| 2. 「只比较前 N 字节」（默认关闭）+ 开启时标注「部分比较」 | **本轮的主体**。补丁已写好引擎 / 设置键 / 视图往返，本轮的增量是**断言**与两处新用例函数 |
| 3. 分块读取、块大小有上界 | **早就在**（256 KiB），但常量藏在 `.cpp` 里——测试只能去匹配源码字面量，那是第二份事实来源。本轮提成公开的 `kMaximumCompareBlockSize` |
| 4. 记录首个差异偏移量、报表中可查看 | 实现早就在（`Report::fromFolder` 把 `firstDifference` 拼进 `row.detail`），**但从来没有被断言过**。本轮补 `Tests/Report` 用例，含「没有结论的条目不许长出偏移量」 |
| 5. 比对期间文件被改动 → 标为变化 | 早就在且有用例；本轮补的是第 2 × 第 5 条的**交叉**（见下） |

**三条边界，各有一条专门语料**（这是本轮最有价值的产出）：

1. **限 == 文件长度算完整**。文件正好 N 字节时，循环在 `atEnd()` 处自然退出，`budgetReached` 保持
   假——两侧都读完了，结论确实是完整的。一个「读到预算就一律报部分」的实现会在这里说谎。
2. **差异在限内算完整**。限内已经证明不同，就不该再降级成「不确定」——差异已被证明，
   `partialComparison` 保持 `false`、`complete` 保持 `true`。
3. **差异刚好在限外一个字节算不完整**，而**再宽一个字节就必须被发现**。这一对是刻意的成对断言：
   少了后一条，一个「第二块永不比较」的实现也能让前一条通过。

**顺序上的一处刻意**：字节预算卡在「大小不等」短路**之后**。放最前面最省事，但那样
「1 KiB 与 1 MiB 的两个文件」会被报成「不确定」——而它们百分之百不同，且一个字节都不用读。
这不是保守，是把已经拿到的证据丢掉。分块循环里再用 `want = min(块上界, 预算 − 已读)` 夹逼，
保证**不越过第 N 字节**（越界读会让「限 N 字节」变成一个只在日志里成立的承诺）。

**主状态复用 `Unknown`，「部分比较」另开一维**：不新增 `Status::Partial`，而是把
「结论覆盖了多少内容」放进 `Entry::partialComparison`（与 `status` 正交）。理由是主状态那一栏
回答「结论是什么」，两者混在一起必然长出一套优先级规则，界面 / 报表 / 命令行各实现一遍。
副作用是刻意的：`run()` 早已把 `Unknown` 映射成 `complete == false`，所以一开这个开关整体结果
必然标注为「不完整」——正是验收标准要的那句。三条设计理由已进 `architecture.md` §4。

**值必须被校验，不能被静默改写**：`folder.compareFirstBytes` 只接受非负**整数**。
`0.5` 截断后恰好等于 `0`，而 `0` 就是「关闭」——用户设了个值却得到默认行为，还**没有任何反馈**，
这类设置错比「值非法被拒绝」难发现得多。`"4096"`（字符串形式的数字）同样拒绝。
负数既有反序列化那一层、也有 `optionsError()` 那一层，两条路各有一条断言。

**第 2 × 第 5 条的交叉（本轮新增的用例函数）**：文件在**部分比较**期间被改动时，
「只比较了前 N 字节」这个说法本身也不再成立。两条都验：① 限内全同、随后文件变了 →
`partialComparison` 必须被收回（否则一条 `Error` 会同时声称「读失败」与「比较了前 N 字节」）；
② 差异已被找到、随后文件又变了 → `firstDifference` 必须清回 `-1`，否则报表会对一条读失败的
条目印出一个「首个差异偏移量」，而那个偏移量是对**旧内容**的结论。
**这一条是变异驱动的**：M5 / M6 两处打在原来的写法上时**都是漏检**，说明这两行原本没有任何用例盯着。

**第 1 条的核心词「提前」单独补了一条断言——它是本轮真正的增量之一**：核对时多问了一句
「这一条被**怎么**验的」，才发现既有断言只钉了首个差异的**偏移量**，而没有钉「**在命中处停下**」。
判据很简单：**把那个 `break` 删掉，全部 3687 条用例一条都不会红**——因为既有夹具都是
「大小相同、只有一处差异」，命中那一处之后剩下的块两侧全同，循环就算继续跑也改写不了
`firstDifference`。**所以那处 `break` 是没人知道的死代码，而「提前」这个词从头到尾没被验过。**
补法不动一行生产代码：给测试替身加两个注入点（`physicalPaths` 逻辑路径 → 真正被打开的路径、
`frozenInfo` 冻结的申报元数据），造一个「**申报尺寸 600000、实际只有 100 字节**」的右侧——
于是「大小相等」这条前置成立、循环进得去，第一个块就不同；而如果循环不停，它会接着读第二块
（左 262144 / 右 0），`firstDifference` 会被改写成 262144、再改成 524288。
**「断言 100」因此是唯一能分辨「停 / 不停」的量。** 这也解释了为什么它此前一直没被验过：
不是没人想到，而是**默认的夹具形状天然掩盖了它**。
（口径上的诚实说明：标准里那句括注「**不读完整文件**」**仍然没有直接断言**——
`compareFile()` 直接开 `QFile`，没有读层的缝可以观测。要把它也变成可验的，
得先给读路径加一个 device 缝；本轮没有为了凑这句话而新造生产接口。）

**验证数据**：`Tests/Folder` **36 / 0 / 0**（本轮 30 → 36，6 个新用例函数）、`Tests/Report` **37 / 0 / 0**
（36 → 37）；全量 **3688 passed / 0 failed / 2 skipped（69 个套件）**（此前 3681，+7 全部是新增，
**未删改任何既有用例**）；五道护栏全绿（winapi 源文件 341）；主程序 `make -j8` 本仓 0 warning、
离屏启动三行日志齐全；**15 处变异 15 处检出、0 漏检**，且每处都比对过目标 `.o` 的 mtime
确认变异真的编进去了（头文件变异把整套件目录的 `.o` 全部推老，含 moc 依赖链）。
其中 M15（删掉「命中即 `break`」）**只打红这一条新用例**，其余 3687 条纹丝不动——
这正是「此前没有任何用例守着」的量化形式。

**顺带修掉一处红护栏**：`tools/check_spec.py` 在 `HEAD` 上就是红的——上一轮 handoff 里
那个「新套件 `Tests/TextRules`」后面的用例数写法（`` 19 `` 紧跟着「条」、又被全角括号括住）
撞上了它的「全角括号 + 数字 + 条」网（那张网专门核对「文档里手写的规格条目数 == 369」）。
**上一轮「五道护栏全绿」的结论因此不成立**（当时那个循环用 `| tail` 收尾，
把退出码吃掉了，只看了输出没看 `$?`）。处置是把那半句改成仓库既有的写法
「（19 个用例函数）」——绕开而不是放宽护栏，因为那 5 处**真**的规格条目数全都长在
「规格 / 条目 / issue / PRD」同行的句子里。这条处置与手法已进 §6。

**还没做到的**：完成标准第 2 条只有引擎与设置那一半。`FolderCompareView` 里**没有**这个输入框，
本轮沿用 `m_maximumDepth` 的老办法——把它作为 `m_compareFirstBytes` **原样保留**，
`options()` 读回、`setOptions()` 写入。**这一处不是可选的**：视图不带回它，任何一次
「读视图选项 → 写回会话」都会把用户设好的预算静默清零（关闭）。真正的输入框要等目录选项
那一组界面落地，与 `TXT-009` 第 4 条后半句、`TXT-012` 第 2 条是同一处置：
**不要**为了勾掉它在 `general` 下另起一个键。

### 1.34 崩溃套件自动补跑 `-v2`：把「跑到哪一条用例才崩」变成日志里直接看得见的东西（2026-09-21 21:0x）

**这一节是 §4.0.10 / §4.0.12 里连续**九**轮排在第一位的那件事**——`Tests/Folder` 在 ubuntu
上被 glibc 的 `_FORTIFY_SOURCE` 抓住（`stderr.log` 全文只有一行
`*** buffer overflow detected ***: terminated`）。它不是「还没查」，而是**本机结构性查不了**：
macOS 的 libc 没有 `_FORTIFY_SOURCE` 这道检查，ASan 在本机也**静默**（`otool -L` 已确认
`libclang_rt.asan_osx_dynamic.dylib` 链上了，`Tests/Folder` 30 passed / 0 failed、一段 ASan 输出都没有），
Docker 守护进程没起、本机没有 Linux。所以本轮**不再去猜哪一行**（那是浪费），只做那件
唯一能推进的事：**让运行器把答案自己带出来**。

**一、做了什么**。`run-tests.sh` 现在对**崩溃**（没产出 `Totals:` 行、且不是超时）的套件
自动补跑一遍 `-v2`，并把结论写进日志：

```
    自动补跑一遍 -v2（诊断用，不覆盖上面那一轮的结果产物）：
      补跑退出码 3，首次退出码 3。
      崩在用例：ZZProbeHardExit::diesWithoutWritingResults()（-v2 里最后一条 entering 的用例）
      -v2 结尾（完整内容见 .../verbose.txt，随日志产物一起上传）：
    | INFO   : ZZProbeHardExit::diesWithoutWritingResults() entering
      （`Loc:` 那行是最后一条**跑完**的断言；崩掉的是它之后的下一条，
        或者就是该用例第一条断言之前的调用链里。）
```

**二、为什么这条诊断成立（三件事都是**实测**出来的，不是推测）**：

| 问题 | 实测结论 |
| --- | --- |
| `-v2` 的输出往哪走 | **只写文件**。`-o <file>,txt` 下 stdout 是 **0 字节**（与文件头第 6 条一致），所以走「写文件、再读回来」，不依赖任何 stdout 约定 |
| 进程被硬杀时，文件里还留着尾巴吗 | **留着**。本轮造了一个探针 `VProbe`（`/tmp/vprobe`，**不进仓库**）：`std::abort()` 之后文件里有 `QDEBUG : VProbe::diesByAbort() MARKER-BEFORE-ABORT`，连 `atexit` 都不跑的 `std::_Exit(3)` 之后也有 `QDEBUG : VProbe::diesByExit() MARKER-BEFORE-EXIT`，两种死法下 `INFO : … entering` 那一行都在。⇒ 判定**不依赖任何一次 flush**（Qt 的纯文本日志器逐条写盘） |
| 「跑到哪一条」怎么读出来 | `-v2` 为每个用例先写 `INFO : Class::func() entering`、跑完再写 `PASS : Class::func()`。**最后一条 entering 的用例**就是崩之前正在执行的用例；`Loc: [文件(行号)]` 则是最后一条**跑完**的断言（这一句解释也打印出来，否则读日志的人会去怀疑那条明明已经跑完的断言） |

**三、三条边界，都是刻意划的**（每条都有断言守着，见下面第五点）：

1. **只对崩溃补跑**。超时的套件上面已经单独点名，再补跑一次只会白等一个超时；断言失败的
   套件用例名本来就在 `results.txt` 里。
2. **补跑不传 `-o results.txt`**。首轮那份「崩之前已经跑过的用例」记录是**证据**，让诊断顺手
   覆盖它等于让诊断改写证据。
3. **补跑这一次如果完整跑完了**，单独说出来：「⇒ 首次崩溃是偶发的（依赖时序或输入），
   不是确定性失败。」——这两种情况的排查方向完全不同，混成一句会把读日志的人引到错的方向。

**四、顺手做的重构**：「启动二进制并带超时等它」原来只有一份（写在 `run_suite` 里），
补跑要走**逐字相同**的路径，于是抽成 `run_binary_with_timeout()`。理由不是「好看」：
两处各写一份轮询，迟早会在某一处漂移，而「超时」这条路的**全部**证据（标记文件、汇总里
单独一行）正是自测断言的对象——漂移会静默地只影响其中一条路。

**五、验证数据**：

- `run-tests.sh --self-test`：**30 条断言全绿**（原 24 条 + 本轮 6 条），两遍（并行 4 / 串行 1）
  的合计仍逐字相同。新增的 6 条分别守：补跑发生了 / 补跑**点了名**（只断言「跑过了」的话，
  把判定逻辑删掉也照样绿）/ `verbose.txt` 留档 / 补跑复现了同一句遗言 / **只有**崩溃的套件补跑
  （通过·失败·超时三个探针都不该多出 `verbose.txt`）/ 补跑没有改写首轮的 `results.txt`。
- 全量 **3688 passed / 0 failed / 2 skipped（69 个套件）**，与本轮之前逐项相同（本机没有崩溃
  套件，所以补跑这条路在本机**不会**被触发——它是为 ubuntu 准备的）。
- 五道护栏逐个 `exit=0`（含 `check_shell`，它扫的就是这个文件）。
- 主程序 `MAKE_EXIT=0`、`warning:` 计数 **0**、离屏启动三行齐全（「单实例机制已由选项关闭」→
  「Ribbon 构建完成：10 页 / 45 组 / 169 个按钮」→「LqCompare 0.1.0 启动完成」）。
  本轮**一行 C++ 都没动**。
- **反向验证：7 处变异 7 处检出、0 处漏检**（驱动 `/tmp/lqcompare-mutate-rerun.py`，不进仓库）：

  | 变异 | 结果 |
  | --- | --- |
  | M1 崩溃后不补跑 | 检出（4 条红：补跑发生 / 点名 / 留档 / 遗言） |
  | M2 补跑不带 `-v2` | 检出（**只**红「点名」那 1 条 ⇒ `-v2` 是承重的） |
  | M3 超时套件也补跑 | 检出（**只**红「只有崩溃的套件才补跑」1 条） |
  | M4 补跑顺手覆盖 `results.txt` | 检出（**只**红「没有改写首轮 results.txt」1 条） |
  | M6 取第一条 entering（不是最后一条） | 检出（**只**红「点名」那 1 条，它会报成 `initTestCase`） |
  | M9 看门狗失效（到点不打断） | 检出（4 条红，含 3 条既有的超时断言 ⇒ 重构没把超时路弄坏） |
  | M10 看门狗的标志位没传回调用方 | 检出（3 条红） |

**六、本轮**真的**踩到的两个坑（都进了 §6）**：

1. **M4 第一次是漏检，原因不是实现有 bug，是断言盯错了量**。原始判据是
   `grep 'Loc:' results.txt`——听着对（`Loc:` 只有 `-v2` 会写），但 `ZZProbeHardExit` 这个探针里
   **一条断言都没有**，它的 `-v2` 输出里根本没有 `Loc:`。于是「results.txt 已经被改写成
   -v2 的内容」这个事实（实测确实被覆盖了）在断言上完全看不见。改成「-v2 独有的行首标记」
   （`INFO` / `DEBUG` / `QDEBUG` / `QWARN`）之后立刻检出。
   **这是 §1.24 M4「盯错了量」的同族错误，也是同一句话：断言要盯这份夹具真正会变的那个量。**
2. **自测出现了一条偶发红点**（约 2/13 次，HEAD 版 11 次里 0 次）：某一次跑里 `ZZProbePass`
   的产物与合计对不上（`results.xml` 里没有 `<testsuite>`、`stderr.log` 非空、两遍合计不同）。
   6 次空跑循环 + 3 次 CPU 负载下对比 + 8 次 HEAD 版对照都**没能复现**，机制未查明，
   **不能断言它与本轮改动无关**（本轮确实让 `ZZProbeHardExit` 那条崩溃路径多做了一次启动）。
   **处置**：变异驱动里显式识别这个签名——基线撞上就重跑一次基线；某条变异的多余红点
   **全部**落在该签名里才重跑一次；出现别的红点一律按真红点处理，**不做重试**。
   它同时也说明：**自测本身的稳定性是这套验证链的地基**，这条债记在 §4.1。

**七、还没做到的**：`Tests/Folder` 在 ubuntu 上到底撞了哪一行，**本轮仍然不知道**——
它要等下一次 CI（补跑产物 `verbose.txt` 会被上传）。按 §5 的纪律，ubuntu 那个崩溃
**不能**因为「诊断工具就位了」而勾掉任何一条完成标准。

### 1.35 ubuntu 自测连续 13 次红的真因：运行器把**超时**套件的用例数也累加了（ENG-003 / ENG-004 的基础设施，2026-09-21 21:5x）

**这一节回答的是上一轮留下的那个「为什么会红」，答案不在 `Tests/Folder` 这条线上，
而在运行器自己的累加逻辑里。** 上一轮把「自测」加进 CI（`aca7621`，11:23）之后，
ubuntu 腿**连续 13 次**红在同一条断言上：`合计把各套件的用例数累加起来了`。
更贵的是它的连带效应——这一步红了，紧随其后的「运行测试套件」会被 **skipped**，
于是 ubuntu 腿**一个套件都没跑过**（「ubuntu 腿只剩 1 个崩溃」这句结论，实际上是
`Tests/Folder` 在自测修好之前最后一次真的跑起来时的旧数据）。

**一、定性：先证明它是确定性的，再去找根因**。两条证据：① 顺着 CI 历史往回读，
这条红从自测加入的那一次起**每一次都在**、红点固定是同一条 ⇒ 不是偶发；
② 起一条 scratch 分支（`diag/runner-selftest-ubuntu`，run `35612212537`）专门跑一次，
读它的证据块：**两遍都是 `合计：7 passed, 2 failed`**，而挂死探针那一行是
`ZZProbeHang status=134 totals=Totals: 1 passed, 1 failed, 0 skipped, 0 blacklisted, 5007ms has_summary=1 timed_out=1`。
把这行与 macOS 上同一条探针对照，差别只有一处：**macOS 是 `has_summary=0`**。

**二、根因（平台分歧的「超时」形状）**：Qt Test 在 Linux 上接住 `SIGTERM` 之后
**会自己补写一份统计行**再以 `SIGABRT` 收尾（于是退出码是 134、`Totals:` 行存在）；
macOS 上进程直接被 `TERM` 带走，什么也不写。而运行器累加用例数那一步用的是
「有没有统计行」（`hs -eq 1`），**没有排除超时** —— 于是挂死探针那 `1 passed, 1 failed`
被算进了合计，与断言里写死的 `6 passed, 1 failed` 对不上。修法是一行：

```bash
if [[ ${hs} -eq 1 && ${to} -eq 0 ]]; then   # 原来是 if [[ ${hs} -eq 1 ]]
```

这与运行器自己打印给用户的那句话（「超时的套件不计入上面的合计」）本来就该一致——
**是实现的判据比它的承诺少了一个条件**。

**三、光改实现不够：这条边界的可观察性本身是平台相关的**。本机 macOS 上挂死探针
**不写**统计行，`hs` 恒为 0 ⇒ 累加这一步根本不会被走到，于是**任何本地变异都
redden不了这条断言**（真实发生的漏检机制：写在 CI 上、本机永远绿）。
所以本轮的第二个改动是**让挂死探针先自己写下统计行、再挂死**：
它现在从 `QCoreApplication::arguments()` 里取出 `-o <file>,junitxml` 的路径、截掉逗号
后面的格式，写一行 `Totals: 1 passed, 1 failed, 0 skipped, 0 blacklisted, 5007ms`，
然后 `QThread::sleep(25)` 等看门狗来打断。这样「有统计行 + 超时」这个组合在
**两个平台上形状一致**，边界在任何平台上都可验；同时它也是「超时不计入合计」这句话
的**反例夹具**：删掉 `to -eq 0` 这半句，合计立刻变大。

**四、两条顺手的诊断改进**（都是「失败时无从判断」被真踩过之后的产物）：

1. `st_line_matches()`：整行断言失败时**同时打印期望与实际**。原来只报一句
   「不相等」，而 CI 上那条断言的期望值是硬编码的一整行，实际值是什么完全看不见——
   这正是上面那条红连续 13 次没人能一眼定性的原因。
2. `st_snapshot_evidence()`：在**每一遍结束立刻**把证据写盘
   （`evidence-jobs4.txt` / `evidence-jobs1.txt`，含合计行、运行器自己的异常清单、
   每个探针的 `summary.env` 字段与 `Totals:` 行）。**两遍共用一个构建目录**，
   第二遍会把第一遍的产物原样覆盖——所以「按遍快照」不是锦上添花，而是唯一能让
   第一遍的证据活下来的方式。上一轮那条 scratch 运行能一眼定位根因，靠的就是它。

**五、探针的第一版一个字都没写出来**：新挂死探针刚写好时**三个探针同时失败**，
报的是 `QCoreApplication::arguments: Please instantiate the QApplication object first`
——`QTEST_APPLESS_MAIN` **不创建任何 application 对象**，而取命令行参数要它。
改用 `QTEST_MAIN` 即可（本工程 `QT -= gui`，于是创建的是 `QCoreApplication`）。
**记住这条一般规律：探针要用到 `QCoreApplication` 的任何静态设施时，
`QTEST_APPLESS_MAIN` 就不成立。**

**六、验证数据**：

- `run-tests.sh --self-test`：**32 条断言全绿**（上一轮 30 + 本轮新增 2 条：
  「挂死探针在被打断前写下了统计行」——否则「超时不计数」是空转的；
  「无统计行清单只收『非超时且没统计行』的两个探针」——超时的那个不许重复出现）。
- 全量 **3688 passed / 0 failed / 2 skipped（69 个套件）**，`EXIT=0`，与本轮之前逐项相同。
- 五道护栏逐个 `exit=0`（layering / winapi / spec / icons / shell），**收进变量再取 `$?`**。
- 主程序增量构建 `MAKE_EXIT=0`、本仓 `warning:` 计数 **0**；`codesign -f -s -` 重签；
  离屏启动三行齐全（「单实例机制已由选项关闭」→「Ribbon 构建完成：10 页 / 45 组 / 169 个按钮」→
  「LqCompare 0.1.0 启动完成」），`qt.svg` 报错 0 次。**本轮一行 C++ 都没动。**
- **反向验证：5 处变异 5 处检出、0 处漏检**（驱动 `/tmp/lqcompare-mutate-r27.py`，不进仓库）：
  M1 累加退回 `hs` 单独判定 / M2 挂死探针不再写统计行 / M3 看门狗不置超时标志 /
  M4 无统计行清单的判据反过来写 / M5 无统计行计数不再累加。**这一轮不再需要
  「已对偶发签名重跑过」那句限定**——上一轮那条偶发红点（`ZZProbePass` 产物与合计对不上）
  本轮 6 次自测（基线 + 5 处变异各一次）一次都没出现，但仍未查清机制，债留在 §4.1。

**七、仍然不知道的**：`Tests/Folder` 在 ubuntu 上到底撞了哪一行。上一轮刚就位的
「崩溃自动补跑 `-v2`」要等**这次** ubuntu 腿真的跑起套件来才会产出 `verbose.txt`
（自测修好之前，那个补跑功能在 CI 上从来没有机会被触发）。**本轮没有勾掉任何完成标准。**

**八、顺带关掉一条此前的存疑**：Windows 腿的「运行器自测」在 CI 页面上显示 **success**，
不是「步骤号映射错了」——那一步在 Windows 上先 `echo "::warning::已跳过 …"` 再 `exit 0`，
是**看得见的跳过**（`::warning::` 会出现在注解里）。§4.1 里那条「需要再核对」的疑问就此关闭。

### 1.36 CI 终于回答了那条崩溃线：崩在用例 `linksAreComparedWithoutFollowing()`（2026-09-21 22:4x）

**为什么现在才有答案**：`Tests/Folder` 在 ubuntu 上被 glibc 掐掉的这条线，前面连排了**十几轮**
第一位都推不动——本机结构性看不到（macOS 的 libc 没有 `_FORTIFY_SOURCE`、ASan 也静默）。
上一轮把「崩溃的套件自动补跑一遍 `-v2`」落进运行器，本轮把自测那条红修掉之后，
**ubuntu 腿第一次真的跑起了套件**，于是这份诊断**第一次有机会产出**。

**一、实测到的三件事**（读的是 run `35613355230` 的 `test-logs-ubuntu-latest` 产物）：

1. **首轮 `results.txt`** 停在 `PASS : FolderTests::recursionLimitIsExplicitUnknown()` 之后，
   **没有 `Totals:` 行** ⇒ 崩在**下一个**用例，即 `linksAreComparedWithoutFollowing()`。
2. **补跑的 `verbose.txt`**（上一轮那条功能的新产物，第一次在真实 CI 上生成）把范围收得更紧：
   最后一条 `INFO : … entering` 就是那条用例，里面**逐条记下了 4 条断言**（`QFile::link` 四次，
   行号 466 / 467 / 468 / 469）——**最后一条跑完的是 469**，`470` 是空行，
   `471` 是 `const auto result = Folder::compare(pair.left, pair.right);`。
   **⇒ 崩溃发生在 `Folder::compare()` 这一次调用里**，而且是在它还没写出任何一条断言之前。
3. **补跑同样崩了**（`stderr.log` 与 `verbose.stderr.log` 都是那两行），所以**它不是偶发**：

   ```
   *** buffer overflow detected ***: terminated
   Code/Tests/run-tests.sh: line 613:  5919 Aborted                 (core dumped) "$@"
   ```

**二、这条用例的特殊之处**（下一步的二分方向就在这）：它是全仓唯一一个造出
**指向自己所在目录的目录符号链接** `cycle`（466 / 467 行，两个根各一个）与
**悬空符号链接** `dangling`（468 / 469 行，目标是临时目录下一个不存在的路径）的用例——
两者都是「解析目标」这条路的边界输入。而 `Code/Tests/Folder/tst_folder.cpp` 的
崩溃点之前**每一条断言都过了**：也就是说，`QFile::link` 成功、两个根目录也都建好了，
炸的是**比对过程本身**（`enumerateDirectory` → 符号链接判定 → `linkTarget()` →
`canonicalFilePath()` 这一串里的一处）。

**三、可以立刻排除的**（省掉下一轮的无效方向）：

- **不是我们的固定缓冲**：`Services/Files/` 与 `Services/Folder/` 下**没有任何栈上 `char[]`**，
  也没有 `strcpy` / `sprintf` / `memcpy` / `strncpy`（`grep` 实查）。
  `linkTarget()` 用的是**动态增长**缓冲（256 起、每次翻倍、64KB 上限），并且按 `length` 截断后
  才解码——这一处**看不出越界**。
- **不是死循环/栈溢出/OOM**：glibc 那句话点名的就是 `_chk` 家族（缓冲区越界），不是别的。
- **不是本仓可复现的**：本机 `Tests/Folder` 是 **36 passed / 0 failed**。

**四、下一步的候选动作**（按代价从低到高，全部需要 CI 回答）：

1. **二分夹具**：在一条 scratch 分支上把 `cycle` 那两个链接去掉（只留 `dangling`），
   再跑一次 ubuntu 腿——崩溃如果消失，触发条件就是「目录符号链接指向自己」；
   如果还在，就是悬空链接那条路。
2. **要一份栈**：在第 7 步之前给 ubuntu 腿加一次 `gdb -batch -ex run -ex bt`（只在
   该套件崩溃时跑），把 `_chk` 失败的那一帧指出来——`verbose.txt` 只能给「哪一条用例」，
   给不了 C++ 帧。
3. 拿着 ①/② 的结论再动代码。**在拿到结论之前不要改 `foldercompare.cpp`**：
   那一串调用里既有本仓代码也有 Qt/glibc，凭猜测改一处的成本是「改错 + 一次 CI 往返」。

**五、这条线的账要记清**：`Tests/Folder` 的崩溃**依旧没有被修好**，本轮也没有勾掉
`ENG-004`（#336）的任何一条完成标准。本轮真正改变的只有一件事：
**它从一个「只知道进程没了」的谜，变成一个「知道崩在哪个用例、哪一次调用」的已知问题。**

### 1.37 DIR-011 条目状态判定与语义：把「一档结论 + 三个维度」从一句话变成可执行的东西（2026-09-22 14:4x）

issue [#120](https://github.com/LorenHan/LqCompare/issues/120)。五条完成标准全落，
服务层新模块 `Code/Services/Folder/entrystatus.{h,cpp}`（约 780 行，含注释），
新套件 `Code/Tests/EntryStatus/`（**27 个用例函数，纯 QtCore**），
`Tests/Folder` 35 → **45**（+9 个用例函数，走真实文件系统与真实视图）。

**为什么这条值得单开一个模块**：这条规格的原文是「主比较状态互斥；存在性、内容结论、
时间关系与扫描完整性分别存储」。前半句早就成立了（`Status` 一直是互斥枚举），
后半句才是这条条目的实质——**「分别存储」不是三个字段，是三套各自可读的事实**。
所以本轮的产出大部分不落在引擎上，而落在「怎么让这四句话各自都能被判对错」上：

- **四张描述子表**（主状态 9 档 / 内容证据 8 档 / 时间关系 4 档 / 三条存在性文案），
  每张表都带一个**把表当参数**的校验函数（`validateMainStatusTable(table)` 等）。
  校验函数若自己读模块内部那张表，「喂一份故意写坏的表进去」也永远绿——自检变成恒真。
  `Tests/EntryStatus` A/B/C 三组正是喂坏表进去的那一侧。
- **内容证据这一维刻意多出三档「…不同」**。规格点名的是五档（字节相同 / 规则相同 /
  CRC 相同 / 未比较 / 部分比较），全是「相同侧」的说法。只留这五档的话，
  **「比过而且不同」这个最常见的情形**就只能靠主状态反推，两维当场失去独立性——
  一个 `status == Different` 的条目，证据该填「字节不同」还是「未比较」？
  那时又得靠猜，而猜错的方向恰好是「声称读过没读过的内容」。另加一条**蕴含关系**
  并由校验函数守着：声称能证明内容相同（`provesContentIdentity`）就必须真的读完了
  全部内容（`coversWholeContent`）。
- **`partialComparison` 从布尔字段改成 `contentEvidence == Partial` 的只读视图**。
  DIR-008 那一轮把它做成独立字段，于是「部分比较」同时存在两份说法（一个布尔 +
  证据维里的 `Partial`）；两份说法迟早分叉。本轮删掉字段，`Entry::partialComparison()`
  改成方法，四个消费点（引擎 + 三个用例）改为调用——DIR-008 那一轮的三条边界行为
  逐字节不变。
- **存在性是派生视图，不另存字段**。`Existence` 由两侧 `exists` 算出来；
  另存一个字段就等于给同一件事两份说法，而它没有任何一份是「另一份算不出来」的。
- **基线只细化、不推翻，且只在叶子上**。`BaselineView` 要过 `validateBaselineView()`：
  必须标记有效、必须同时绑定两侧根目录、至少一条祖先记录、键必须是相对路径。
  最后一条尤其重要——绝对路径的键**永远查不中任何条目**，于是整条基线会**静默失效**，
  只看「非空」是发现不了的。`Result::baselineApplied` 如实记录「这一次到底用没用上
  有效基线」：界面要靠它解释「为什么这一条不是两侧均改」，没有这一位界面只能靠猜。
- **父目录的结论就是子条目汇总本身**。`aggregateChildren()` 是引擎与用例**共用**的
  唯一实现——引擎自己写一份、测试另写一份的话，「固定数据源与准则下父子视图结论一致」
  会退化成「两套口径碰巧今天结果相同」。共用之后真值表可以脱离文件系统跑（15 行组合
  + 掩码排除两行），引擎那侧只需断言「把子条目喂回同一个函数，结论与写在父目录上的
  那一格逐字相同」，并且**把子条目反过来再喂一遍**（目录的枚举顺序来自文件系统，
  不归我们决定，所以汇总不许依赖遍历顺序）。
- **视图侧**：状态列新增图标（`DecorationRole`）、状态筛选下拉改成按主状态表铺
  （界面不另写一份清单，否则新增一档状态时界面会静默落后一格）、
  `setStatusFilter()` 对表外整数回落到「全部」（状态整数要经「模型角色 → 下拉数据 →
  筛选」三跳，任一跳出错都会给出一整屏空白——那是最难归因的一种失败）、右键
  「为什么是这个状态…」菜单项与非模态理由弹窗。**三节（准则 / 覆盖策略 / 最终结论）
  恒定出现**，空小节写「（无）」：按需省略会让读者分不清「这一节没有内容」和
  「这一节根本没实现」，而排查一个诡异状态时最要紧的恰是这一区分。
  9 个状态图标是**中性灰描边**（不同形状，不靠颜色）：灰度打印与色觉障碍下
  颜色是第一个失效的信息。
- **报表与命令行**改为查同一张表：`report.cpp` 把「两侧均改」映到既有的
  `State::Changed`（同步意义上仍是「内容不同」）、「冲突」映到 `State::Conflict`；
  `cliexecution.cpp` 由「自带 `default: break;` 的 switch」换成表查询 + 空标识符报错。
  **刻意新增一条纪律**：`MainStatusDescriptor` 里**不加** `countsAsDifference` 字段——
  本仓对「哪些状态算差异」有三套各自合理的口径（`differenceIndexes()` 跳过 Error /
  TypeConflict；`FolderFilterModel` 只排掉 Same；CLI 把 Unknown 也算差异）。
  加一个字段就会顺带改掉三个消费点的行为，那不是 DIR-011 要动的东西。

**验证**：`Tests/EntryStatus` 27 全绿、`Tests/Folder` 45 全绿、全量
**3724 passed / 0 failed / 2 skipped（70 个套件）**、五道护栏全绿
（`check_icons` 从 32 个图标涨到 **41**，正是新增的 9 个状态图标 +
`check_winapi` 从 341 涨到 **345** 个源文件）、主程序全量重编 **0 条本仓 warning**、
离屏启动到 `LqCompare 0.1.0 启动完成` 且**一次 `qt.svg: Cannot open file` 都没有**。

**反向验证：15 处变异，15 处检出**。其中两处需要一次改**两**个地方才可观察，
理由是仓库里同一判据写了两遍、两道防线互相遮蔽（详见 §6 新增的那条）：
① `applyBaselineStatus()` 的 `entry.isDirectory()` 与 `sideDiffersFromAncestor()` 里
`Kind::Directory` 那道；② `timeRelationFor()` 的 `exists()` 与 `compareTimes()` 的
`isValid()`。单摘一道时全集仍然绿——这不是缺口（行为确实被守着），但它意味着
变异脚本必须支持「一次改多处」，否则会把**等价变异**误报成漏检。

**还没做的 / 有意留空的**：
- `RuleIdentical` / `CrcIdentical` 两档进了类型模型、证据表与理由链，但引擎暂时
  只产得出字节档——规则比对与 CRC 比对分别属 DIR-009 / DIR-007，尚未开工。
  第 1 条要求的是证据这一维**能区分**这些档，不是要求兄弟条目先落地。
- 主状态表里「扫描完整性」只以 `Unknown` + `complete == false` 的形式表达，
  没有做成第五个维度；规格把「扫描完整性」与另外三个维度并列提及，但从未要求
  它成为一条独立的条目字段。**如果维护者认为这算缺口，那是一条新的条目**，
  不是 DIR-011 少做了一半。
- 状态着色口径（「两侧均改」用中性偏暖、「冲突」用最重的红）**只落了颜色值**，
  系统性的配色规则归 DIR-012。

### 1.38 DIR-003 递归子目录策略：三档本来就可达，缺的是「谁在守它」（2026-09-22 15:2x）

**issue #111 的五条标准**：① 三档（根目录直属条目 / 递归深度 1 / 完全递归）；
② 深度边界上的子目录仍显示且标记未扫描，内部未枚举内容不得计为相同或不存在；
③ 切换档位触发重新扫描；④ 递归深度可另设上限、达到上限时明确提示；
⑤ 循环符号链接导致的无限递归被检测并终止，记为错误条目。

**先核对，再动手**（DIR-008 的教训）：`Services/Folder/foldercompare.{h,cpp}` 里的
`Options::recursive` 与 `Options::maximumDepth` **早就在**，`walk()` 也早就在
`depth < qBound(0, maximumDepth, …)` 上做递归判定。也就是说第 1、2、4 条在**引擎**里
是可达的，缺的是：

- 三档**没有名字**，界面里是一个两态复选框 `递归子目录`，用户表达不出「深度 1」；
- 深度上限**没有任何控件**，只能靠改 `.lqc` 或命令行；
- 边界上的解释文案**写在引擎里一句 hard-coded 的中文**，用例够不着；
- 第 5 条**从来没实现**——符号链接在 `classify()` 里只比较目标串。

**新模块 `Code/Services/Folder/recursionstrategy.{h,cpp}`**：

| 能力 | 接口 |
| --- | --- |
| 三档表 + 自检 | `recursionTierTable()`、`validateRecursionTierTable(table)`（七项检查） |
| 档位 ↔ `Options` | `applyRecursionTier()`、`recursionTierOf()`（反查**按行为**归类） |
| 文案 | `recursionTierLabel/Description()`、`recursionBoundaryExplanation(options)`、`linkCycleExplanation()` |
| 循环判据 | `linkTargetReentersAncestor(root, relative, target, cs)` |
| 常量 | `kDirectChildrenDepth = 0`、`kDefaultFullDepth = 128`、`kMaximumRecursionDepth = 256` |

**本轮最值钱的一条设计取舍：不给 `Options` 加第三个字段。** 三档在引擎里已经全部
可达（`(recursive, maximumDepth)` 的三种组合），再存一个 `RecursionTier` 就会让同一件事
有两份说法。代价必须说清：`recursive == false` 配 `maximumDepth == 7` 是一个**合法存档值**
（引擎里与「不递归」同行为），所以反查**按行为归类**（`!recursive || maximumDepth <= 0`
是第一档），而 `setOptions()` **一个值都不归一化**——否则 `.lqc` 里那个 7 会在一次
往返里静默变成 0，`savedFolderOptionsRoundTripThroughLqcAndActuallyScan` 会红。

**两处「谁在守它」的追问各换来一条真防线**（这是 §6 那条纪律的直接收益）：

1. **`recursionBoundaryExplanation()` 印的是 `qBound(0, maximumDepth, 256)`，不是原值。**
   问「谁在守这个 `qBound`」→ 没有任何用例。补一条：传 300 进去，文案必须含 `256`、
   不含 `300`。不补的话，把 `qBound` 换成原值**全绿**（变异 M10 已验证它会红）。
2. **`isStrictAncestor()` 里那个 `ancestor.endsWith('/')` 的三元。** 问「谁在守根目录
   作上级这一支」→ 也没有。补一行纯函数表格行（`root = "/scan"`、`target = "/"`）。
   不补的话，把前缀写成 `ancestor + '/'`（于是 `/` 变成 `//`）**全绿**（变异 M09 已验证）。

**踩到的另一个坑：`applyTierToControls(tier, bool fromUser)` 是一道遮蔽防线。**
原来的写法把「深度控件写入」放在 `if (fromUser)` 里，理由是「恢复存档时一个值都不动」。
听起来是纵深防御，实测**删掉它没有任何用例变红**（变异 M13 第一次跑就是 green）：
程序性路径（`setOptions()`）里那条写入紧跟着就被 `setValue(qBound(options.maximumDepth))`
覆盖；而深度控件自己的处理器那条路上，由档位反推出来的深度**恒等于**控件当前值
（`probe` 的 `recursive` 默认是 `true`，所以 `Full/4 → Full`、`OneLevel/1 → OneLevel`、
`DirectChildren/0 → DirectChildren`，都是恒等写入）。处置与 TXT-010 那条**同一个判据**：
「删掉之后没有任何用例变红的分支不是纵深防御，是没人知道的死代码」。收成
「深度控件只有两个写入者」（用户换档处理器 / `setOptions()`）之后，M13 变红。

**界面侧（`Code/Views/Folder/foldercompareview.{h,cpp}`）**：两态复选框换成按
`recursionTierTable()` 铺的 `QComboBox`（`objectName = folderRecursionTier`，每项带
`Qt::ToolTipRole`）+ 一个 `QSpinBox`（`objectName = folderMaximumDepth`，
`setRange(0, kMaximumRecursionDepth)`）。新增**公开**访问器 `Folder::RecursionTier recursionTier() const`，
好让「控件选中的」与「实际生效的」两条链都能被断言；新增信号 `rescanRequested()`
**刻意不带路径参数**（路径的唯一来源是会话）。深度上限只在「完全递归」档下可改，
不可改时**保留**数值（清成 0 会让「换个档试一下再换回来」把用户设好的上限丢掉）。

**会话侧（`foldercomparesession.cpp`）**：`rescanRequested` → 已打开就 `reload()`，
未打开就只记档位（不许弹「请选择文件夹」）。两个写 256 的地方换成 `kMaximumRecursionDepth`。

**测试**：`Tests/Folder` **45 → 51 个用例函数**（+6），套件合计 **45 → 51**；
`run-tests.sh Folder` 合计 **100 passed**（Folder 51 + FolderMerge 31 + FolderMergeView 18）。
全量 **3730 passed, 0 failed, 2 skipped**（本轮 +6）。

**变异测试：15 处改动，15 处全部被检出**，无一处「删掉之后没有任何用例变红」。其中 4 处
（M02 / M08 / M12 / M15）额外多红了别的用例，属同一判据被多条用例同时守着（多点覆盖），
不是漏检——但值得记下来：M08（关掉 `linkTargetReentersAncestor` 的「相等」分支）**没有**
打红 `linksAreComparedWithoutFollowing`，因为那条用例里 `cycle` 的链接目标是**绝对路径
恰好是扫描根**，走的是「严格上级」那一支。即两条分支各有各的用例，互不代劳。

**还没做的 / 有意留空的**：
- **入口没接**：档位下拉只在**文件夹比对页**的工具条上，设置页（`OPT-*`）里还没有
  对应的设置项——「谁在哪里配置递归策略」属于设置页那一族条目。
- `Code/Services/Snapshot/snapshot.{h,cpp}` 里另有一份 `maximumDepth = 128` /
  `qBound(0..256)`，那是**快照捕获**的独立模块，不属于 DIR-003 的射程（本轮**未动**），
  两处常量目前是两个来源；真要合并得等快照也接入同一张档位表。
- `entrystatus.cpp` 的 `statusReasonLines()` 里已有一句「递归已关闭：子目录不展开比较。」
  与 `recursionBoundaryExplanation()` 说同一件事。两者受众不同（一个进「为什么是这个状态」
  的理由链、一个是条目 explanation），文案也不逐字相同，本轮**未合并**——记在这里，
  免得下次有人把它当新发现的重复。

### 1.39 DIR-012 状态着色与图标：接手一轮被中断的活，并扫掉五处「没人喂过输入」的校验分支（2026-09-24 00:5x）

**issue #122 的五条标准**：① 每类状态有默认颜色与图标，且图标随主题切换自动适配深浅；
② 至少 3 套配色方案（默认 / 高对比 / 色盲友好）并可切换；③ 颜色仅作为辅助，
状态图标与文字提示必须同时存在；④ 着色方案切换立即重绘列表，不需要重新扫描；
⑤ 自定义颜色可导出为配色文件并分享。

**本轮先接手。** `run.lock` 里 `pid=36005` 已死（`kill -0` 失败）、`pgrep` 里没有别的
`--session-id`、`Code/` `docs/` `tools/` 下 12 分钟内无文件改动 → 判为「上一轮被中断」，
接手（写自己的 pid，`$PPID`）。工作树里是上一轮留下的半成品：新模块
`Services/Folder/statuspalette.{h,cpp}` 已在、新套件 `Code/Tests/StatusPalette/` 已在、
视图侧已接线、`architecture.md` §3 的目录行已改，**但新套件构建不过**——
头文件里声明了 `anIdentifierThatIsNotMachineReadableIsRejected()` 与
`anIdentifierIsJudgedOnItselfNotOnlyAgainstItsSiblings()` 两个用例，`.cpp` 里没有实现，
链接报 `Undefined symbols`。这两条实现掉了，本条就闭环了。

**接手时先核对三件事**（DIR-008 的教训）：① 逐条读五条标准；② `ls` 源码与测试目录，
并在用例里找得到对应断言才算落过；③ 在本文档里 `grep` 一次 `DIR-012` /
`statuspalette`。结论：实现与界面接线都在，**第 4 条与第 5 条各有一次「语义重复」**
（见下），而校验层有五条分支没有任何输入能走到。

**那两个用例守的是一条曾经被蛀空的判据。** `validateOneColorScheme()` 里「标识符必须是
小写字母 / 数字 / 连字符」那段原本被 `if (false)` 包着（上一轮接手时已修），
而 `isMachineReadableIdentifier()` 当时全仓找不到调用者。两条用例**分开**钉「形状」
与「唯一性」，因为只有唯一性在位时有一条很隐蔽的漏检路径：**一个叫「High Contrast」
的方案没有重名，唯一性检查放它过去**，于是「标识符机器可读」这件事没有任何东西会红。
判据一句话：**「不重名」不等于「名字合法」。**

**本轮最值钱的一条（同族通用）：扫一遍有没有「没人喂过输入」的校验分支。**
做法是把 `validateOneColorScheme()` / `validateColorSchemeTable()` 里每一条
`problems <<` 的中文片段当针，在整个测试集里搜一遍。搜不到的五条——**缺展示名、
缺说明、参考背景不是 `#rrggbb`、两档背景不呈一明一暗、「已排除」弱化色不是
`#rrggbb`**——不是代码写错了，而是**没人喂过能触发它们的表**。这与「分支写了但走不到」
是同一类风险：下一个人会把它当纵深防御，删掉之后不会有任何东西变红。
补法极便宜：新加 `aSchemeIsJudgedOnItsNameAndItsReferenceBackgroundsToo()` 一个用例
（逐条把表掰到能触发它们的形状），并在既有的
`theExcludedColourMustBeReadableAndDistinctFromSame()` 里补一条
（那一支与「弱化色对比度不足」共用「已排除」三个字，所以判据要用只在这一支里出现的
「弱化色必须」）。

**变异测试：30 处改动，30 处全部被检出、0 处漏检、0 处无效**
（驱动 `/tmp/lqcompare-mutate-dir012-v2.py`，**不进仓库**）。两处值得单独记：

- **一处探针（P01）抓到了真缺口**：把 `ColorScheme::coversEveryStatus()` 的第二句
  `return highlights.size() == statuses.size();` 改成 `return true;`（即只查「九档都在」、
  不查「没有多余的行」），**29 条用例一条都不红**。这半句此前无人守——既有夹具只有
  「九档齐全」这一种形状。补了三条断言（`duplicated[0]` 十行九档、`missing[0]` 八行缺一档、
  `alien[0]` 十行含表外状态）之后，同一个探针当场变红，计入检出。
- **5 处变异（M02 / M03 / M14 / M16 / M19）除期望的那条之外还连带打红了别的用例**。
  逐条看过，全是**同一判据在仓库里写了两遍**造成的（例如 `contrastRatio()` 对非法值
  返回 `-1`，同时被 `anUnparsableColourIsNotSilentlyTreatedAsBlack` 与
  `theTwoReferenceBackgroundsAreOppositeInLightness` 两个用例钉住）。这与 DIR-011 那条
  「先问这个行为是不是有第二个实现处」是同一件事，本轮按「连带变红也算检出」记账，
  但列在报告里，免得下一轮把它当无关污染或反过来当漏检。

**测试**：**新套件** `Tests/StatusPalette`（**29 个用例函数**，刻意 `QT -= gui`——配色表、
对比度与色觉模拟全是纯算术，一个 `QColor` 都不需要；一旦有人把 `QColor` 塞进
`statuspalette.cpp`，这个工程会立刻构建失败而不是等某台没有图形环境的机器上才发现）；
`Tests/Folder` **51 → 55 个用例函数**（J 组四条守界面侧的第 1 / 3 / 4 / 5 条）。
全量 **3730 → 3763 passed / 0 failed / 2 skipped（70 → 71 个套件）**。

**验证**：五道护栏全绿（`check_icons` 41 个图标、`check_winapi` 351 个源文件、
`check_spec` 369 条 / P0 59 条）；主程序全量重编 **0 条本仓 warning** + 重签 +
离屏三行日志（「单实例机制已由选项关闭」→「Ribbon 构建完成」→「LqCompare 0.1.0 启动完成」）。

**还没做的 / 有意留空的**：
- **入口住所**：配色下拉与「配色…」菜单落在**文件夹比对页**的「显示」工具条上；
  规格说的 View 页 Coloring 组属 `OPT-*`（未落地）。与 DIR-003 的档位下拉同一处置，
  理由也相同：它改的是画出来的样子，**不进 `Options`、不改变结果集**，
  放在「子目录 / 逐字节比较内容」旁边会让人以为它影响比对。
- **导入的自定义配色不持久化**：它只活在本次会话，不进 `colorSchemeTable()`（那是出厂
  常量）、也没有对应的设置键。把用户导入的配色存成第四个设置项是一次独立的产品决定
  （属 View 页 `OPT-*`），本轮刻意不做。
- **`ColorScheme::colorFor()` 取不到该档时返回空串**，由视图回落到 Qt 默认前景色。
  刻意**不**回落到「未知」那一档的颜色——否则「配色缺了一档」在界面上完全看不出来，
  而它正是配色导入最可能的坏法。
- `TypeConflict` 的浅色档从 `#aa2424` 改成了独立色值：上线时它与 `Error` 共用同一个
  颜色，而第 1 条要求「每类状态有默认颜色」，共用等于其中一档没有自己的颜色。
  这是本条对默认外观**唯一**有意改掉的一处，其余八档与上线时逐值相同（零回归）。

| 项目 | 结论 | 验证方式 |
| --- | --- | --- |
| 构建 | Qt 5.15.2 clang_64 上 qmake + make 通过，产出 `dist/macos/LqCompare.app` | `qmake && make -j8` |
| 运行 | 主程序离屏启动正常，日志显示「Ribbon 构建完成：10 页 / 45 组 / 169 个按钮」 | `QT_QPA_PLATFORM=offscreen ./LqCompare --log-level info` |
| 测试（全量） | **3763 passed / 0 failed / 2 skipped，71 个套件**（2026-09-24 00:5x 实测，EXIT=0）。3763 = 3730（DIR-003 那一轮，见 §1.38）+ 33（DIR-012 本轮：**新套件** `Tests/StatusPalette` 29 个用例函数 + `Tests/Folder` 51 → 55）；3730 = 3724（DIR-011 那一轮，见 §1.37）+ 6（DIR-003：`Tests/Folder` 45 → 51，**没有新增套件**）；3724 = 3688（DIR-008 那一轮，见 §1.33）+ 36（DIR-011：**新套件** `Tests/EntryStatus` 27 + `Tests/Folder` 35 → 45）；此前的 3688 = 3681（TXT-012 那一轮，见 §1.32）+ 7（`Tests/Folder` 30 → 36、`Tests/Report` 36 → 37），**没有删改任何既有用例**。2 条跳过分别来自 `PathName`（40/0/1）与 `Registry`（61/0/1），都是按平台条件跳过的用例 | `Code/Tests/run-tests.sh` |
| 运行器自己也有红有绿（ENG-003 第 2、3 条，§1.34 从 24 涨到 30，**§1.35 起是 32**） | `run-tests.sh --self-test` 在**临时目录**里现造五个探针套件（通过 / 失败 / 挂死 / 硬退出 / 构建期失败），用同一个运行器跑两遍（并行 4 与串行 1），**32 条断言全绿**；并发度实测**并行峰值 3 / 串行峰值 1**。挂死探针**自己先写一份统计行再挂死**（否则「超时不计入合计」这条边界在 macOS 上根本不可观察，见 §1.35），两遍的 `合计：6 passed, 1 failed, 0 skipped` 逐字相同。**证据在每一遍结束时立刻按遍快照**（`evidence-jobs4.txt` / `evidence-jobs1.txt`）——两遍共用一个构建目录，不快照就只剩第二遍的。探针跑完即删、不进仓库（`LQCOMPARE_SELFTEST_KEEP=1` 可留现场）。自测同时是超时保护与「产物必须先删」两条规则的**唯一可重复证据**（这两条只在失败路径上才看得出来） | `Code/Tests/run-tests.sh --self-test` |
| ubuntu 腿自测连续 13 次红的真因（§1.35，本轮修，**以后不用再猜**） | 红点固定是 `合计把各套件的用例数累加起来了`。真因不是 `Tests/Folder` 那条崩溃线，而是**累加那一步没排除超时套件**：Linux 的 Qt Test 接住 `SIGTERM` 后会**自己补写一份统计行**再以 `SIGABRT` 收尾（`status=134` + `has_summary=1`），macOS 则直接被 `TERM` 带走、`has_summary=0` —— 于是「有没有统计行」这个判据在两个平台上是**两种形状**，挂死探针的 `1 passed, 1 failed` 在 ubuntu 上被算进了合计。修法：`if [[ ${hs} -eq 1 && ${to} -eq 0 ]]`。连带效应值得记住：自测步骤一红，后面的「运行测试套件」会被 **skipped** ⇒ ubuntu 腿**一个套件都没跑过** | CI run `35612212537`（scratch 分支）的证据块 + 本机 5 处变异 |
| ENG-003 能反向验证 | **13 处变异 13 处检出、0 处漏检**（其中有**两处第一次是真漏检**：M4「盯错了量」、M13「只比文件名，覆盖写看不出差异」——两处都是**验证本身有洞**而不是实现有 bug，补强断言后才抓住；成因见 §1.24 第四、七节） | 变异测试（结论写在 issue #335 的落地说明里），驱动 `/tmp/lqcompare-mutate-eng003.py`，**不进仓库** |
| 测试不许脏工作目录（ENG-003 第 5 条，本轮修） | `Tests/OptionsDialog` 原来每跑一次全量测试就把三张对话框截图写进仓库根（`QDir::current()`），现已落到 `QTemporaryDir`，并断言「工作目录里那几个 `options-*.png` 的**指纹**（名字 + 大小 + 修改时间）没变」——只比名字会被同名旧文件掩盖（实测 M13 第一版就此漏检）。实测：改成临时目录之后再跑该套件，三个旧文件的 mtime 保持不变 | `Code/Tests/run-tests.sh OptionsDialog` |
| 行对齐的四条标准**现在真的被守住了**（TXT-002，本轮补） | 标准 3 的「全相同 → **单个**相同块」与标准 4 的「逐字段相同 + 快照」原本都没有约束（只断言了 `differences` 为空、只比过两遍的 `lines.leftLine/rightLine`）。本轮补三个用例后：**9 处变异 9 处检出、0 处漏检**，其中 M5 第一次是真漏检（夹具让 `firstRow` 与块序号恰好相等）。`alignmentLimited` 也第一次有了自己的输入（两侧各 4000 行、只在正中间共享一行） | `Code/Tests/run-tests.sh Text` + `/tmp/lqcompare-mutate-txt002.py`（不进仓库） |
| Qt 5.15.2 的大小写折叠到底是什么（本轮新增，**以后不用再猜**） | `QString::toCaseFolded()` 是 **Unicode simple case folding**（逐码位 1:1），且**与 `QLocale` 无关**。实测：`A`→`a`；`ß`(U+00DF)→`ß` **不展开成 `ss`**；`ẞ`(U+1E9E)→`ß`；`Σ`/`σ`/`ς` **一律→`σ`**；`Ä`→`ä`；`K`(U+212A)→`k`；`µ`(U+00B5)→`μ`(U+03BC)；`Μ`(U+039C)→`μ`；`İ`(U+0130)→**它自己**（不展开成 `i`+U+0307）；`ı`(U+0131)→它自己；`ﬁ`(U+FB01)→**它自己**（不展开成 `ffi`）；非 BMP 的 Deseret U+10400→U+10428 **会被折**。长度守恒实测：BMP 逐码位扫一遍，**1189 个码位会变、0 个长度变化**；非 BMP 的代理对进出都是两个 `QChar`。`toLower()` 与折叠**不等价**：`toLower("ΟΔΟΣ")` = `οδοσ` 而 `toLower("οδος")` = `οδος`（词尾 sigma 不折），两者被判成不同 | 临时探针 `/tmp/foldprobe/`（**不进仓库**），三个小程序分别打字符表、locale 对照、BMP 穷举 |
| 规范化链的四条标准**现在真的被守住了**（TXT-008，本轮补） | 实现（`ignoreCase`、`Change::Ignored`、Rules 勾选框、`text.ignoreCase` 键、`colorFor(Ignored)` 的弱化底色）**早就都在**，缺的是断言与「链」这个名字。本轮把链提成公开的 `normalizedLine()` 并补 **7 个**用例函数（`Tests/Text` 25 → 31，`Tests/TextView` 18 → 19）。**8 处变异 8 处符合预期**（7 处检出 + 1 处按设计漏检），且**每处都比对了 `.o` 的 mtime** 来确认构建真的发生 | `Code/Tests/run-tests.sh Text` + `/tmp/txt008_mutate.py`（不进仓库） |
| 空白三级模式的边界就是「数量 vs 有无」（TXT-009，本轮补） | 本机 Qt 5.15.2 实测：`simplified()` 折叠内部连续空白为一个空格并去掉首尾，`QChar::isSpace()` 认空格 / Tab / CR / LF / VT / FF / **U+00A0 NBSP** / **U+202F 窄 NBSP** / **U+3000 表意空格** / U+2028 / U+2029，**不认 U+200B 零宽空格**。因此 `IgnoreChanges` 下 `ab` 与 `a b` **仍不同**（折叠后那个空格还在），只有 `IgnoreAll` 判等——这就是两级的分水岭，也是「两个模式不是一强一弱」的判据。`Tests/Text` 的固定语料表按这条边界搭，并额外断言判等关系必须嵌套（`Exact ⊆ IgnoreChanges ⊆ IgnoreAll`） | `Code/Tests/run-tests.sh Text`（探针 `/tmp/foldprobe/probe4.cpp`，不进仓库） |
| 空白模式的三级语义**现在真的被守住了**（TXT-009，本轮补） | 实现（`Whitespace` 三值、`normalizedLine()` 里的分支、界面的下拉、`text.whitespace` 键）**早就都在**，缺的是「分水岭」那类语料与模式表。本轮补 **4 个**用例函数（`Tests/Text` 31 → 34，`Tests/TextView` 19 → 20）与模式表/自检。**10 处变异 10 处检出、0 漏检**；其中第 10 处（把下拉的铺法改成倒序）**第一版是真漏检**——见 §1.28 与 §6 那两条新坑 | `Code/Tests/run-tests.sh Text` + `/tmp/txt009_mutate.py`（不进仓库） |
| 相似度分值的**确切数值**（TXT-005，本轮新增，**以后不用再猜**） | 分值定义为 `2·LCS/(len左+len右)` 的百分点（LCS = 最长公共子序列长度），两侧都过 `normalizedLine()`。实测（`/tmp/lqdbg/`）：`abc` vs `cba` = **33**（顺序被计入；按字符集合算会是 100）；`int x;` vs `int x; // 一整段很长的注释` ≈ **38**（短行被长行包含**不**自动满分，这是刻意的）；`bravo Two` vs `BRAVO two` = **33**、`a` vs `left` / `a` vs `right` = **22**——**出厂阈值 50 下它们配不上对**，本轮那批「四个套件一起变红」的假红正是它们造成的。阈值判定**含等号**（分值 == 阈值也算相似）。开了忽略大小写之后 `HELLO world` vs `hello world` 必须是 **100**，否则「开了忽略大小写反而更不像」 | `Code/Tests/run-tests.sh Similarity`（探针 `/tmp/lqdbg/`，**不进仓库**） |
| 相似行对齐**现在真的被守住了**（TXT-005，本轮） | 新套件 `Tests/Similarity` **15 条全绿**（**纯 QtCore**，刻意不链接 QtGui），加上引擎侧「一处改动」的归并层 `DifferenceRun` / `differenceRuns()`。**12 处变异 12 处检出、0 处漏检**（分布 M1 9 红 / M2 3 / M3 1 / M4 1 / M5 1 / M6 2 / M7 1 / M8 1 / M9 3 / M10 32 / M11 2 / M12 2）。其中 **M12 第一版是真漏检**——命令行 `--similarity-threshold` 在 `Tests/Cli` 里零覆盖，补两个用例函数 + 6 行非法参数之后才检出。**注意这一轮的连带修复**：TXT-005 把「一处改写」拆成多块之后，三方合并会**丢基线行**（见 §1.29 与 §6），已修 | `Code/Tests/run-tests.sh` + `/tmp/txt005_mutate.py`（**不进仓库**） |
| 双窗格骨架**现在真的被守住了**（TXT-001，本轮） | 会话与双窗格**早就实现了**（`TextCompareSession` / `TextCompareView`），四条完成标准此前**只被间接覆盖**：既有 20 条用例里没有一条断言过「分栏是**水平**的」、没有一条断言过「某一侧为空时另一侧全部是插入/删除」，`scrollingUsesAlignedRows` 也只比了两侧 `blockCount` 相等。本轮补 **4 个**用例函数（`Tests/TextView` 20 → 24），并在 `TextPane` 上补一个 `lineNumbers()` 访问器让「号码槽」这条链可验。**8 处变异 8 处检出、0 处漏检**——八处分别打在「分栏方向」「填充行是否保留」「号码是否铺到槽里」「窗格是否自动换行」「空侧算作插入还是替换」「空侧方向是否写反」「`rowCount` 是否取两侧最大值」「状态栏是否报告受限」上 | `Code/Tests/run-tests.sh TextView` + `/tmp/txt001_mutate.py`（**不进仓库**） |
| 行尾规则与状态栏严重度**现在真的被守住了**（TXT-010，本轮） | 两条开关（`ignoreEol` / `ignoreFinalNewline`）**早就在同一条键函数里**，但「CR 那一档」「3 种风格两两组合」「2×2 开关矩阵」「另一侧为空时没有可借的末行行尾」四条边界此前**一条断言都没有**；第 3 条（状态栏提示）**零覆盖**；第 4 条的「警告图标」**根本没有机制**。本轮补 **9 个**用例函数（`Tests/Text` 34 → 38、`Tests/TextView` 24 → 26、`Tests/Session` 50 → 52、`Tests/AppIntegration` 12 → 13）并新增一条独立的严重程度通道 + 状态栏永久控件 `statusWarningIcon`。**18 处变异 18 处检出、0 处漏检**，每处都比对过目标 `.o` 的 mtime（证明变异真的编进去了）。**其中 M13/M14 第一次双双漏检**——图标的刷新原本有两条路（会话严重度信号、`refreshStatusBar()`），切标签时两条必然同进同出，于是删掉任一条都不会让任何用例变红。处置是**删掉冗余的那一条**（收敛成一个写入点）后重跑，2/2 检出 | `Code/Tests/run-tests.sh` + `/tmp/txt010_mutate.py` / `/tmp/txt010_mutate_b.py`（**都不进仓库**） |
| 状态栏「提示」与「图标」是两条通道，不能互相推导 | `CompareSession::StatusSeverity` 只有 `Normal` / `Warning` 两档（需求里没有「错误/致命」这一级，失败走 `reportError()` 另一条出口，多留一档就会有人随手用错）。两条通道**各自去重**：文本没变不发 `statusTextChanged`（状态栏刷新是高频路径），但严重度**不能**被文本的去重一起吞掉——`Tests/Session` 里有一条专门造「文本一字不差、只有严重度变」的输入钉住这一条（去掉 `!severityChanged` 这个条件后它会红）。图标落在状态栏的**永久控件**上，装配时设一次 pixmap、之后只切可见性（每次刷新都重建 pixmap 会让状态栏这条高频路径白白多一次分配） | `Code/Tests/run-tests.sh Session` + `AppIntegration` |
| 替换规则的命中走 `Change::Ignored` 而不是 `Equal`（TXT-012，本轮新增） | 两行的键相同、原文不同 → 收尾循环（`textdiff.cpp`）判成 `Change::Ignored`。因此「启用替换规则后两份文件只在被替换的那两行上不同」的结果是 `[Ignored(左2/右2), Equal(左1/右1)]`，而不是「一个大的 `Equal` 块」；`ignoredBlocks` 数的是**块**不是行 | `Tests/TextRules` 的集成用例（断言块的构成而不只是个数） |
| 「只比较前 N 字节」的三条边界（DIR-008，本轮新增，**以后不用再猜**） | ① 限 == 文件长度 → **完整**（两侧都读完，`budgetReached` 为假）；② 差异在限内 → **完整**（差异已被证明，`partialComparison == false`、`complete == true`）；③ 差异刚好在限外一个字节 → **不完整**（`Unknown` + `partialComparison == true` + `complete == false`），再宽一个字节就必须被发现。字节预算卡在「大小不等」短路**之后**：大小不同时直接判 `Different`，不看预算也不读字节 | `Code/Tests/run-tests.sh Folder`（`partialByteLimitIsExactAcrossBlockBoundaries` 用 `kMaximumCompareBlockSize` 造了跨块夹具，`b[block + 1] = 'b'`） |
| 二进制逐字节比对**现在真的被守住了**（DIR-008，本轮） | 第 3 条的块大小上界原来**只能靠匹配源码字面量**（常量藏在 `.cpp` 里），第 4 条的偏移量进报表**从来没有被断言过**，第 2 条完全不存在，第 1 条的**「提前」**那一半也没有守（`break` 删掉不红）。本轮把上界提成公开常量、补 6 个用例函数（`Tests/Folder` 30 → 36）+ 1 个（`Tests/Report` 36 → 37）。**15 处变异 15 处检出、0 处漏检**——M1 默认关失效打红 45 项、M2 不夹逼预算打红 4 项、M7 把上界放大到 4 MiB 被 `block <= 1024 * 1024` 当场拦下。**M15（删掉「命中即 `break`」）只打红 `comparisonStopsAtTheFirstDifferingByte()` 这一条**，其余 3687 条纹丝不动——这是「该处此前没有任何用例守着」的量化形式；M5 / M6 打在「文件变化时收回 `partialComparison` / `firstDifference`」上时原本也都是漏检 | `Code/Tests/run-tests.sh Folder` + `/tmp/mutate_dir008.py`（**不进仓库**，相比上一轮多一道「哨兵 `.o` 的 mtime 必须变了」的校验，专防「构建失败被读成漏检」） |
| 递归子目录三档**现在真的被守住了**（DIR-003，本轮） | 三档在引擎里**早就可达**（`recursive` + `maximumDepth` 的组合），但没有任何名字、没有深度控件、边界文案是引擎里一句 hard-coded 中文、第 5 条（循环符号链接）**从来没实现**。本轮新增 `recursionstrategy.{h,cpp}` 并以**行为归类**反查档位（因此不新增 `Options` 字段，「不递归 + 深度 7」这种合法存档值既不被归一化、也不会显示成错档）。补 **6 个**用例函数（`Tests/Folder` 45 → 51）。**15 处变异 15 处检出、0 处漏检**；其中两处是「先问谁在守它」当场补出来的新防线——**M10**（把 `recursionBoundaryExplanation` 里的 `qBound` 换成原值）与 **M09**（`isStrictAncestor` 的前缀写成 `ancestor + '/'`，于是根目录 `/` 变成 `//`）在补断言之前**全绿**。另有一处**遮蔽防线**被删掉：`applyTierToControls(tier, bool fromUser)` 的 `if (fromUser)` 分支在程序性路径上恒被覆盖或恒等，删掉它没有任何用例变红（M13 第一次跑就是 green），收成「深度控件只有两个写入者」后 M13 变红 | `Code/Tests/run-tests.sh Folder` + `/tmp/dir003_mutation.py`（**不进仓库**） |
| 递归三档的「不该被改动的」那一半（DIR-003，本轮新增） | ① `setOptions()` **不做归一化**：`recursive == false` 配 `maximumDepth == 7` 原样落到控件上（`savedFolderOptionsRoundTripThroughLqcAndActuallyScan` 钉住）；② 深度上限在非「完全递归」档下**保留数值**而不是清 0（清 0 会让「换个档试一下再换回来」丢掉用户设好的上限）；③ `Full` 档写回**只碰 `recursive`**，仅当上限 `<= 1` 才提到缺省。三条都是「反查按行为归类」的配套约束，缺任何一条都会让存档往返静默丢值 | `Tests/Folder` 的 `recursionTierKeepsAUserSetDepthLimit` / `recursionControlsDriveOptionsAndRescan` |
| 循环符号链接的两条判据分支各归各的用例（DIR-003，本轮新增） | `linkTargetReentersAncestor` 里「**解析目标 == 链接自身**」与「**解析目标是链接的严格上级**」是两支。变异 M08（关掉相等分支）**没有**打红 `linksAreComparedWithoutFollowing`——那条用例里的链接目标恰好是**扫描根的绝对路径**，走的是「严格上级」那一支。所以两支**互不代劳**：等支由纯函数表的 `{"a/cycle","."}` / `{"cycle","."}` 两行守，「绝对目标是扫描根 / 其上级 / 文件系统根」三行守另一支 | `Tests/Folder` 的 `cyclicLinkTargetsAreDetected`（13 行纯函数表 + 引擎侧真链接） |
| `tools/check_spec.py` 的「文档条目数」护栏扫的是哪几行（本轮新增） | 它把 `（N 条）` / `（N 条，` / `N 个条目` / `N 条规格` 一律当成「手写的规格条目数」，要求等于 369。**只扫手写文档**（`README.md` / `CHANGELOG.md` / `docs/**`），排除生成物与两份竞品测绘文档。因此**测试用例数绝不能用「（N 条）」这种写法**——用仓库既有的「（N 个用例函数）」。CI **不**跑这五道护栏，所以护栏红了不会让三条腿变红，只会让本机自查失效 | `python3 tools/check_spec.py`（`exit=1` 即红） |
| 套件崩溃的原因现在看得见（本轮新增） | 每个套件跑完都会留下 `<套件>/stderr.log`：**通过时是 0 字节**（Qt 正常跑完不写 stderr），崩溃时有内容且失败日志里直接贴出末尾 15 行；「stderr 是空的」单独成句（被 SIGKILL/段错误直接带走的情形本身也是信息）。用三个临时探针套件端到端验过（配方见 §1.23）：`ZZProbeStderr`（stderr 有内容 + stdout 不泄漏）、`ZZProbeSilent`（空 stderr）、`ZZProbeBadBuild`（qmake 失败 → 陈旧产物必须已删）。**5 处变异 5 处检出**，其中 M4 连漏两次的原因（真等价 vs 观测点选错）见 §6 | `Code/Tests/run-tests.sh ZZProbe`（探针跑完即删，不进仓库）+ `/tmp/lqcompare-mutate-stderr.py` |
| 崩溃的套件**现在会自己说死在哪一条用例**（§1.34，本轮新增） | 运行器对「没产出 `Totals:` 行的非超时失败」自动补跑一遍 `-v2`，日志里直接打出「崩在用例：`Class::func()`」+ `-v2` 结尾 20 行；产物里留 `verbose.txt` 与 `verbose.stderr.log`，CI 一并上传。**两个实测前提**：① `-v2` 只写文件（stdout 是 **0 字节**）；② `abort()` 与连 `atexit` 都不跑的 `std::_Exit()` 之下文件里**仍留着最后一行**（探针 `/tmp/vprobe`，不进仓库）⇒ 判定不依赖任何一次 flush。**边界**：只对崩溃补跑（超时与断言失败不补）、不传 `-o results.txt`（诊断不许改写证据） | 探针 `ZZProbeHardExit` + 自测的 6 条新断言；`/tmp/lqcompare-mutate-rerun.py` **7 处变异 7 处检出、0 漏检** |
| CI 三条腿的实测（读的是 run `35549157384`，并已在 `35551066378` 上复现一致） | **macOS 腿完整**：`macos-15-intel` 64 套件全部产出 `Totals:`，**3568 passed / 0 failed / 2 skipped**，其中 `Tests/Archive` **106 passed**——这是第一次真的跑在 **Python 3.14** 上，同时验证了 §1.21 的夹具修法。**ubuntu 腿只剩 1 个运行期崩溃**：构建失败 **17 → 0**（`trash_linux.cpp` 的 include，`525a872`），`3538 passed / 0 failed`，`Tests/Folder` 跑到第 8 个用例 `linksAreComparedWithoutFollowing()` 时进程消失。**Windows 腿仍是 26 个套件构建失败**（本轮故意未动，清单在 §1.22）。两次运行的这三组数字**逐项相同** | `/opt/homebrew/bin/gh run download <id>` 后逐套件读 `results.txt` / `build.log` / `stderr.log` |
| `Tests/Folder` 崩溃的原因已定性（本轮新增） | `stderr.log` 全文只有一行：**`*** buffer overflow detected ***: terminated`**——glibc 的 `_FORTIFY_SOURCE` 抓到的**缓冲区越界**（不是死循环/栈溢出/OOM）。**macOS 的 libc 没有这个机制，所以本机结构性看不到这一类失败**。下一个动作是在本机用 **ASan** 复现（同盯一类错误、macOS 可用），其次才是让脚本在崩溃时自动补跑 `-v2`（详见 §4.0.1 第 4 条与 §6） | `cat /tmp/ci-art2/test-logs-ubuntu-latest/Folder/stderr.log` |
| `Tests/Folder` 崩在**哪一条用例、哪一次调用**（§1.36，本轮新增，**这就是那条崩溃线的账本**） | run `35613355230` 的 ubuntu 产物给出了两半：① 首轮 `results.txt` 停在 `PASS : FolderTests::recursionLimitIsExplicitUnknown()`，**没有 `Totals:`** ⇒ 崩在下一个用例 `linksAreComparedWithoutFollowing()`；② 补跑的 `verbose.txt`（上一轮那条功能第一次在真实 CI 上产出）把那一条用例里的 4 条断言全部记了下来（行 466/467/468/469，全是 `QFile::link`），**最后一条跑完的是 469** ⇒ **崩在 `Folder::compare(pair.left, pair.right)`（行 471）这一次调用里**。补跑也崩了 ⇒ **不是偶发**。该用例是全仓唯一造「指向自己的目录符号链接 `cycle`」+「悬空符号链接 `dangling`」的用例；`Services/Files` 与 `Services/Folder` 下**没有任何栈上 `char[]`、也没有 `strcpy`/`sprintf`/`memcpy`**（`grep` 实查），`linkTarget()` 用的是动态增长缓冲 ⇒ 越界不在这几处。**仍未修好，也未勾掉任何完成标准**；下一步是「二分夹具（先去 `cycle`）」或「给 ubuntu 腿加一次 gdb 栈」 | `gh run download 35613355230 -n test-logs-ubuntu-latest` 后读 `Folder/results.txt` 与 `Folder/verbose.txt` |
| `stderr.log` 的误报情况（本轮新增） | 三平台上非空的分别是 macOS **4** 个、ubuntu **5** 个、Windows **3** 个套件（`Logging` / `Options` / `OptionsDialog` / `SpecialPicture`，内容是正常的 `[WARN]`/`[ERROR]` 与 Qt 平台提示），**其余全部 0 字节**。所以「只在失败时打印 stderr」不会带来噪音，而「空 / 非空」这个区分确实分开了两类情形 | 遍历 `test-logs-*/<套件>/stderr.log` 的 `-s` 判定 |
| 主程序构建与启动（本轮改动不碰 C++，复测确认没被带坏） | 增量构建 0 条本仓 warning（`_build-lqcompare/`，产物 `dist/macos/LqCompare.app`）；离屏启动日志「单实例机制已由选项关闭」→「Ribbon 构建完成：10 页 / 45 组 / 169 个按钮」→「LqCompare 0.1.0 启动完成」 | `make -j8` + `QT_QPA_PLATFORM=offscreen …/LqCompare --log-level info --new-instance`（跑完记得 `pkill`） |
| 文件格式定义与识别（FMT-001） | **82** 个用例函数（QTest 合计 82）全通过、0 跳过。这套件**刻意不链接 QtGui**（定义是纯数据、识别只读文件前缀）。覆盖：优先级顺序（覆盖 → 掩码 → 内容签名 → 未知兜底）、跨侧优先级与掩码冲突诊断、registry 工厂与平台可用性要求、未知兜底与拒绝打开、Unicode BOM / 截断 / 非法字节、真实文件前缀采样与 I/O 错误、18 类内置格式的具体 ID、JSON 继承与未知 `settings` 往返、坏条目跳过与坏文档拒绝、合并不变性与原子保存、以及**本轮新增的重复 ID 与 ID 格式两条**。交付记录见 `docs/development/team-format.md` | `Code/Tests/run-tests.sh Format` |
| FMT-001 能反向验证 | **5 处变异 5 处检出、0 处漏检**。全部打在 `formatdefinition.cpp` 上：① 去掉 `ids.contains(id)` 的重复检查 → `duplicateIdsAreRejectedWithADiagnostic` 红；② 删掉 `ids.insert(id)`（不再记录已见 ID）→ 同一条红；③ 把 ID 正则放宽成允许大写/空格/点 → `idFormatRulesRejectUnstableIdentifiers` 红；④ 把 `if (!errors.isEmpty())` 改成恒假（有错也不跳过）→ 4 条红；⑤ 把版本校验从 `!= 1` 放宽成 `!= 0` → 7 条红。基线先确认 82/0/0，驱动在改之前与还原之后都删掉 `formatdefinition.o`（`shutil.copy2` 会连旧 mtime 一起还原，见 §6） | 变异测试（结论写在 issue #240 的落地说明里） |
| 归档夹具的字节**不随 Python 版本变**（本轮修） | `Tests/Archive` **106 passed / 0 failed / 0 skipped**（与套件 README 记录一致），`Tests/ArchiveView` 15 passed。**核心不变式靠实测三条**：① 本机 Python 3.13 生成 91 个夹具成功；② 用复现器把本机 `_open_to_write` 换成 3.14 的行为后同样成功（修之前是 `UnicodeDecodeError: 'utf-8' codec can't decode byte 0x82 in position 3`）；③ 两者产物 **92 个文件逐字节一致**（修之前 72 个不同）。另：与仓库里已提交的 `fixtures/` 也逐字节一致 → **3.13 上零改动**，不需要重新生成。`cp437.zip` 的 flag 回到 `0x0000`，两个故意置位的夹具保持 `0x0800` | `Code/Tests/run-tests.sh Archive` + `/tmp/lqcompare-sim314.py`（行为复现器，**不进仓库**） |
| 归档夹具能反向验证 | **8 处变异：7 处检出、1 处按设计漏检**。判定是「本机 3.13 / 模拟 3.14 / 行为断言」三组件里任一结果相对基线变化。检出：`RawName` 与 `PlainName` 各自不再抹掉 UTF-8 位、`intended_utf8_flag` 恒 0 或恒置位、中央目录锚点改回扫 EOCD（`comment-signatures.zip` 的注释里有假 EOCD → `struct.error`）、`PlainName` 不再进入检查。**按设计漏检的是「只把写入后的断言关掉」**——那个护栏自己没有行为。它的价值用**成对变异**证明：把「回归」与「断言失效」一起变异，失败性质从「一句说明意图不符的话」退回成 `UnicodeDecodeError`。另有 **13 项行为断言** | 变异测试（结论写在 issue #336 的落地说明里） |
| 滚动与诊断（OPT-010） | 89 个用例函数（QTest 合计 91，含 `initTestCase`/`cleanupTestCase`）全通过、0 跳过。分九组：A 滚动策略与自检 12、B 纯函数滚动判定 14、C 真实文件的滚动与清空 13、D 历史枚举 8、E 脱敏 14、F 环境报告与文件名 8、G 导出前告知 6、H 诊断包 14、I 源码级护栏 4。这套件**刻意不链接 QtGui**：滚动与诊断都是纯文件工作，哪天有人往里面拽图形依赖，本工程立刻构建失败。C 组真的在 `QTemporaryDir` 里建日志文件、写到超限、断言 `.1` 出现且旧内容在里面 | `Code/Tests/run-tests.sh LogDiagnostics` |
| 滚动与诊断能反向验证 | **17 处变异 17 处检出、0 处漏检**。M1~M12 打服务层（空文件也去滚动、大小判据从 `>=` 改成 `>`、按日模式不比较日期、`keepFiles == 0` 时不清理已有历史、清空改成删除文件、历史枚举退回 `QDir` 通配、脱敏不做最长前缀优先、脱敏只替换前缀不校验右边界、环境报告漏一项、目录名冲突时不退让、失败时不回滚半成品目录、清单不含自身），M13 打 `logging.cpp`（写路径上不再做滚动检查），M14~M15 打设置仓库与设置页，M16~M17 打界面与日志模块的接线。**驱动脚本本身踩过一个坑**：变异落在头文件上时必须把整个套件重建（`purge_suites()`），否则旧 `.o` 会让变异编不进去而报「漏检」（见 §6） | 变异测试（结论写在 issue #365 的落地说明里） |
| 文件操作策略（OPT-005） | 92 个用例函数（QTest 合计 93，含 `initTestCase`）全通过、0 跳过。分九组：A 三种枚举与标签 13、B 出厂默认与安全契约 11、C 覆盖判定四态 12、D 阈值（大文件 / 批量删除）11、E 校验方式 7、F 元数据保留三项 8、G 键表与自检（含把**故意写坏**的表喂进同一判定）13、H 界面共用同一套标签 5、I 源码级护栏（不碰文件系统 / 不 include 视图头 / 反向验证护栏本身会红）12。这套件**刻意不链接 QtGui**：`fileopsoptions` 是纯策略数据，哪天有人往它里面拽图形依赖，本工程立刻构建失败 | `Code/Tests/run-tests.sh FileOpsOptions` |
| 文件操作策略能反向验证 | **25 处变异 25 处检出、0 处漏检**。M1~M20 打服务层（默认删除方式改成永久、覆盖默认改成直接覆盖、`safetyContractViolations()` 不再报直接覆盖、`OverwriteSituation` 四态塌缩成一态、目标缺失也去询问、MB→字节少乘一层、0 不再表示关闭、阈值判定用 `>=` 而非 `>`、校验方式表少一项、三个保留布尔合并成一个、键表去重/去空/去前缀、键表的 purpose 恒为空…），M21 打设置仓库的出厂值，M22~M25 打设置页（安全提示不再随草稿刷新、下拉文本改成界面自己写一份、单位后缀写死）。**注意驱动脚本本身踩过一个坑**：把 `.o` 的时间戳推到未来会让 `make` 认为目标比源新而**整轮跳过编译**，于是 25 处全报「未确认重编」——必须推**源文件**的 mtime 并断言变异确实编进去了（见 §6） | 变异测试（结论写在 issue #317 的落地说明里） |
| 单实例与进程间通信（PLAT-006） | **103 个用例函数（QTest 合计 105，含 `initTestCase` 与 `cleanupTestCase`）**全通过、0 跳过。分十二组：A 标识符 12、B 线协议编解码 20、C 分帧 6、D 退出码表 6、E 退出码表自检的反向验证 9、F 命令行开关 6、G 置前策略 6、H 角色与启动结论 4、I 真子进程的转交 8、J 真子进程的失败与降级 9、K 崩溃遗留与生存期 13、L 源码级护栏 4。**它是本仓第一个起真子进程的套件**：`ChildProcess` 用 `QProcess` 把**自己**再拉起一次（`--child <mode>`），因此「第二个实例把参数交出去并带退出码退出」「首个实例不应答时降级」「进程 `_exit()` 之后标识仍可用」这些事是真的在两个进程之间发生的，不是在同一个进程里假装。子进程模式写在测试源码里，**没有往生产代码里加测试钩子** | `Code/Tests/run-tests.sh SingleInstance` |
| 单实例能反向验证 | **12 处变异检出 11 处、1 处为环境受限（不是漏检）**。检出的是：净化后丢掉原始输入摘要、FNV-1a 乘法改加法、标识不再附带字段边界摘要、转发结局一律返回退出码 0、开关优先级改成「表里靠前的胜出」、`Never` 策略仍然抢焦点、静默窗口失效（正在打字时抢焦点）、帧探测不再拒绝超界载荷声明、解码不再校验协议版本、关掉单实例后仍占标识、解码不再要求「正好一帧」（**这一处最初漏检，本轮补了一条用例才抓住，见 §6**）。**唯一未检出**的是「`recoverStaleIdentifier()` 不再回收遗留标识」——本机 `QSharedMemory` 不可用，该分支不可达，对此类环境无法构造判定输入（理由与实测数据见 §1.16） | 变异测试（结论写在 issue #327 的落地说明里） |
| 三层过滤叠加与落点 | 84 个用例函数（QTest 合计 84），0 失败 0 跳过。分七组：A 层级与落点 8、B 三层叠加 20、C 启用与生效 14、D 表达式与面板 15、E 视图层不落盘 11、F 计数 6、G 层级表自检 8（含 6 条对**故意写坏**的表跑同一个判定，证明护栏不是恒真的）。这套件**刻意不链接 QtGui**，但**必须**链接 `Services/Session`——第 4 条要拿真正的 `SessionSettings` 存储来断言，测试替身会连「存到哪个存储」一起替掉 | `Code/Tests/run-tests.sh FilterStack` |
| 三层过滤能反向验证 | 十六处变异逐一被拦下：视图层落点改成随会话保存 → 多条红；`active()` 只看启用标志 → 多条红；去掉层间的排除优先 → 多条红；排除集合用 AND 连接 → 表达式红；原子一律不加引号 → 引号用例红；表达式不去重 → 去重用例红；视图存储缺失时退而写入会话存储 → 「不许退而写入」红；会话层也从视图存储读 → 「按存储分别读」红；`loadInto` 整份替换状态（丢掉启用标志）→ 红；`setLayerState` 不按声明重新解析 → 红；大小写覆盖在重新解析后不再应用 → 红；不统计 `decided` → 面板与计数红；不计入逐层统计 → 红；自检去掉「视图层落点」这一条 → 红；自检不查顺序 → 红；汇总文案改成另一句 → 红。**16 处全部检出，0 处漏检** | 变异测试（结论写在 issue #233 的落地说明里） |
| 会话设置声明与草稿 | 65 个用例函数（QTest 合计 71，含 `initTestCase` 与六种控件的 6 行数据）。分六组：A 声明与六种控件 17、B 自检 11、C 草稿读写与脏判定 13、D 应用与恢复默认 10、E 询问策略 6、F 声明目录与反向验证 8。这套件**刻意不链接 QtGui**（声明与草稿都是纯数据），因此「任一会话设置 Tab 均可无界面构造与读写」这条标准是靠构建配置 + 用例两边一起钉住的 | `Code/Tests/run-tests.sh Settings` |
| 会话设置对话框 | 44 个用例函数（QTest 合计 45，含 `initTestCase` 与 QTest 隐含的 `cleanupTestCase`）。分五组：A 结构（左侧 Tab 列表 / 右侧内容 / 底部作用域下拉 + 四按钮的几何与取值）14、B 切 Tab 与关闭的确认 11、C 四个按钮的行为 10、D 校验反馈与脏标记 5、E 反向验证 3。**本轮 45 是重跑实测值**：上一轮留下的「43 个用例函数 / 44 条」是限制出现之后按源码静态数的，数漏了两个；加固没有合并掉任何用例 | `Code/Tests/run-tests.sh SettingsDialog` |
| 会话作用域链与写入路由 | 40 个用例函数（QTest 合计 41，含 `initTestCase`）。分六组：A 优先级顺序 5、B 覆盖链与出厂默认 6、C 写入路由与「三层互不覆盖」11、D 去向文案与切换提示 7、E 关闭标签丢弃视图级设置 7、F 解析诊断与反向验证 3。这套件**刻意不链接 QtGui**（覆盖链是纯数据合成），因此「视图级改动不得污染会话默认值」这条边界是在**没有界面**的情况下被断言住的 —— 与 SESS-006 第 4 条同一手法 | `Code/Tests/run-tests.sh SettingsScope` |
| 会话作用域链能反向验证 | 十二处变异逐一被拦下：反转三层优先级 → 7 条红；覆盖链丢掉「出厂默认」这一环 → 5 条红；出厂默认不归一就交出去 → 1 条红；`contains()` 把出厂默认也算成「已设置」→ 2 条红；`clear()` 清空三层 → 1 条红；丢弃视图级设置时连会话层一起清 → 4 条红；`sameValue` 恒为假（`changed` 不再按有效值判断）→ 3 条红；写入目标层缺失时退而写入视图层 → 1 条红；显式设成空值的项被当成「没设置」→ 1 条红；没有视图级设置也要问一句 → 1 条红；没有待定改动也弹切换提示 → 1 条红；去向文案改用机器标识 → 1 条红。**12 处全部检出，0 处漏检** | 变异测试（结论写在 issue #42 的落地说明里） |
| 文件系统抽象层 | 47 个纯逻辑用例 + 3 个真实文件系统用例全通过；其中 20 个覆盖 **Windows** 路径规则（盘符 / UNC / 长路径前缀 / 大小写），在 macOS 上真实执行 | `Code/Tests/run-tests.sh FileSystem` |
| 回收站 | 35 个用例全通过。其中 9 个验证 XDG（Linux）的路径与 `.trashinfo` 规则、2 个是**真实**的废纸篓往返与冲突拒绝、多个断言「不可用时搬移函数一次都没被调用」 | `Code/Tests/run-tests.sh Trash` |
| 名称与 Unicode | 40 个用例通过 + 1 个跳过（无效 UTF-8 名字的用例只在 Linux 上执行，CI 会跑）。覆盖字节保真往返、UTF-8 边界与过长编码、Unicode 组合形式、六类文件名问题的原因与位置 | `Code/Tests/run-tests.sh PathName` |
| 错误携带与批量处置 | 36 个用例全通过。其中 9 个验证错误码在三种域下的携带与显示（含「未识别的码只给数字」）、11 个验证失败清单分组、12 个验证执行流程（含「重试只跑失败项」与「停止不移除已完成进度」）、4 个走真实文件系统做一次「设为只读 → 解除只读」往返 | `Code/Tests/run-tests.sh Batch` |
| 系统图标 | 46 个用例函数（QTest 合计 48，含 `initTestCase`/`cleanupTestCase`）全通过、0 跳过。分四组：17 个验证缓存键与尺寸规则（扩展名折叠 7 + 键的合成与解析 5 + DPI 缩放 4 + Windows 档位收拢 1）、9 个验证有界 LRU 的淘汰与命中统计、6 个验证请求去重队列、11 个验证服务层（同步/异步/去重/回退/换比例清缓存）；另有 **3 个走真实系统图标源**（macOS 上真实执行：断言拿到非空像素、断言文字文件与文件夹的图确实不同） | `Code/Tests/run-tests.sh PlatformIcon` |
| 主程序构建 | 通过，**本仓库自己的代码 0 warning**（`make -B -j8` 全量重编，实测 0 条本仓 warning、0 条 error）。链接行里能看到 `singleinstance.o` / `moc_singleinstance.o` 与 `instanceprotocol.o`，`nm -C` 在产出的可执行文件里数到 `LqCompare::Platform::SingleInstanceGuard` **72** 个符号——即单实例模块**确实进了产物**而不只是躺在磁盘上。本轮另数到 `FileOperationPolicy` **48** 个符号与 `fileOperationKeyTable` **27** 个（`fileopsoptions.o` 在链接行里；**没有 `moc_fileopsoptions.o`**，因为 `fileopsoptions.h` 里没有任何 `Q_OBJECT`——它是纯数据结构，不是 `QObject` 派生类），即 OPT-005 的策略模块同样进了产物。OPT-010 的两个新模块在链接行里是 `logfiles.o` 与 `diagnostics.o`（同样**没有** `moc_*`，都是纯 QtCore 数据/函数，不是 `QObject`），`nm -C` 数到 `LqCompare::Log::rotationDecision`、`clearLogFile`、`buildDiagnosticBundle`、`writeTiming`（均为 `T`，即已实体化）。全量重建时唯一的告警仍是第 3 方 `MyClass/3rd-party/LqRibbon/…/LqRibbon.cpp:569: unused function 'nativeWindowScaleFactor' [-Wunused-function]`，不在本仓改动范围内。**一个构建上的坑**：`make -B` 会把 app bundle 的 `PkgInfo` / `Info.plist` 也一起删掉重建，而本机沙箱的删除守卫会拦住它，于是报 `Error 1` 但**编译与链接其实都成功了**（看 `MacOS/LqCompare` 的 mtime 是否为最新即可）。`touch` 一下这两个文件再 `make -j8` 就恢复正常（见 §6） | `cd _build-lqcompare && ~/Qt/5.15.2/clang_64/bin/qmake -o Makefile ../Code/LqCompare.pro && make -B -j8`（**注意是 `Code/LqCompare.pro`**，仓库根没有 `LqCompare.pro`；用户指令里写的 `../LqCompare.pro` 会报 `Cannot find file`） |
| 主程序运行 | 离屏启动正常，日志显示「LqCompare 0.1.0 启动完成」与「Ribbon 构建完成：10 页 / 45 组 / 169 个按钮」，且**一次 `qt.svg: Cannot open file` 都没有**。本轮新增了 PLAT-006 那一行：默认启动（单实例开启）时是「共享内存不可用（QSharedMemory::create: out of resources），已由进程锁保证单实例；本进程是首个实例，已在端点 lqcompare-loren-org.lqcompare.d-4102bf82 上等待转交」；加 `--new-instance` 时是「单实例机制已由选项关闭：不占用标识、不建端点，直接启动」。**⚠️ 指令变更**：夜间对 `main.cpp` 的重写（233 删 / 91 增）**不再调用**之前几轮加的启动自检——`validateSessionTypeTable` / `validateFilterLayerTable` / `validateAttributeConditionTable` / `validateNameFilterTables` 现在**全仓没有任何调用方**（`validateContentFilterTables()` 从一开始就没有调用方，它落在同一个处境里），日志里也不会再出现「会话类型注册表：…」「三层过滤：…」「属性过滤：…」「名称过滤：…」那四行。保留下来的只有 `CommandRegistry::instance().validate()`（`main.cpp` 里那一处 `LQCOMPARE_ERROR("command", …)`）。这些表的校验**仍然被单元测试覆盖**（例如 `Tests/FilterStack` G 组有 6 条把**故意写坏**的表喂进同一个判定），因此丢的是「启动时的一声警报」，不是唯一的防线；**要不要把这四行自检加回 `main.cpp` 是一个待定的决定**，加回之前不要在文档里声称启动时会打印它们 | `QT_QPA_PLATFORM=offscreen ./dist/macos/LqCompare.app/Contents/MacOS/LqCompare --log-level info [--new-instance]`（**别用 `\| head` 收尾**，管道关闭会把进程直接杀掉、看不到真实退出码）。另：**上一轮的离屏实例可能还活着**，那时这次启动会走转发路径、立刻以 **10** 退出且**不打任何日志**——要做一次干净的启动确认请加 `--new-instance`，确认完记得把进程杀掉并清掉它留下的端点与 `.lock`（本轮清过一次：`/tmp/lqcompare-loren-org.lqcompare.d-4102bf82{,.lock}`），详见 §6 |
| Shell 集成 | 99 个用例函数（QTest 合计 101，含 `initTestCase`/`cleanupTestCase`）全通过、0 跳过。分十一组：A 动作与目标 14、B 选项 8、C 命令行引号 8、D 计划 13、E 安装 10、F 卸载与还原 9、G 校验 5、H 残留 6、I 能力 4、J 预演 3、K 命令行解析 9。**全部跑在功能完整的内存注册表上**，因此安装回滚与卸载还原是在本机真实执行的流程，不是桩 | `Code/Tests/run-tests.sh ShellIntegration` |
| Shell 集成的命令行引号 | 用测试内置的 `CommandLineToArgvW` 参考实现做往返：`"C:\Program Files\…\LqCompare.exe" --shell-action=compare "%1"` 切回来必须还是两个原值，含「结尾反斜杠要翻倍」这条最容易写错的规则 | `Code/Tests/run-tests.sh ShellIntegration` |
| 分级日志 | **43** 个用例函数（QTest 合计 45，含 `initTestCase`/`cleanupTestCase`）全通过、0 跳过。分七组：A 级别与过滤 6、B 格式 4、C 输出目标 11、**D2 性能计时开关 6**、**D3 滚动策略与写路径检查 5**、D 耗时辅助 6、E 级别名解析 4，以及 `initTestCase`/`cleanupTestCase`。这套件**刻意不链接 QtGui**：哪天有人往 `logging.cpp` 里加图形依赖，本工程会立刻构建失败。**新增组延续了本模块的卫生要求**：日志级别 / 日志文件 / 接收者 / 滚动策略 / 性能计时开关都是进程全局状态，因此 `init()` 与 `cleanupTestCase()` 必须把它们逐个复位，否则用例之间的顺序会让结果随机 | `Code/Tests/run-tests.sh Logging` |
| 掩码语法与过滤声明 | 87 个用例函数（QTest 合计 89，含 `initTestCase`/`cleanupTestCase`）全通过、0 跳过。分九组：A 掩码基本语义 13、B 字符集 12、C 跨目录 8、D 大小写策略 9、E 声明解析 17、F 叠加 9、G 预览 6、H 语法速查 7、I 恶意与畸形输入 6。这套件同样**刻意不链接 QtGui**（掩码只处理字符串） | `Code/Tests/run-tests.sh Filter` |
| 掩码语法速查与实现同源 | 21 条速查条目、53 条样本被逐条**真的跑一遍**（掩码类走 `Mask::compile` + `matches`，声明类走 `MaskFilter::parse` + `accepts`），因此「帮助里写的行为」与「程序的行为」不可能分家。另有断言：纯文本速查表里含每一个掩码与样本（它确实是生成物）、条目无重复、全表同时出现「匹配」与「不匹配」两种样本 | `Code/Tests/run-tests.sh Filter` |
| 掩码的恶意输入有界 | `**/**/…`（24 个）对上 40 段路径、`*a*a*…`（12 个）对上 200 个 `a`、400 段路径、5000 字符掩码、500 成员字符集——全部在毫秒内出结果。这几条盯的是**指数级退化**（朴素递归分别是 2^40 与 2^200 量级），不是性能基线 | `Code/Tests/run-tests.sh Filter` |
| 属性条件（FILT-003） | 89 个用例函数（QTest 合计 90，含 `initTestCase`）全通过、0 跳过。分九组：A 大小范围与单位 15、B 修改时间范围 14、C 属性位 10、D 所有者与组 9、E 与名称过滤的与关系 8、F 声明文本 13、G 缺失与写错 9、H 条件表自检 8、I 与内容解耦 2。这套件**刻意不链接 QtGui**（条件判定是纯数据），因此「属性过滤不依赖界面」也是靠构建配置钉住的 | `Code/Tests/run-tests.sh AttributeFilter` |
| 属性条件能反向验证 | 十八处变异逐一被拦下（含把「与」改成「或」、把 `undecided` 当成 `accepted=false`、日期上限退回当天 `00:00`、1024 改成 1000、单位表去掉中文「字节」、`knownAttributeBits` 与 `attributeBits` 合并、大小写策略在所有者比较上失效、条件表自检不查顺序、声明切行只按 `\n` 切、畸形条件也去缩小结果集、`claimFamilyKey` 的重复键分支、互斥家族冲突不报错）。**18 处全部检出，0 处漏检** | 变异测试（结论写在 issue #230 的落地说明里） |
| 属性过滤与内容比对解耦 | **读源码**的护栏：`Tests/AttributeFilter` 的 I 组直接读 `Services/Filter/attributefilter.{h,cpp}`，一旦出现 `QFile` / `readAll` / `QTextStream` / `QDataStream` / `readFileContents` 即红；另有一条用例对一段故意植入 `QFile(path).readAll()` 的源码跑同一流程，证明判定不是恒真 | `Code/Tests/run-tests.sh AttributeFilter` |
| 名称过滤（FILT-002） | 93 个用例函数（QTest 合计 93）全通过、0 跳过。分**十组**：A 模式与模式前缀 12、B 通配与精确语义 11、C 组合语义（含 / 不含 / 全含）11、D 判定结论与放行 11、E 超时保护与断路器 12、F 静态风险预检 9、G 声明文本与实时校验 10、H 具名预设与导出往返 8、I 表自检 5、J 与界面共用同一套解释 4。这套件**刻意不链接 QtGui**（判定是纯数据），因此「名称过滤不依赖界面」也是靠构建配置钉住的 | `Code/Tests/run-tests.sh NameFilter` |
| 名称过滤能反向验证 | **二十九处**变异逐一被拦下（含精确名忽略大小写、整名匹配退回 `match()` 的部分匹配、只用 `anchoredPattern()` 覆盖用户自己的锚点、断路器阈值 +1、超时后不清零连续计数、超时后不放行、超时结论不给 `Timeout` 问题、连字符类里的量词也当变量量词报、`(?…)` 分组修饰里的量词也报、`{2}` 定长量词误报、模式前缀按错的优先级解析、组合语义 `NoneOf` 用「任一命中即排除」而不是「全部命中才排除」、预设往返多出末尾空行、记录开始前的空行也进正文、`disabledExpressionCount` 数槽位数而不是真被停用的条数、`nameFilterDeclarationKey()` 改成别的字符串、自检不查模式表顺序、自检不查共用解释、大小写选项在正则上不生效…）。**29 处全部检出，0 处漏检** | 变异测试（结论写在 issue #229 的落地说明里） |
| 名称过滤的 Qt 5.15 现实 | `QRegularExpression` 在 Qt 5.15 **没有** `setMatchTimeout()` / `matchTimeout()`（逐字核对 `~/Qt/5.15.2/clang_64/lib/QtCore.framework/Versions/5/Headers/qregularexpression.h` 确认；这两个成员是 Qt 6.0 才加的）。因此 200ms 超时**不能**靠 Qt API 实现，只能自己做（工作线程 + 截止时间）；也不引第三方 PCRE2——那与「一个 exe 分发」相冲 | 头文件核对 + 实测 |
| 内容过滤（FILT-004） | 85 个用例函数（QTest 合计 85）全通过、0 跳过。分**九组**：A 行过滤的模式与前缀 12、B 行过滤的判定与计数 11、C 行过滤的解析与往返 12、D 行过滤的校验与错误 9、E 关键字节的编解码 11、F 关键字节的判定与组合 10、G 两条轴与忽略规则的顺序 7、H 启用状态与提示文案 7、I 表自检与源码级护栏 6。这套件**刻意不链接 QtGui**（输入输出都是行与字节），因此「内容过滤不依赖界面」也是靠构建配置钉住的。它还**必须**额外 include `Services/Text` 的 `.pri`——G 组要拿真的 `Text::CompareOptions` 与 `Text::compare()` 才能证明「忽略空白救不了『多了一行』」 | `Code/Tests/run-tests.sh ContentFilter` |
| 内容过滤能反向验证 | 源码级护栏成对出现：`moduleNeverReadsFiles` 读 `contentfilter.{h,cpp}`，一旦出现 `QFile` / `readAll` / `QTextStream` / `QDataStream` 即红；`sourceGuardWouldCatchAnInjectedRead` 对一段**故意植入** `QFile(path).readAll()` 的源码跑同一流程，证明判定不是恒真（这条是必需的——见 §6 里「护栏扫到自己的注释」那条）。另有 `touchesInclude` 成对用例钉住「本模块不许 include `Services/Text` 的头」。行为侧的差别由 G 组用真比对引擎给出：同一对输入，什么都不做时有两处差异；先按 `re:^\d{4}-\d{2}-\d{2} ` 把两侧的时间戳行都滤掉、再按 `Whitespace::IgnoreAll` 忽略剩下的空白，结果是**差异为空且 `ignoredBlocks > 0`**——后半句说明那处空白差异真实存在过、只是被忽略规则处理掉了，从而证明「先过滤行」不是「顺手把所有东西都过滤没了」 | 变异/源码护栏 + `Code/Tests/run-tests.sh ContentFilter` |
| 会话抽象基类 | 46 个用例函数（QTest 合计 48，含 `initTestCase`/`cleanupTestCase`）全通过、0 跳过。分七组：A 生命周期与状态迁移 19、B 视图契约 5、C 三个公共出口 7、D 设置接口 7、E 可扩展性 2、F 源码级护栏 2、G 与类型注册表的衔接 4。**这是本仓库第一个链接 QtWidgets 的测试套件**（基类的 `createWidget()` 返回 `QWidget*`），跑在 offscreen 平台上 | `Code/Tests/run-tests.sh Session` |
| 会话类型注册表 | 57 个用例函数（QTest 合计 59，含 `initTestCase`/`cleanupTestCase`）全通过、0 跳过。分六组：A 条目字段齐备 13、B ID 稳定性与登记校验 11、C 按掩码查询与注册顺序 14、D 可枚举 7、E 按名字查 5、F 自检与两道源码级护栏 7。这套件**刻意不链接 QtGui**（注册表是纯逻辑）——它是本仓库第一次有「服务层的会话框架测试」 | `Code/Tests/run-tests.sh SessionType` |
| 会话类型注册表能反向验证 | 六处变异逐一被拦下：对调 `text` 与 `table` 的登记顺序 → 快照、重叠结论与 Home 页一致性三条红；改掉 `homepage.cpp` 里一个类型 ID → Home 页一致性红；给表格类型多加一条与文本重叠的掩码 → `onlyHtmlOverlaps` 红；把一条内置掩码写成大写 → `validate` 两条红；让 `findByFileMask` 不跳过当前平台不可用的类型 → 该条红；`add()` 不再校验 ID 格式 / 不再拒绝重复 ID / 不保存传入的工厂 / `byGroup` 不按可用性过滤 → 各自对应的用例红。全部检出 | 变异测试（结论写在 issue #37 的落地说明里） |
| 会话类型 ID 与 Home 页硬编码的 ID 一致 | 源码级比对：`Tests/SessionType` 读 `Views/Shell/homepage.cpp`，从 `sections()` 里抠出 14 个 `QStringLiteral("…")`，与 `builtInSessionTypeIds()` **逐个比对（含顺序）**；另有一条用例对一段故意写错 ID 的源码跑同一流程，证明判定不是恒真 | `Code/Tests/run-tests.sh SessionType` |
| 掩码语言只有一份实现 | `SessionTypeRegistry` 的默认文件掩码直接 `#include "mask.h"` 编译与匹配，因此 `**`、字符集、转义这些语义**免费成立**。有一条用例注册自定义的 `src/*.txt` 掩码并断言 `src/a.txt` 命中、`other/a.txt` 与根下的 `a.txt` 不命中——这条在自制「看扩展名」的实现下必然失败 | `Code/Tests/run-tests.sh SessionType` |
| 会话基类的用例能反向验证 | 五处变异逐一被拦下：`createWidget()` 每次重建 → `createWidgetBuildsTheViewOnlyOnce` 与 `createWidgetPassesTheParentThrough` 红；`save()` 去掉 `canSave()` 守卫 → 4 条红；`reload()` 去掉脏守卫 → `reloadRefusesWhenSessionIsDirty` 红；`save()` 成功后不清脏 → 2 条红；给基类加一行 `#include "homepage.h"` → **测试工程构建失败**（编译期护栏生效）。五处全部检出 | 变异测试（结论写在 issue #36 的落地说明里） |
| 会话基类不依赖具体视图 | 编译期：`SessionTests.pro` 的 INCLUDEPATH 里只有 `Views/Session` 与 `Services/Session`，构造出的是**失败**而不是「用 include 白名单扫一遍心理上放心」。源码级：F 组两条用例，其中一条对故意写坏的源码做反向验证 | `Code/Tests/run-tests.sh Session` |
| 会话设置接口 | 接口与内存实现都在 `Services/Session/session.{h,cpp}`，只依赖 QtCore。用例覆盖「键不存在返回调用方给的回退值」「空键整条拒掉」「写入同一个值不算改动」「`clear()` 用空键承载全变」四条约定 | `Code/Tests/run-tests.sh Session` |
| 日志级别真的生效 | 不带参数启动**不产生任何日志输出**（默认 `warning`，启动横幅是 `info`）；`--log-level info` 打印带线程 id 的完整启动序列；`--log-level debgu`（拼错）打印「无法识别的日志级别「debgu」，改用 warning」 | `QT_QPA_PLATFORM=offscreen ./LqCompare [--log-level …]` |
| 分层检查 | 通过（Services 未反向依赖界面） | `python3 tools/check_layering.py` |
| 图标检查 | 通过（32 个图标，声明/引用/文件三者一致） | `python3 tools/check_icons.py` |
| 规格自检 | 通过（369 条，P0 59 条，PRD 与数据同步） | `python3 tools/check_spec.py` |
| Shell 可移植性 | 通过（16 个脚本，无 bash 4 内建与 GNU 工具扩展；含 `_test-build/` 与 `.codex-work/` 下各套件构建目录里的 `target_wrapper.sh`） | `python3 tools/check_shell.py` |
| Windows 宽字符 API | 通过（**341** 个源文件、清单内 43 个 API；自测 17 个样本）。本表此前记的 332 是 OPT-010 那一轮的数（从 326 涨到 332），之后又陆续新增了几个模块（如 `linesimilarity` / `linereplacements`）——**这个数是随源码增长的，别把它当常量** | `python3 tools/check_winapi.py [--self-test]` |
| 测试套件（无匹配视为失败） | 「无套件匹配」被视为失败（exit 2）——套件改名或过滤器拼错时不会报「全部通过」而实际 0 个用例执行。全量的实际数字见上面那一行 | `Code/Tests/run-tests.sh <不存在的套件名>` |
| 测试套件被显式排除时也要报出来 | 有套件被 `LQCOMPARE_TEST_SKIP` 排除时，末尾汇总**不再**说「全部套件通过」，而是「通过（已排除 N 个套件、未验证）：…」；开头另有一行排除清单；全部被排除时仍以退出码 2 报「一个测试都没跑」。**4 处变异 4 处检出**（忽略环境变量、命中不置位、边界判断失效、汇总掩盖排除） | `LQCOMPARE_TEST_SKIP=Version Code/Tests/run-tests.sh Version`（期望 exit 2） |
| 测试运行器的跨平台写法 | 输出格式是 `junitxml`（Qt 的 `xml` 是私有格式，CI 的解析器不认）；`make` 按 `mingw32-make` → `make` → `gmake` 探测；找不到时报错退出 2（而不是让每个套件「构建失败」并把真实原因吞掉）；Windows 上补试 `${binary}.exe`。**4 处变异 4 处检出**。驱动就地变异 + `finally` 还原 + sha256 校验 | `Code/Tests/run-tests.sh` 与 /tmp 下的两个变异驱动 |
| CI 的降级范围（哪些套件真的依赖私有仓） | 66 个测试工程里**只有 2 个**依赖 LqRibbon：`Tests/AppIntegration`（include 了 `Views/views.pri` → `Page/ribbonlayout`）与 `Tests/CommandActions`；其余 **64 个**不依赖。这是**实测**出来的，不是 grep 推断 | 对每个 `.pro` 跑 `LQCOMPARE_MYCLASS_ROOT=/nonexistent qmake <pro>`，失败的即依赖方（66 个里失败 2 个） |
| GitHub 托管 macOS 运行器的选型 | 只能用 `macos-15-intel`。Qt 5.15.2 只有 macOS x86_64（clang_64）官方包，而 arm64 运行器**没有预装 Rosetta 2**（x86_64 的 qmake 跑不起来）；`macos-13`（原 Intel 标签）已于 **2025-12-04 退役**，用了直接失败。该 Intel 标签可用到 2027-08，之后托管运行器不再有 x86_64 | GitHub 官方公告（2025-09-19）与 runner-images 的 EOL 表；本轮已写进 workflow 注释 |
| 命令注册表自检 | 启动时 0 问题（说明不缺图标、不缺说明、无快捷键冲突） | 启动日志 |
| 会话设置目录自检 | ⚠️ **不在启动时打印**（理由见本表「主程序运行」那一行的指令变更）。`SessionSettingsCatalog::describe()` 现在**只有测试调用点**，生产路径上没有；目录当前是**空的**（框架刻意不含具体设置项），`describe()` 会输出「会话设置目录：0 份声明，共 0 个设置项」。它查的是每一份会话设置声明是否字段齐备、键是否唯一；各会话类型登记声明时它会立刻开始替它们把关——**只要有人把调用点接上** | `Code/Tests/run-tests.sh Settings` |
| 过滤层级表自检 | ⚠️ **不在启动时打印**：`validateFilterLayerTable()` 现在是个**孤儿函数**——全仓除测试外没有调用方，`architecture.md` §5 记下了「暂不加回 `main.cpp`」这个决定。它查三件事：三层是否齐、顺序是否是规格顺序、以及**视图层的落点是否仍然是「仅当前视图」**（这一条错了没有任何运行期现象）。它的判定**把表当参数**，所以 `Tests/FilterStack` 的 G 组能拿一份**故意写坏**的表证明它真会报 | `Code/Tests/run-tests.sh FilterStack` |
| 属性条件表自检 | ⚠️ **不在启动时打印**（同上，`validateAttributeConditionTable()` 无生产调用方）。查四件事：四类条件是否齐、顺序是否是规格顺序、标识符/声明键是否唯一且非空、以及**每一类是否都只依赖元数据**（`metadataOnly` 为假的条目会被报出来）。判定**把表当参数**，`Tests/AttributeFilter` 的 H 组有对故意写坏的表跑同一判定的用例 | `Code/Tests/run-tests.sh AttributeFilter` |
| 名称过滤表自检 | ⚠️ **不在启动时打印**（同上，`validateNameFilterTables()` 无生产调用方）。查四件事：模式表（3 种）与组合语义表（3 种）是否齐、顺序是否是规格顺序、前缀/标识符/解释是否唯一且非空、以及**每一行的「共用解释」是否与 `analyzeNameFilterLine()` 的行为一致**。判定**把两张表都当参数**（`validateNameFilterTables(modeTable, combineTable)`），因此测试能拿故意写坏的表证明它真会报——这一点是早先**修出来的**：最初自检从内部表读，于是它永远不会红（见 §6） | `Code/Tests/run-tests.sh NameFilter` |
| 名称过滤的时间预算可读出来 | 空过滤器的 `matchBudget()` 必须是 `perEntryMs == 200` 且 `hasCircuitBreaker()` 为真（`consecutiveTimeoutLimit == 2`）。**「主程序启动时断言这两个值并打印出来」这句已经不成立**（同一处指令变更），现在由 `Tests/NameFilter` 断言。意义不变：这是「超时保护确实开着」在测试路径上的最小证据——否则一个被改成 0 的默认值会让保护静默消失 | `Code/Tests/run-tests.sh NameFilter` |

**已知未验证**：Windows MinGW 32 位构建未在本机验证（无该环境）；
macOS 上只有 Qt 5.15.2 一套（用户机器上 6.11.0 已卸载）。

**前一轮（16:5x）出现、并已在那一轮内解决的一条环境限制（不必再排查，坑表里有完整成因）**：
2026-09-20 下午起，本机**任何不在 dyld 共享缓存里的 x86_64 可执行文件都无法启动**
——进程进入 `U`（不可中断等待）状态、CPU 时间恒为 0、`SIGKILL` 与 `SIGALRM` 都进不去，
因此既跑不完也超时不了；`qmake`、各 `tst_*` 与主程序（均为 Qt 5.15.2 clang_64 编出的
x86_64）因此全部无法启动。**根因是「未做 ad-hoc 签名」，不是 Rosetta 坏了、也不是代码问题**：
`codesign -f -s - <二进制>` 之后立刻能跑（最小实验：`clang -arch x86_64` 编出的
`int main(){return 42;}` 未签名时卡住、签名后正常退出并返回 42；`arch -x86_64 /bin/echo`
一直正常，因为它在共享缓存里且由 Apple 签名；arm64 程序全程正常）。
处理：给 `~/Qt/5.15.2/clang_64` 下的 232 个 Mach-O（dylib / framework 二进制 / 可执行文件）
各补一次 ad-hoc 签名，并在 `Code/Tests/run-tests.sh` 构建成功之后加一行签名——
这样后续每一轮都不必手工处理。**这是宿主环境的一次性修复，不影响仓库的跨平台性**
（`uname -s` 判断成 Darwin 才做，且失败不阻断）。

## 3. 当前代码结构（哪些文件已经存在）

```
Code/
├── LqCompare.pro                 构建入口，已预留各工作流的 include
├── App/
│   ├── main.cpp                  进程入口、日志、命令行、注册表自检
│   ├── RibbonWindow.{h,cpp}      Ribbon 外壳（样式 / QAT / 搜索栏 / 右键菜单）
│   ├── MainWindow.{h,cpp}        主窗口 + 31 条已实现命令 + 输出面板 + 状态栏
│   └── app.pri
├── Views/
│   ├── Session/                  comparesession（会话抽象基类：契约 / 状态机 / 三出口）、
│   │                             settingsdialog（会话设置对话框外壳：声明 → 控件 /
│   │                             草稿读写 / 按钮可用状态与错误行）
│   ├── Shell/                    homepage（Home 页）、sessionarea（会话标签容器）
│   ├── Page/                     ribbonlayout（声明表驱动的 Ribbon 构建）
│   ├── Options/                  optionsdialog（选项对话框：分类树 / 搜索 / 五分类页面
│   │                             / 草稿与校验；`fileops` 页的安全提示由服务层策略
│   │                             动态算出，标签也直接取自服务层）、
│   │                             optionsruntime（选项的读写与应用）
│   ├── sessionview.pri / shell.pri / page.pri / options.pri / views.pri
│   │                             （另有 Archive / Filter / Folder / FolderMerge / Media /
│   │                             Merge / Registry / Special / Sync / Table / Text / Vcs /
│   │                             Version 等目录，属尚未接线的视图批次，本表不逐个展开）
├── Services/
│   ├── Command/                  commandregistry（命令注册中心）
│   ├── Log/                      logging（分级日志 / 级别过滤 / 三目标 / 耗时辅助 /
│   │                             滚动策略与性能计时开关；滚动检查在写路径上）、
│   │                             logfiles（滚动与清空：RotationPolicy 纯数据策略对象 +
│   │                             自检、注入时间的纯函数 rotationDecision、落盘
│   │                             applyLogRotation、历史枚举、截断式 clearLogFile）、
│   │                             diagnostics（环境报告 / manifest.json / 前缀表驱动的
│   │                             路径脱敏 / 导出前告知文案 / buildDiagnosticBundle
│   │                             失败整目录回滚）
│   ├── Session/                  session（会话设置接口 + 内存实现）、
│   │                             sessiontype（会话类型描述子与注册表：14 种内置
│   │                             类型的字段 / ID 快照 / 按掩码的注册顺序优先）、
│   │                             settingschema（设置项的声明 / 草稿 / 询问策略 /
│   │                             声明目录；复用 Filter 的掩码解析器，**纯 QtCore**）、
│   │                             settingscope（三层作用域的覆盖链 / 只落目标层的
│   │                             写入路由 / 去向与丢弃提示三条文案，**纯 QtCore**）
│   ├── Filter/                   mask（掩码语法 / 匹配 / 语法速查表）、
│   │                             maskfilter（包含排除叠加 / 大小写策略 / 预览计数 /
│   │                             公共的声明切行 `splitDeclarationLines()`）、
│   │                             attributefilter（大小 / 时间 / 属性位 / 所有者四类
│   │                             条件、条目元数据快照 EntryMetadata、三态结论、
│   │                             条件表自检、decideEntry 把名称侧与属性侧合成**与**；
│   │                             **模块内不读任何文件内容**）、
│   │                             filterstack（三层叠加 / 每层启用状态 / 合并表达式 /
│   │                             面板数据 / 落点路由与层级表自检；**唯一一处只给
│   │                             `../Session` 搜索路径而不给源文件的模块依赖**）、
│   │                             namefilter（三种模式 精确/通配/正则、三种组合语义
│   │                             任一/不含/全含、三态结论与「不确定一律放行」、
│   │                             200ms 超时保护（工作线程 + 截止时间）与连续超时
│   │                             断路器、正则灾难性回溯静态预检、具名预设与导出
│   │                             往返、两张表自检）、
│   │                             contentfilter（行过滤器：整行/通配/正则三种模式、
│   │                             缩进无关的行首前缀、逐行诊断与命中计数、
│   │                             LineFilterResult 同时交回留下的行与被丢掉的行；
│   │                             关键字节：`\xHH` 解码/编码、任一/全部两种组合方式、
│   │                             文本输入判「不适用」、空规则集不构成约束；
│   │                             两条轴与忽略规则的先后顺序表、启用与性能提示文案；
│   │                             **输入永远是「已经读进来的行与字节」，自己不碰
│   │                             文件系统**——有读源码的护栏成对盯着）
│   ├── Format/                   formatdefinition（稳定 ID / version=1 的 JSON 解析、
│   │                             导出、原子保存与合并；坏条目跳过、坏文档拒绝；
│   │                             settings 作为原样往返的不透明袋）、
│   │                             formatdetector（覆盖 → 掩码 → 内容签名 → 未知兜底
│   │                             的固定优先级；判定前先问 registry 与平台可用性；
│   │                             内容探测只读前缀，做 UTF-8/16/32 与截断校验；
│   │                             **全程不调用工厂、不创建视图**）
│   ├── Files/                    filesystem（抽象层 + 错误携带）、pathutils（路径与名称规则）、
│   │                             pathname（字节保真 / Unicode / 显示）、
│   │                             trash（回收站服务 + XDG 规则）、
│   │                             batch（失败清单 / 重试 / 进度）、
│   │                             fileopsoptions（文件操作**策略**：删除方式 / 覆盖策略与
│   │                             四态判定 / 元数据保留三项 / 大文件与批量删除阈值 /
│   │                             校验方式；把「默认值必须保守」写成可执行的
│   │                             `safetyContractViolations()`；**不执行任何文件操作、
│   │                             不读文件系统**，有源码级护栏成对盯着）、
│   │                             filesystem_posix/_win、trash_mac.mm/_linux/_win
│   ├── Platform/                 iconkey（缓存键与尺寸）、iconcache（有界 LRU + 去重队列）、
│   │                             iconservice（同步/异步/回退 + 提供者接口）、
│   │                             registrystore（注册表抽象 + 内存实现 + 故障注入）、
│   │                             shellintegration（计划 / 安装回滚 / 卸载还原 / 校验 / 残留）、
│   │                             instanceprotocol（标识符规则 / 线协议编解码 / 分帧 /
│   │                             退出码表 / 开关表 / 置前判定，纯 QtCore）、
│   │                             singleinstance（进程锁选主 + 共享内存标识 + QLocalServer 转交
│   │                             + 超时降级 + 遗留标识回收）、
│   │                             iconservice_mac.mm/_win/_linux、registrystore_win.cpp/_stub.cpp
│   ├── command.pri / log.pri / filter.pri / files.pri / platform.pri / services.pri
├── Pictures/                     32 个 SVG 图标 + Pictures.qrc
├── Tests/
│   ├── Support/                  fakefilesystem（内存文件系统）、faketrashservice
│   │                             （内存回收站），多套件共用
│   ├── AttributeFilter/          tst_attributefilter + .pro（89 用例函数，**纯 QtCore**，
│   │                             刻意不链接 QtGui；兼作「属性过滤不读文件内容」的
│   │                             源码级护栏，靠 DEFINES 传进来的 LQCOMPARE_CODE_ROOT
│   │                             直接读 Services/Filter/attributefilter.{h,cpp}）
│   ├── Alignment/               tst_alignment + .pro（14 用例函数，**纯 QtCore**，
│   │                             刻意不链接 QtGui）。TXT-003 的对齐算法验收：A 组唯一行
│   │                             优先、B 组无唯一行时与 Myers **逐字段相同**、C 组算法表
│   │                             与「未实现不得可选」的两道机制、D 组固定语料上切换算法
│   │                             改变块的数量与位置。**刻意不与 Tests/Text 合并**：两者
│   │                             守的是不同的事（Myers 自己的标准 vs 换算法后契约还成不
│   │                             成立），分开之后变异测试只需重建这一个套件。断言骨架是
│   │                             **手推的黄金串**（纸上枚举锚点与后缀剥离）加一个返回
│   │                             「第一条被违反项」的契约纯函数
│   ├── Similarity/              tst_similarity + .pro（15 用例函数，**纯 QtCore**，
│   │                             刻意不链接 QtGui）。TXT-005 的相似行对齐验收：分值是
│   │                             `2·LCS/(len左+len右)` 的百分点，用例按**定义另算一遍**
│   │                             再比对（不抄实现里的数）；阈值判定钉住「含等号」；
│   │                             配对用三条结构不变量判——左右下标各自严格递增、
│   │                             每一对都 `>= 阈值`、配对数与总分值不得被更优解压过；
│   │                             「无丢行」由固定语料显式断言（配对前后左右被覆盖的
│   │                             行数相等）。**刻意不与 Tests/Text 合并**：那一套守的是
│   │                             「切完之后块边界对不对」，这一套守的是「切完之后哪些行
│   │                             配成一对」，夹具与变异面都不重叠。两条工作量上限
│   │                             （512×512 单元格 / 10^8 字符）的「超限退回按位配对」
│   │                             也在这里断言
│   ├── CommandRegistry/          tst_commandregistry + .pro（14 用例）
│   ├── ContentFilter/            tst_contentfilter + .pro（85 用例函数，**纯 QtCore**，
│   │                             刻意不链接 QtGui；九组 A~I，含用真的 Text 比对引擎
│   │                             验证「先过滤行再应用忽略规则」、以及读源码钉住
│   │                             「本模块不读文件」两条成对护栏）
│   ├── FileSystem/               tst_filesystem + .pro（50 用例）
│   ├── Format/                   tst_format + .pro（82 用例函数，**纯 QtCore**，刻意不链接
│   │                             QtGui；覆盖优先级的四级顺序、内容探测的 Unicode 与截断、
│   │                             18 类内置格式的 ID、JSON 继承与不透明 settings 往返、
│   │                             坏条目跳过与坏文档拒绝、合并不变性与原子保存，以及
│   │                             本轮新增的重复 ID 与 ID 格式两条）
│   ├── Filter/                   tst_filter + .pro（87 用例函数，刻意不链接 QtGui；
│   │                             自 FILT-005 起还要 include Session 的 .pri，
│   │                             原因是 filterstack.cpp 引用了 SessionSettings 接口）
│   ├── FilterStack/              tst_filterstack + .pro（84 用例函数，**纯 QtCore**，
│   │                             刻意不链接 QtGui；链接 Services/Session 以拿真正的
│   │                             存储断言「视图临时过滤不写入会话」）
│   ├── FileOpsOptions/           tst_fileopsoptions + .pro（92 用例函数，**纯 QtCore**，
│   │                             刻意不链接 QtGui（策略是纯数据）；九组 A~I，含把
│   │                             **故意写坏**的键表喂进同一个自检判定、以及读源码钉住
│   │                             「本模块不碰文件系统 / 不 include 视图头」的成对护栏；
│   │                             另 include OptionsDialog 的编译单元以断言「界面标签
│   │                             与服务层同源」）
│   ├── Logging/                  tst_logging + .pro（43 用例函数，刻意不链接 QtGui）；
│   │                             七组含本轮新增的 D2 性能计时开关、D3 滚动策略与写路径
│   │                            检查；日志模块是进程全局状态，因此 init/cleanup 必须
│   │                            把级别 / 文件 / 接收者 / 滚动策略 / 计时开关逐个复位
│   ├── LogDiagnostics/           tst_logdiagnostics + .pro（89 用例函数，**纯 QtCore**，
│   │                             刻意不链接 QtGui）；九组 A~I，C 组真的在 QTemporaryDir
│   │                             里建日志文件并写到超限、H 组真的产出一整个诊断包目录、
│   │                             I 组读源码钉住「本模块不 include 视图头 / 不弹对话框」
│   ├── NameFilter/               tst_namefilter + .pro（93 用例函数，**纯 QtCore**，
│   │                             刻意不链接 QtGui；十组 A~J，含用脚本替身
│   │                             ScriptedRunner 覆盖超时策略、以及读源码钉住
│   │                             「任务按值捕获」与「解析/校验/复验同源」两条不变式）
│   ├── PathName/                 tst_pathname + .pro（40 用例 + 1 个仅 Linux）
│   ├── Trash/                    tst_trash + .pro（35 用例）
│   ├── Batch/                    tst_batch + .pro（36 用例）
│   ├── PlatformIcon/             tst_platformicon + .pro（48 用例）
│   ├── ShellIntegration/         tst_shellintegration + .pro（99 用例函数）
│   ├── SingleInstance/           tst_singleinstance + .pro（103 用例函数 / QTest 105；
│   │                             **本仓唯一会起真子进程的套件**——把 QProcess 拉起
│   │                             自己（--child <mode>）做两进程的转交与超时；子进程
│   │                             模式写在测试源码里，生产代码没有测试钩子）
│   ├── Session/                  tst_session + probesession + .pro（46 用例函数，
│   │                             唯一链接 QtWidgets 的套件；INCLUDEPATH 只有
│   │                             Views/Session 与 Services/Session，兼作编译期护栏）
│   ├── SessionType/              tst_sessiontype + .pro（57 用例函数，**纯 QtCore**，
│   │                             刻意不链接 QtGui）；兼作两道源码级护栏：类型 ID
│   │                             与 Home 页硬编码的一致性、图标键必须在 qrc 里
│   ├── Settings/                 tst_settings + .pro（65 用例函数，**纯 QtCore**，
│   │                             刻意不链接 QtGui——「任一会话设置 Tab 均可无界面
│   │                             构造与读写」因此是构建配置钉住的结论）
│   ├── SettingsDialog/           tst_settingsdialog + .pro（44 用例函数，链接
│   │                             QtWidgets）；含一个默认答案恒为「返回」的对话框
│   │                             子类，让「意外被问到」表现为断言失败而不是挂住
│   ├── SettingsScope/            tst_settingsscope + .pro（40 用例函数，**纯 QtCore**，
│   │                             刻意不链接 QtGui——「视图级改动不得污染会话默认值」
│   │                             因此在没有界面的情况下被断言住）
│   ├── OptionsDialog/            tst_optionsdialog + .pro（23 用例，链接 QtWidgets；本轮
│   │                             从 17 加到 23，新增的都是 OPT-010 的接线：清空按钮
│   │                             存在 / 关掉文件日志时清空要报错 / 清空真的截断了配置
│   │                             的那个文件 / 导出遵守脱敏勾选 / 目录不可用时如实报错 /
│   │                             滚动设置一路走到日志模块）。
│   │                             分类树 / 搜索 / 五分类页面与草稿、校验、文件操作页的
│   │                             动态安全提示与单位后缀都在这套件里；它 include
│   │                             `Services/Files/files.pri` 才能拿到服务层的标签
│   ├── Options/                  tst_options + .pro（50 用例，选项仓库的声明与默认值；
│   │                             本轮从 46 加到 50，新增四条钉住 `logging.*` 四个键的
│   │                             出厂值、取值范围是否**取自服务层**而不是界面自己抄一遍）
│   ├── （其余套件：AppIntegration / Archive(V) / Cli(Probe) / CommandActions /
│   │   CommandShortcuts / Folder(Merge) / Format / HomeRegistry / Media(V) / Merge(Output) /
│   │   PatchApply / PatchRegression / Registry(V) / Report / Script / SessionArea /
│   │   SessionDocument / Snapshot / Special* / Sync* / Table* / Text(V) / Vcs(Blam)eView /
│   │   Version* 等，属别的工作流，本表不逐个展开）
│   └── run-tests.sh              统一测试运行器（跨 macOS / Linux / Windows Git Bash：
│                                 `mingw32-make` 探测、`.exe` 后缀、`-o results.txt,txt`
│                                 `-o results.xml,junitxml` 双路输出、`LQCOMPARE_TEST_SKIP`
│                                 显式排除可选依赖缺失的套件并**把排除报出来**）
└── ThirdParty/                   myclasspath.pri（定位 LqRibbon）、lqribbon.pri

tools/
├── spec/                         规格数据（唯一事实来源，8 个模块文件，369 条）
├── publish_issues.py             生成 PRD + issue 索引 + 创建 issue（幂等）
├── generate_icons.py             生成图标集
├── check_layering.py             ENG-001 依赖方向
├── check_icons.py                ENG-009 图标一致性
├── check_shell.py                ENG-003 脚本可移植性（bash 4 / GNU 扩展护栏）
├── check_winapi.py               PLAT-007 宽字符 API（禁止 ANSI 版与不带后缀的写法）
└── check_spec.py                 规格自检 + PRD 同步 + 优先级策略 + 文档计数
                                  （文档计数只扫**被 git 跟踪**的 `.md`，见 §6）

docs/
├── PRD-actions.md                规格正文（生成物，勿手改）
├── PRD.md                        产品定位与范围
├── design/architecture.md        架构与目录清单
├── development/                  交接、并行划分、GitHub 流程
├── github/                       issue 索引与发布记录（均为生成物）
└── research/                     两份竞品测绘 + 开源实现参考 + 裁决规则

.github/workflows/build.yml       CI：checks（五道静态护栏）+ build-and-test
                                  （Ubuntu / macos-15-intel / Windows-MinGW 三条腿）。
                                  拿不到私有仓 LqRibbon 时**降级**：跳过主程序构建与打包、
                                  照跑 64 个套件、显式排除那 2 个依赖它的（见 §1.20）
```

## 3.1 基线提交与远端状态

基线导入已完成并推送到 `origin/main`（本地与远端一致，工作区干净）。五个提交按
「可编译的整体」划分，而不是按动作条目切碎——`Code/` 拆开任何一个提交都编不过：

| 提交 | 内容 | 关联条目 |
| --- | --- | --- |
| `451dbc2` | 工程骨架、四层结构、命令注册中心与 Ribbon 外壳 | PLAT-001、ENG-002/003/005/006/011、UI-007、UI-024 |
| `ae53e47` | 分层与图标两项构建期护栏 | ENG-001、ENG-009 |
| `d9e1b63` | 规格数据层与 issue 发布链路 | ENG-014 |
| `5034ae3` | 产品定位、架构、并行划分与竞品测绘 | DOC-003、DOC-005、DOC-006 |
| `6f33117` | issue 模板、状态标签工作流与 CI | ENG-004、ENG-015 |
| `a693346` | 参考项目克隆实测结果与受限网络下的取用方式 | DOC-006 |
| `e456c83` | 文件系统服务抽象层与可替换的假实现 | PLAT-002 |
| `de1ba13` | 回收站与可逆删除服务 | PLAT-003 |
| `ad7f004` | Unicode、特殊文件名与名称的字节保真 | PLAT-007 |
| `03a481c` | 错误携带（分类 + 原始系统码）与批量操作的失败处置 | PLAT-008 |
| `81a33a4` | 系统图标的缓存、去重与异步解析 | PLAT-004 |
| `3701144` | 推送流程里「租约永远过期」的成因与正确写法（纯文档） | ENG-003 |
| `01165d6` | Shell 集成的注册表计划、安装回滚与卸载残留校验 | PLAT-005 |
| `20602e6` | 补上 PLAT-005 的提交记录，并纠正「回填提交号」的做法（纯文档） | ENG-003 |
| `042f0f6` | 分级日志的级别过滤、结构化记录、接收者目标与耗时辅助 | ENG-006 |
| `9622527` | 掩码语法、过滤声明与语法速查表 | FILT-001 |
| `bab3f5b` | 会话抽象基类与统一的会话设置接口 | SESS-001 |
| `4d78efa` | 会话类型描述子与注册表（内置类型的字段、ID 快照、按掩码的注册顺序优先、创建工厂） | SESS-002 |
| `9ecd269` | 会话设置对话框框架与会话设置的声明模型（声明 / 草稿 / 询问策略 / 声明目录，以及按声明生成的对话框） | SESS-006 |
| `f71a0e1` | 测试运行器为 macOS 上的 x86_64 套件补 ad-hoc 签名（未签名的 x86_64 二进制在 Apple Silicon 上会卡在 `U` 状态，既跑不完也超时不了） | ENG-003 |
| `ac26805` | 会话设置的三层作用域、写入路由与去向提示（`ScopedSessionSettings` + 三条可测文案） | SESS-007 |
| `b5c1c28` | 三层过滤的叠加、启用状态、合并表达式与各层落点（`FilterStack` + 层级表自检 + 存储路由） | FILT-005 |
| `561014d` | 属性过滤的大小、时间、属性位与所有者条件（`AttributeFilter` + 元数据快照 + 三态结论 + 条件表自检 + `decideEntry` 的与关系） | FILT-003 |
| `665c243` | 名称过滤的三种模式、组合语义与 200ms 超时保护（`NameFilter` + 工作线程截止时间 + 连续超时断路器 + 灾难性回溯静态预检 + 具名预设往返 + 两张表自检） | FILT-002 |
| `92a0226` | 内容过滤的行过滤与关键字节判定（`LineFilter` + `KeyByteFilter` + `\xHH` 编解码 + 三态结论 + 两条轴的先后顺序表 + 启用与性能提示文案；另修掉「入口对空行提前返回挡掉 `re:^$`」与「前缀不跳缩进导致 `= EXACT` 退化成通配」两条真 bug，并把 `check_spec.py` 的文档计数收紧成只扫被 git 跟踪的 `.md`） | FILT-004 |
| `ac32665` | **夜间 24 个工作流的产出整体入库**，并补完被额度中断的两条：补丁应用（`patchapply.cpp` + `Tests/PatchApply`，预演→备份→暂存→提交→校验的事务与回滚）与 Blame 追溯（`blameview.cpp` 补完尾部并接进 `vcsview.pri` + `Tests/VcsBlameView`）；另修掉 2 条构建告警、补齐 5 个缺失的 Ribbon 图标、修掉十六进制搜索对 `startOffset` 的静默夹紧 | PAT-002、PAT-005、VCS-012、VCS-013（连同夜间各工作流的条目） |
| `c03dc79` | 单实例与进程间通信的**验证与闭环**（实做由夜间那批工作流落地）：补一条「声明长度比实际多、而已有字节本身仍可完整解析」的解码用例（`decodeRejectsAFrameShorterThanItsDeclaredPayload`）——拿掉它时那一处校验的变异零条用例变红；并把「本机 `QSharedMemory` 建不起来、生产实际走进程锁 + 本地套接字回退」这条环境限制写进文档 | PLAT-006 |
| `bdede29` | 文件操作的默认行为与「默认值必须保守」的可执行契约（`fileopsoptions.{h,cpp}` 纯数据策略对象 + 8 个 `fileops.*` 设置键 + 选项页的 `fileops` 分类与随草稿刷新的安全提示 + `Tests/FileOpsOptions` 92 个用例函数；另把规格的边界条款写成语义可执行的 `safetyContractViolations()`，25 处变异 25 处检出） | OPT-005 |
| `d084060` | 日志滚动、清空与诊断包导出（新增 `logfiles.{h,cpp}` 与 `diagnostics.{h,cpp}` 两个纯 QtCore 模块 + 滚动检查放进写路径 + 4 个 `logging.*` 设置键 + 日志页的清空与导出按钮 + `Tests/LogDiagnostics` 89 个用例函数；另修掉一个真实的隐私缺陷——脱敏的右边界原本要求「后面必须是分隔符」，于是 `/Users/loren `（后面是空格）不会被替换，导出的包里带着真实家目录；17 处变异 17 处检出） | OPT-010 |
| `a1cd3ee` | 文件格式定义模块（FMT-001）的**核对与闭环**：实做由夜间那批工作流落地，本轮逐条核对五条完成标准、补上此前**零覆盖**的「唯一稳定的 ID」两条用例（重复 ID 只保留第一个、非法 ID 逐条拒绝）、5 处变异 5 处检出，并删掉本文档里传染了四处的「文件格式定义模块未落地」。**没有新增生产代码** | FMT-001 |
| `786cf46` | **持续集成流水线（ENG-004）的修复**：这条流水线自建立起从没跑过构建与测试——匿名 clone 私有仓 `MyClass` 在第一跳就以 128 退出，它后面的「构建主程序」与「运行测试套件」从来没有开始过。本轮把它改成**降级开关**（拿不到私有依赖就跳过主程序构建与打包、照跑其余 64 个套件并**显式排除**那 2 个真的依赖它的套件），补齐四个阶段、三平台矩阵（Ubuntu / `macos-15-intel` / Windows MinGW 32 位）、失败时上传日志与 JUnit 用例清单、可执行产物上传、Qt 缓存与两种降级说明；`run-tests.sh` 顺带修掉四类跨平台问题（`junitxml` 输出、`mingw32-make` 探测、`.exe` 后缀、显式排除机制），8 处变异 8 处检出 | ENG-004 |
| `24225e8` | **归档夹具的字节不再随 Python 版本变**（ENG-004 的其中一条红腿）：真因是 Python 3.14 起 `zipfile._open_to_write()` 会无条件把 UTF-8 位置上，**覆盖**了夹具生成器 `RawName._encodeFilenameFlags()` 想表达的「这个文件名的字节是不是 UTF-8」意图，于是 `cp437.zip` 被写成「置了 UTF-8 位、字节却不是 UTF-8」，直到回读时才炸 `UnicodeDecodeError`。修法是新增 `PlainName` + `intended_utf8_flag()`，把「想置的位」与「库要置的位」对齐，并在写完**立刻**用写出的字节复核意图（92 个产物文件：改前 72 个随版本变，改后 0 个）。8 处变异 8 处检出——其中一处是等价变异，靠配对的另一处才证明该护栏确实有价值 | ENG-004 |
| `525a872` | **Linux 回收站实现漏了一个 include**：`trash_linux.cpp` 用了 `PathUtils::Style::posix()` 与 `PathUtils::parentPath()`，却没有任何头文件替它把 `pathutils.h` 带进来。这个文件**只在 Linux 上编译**，而开发机是 macOS，所以缺了多久都没人知道——直到 CI 的 ubuntu 腿第一次真的开始构建，它**一个文件造成 17 个套件**构建失败。修法是一行 include 加一段「为什么必须显式 include」的注释 | ENG-004 |
| `0ba8476` | **两条红腿的账本**（纯文档）：ubuntu 的 17 个失败＝1 个文件、Windows 的 26 个失败＝5 个文件（两类系统性原因＋两个真 bug），逐文件记下原始报错、判定与改法；同时把 §1.20 里**写错的诊断原地改掉**（当时说「回读校验太严」，其实是「夹具被写坏了」），并把「报错的那一步不一定是错的那一步」记进 §6 | ENG-004 |
| `6bd1385` | **把 ubuntu 的崩溃定性，并留下可执行的下一步**（纯文档）：`Tests/Folder` 的 `stderr.log` 只有一行 `*** buffer overflow detected ***: terminated`——glibc 的 `_FORTIFY_SOURCE` 抓到的缓冲区越界，而 **macOS 的 libc 没有这个机制，所以本机结构性看不到这类失败**（同类可推 ASan）。同时记下三平台 `stderr.log` 的误报情况（非空 4/5/3 个，其余 0 字节）与「崩溃时自动补跑 `-v2`」这条待做的诊断改进。新增 §1.23.1、§2 三行、§4.0.1 第 4 条与 §6 三条坑 | ENG-004 |
| `6cd5831` | **让套件崩溃的原因不再被丢掉**（ENG-004 的第三批修复，由第一次真实 CI 的三条腿结果驱动）：子进程的 stderr 以前和 stdout 一起被丢进 `/dev/null`，而崩溃原因（`Received signal 11`、`ASSERT`、Qt 崩溃处理器的回溯）**只走 stderr**——于是 CI 只能说「某个套件红了」；现在落盘 `<套件>/stderr.log`、进上传产物、失败时贴末尾 15 行，并且**分开说**「stderr 有内容」与「stderr 是空的（被 SIGKILL/段错误直接带走）」。同一批还把「清理上一轮产物」从「构建成功之后」挪到**本轮最前面**：原来的位置在构建失败/找不到二进制时会 `continue` 掉，于是失败的那一轮会原样带上上一轮的产物当成本轮结果上传。**5 处变异 5 处检出**（配方与两版弱探针的教训见 §1.23） | ENG-004 |
| `810262f` | **让流水线的输出真的可读**（ENG-004 的第二批修复，由第一次真实 CI 跑出来的结果驱动）：不再用 `-o -,txt` 从 stdout 拿测试结果（Windows 上一行都不输出、文件产物却正常，于是日志没有 `Totals:` 而合计被算成 0），改成只写文件再由脚本 `cat` 回来，并在跑之前 `rm -f` 上一轮产物（否则套件崩溃会被上一轮的旧结果掩盖）；崩溃的套件在合计之外单独点名（`34 个套件红` 与 `26 failed` 两个都对，但以前没人解释差从哪来）；构建输出落盘 `<套件>/build.log` 并加进上传产物（以前丢 `/dev/null`，17/26 个构建失败一个字的原因都没有）。10 处变异 10 处检出、另有 8 项行为断言 | ENG-004 |
| `83a1c8f` | **TXT-002 的验证对齐**（四条完成标准里第 3、4 条从来没有被断言过；算法本身没问题，**本轮只加测试、不动一行生产代码**）：新增 `identicalInputYieldsASingleEqualBlock()`（全相同 → **单个**相同块；含单行 / 两个空输入 / 「按选项等价」这些边界，并区分 `Ignored` 与 `Equal`）、`resultIsPureAndMatchesSnapshot()`（把 `Result` 摊成文本做逐字段快照，五个**手推**的黄金串，其中一个是另一个的左右对调）、`boundedAdversarialInputIsReportedAsLimited()`（为 `alignmentLimited` 单独造输入：两侧各 4000 行、只在正中间共享一行）。9 处变异 9 处检出，其中 M5 第一次是真漏检——**断言是对的，是夹具让 `firstRow` 与块序号恰好相等** | TXT-002 |
| `3b3a993` | **TXT-003 耐心对齐（Patience）与 Myers 回退**：`Alignment` 枚举 + `CompareOptions::alignment`（新字段加在末尾）、内部共享的区间收集器 `AlignmentBuilder`（区间表 / 预算 / `limited` 由两种算法共用，因此块边界与受限度都只有一个来源）、内部 `Patience`（公共前后缀 → 两侧唯一行计数 → 严格递增最长子序列挑锚点 → 锚点间递归；**没有唯一行的一段与深度到顶都交给 Myers**）、算法表 `AlignmentDescriptor` 与四个查询加一个自检（默认值取「表里第一条已实现项」，未实现的两道机制分别是 `availableAlignments()` 过滤与 `validateAlignmentTable()` 报警）、枚举外取值退到默认算法的兜底；新增 `Tests/Alignment`（14 个用例函数，**纯 QtCore**）。9 处变异 9 处检出，另有 1 处**按设计漏检**（`<` 与 `<=` 在「候选下标互不相同」这条不变量下等价，注释里写明了上层放宽后会怎么坏） | TXT-003 |
| `5c16735` | **测试运行器的并行、超时与自测**（ENG-003 的五条完成标准里，原本有三条没做）：套件级并行（默认 `min(4, 核数)`，墙钟 3 分 53 秒 → 1 分 21 秒，合计逐字不变）、单套件超时（自建轮询，不依赖 macOS 上不存在的 GNU `timeout`，超时与断言失败分开命名且不计入合计）、`Tests/OptionsDialog` 的截图不再写进仓库根（并按**指纹**而非文件名断言落点，否则「覆盖写」会隐身）；另新增 `run-tests.sh --self-test`——在临时目录里现造五个探针套件，用同一个运行器跑两遍（并行 4 / 串行 1）逐条断言它自己该说的话，因为「运行器自己错了」恰恰是它本该报告的那类静默失败。13 处变异 13 处检出（其中两处第一次是真漏检：断言盯错了量、断言比名字不比内容） | ENG-003 |
| `5af6b9e` | **TXT-008 忽略大小写差异的核对与闭环**：实现（`ignoreCase` / `Change::Ignored` / Rules 勾选框 / `text.ignoreCase` 键 / 弱化底色）**早已在仓库里**，缺的是断言与「链」这个名字。把匿名命名空间里的 `normalized()` 提成公开的 `normalizedLine()`（整条链只有一个实现，头文件那段注释同时就是第 4 条要求的那份土耳其语取舍记录），并补 7 个用例函数：`Tests/Text` 25 → 31（逐行重要性切换、只有仅大小写不同的行失去差异身份、链「少做任何一步都不相等」+ 左右对调、12 行非 ASCII 折叠对照表、土耳其语四格 + 土耳其语 locale 下结论不变、折叠长度守恒），`Tests/TextView` 18 → 19（被忽略的行仍被标记、与从**真实渲染**采出的三种差异色都不同且彩度更低）。8 处变异 8 处符合预期，其中 1 处**按设计漏检**（链的两步可交换，调换顺序不该红） | TXT-008 |
| `15a6530` | **TXT-009 忽略空白变化的核对与闭环**：实现（`Whitespace` 三值、`normalizedLine()` 里的分支、Rules 里的下拉、`text.whitespace` 键、`Change::Ignored` 的弱化底色）**早已在仓库里**，缺的是「分水岭」那类语料、模式表，以及界面文案那条链的断言。新增 `WhitespaceDescriptor` / `whitespaceTable()` / `whitespaceIdentifier()` / `defaultWhitespace()` / `availableWhitespaces()` / `validateWhitespaceTable()`（与 `alignmentTable()` 同一个定位：界面按表铺下拉、按表读回，缺项/重复/未实现都由自检报出来），界面改按 `availableWhitespaces()` 铺下拉与读回（不再 `static_cast<Whitespace>(currentIndex())`），并把界面文案提成公开静态接口 `TextCompareView::whitespaceLabel()` 让「文案链」可验；补 4 个用例函数：`Tests/Text` 31 → 34（9 行三模式并排语料 + 嵌套不变量、10 行 Tab/空格混排语料（含 `isSpace()` 的边界字符）、模式表与自检四份坏表、枚举外取值退化为 `Exact`），`Tests/TextView` 19 → 20（下拉第 i 行**显示的字**与**实际生效的模式**两条链 + 被忽略的空白差异仍留标记、真正相同的行不留标记）。**10 处变异 10 处检出**，其中第 10 处（把下拉铺法改成倒序）**第一版是真漏检**——只钉了值那条链 | TXT-009 |
| `1e7ce78` | **TXT-005 相似度阈值与相似行对齐**：新增纯函数模块 `Services/Text/linesimilarity.{h,cpp}`——分值 `lineSimilarityPercent()`（`2·LCS/(len左+len右)` 的百分点，两侧都过 `normalizedLine()`，因此忽略规则在相似度这一层同样生效）、阈值判定 `isSimilarEnough()`（**含等号**）、单调配对 `pairSimilarLines()`（**先最大化配对数、再最大化总分值**）、两个**开跑前就能算出来**的工作量上限（512×512 单元格 / 10^8 字符，超限**退回按位配对**并把 `limited` 交出去，不静默）、`clampSimilarityThreshold()`。引擎侧新增「一处改动」的归并 `DifferenceRun` / `differenceRuns()`（判据只用「块下标连续」，背后是 `AlignmentBuilder` 的区间交替结构），状态栏、上一处/下一处、复制这一处、命令行摘要**四处统一改用它**——否则现象是「状态栏说 3 处、按两次『下一处』就到头了」。`CompareOptions::alignSimilarLines` / `similarityThreshold` 由引擎真正读取；会话键 `text.alignSimilarLines` / `text.similarityThreshold` 往返有断言；命令行补 `--similar-lines` / `--no-similar-lines` / `--similarity-threshold <0-100>` 三条（越界 / 非数字 / `70.5` / 缺值 / 两个开关同时给**都算用法错误**，退出码 2——与「会话文件里的坏值钳制」刻意不同）。**顺带修掉一个连带缺陷**：拆分「一处改写」之后三方合并按**块**粒度解读，会让**基线行从冲突结果里消失**（`A\nC\n` 而非 `A\nB\nC\n`，丢数据）；`mergeengine` 加 `changeRunLength()` 把「基点相接」的同侧相邻非 Equal 块归并成一处，`textmergesession` 的输出行→块归属同改「一处改动」口径。新增套件 `Tests/Similarity`（15 条用例函数，**纯 QtCore**），另有 `Tests/Text`、`Tests/TextView`、`Tests/Report`、`Tests/AppIntegration`、`Tests/Cli`、`Tests/Merge`、`Tests/MergeView` 的断言改动。**12 处变异 12 处检出、0 漏检** | TXT-005 |
| `682d461` | **TXT-001 文本比对会话与双窗格骨架的验收断言**（实现早已在仓库里，而本文档此前**从未提过这条**，见 §1.30）：补 4 个用例函数，四条完成标准各一条——分栏必须是**水平**且两侧窗格纵向齐平（既有用例只查「两个 `TextPane` 找得到」，上下堆叠同样满足）、两侧视觉行数都等于模型行数且**号码槽与正文两条链各钉一遍**（为此把 `TextPane::lineNumbers()` 提成公开接口）、单侧为空时按**规格点名的动词**选 `Insert`/`Delete` 而不是「能表示」的 `Replace`（且两个方向都验）、三组「完全/近似不相关」输入上不退化（最紧的一组断言受限被报告**且会话把它说到了状态栏文字里**）。`Tests/TextView` 20 → 24；**8 处变异 8 处检出、0 处漏检**；并记下两条坑（变异让套件段错误时进程会在预期用例之前就死掉、驱动会把「没轮到跑」错报成漏检；用例里的具体数字必须有独立来源或实测来源） | TXT-001 |
| `1bea981` | **TXT-010 忽略行尾差异的核对与闭环**：两条开关（`ignoreEol` / `ignoreFinalNewline`）早已在同一条键函数里，缺的是组合断言；**第 4 条的「警告图标」是本轮唯一真正新增的能力**——`CompareSession::StatusSeverity`（两档，与状态文本**各自去重**）+ `statusSeverityChanged` 信号、`SessionArea` 转发并在切标签时重播、`MainWindow` 在状态栏放永久控件 `statusWarningIcon`（装配时设一次 pixmap、之后只切可见性）；`Document` 侧把「混合行尾」做成谓词 `hasMixedEndings()`（**末尾无换行不算一种风格**）并与 `eolDescription()` / `preferredEol()` 共用 `countEndings()`。补 9 个用例函数（`Tests/Text` 34 → 38、`Tests/TextView` 24 → 26、`Tests/Session` 50 → 52、`Tests/AppIntegration` 12 → 13）；**顺带删掉一处冗余路径**——图标原本有两条刷新路，导致两处变异互相遮蔽、双双漏检，收敛成一个写入点后 18 处变异 18 处检出 | TXT-010 |
| `e4fd533` | **TXT-012 内置替换规则**：新模块 `Services/Text/linereplacements.{h,cpp}`（规则表 `ReplacementRuleDescriptor`——四条规则的标识符 / 占位符 / 正则 / 说明，正则**同时**是引擎编译的那一份与界面要显示的那一份；`ReplacementSet` 按**表顺序**而非勾选顺序应用、出厂**空集**、空集时短路）；`CompareOptions` 末尾新增 `replacements` 字段并接进 `normalizedLine()` 的**最前面**；新套件 `Tests/TextRules`（19 条用例函数，刻意 `QT -= gui`），12 处变异 12 处检出 | TXT-012 |
| `cf4be86` | **DIR-008 二进制逐字节比对**：接手第 22 轮定时会话留下的 `.workbuddy/wip/DIR-008-partial-bytes.patch`（它从未被编译过）并把这条闭环。`Folder::Options` 末尾新增 `compareFirstBytes`（0 = 关闭，出厂关闭）；`Entry` 末尾新增与 `status` **正交**的 `partialComparison`——「部分比较」回答的是**结论覆盖了多少内容**，塞进主状态就必须为「开了快速模式却真的发现差异」这类组合长出一套优先级规则，而复用既有的 `Unknown` 则免费得到验收标准要的「明确标注为不完整比对」（`run()` 早已把 `Unknown` 映射成 `complete == false`）；字节预算卡在「大小不等」短路**之后**（那是一个不用读字节就证明了的结论，不该降级成不确定），分块循环再用 `want = min(块上界, 预算 − 已读)` 夹逼以免越过第 N 字节；分块上界提成公开常量 `kMaximumCompareBlockSize`（它原来藏在 `.cpp` 里，于是「分块读取且块大小有上界」这条标准只能靠匹配源码字面量去守）；设置键 `folder.compareFirstBytes` 只接受**非负整数**（`0.5` 截断后恰好等于「关闭」，字符串 `"4096"` 同样拒绝）；`FolderCompareView` 与 `m_maximumDepth` 同法**原样带回**该值（视图不带回它，任何一次「读视图选项 → 写回会话」都会把预算静默清零）；第 4 条的「偏移量在报表中可查看」此前**从没有任何用例见过**，本轮补上。`Tests/Folder` 30 → 35、`Tests/Report` 36 → 37；**14 处变异 14 处检出、0 漏检**，其中「文件变化时收回 `partialComparison` / `firstDifference`」两处原本都是漏检。另修掉一处**在 `HEAD` 上就红着**的护栏：`tools/check_spec.py` 被上一轮 handoff 里的用例数写法撞红（见 §1.33 与 §6） | DIR-008 |
| `2e4272a` | **补上 DIR-008 第 1 条的核心词「提前」的断言**（它此前没有任何用例守着）：把 `compareFile()` 里「命中即 `break`」那一句删掉，全部 3687 条用例一条都不会红——既有夹具都是「大小相同、只有一处差异」，命中之后剩下的块两侧全同。给测试替身加两个注入点（`physicalPaths` 逻辑路径 → 真正被打开的路径、`frozenInfo` 冻结的申报元数据），造一个「申报尺寸 600000、实际只有 100 字节」的右侧，让命中之后**还有**可读内容，于是 `firstDifference` 会不会被后续的块改写就成了可观察量（断言 100；不停会变成 262144 / 524288）。**不动一行生产代码**。`Tests/Folder` 35 → 36；15 处变异 15 处检出，M15 只打红这一条新用例 | DIR-008 |
| `4294cd8` | **崩溃的套件自动补跑一遍 `-v2`**（ENG-003 / ENG-004 的基础设施，**新功能**）：`run-tests.sh` 对「没产出 `Totals:` 行的非超时失败」自动补跑一遍 `-v2` 并**点名崩在哪一条用例**（`-v2` 里每个用例先写 `INFO : … entering`、跑完再写 `PASS`，所以最后一条 entering 的用例就是崩之前正在执行的那条），产物留 `verbose.txt` / `verbose.stderr.log` 并随 CI 上传。三条边界都有断言：只对崩溃补跑 / 补跑不传 `-o results.txt`（诊断不许改写证据）/ 补跑完整跑完就说「首次是偶发」。顺手把「启动二进制 + 超时看门狗」抽成 `run_binary_with_timeout()`（补跑必须走逐字相同的路径）。**两个实测前提**：`-o <file>,txt` 下 stdout 是 **0 字节**；`abort()` 与 `std::_Exit()` 之下文件里**最后一行仍在**（探针 `/tmp/vprobe`）⇒ 判定不依赖任何一次 flush。自测 24 → **30 条断言**；**7 处变异 7 处检出、0 漏检**（其中 M4 第一次整条漏检：判据写成 `grep 'Loc:'`，而那个探针里一条断言都没有——**同族错误第三次**，见 §6）。**未勾掉任何完成标准**：ubuntu 上 `Tests/Folder` 撞了哪一行要等下一次 CI 的 `verbose.txt` | ENG-003 / ENG-004 |
| `3417c59` | **自测失败时印出「期望 vs 实际」，并按遍快照证据**（诊断能力，**本轮新增**）：`st_line_matches()` 在整行断言不等时把**期望与实际**一起打出来——原来只有一句「不相等」，而 CI 上那条断言的期望是一整行硬编码文本，实际值完全看不见，这正是那条红连续 13 次没人能一眼定性的原因；`st_snapshot_evidence()` 在**每一遍结束立刻**把证据写盘（合计行 / 运行器自己的异常清单 / 每个探针的 `summary.env` 字段与 `Totals:` 行）到 `evidence-jobs4.txt` / `evidence-jobs1.txt`——**两遍共用一个构建目录**，不快照就只剩第二遍的。**未动一行生产代码** | ENG-003 / ENG-004 |
| `f809ff4` | **超时套件的用例数不该计入合计，并让这条边界在每个平台上都被验到**（§1.35，真 bug）：ubuntu 腿的自测**连续 13 次**红在 `合计把各套件的用例数累加起来了`，真因是累加那一步只看「有没有统计行」而没排除超时——**Linux 的 Qt Test 接住 `SIGTERM` 后会自己补写一份统计行再以 `SIGABRT` 收尾**（`status=134`、`has_summary=1`），macOS 则直接被 `TERM` 带走、什么也不写（`has_summary=0`），于是同一条判据在两个平台上是**两种形状**，挂死探针的 `1 passed, 1 failed` 在 ubuntu 上被算进了合计；连带效应是自测红了之后「运行测试套件」被 skip，ubuntu 腿**一个套件都没跑过**。修成 `if [[ ${hs} -eq 1 && ${to} -eq 0 ]]`（与运行器自己打印的承诺一致），并让挂死探针**自己先写一份统计行再挂死**——否则这条边界在 macOS 上恒不可达，**任何本地变异都 redden不了它**。自测 30 → **32 条断言**；**5 处变异 5 处检出、0 处漏检**。**未勾掉任何完成标准** | ENG-003 / ENG-004 |
| `446bdcc` | **DIR-011 条目状态判定与语义**：新模块 `Services/Folder/entrystatus.{h,cpp}`（9 档主状态表 + 内容证据 8 档 / 时间关系 4 档各一张表，**三张表都带一个把表当参数的校验函数**——自己读内部表的自检喂坏表进去也永远绿；`Entry::partialComparison` 由独立布尔字段改成 `contentEvidence == Partial` 的只读视图，把 DIR-008 留下的两份说法并成一份；存在性做成派生视图；`BaselineView` 要过 `validateBaselineView` 且细化只作用在叶子；`aggregateChildren()` 是引擎与用例**共用**的唯一父子汇总，否则「父子视图结论一致」会退化成「两套口径碰巧今天结果相同」；`statusReasonLines()` 三节 + `statusModelViolations()` 九类不可能组合）+ 视图侧状态图标列 / 按表铺的筛选下拉 / 越界整数回落 / 右键「为什么是这个状态」（三节恒定出现）+ 报表与命令行改用同一张表 + 9 个中性灰描边状态图标。**新套件** `Tests/EntryStatus`（27 个用例函数，纯 QtCore），`Tests/Folder` 35 → 45；全量 3688 → **3724**；**15 处变异 15 处检出**，其中 2 处必须一次改两个地方（同一判据在仓库里写了两遍、两道防线互相遮蔽，见 §6） | DIR-011 |
| `5d672b5` | **DIR-003 递归子目录策略**：新模块 `Services/Folder/recursionstrategy.{h,cpp}`（三档表 + 七项自检 `validateRecursionTierTable(table)`；档位 ↔ `Options` 双向映射——**不新增第三个字段**，`recursionTierOf()` 按**行为**归类（`!recursive \|\| maximumDepth <= 0` 是第一档），`applyRecursionTier()` 在 `Full` 档只碰 `recursive`、仅当上限 `<= 1` 才提到缺省；深度边界上**唯一一份**解释文案 `recursionBoundaryExplanation()`，印的是 `qBound` 之后**实际生效**的上限；循环符号链接判据 `linkTargetReentersAncestor()`（解析目标 == 链接自身**或**是它的严格上级，相对目标按**链接所在目录**解析，指向兄弟 / 下级子树的刻意放过）+ 文案 `linkCycleExplanation()`，引擎在符号链接分支记 `Status::Error` + `ContentEvidence::NotCompared`）+ 视图侧两态复选框 → 按表铺的档位下拉（`folderRecursionTier`，带 `ToolTipRole`）+ 深度数字框（`folderMaximumDepth`，只在完全递归档可改）+ `rescanRequested()` 信号（**刻意不带路径参数**）→ 会话 `reload()`。**顺带删掉一处遮蔽防线**：`applyTierToControls(tier, bool fromUser)` 的 `if (fromUser)` 在程序性路径上恒被覆盖或恒等，删掉它没有任何用例变红（M13 第一次跑就是 green）。`Tests/Folder` 45 → 51；全量 3724 → **3730**；**15 处变异 15 处检出**，其中两处（越界深度必须印 256、根目录作上级不能拼成 `//`）是补断言之后才抓得住的，见 §1.38 与 §6 | DIR-003 |
> 这张表里**从 `a693346` 起的行**都由**紧随其后的纯文档提交**补写提交号，原因见下——
> 会因为 `--amend` 每次都改变提交号而永远对不上。
> 补写链**只到代码提交为止**：补写这些行的那个纯文档提交自身不再上表，否则每轮都会多出一行
> 永远指不到自己的记录（`bf05600`、TXT-003 与 TXT-008 那两轮的补写提交都是这样未被记录的）。
> 本轮走的是同一条路：`5d672b5` 一条代码提交先定稿，再由紧随其后的纯文档提交
> 把它的提交号写进本表（**那个补写提交自己不上表**，理由同上）。
> 更早一轮的写法同此：`446bdcc` 一条代码提交先定稿，再由紧随其后的纯文档提交
> 把它的提交号与 §1.37 / §4.0.15 一起写进本文档。

远端：369 个 issue 全部创建，标签为 `需求 / 待实现 / <模块> / <优先级>`，
其中 P0 59 条。反查入口是 `docs/github/prd-issues.json`。

**核对「本地与远端是否一致」时走 SSH**：`git fetch origin` 走的是 https，
在本机网络下偶发 `Error in the HTTP2 framing layer`（同一个仓库的 SSH 推送却正常）。
用下面这条更稳，且不依赖任何配置：

```bash
git ls-remote git@github.com:LorenHan/LqCompare.git refs/heads/main
```

**推送必须走 SSH。** `gh` 登录的 token 只有 `gist`、`read:org`、`repo` 三个 scope，
推 `.github/workflows/` 下的文件会被拒（`refusing to allow an OAuth App to create or
update workflow ... without workflow scope`）。走 SSH 不受这个限制：

```bash
git push git@github.com:LorenHan/LqCompare.git main
```

**配套的坑**：`origin` 配的是 **https** URL（见 `git remote -v`），而推送走的是
**SSH** URL，两者在 git 眼里是两个不同的远端。于是 `git push --force-with-lease`
会**永远**报 `stale info` 而被拒——它拿不到与推送目标 URL 对应的 remote-tracking
引用，无法核对租约，于是保守地拒绝。这不是「有人改过远端」，但报错信息看不出
这个区别，很容易被误读成「远端被别人推过」从而不敢继续。

需要强推（例如 amend 后要覆盖刚推上去的那个提交）时，把期望值显式写出来：

```bash
git fetch origin                                     # 先让本地知道远端在哪
git push --force-with-lease=main:<远端当前提交> \
    git@github.com:LorenHan/LqCompare.git main
```

**能不强推就不强推。** 这里有一条踩过的坑：**不要把某个提交的提交号写进这个提交自己**。
`git commit --amend` 每次都产生新的提交号，于是「回填 → amend → 提交号变了 → 再回填」
是个死循环，中途总会留下一个指不到任何东西的假提交号（PLAT-004 那行就一度是 `ffa093a`，
而它早已被 amend 掉了）。正确做法是**两步**：先把代码提交定稿，拿到提交号，
再用一个纯文档提交把这一行补上——本节的 PLAT-005 行就是这么来的。
未推送时本来就该这样做；已推送时先补写会需要强推，更不划算。

## 4. 下一步该做什么

> 本仓库有一个**每 2 小时运行一次的无人值守任务**，按本节与 §4.1 的指引推进 issue 队列，
> 每轮完整闭环 1～2 条（含测试、护栏、提交、推送、issue 更新与本目录下的工作日志）。
> 因此你会看到没有人在场的提交——它们不是手滑推上去的，判定标准与本文件写的完全一致。
> 想让它停下来，把那个自动化暂停即可；已经推上去的提交都是各自独立的闭环，可以单独回退。

### 4.0 2026-09-21 更新：夜间产出的收尾与下一步

> **最新一轮（2026-09-21 11:53）闭环了 `TXT-002`（基础行对齐算法，issue #57），
> 见 §4.0.4 与 §1.25**——它对的是四条完成标准里第 3、4 条一直没被断言的问题
> （「全相同 → **单个**相同块」与「逐字段相同 + 快照」），只加测试、不动生产代码。
> 再往前一轮（11:41）闭环了 `ENG-003`（测试运行器，issue #335），见 §4.0.3 与 §1.24。
> 下面的 §4.0 / §4.0.1 / §4.0.2 是更早几轮的记录，**优先级表以 §4.0.4 的结尾为准**。

**本轮闭环了 `OPT-010`（日志与诊断选项，issue #365）**，它是「界面接通那一批」
（下面第 1 条）里的**第二张设置页**，接手前先读 §1.18。它和 OPT-005 一样把
「服务层出策略、界面只搬运」当范式，但有两点新东西值得后续条目照着做：

- **判定是注入时间的纯函数**（`rotationDecision(policy, currentSize, fileDate, now)`）。
  「按日滚动」天然依赖「今天是哪天」，而把 `QDate::currentDate()` 写在实现里，
  那条分支就只能靠等一天来测。把时间当参数之后，测试用假时间把跨天、同一天、
  文件比现在新（时钟回拨）三种情形一起测掉。
- **副作用必须落在真正的路径上，而不是只有一个显式的入口**。
  `SetRotationPolicy()` 只改状态；真正会滚动的是 `appendRecord()`——
  写每一行日志之前检查一次（`QElapsedTimer` 限流每秒至多一次）。
  只在「应用设置」时检查的实现，会让用户在日志占满磁盘之后仍然要自己想到去点一下。

**本轮修掉了一个真实的隐私缺陷**（不是新写的代码引入的，是新写的测试抓出来的）：
脱敏的右边界原本要求「路径后面必须跟分隔符」，于是 `/Users/loren `（后面是空格）
**不会**被替换，导出的诊断包里就带着真实家目录。判据已反转成
「后面不能是名字的延续字符」。这条值得记住的是**方向**：脱敏宁可多替换一次
（`/Users/lorenx` 被误替换，只是看着怪），也不能漏一次（那是真的泄露）。

**验证数据**：`Tests/LogDiagnostics` 89 个用例函数（九组 A~I，纯 QtCore，刻意不链接
QtGui）；`Tests/Logging` 34 → **43**、`Tests/Options` 46 → **50**、
`Tests/OptionsDialog` 17 → **23**；全量 **3596 / 0 / 2（66 套件）**；
五道护栏全绿（winapi 源文件数 326 → **332**）；主程序 `make -B -j8` 本仓 0 warning、
离屏启动正常；**17 处变异 17 处检出、0 漏检**。

### 4.0.1 本轮（2026-09-21 07:4x）：ENG-004 持续集成流水线

**本轮不是在做功能，是在修一条「看着存在、其实从来没跑过构建与测试」的流水线**，
细节见 §1.0.6 与 §1.20。三句话版本：

1. **它以前红在第一跳**：匿名 clone 私有仓 `MyClass` 拿 LqRibbon，必然 128 退出，
   于是后面的「构建主程序」与「运行测试套件」**一次都没开始过**。五道静态护栏倒是
   一直在跑（13 秒全绿）——所以「CI 全绿」这句话此前只覆盖了静态检查。
   修法：把「拿不到私有依赖」写成降级开关，跳过主程序构建与打包、照跑其余 64 个套件。
2. **降级范围要实测**：66 个测试工程里只有 `Tests/AppIntegration` 与 `Tests/CommandActions`
   真的依赖 LqRibbon。此前 workflow 注释里那句「一个都不 include Views/，所以都不依赖它」
   是**错的**，依据只是「没 grep 到」——见 §6 的两条新坑。
3. **排除必须被看见**：新增 `LQCOMPARE_TEST_SKIP`，但被排除的套件会连同数量打在开头与
   末尾汇总里，末尾不再说「全部套件通过」。**这一条比机制本身重要**：静默跳过与显式排除
   在日志上只差一句话，却能让「64 个已验证」被读成「全部验证过」。

**下一步（本节的核心用途）**：三条腿的真实结果**已经拿到**（run `35546164218`，见 §1.20），
下一步按这个顺序做，**不要**先开新的 issue 条目：

1. ~~修 `Code/Tests/Archive` 的夹具校验~~ **已在同一轮做完，见 §1.21**。
   注意当时写在这里的「改法」是**错的**：真正的原因是 Python 3.14 的
   `zipfile._open_to_write()` 无条件把 UTF-8 位置上，把夹具**写**坏了，
   不是回读校验太严。照原「改法」（把 `cp437.zip` 从回读名单里去掉）做会拆掉
   唯一能发现这件事的机制。结论：**报错的那一步不一定是错的那一步**，
   先把「被写出来的字节」与「声明的意图」对齐，再谈改判据。
   **还差一步 CI 确认**：本机跑不了真 3.14，所以要等下一次 CI 运行看 macOS 腿是否变完整，
   在那之前 ENG-004 第 2 条保持不勾。
2. ~~读第一次 CI 上传的 `build.log`，定位 ubuntu 的 17 个与 Windows 的 26 个套件构建失败。~~
   **已在同一轮读完**（这批 `build.log` 是本轮刚加的上传项，第一次跑就有 6 个 artifact 可读；
   `gh run download <id>`，本机 `gh` 在 `/opt/homebrew/bin/gh`）。两条腿的账都结清了：
   - **ubuntu 的 17 个失败＝1 个文件**：`Code/Services/Files/trash_linux.cpp` 缺一句
     `#include "pathutils.h"`。该文件**只在 Linux 上编译**，所以「本机编得过」对它毫无意义。
     **已修**（`525a872`），已用 `-fsyntax-only` 在本机造出同样的报错再消掉。
   - **Windows 的 26 个失败＝5 个文件**：两类系统性原因（`_WIN32_WINNT` 定得太低、
     `WIN32_LEAN_AND_MEAN` 把 `winioctl.h` 挡掉了）＋两个真 bug
     （`number(DWORD&)` 因 Windows 分支没写显式转换而歧义；`registrystore_win.cpp`
     对 `QString` 调了 `QStringList::removeAll`）。**已写成 §1.22 的可执行清单**
     （逐文件的原始报错、判定、改法）。
   - **下一轮的第一件事**：只做 §1.22 里那**两条系统性原因**（`_WIN32_WINNT` 与 `winioctl.h`），
     推上去、看下一次 CI 剩多少红，**再**动那两个真 bug。理由：系统性原因会一次消掉一大片报错，
     先做能把「26 个」这个数字迅速压小，避免被残留报错误导着去改本来没错的代码；
     而 Windows 是交付目标平台，这 26 个失败此前**从未有人看见过**，更不该一口气盲改 5 个文件。
3. ~~读第一次 CI 结果~~ **已在同一轮读完，见 §1.23**。结果：**macOS 腿已完整**
   （64 套件、3568 passed / 0 failed），ubuntu 的构建失败 **17 → 0**，
   只剩 `Tests/Folder` 一个**运行期崩溃**（`linksAreComparedWithoutFollowing()`，
   符号链接比较，第 8 个用例），Windows 未动。同轮把「崩溃原因看不见」补掉了：
   stderr 现在落盘 `<套件>/stderr.log` 并进上传产物，失败时贴出末尾 15 行
   （5 处变异 5 处检出，配方见 §1.23）。
   **而且这条路第一次跑就回报了**（run `35551066378`，见 §1.23.1）：
   `Tests/Folder` 的 `stderr.log` 是 **`*** buffer overflow detected ***: terminated`**
   ——glibc 的 `_FORTIFY_SOURCE` 抓到的缓冲区越界，**macOS 上永远看不到的那一类**。
   **所以下一轮拿到新 CI 后要看的其实是两件事**：
   - `Tests/Folder` 的 `stderr.log` ——**它现在会给出 Linux 上崩在哪一行**，这是本轮
     特意为它铺的路；先看这个再决定改什么，不要凭猜。
   - Windows 的两条系统性原因有没有把 26 压下来。
4. **`Tests/Folder` 的下一步（按顺序试，别跳步）**：
   1. **在本机用 ASan 复现**（最低成本、最可能一次定位）：
      `Tests/Folder` 单独编一份带 `-fsanitize=address -g` 的、跑
      `run-tests.sh Folder`——`_FORTIFY_SOURCE` 与 ASan 盯的是同一类错误，
      而 **ASan 在 macOS 上可用**。它会给出行号。
   2. 若 ASan 静默：给崩溃的套件**自动重跑一遍 `-v2`**（`run-tests.sh` 里已有
      「没产出 Totals 行」这条判据，加几行就行），`-v2` 会打印每条 `QVERIFY`，
      **最后一条打印出来的语句就是崩之前正在执行的那一句**。
   3. 重点看 `Services/Files/filesystem_posix.cpp::linkTarget()`（`readlink` 的增长循环，
      第 198~233 行）与 `realpath`/`canonicalFilePath` 那几处——
      符号链接比较这条路径上只有这几处会碰固定长度缓冲。
5. 顺手把 `actions/checkout@v4` / `actions/upload-artifact@v4` 升到 v5
   （运行器已有 Node 20 弃用告警，现在是警告、将来是错误）。
6. 上面几条做完再回到功能条目。
7. **别再连推四次**：每一个推送都会触发一整轮 CI（三条腿）。攒成一次推送，
   推完把被覆盖的 run 取消掉（`gh run cancel <id>`），否则三条腿会互相抢 runner。

看结果的命令是 `/opt/homebrew/bin/gh run watch` 与
`/opt/homebrew/bin/gh run view <id> --log-failed`（本机 `gh` 不在默认 PATH 上；
下载产物用 `/opt/homebrew/bin/gh run download <id>`）。若 Windows 腿因为平台原因红掉，
**不要**用 `continue-on-error` 把它盖住——那会把「Windows 交付目标从未被验证」这件事变成
一句没人看得见的话；正确做法是让它在日志里说清楚原因。

### 4.0.2 更早一轮：OPT-005（issue #317）

**再往上一轮（那一轮）闭环了 `OPT-005`（文件操作选项，issue #317）**，它是「界面接通那一批」
（下面第 1 条）里的第一张设置页，接手前先读 §1.17：

- 落地方式可以当**后续所有 OPT-* 设置页的范式**：服务层出一个**纯数据策略对象**
  （`Files::FileOperationPolicy`，`fromValues(QVariantMap)` 从草稿读出、`validate()` 校验、
  `safetyContractViolations()` 表达安全契约），设置项在
  `OptionsRepository::definitions()` 里登记（8 个 `fileops.*` 键），界面在
  `OptionsDialog` 里只做三件事——按分类建控件、把控件值写进草稿、把服务层的解释结果
  贴成提示文案。**界面自己不再写第二份默认值、不做第二套校验、不写第二套标签**
  （下拉文本直接取服务层的 `*Label()`，数字框后缀直接取定义表里的 `unit`）。
- **把「默认值必须保守」写成了可执行的契约**：issue 的边界一栏写的是一句话，它现在是一个
  会返回违规清单的函数。于是「有人把默认删除方式改成永久删除 / 把覆盖默认改成直接覆盖」
  在测试里是红的，而不是靠 review 用眼睛盯。这是本模块唯一一处自我否定式判定。
- **一处易踩的接口错**：`OptionDefinition` 是按**位置**聚合初始化的，给结构体加字段只能
  加在**末尾**。本轮把新字段 `unit` 插在了中间，结果所有只写到 `maximum` 的行整体错位一格，
  编译报的是「没有构造函数」——错误信息指不到具体那一行。已在头文件里写明理由（见 §6）。
- **验证数据**：`Tests/FileOpsOptions` 92 个用例函数（九组 A~I，纯 QtCore，刻意不链接
  QtGui）；`Tests/OptionsDialog` 16 → **17**；全量 **3487 / 0 / 2（65 套件）**；
  五道护栏全绿（winapi 源文件数 322 → **326**）；主程序 `make -B -j8` 本仓 0 warning、
  离屏启动正常；**25 处变异 25 处检出、0 漏检**。

**本轮顺带做掉了一个悬着两轮的待定决定**：§2 与 §4.0 一直在说的「那几条启动自检的去留」。
结论是**不加回 `main.cpp`，并在 `architecture.md` §5 与 §2 里把「启动时打印」的说法删掉**：
这些表的校验已经由各自套件里「拿故意写坏的表喂同一个判定」的用例覆盖，把它加回启动路径
只会在每次启动时多打几行恒定为「0 项问题」的日志。目前处于「有实现、有单测、无生产调用点」
的 `validate*()` 共五个（`validateSessionSettingsCatalog` / `validateFilterLayerTable` /
`validateAttributeConditionTable` / `validateNameFilterTables` / `validateContentFilterTables`）。
**留下的是显式决定，不是遗漏**——接手时不要顺手把它们接回启动路径。

**上上轮（03:3x）闭环了 `PLAT-006`（单实例与进程间通信，issue #327）**，并纠正了
上两轮对它的两处误判，接手前请先读 §1.16：

- 夜间那 24 个工作流**已经把这一条实现完了**（`instanceprotocol.{h,cpp}` +
  `singleinstance.{h,cpp}` + `Tests/SingleInstance` 104 条），但 issue #327
  一直是「待实现 / 0 个勾 / 0 条评论」，文档里的「下一步」也还在推荐做它。
  本轮做的是**验证 + 闭环**：全量测试、五道护栏、全量重编、离屏启动，
  外加 12 处变异（检出 11，1 处受环境影响不可判定）。
- **本轮的实质产出是一条被漏掉的用例**：变异 M9 把 `splitFrame()` 里
  「声明长度必须等于实际长度」这一比较去掉后，**没有一条用例变红**——
  因为原有两条用例构造的输入都恰好被另一处「载荷必须读尽」的检查兜住了。
  补的 `decodeRejectsAFrameShorterThanItsDeclaredPayload()` 让声明比实际多一个字节、
  而**已有的字节本身仍是一个完整可解析的请求**，于是那一比较成为唯一拦得住它的检查。
  这就是「两处纵深防御里，去掉任意一处都不该让整套测试全绿」的具体一例。
- **另一条结论是环境限制**：本机 `QSharedMemory` 不可用（裸 `shmget()` → `ENOMEM`，
  Qt → `OutOfResources`），因此单实例实际跑的是**进程锁 + 本地套接字**这条回退路径，
  `recoverStaleIdentifier()` 分支在本机不可达。**不要**把「单实例测试全绿」
  读成「共享内存那条腿验证过了」。

**下一步**：`PLAT-006` 之后，A 工作流里已经**没有「纯判定、本机可完整闭环」的条目**了
（见 §4.1 那张表）。因此下一轮应转到**界面接通**那一批（下面第 1 条），
而在那之前先做第二件事更划算——**决定那几条启动自检的去留**：`main.cpp`
现在既不打印它们、又没人删它们，四个 `validate*()` 处于「有实现、有单测、
无生产调用点」的状态，悬着不决定的话下一个人会照 §2 的旧描述去找那几行日志。

**下一轮开工前先看这三条实地核对的结果**（本轮顺手查的，省得再查一遍）：

- **`OPT-002`（常规选项）实际是 2/5，不是 0/5**，标签却还是「待实现」。
  已落：第 2 条（`general.singleInstance`，默认「复用已有实例」）与
  第 3 条的**接线**（`MainWindow.cpp` 里 `SessionArea::sessionCountChanged` 计数为 0 且
  `general.lastSessionAction == "exit"` 时 `QTimer::singleShot(0, this, &QWidget::close)`，
  否则留在 Home 页）。**但第 3 条没有任何自动化覆盖**——`Code/Tests/` 里搜不到
  `lastSessionAction`，也就是说「关掉最后一个会话到底会退出还是留在 Home」这件事
  目前只有读代码这一个证据。第 1、4、5 条（记忆上次会话 / 指定工作区 / 系统自启动 /
  文件关联的开关）**确实没做**，而且 `optionsdialog.cpp` 在「常规」页上自己写了一句话
  说明它们尚未实现——那句话是准确的，不要把它当成待删的占位文案。
  → 因此**下一轮如果做 OPT-002，正确的最小集是「给第 3 条补一条测试」**：
  它的实现在 `MainWindow` 里，测试要落在 `Tests/AppIntegration` 那一类能起真窗口的套件上
  （`Tests/Options` / `Tests/OptionsDialog` 只到设置仓库与对话框，够不到这条路径）。
  勾第 3 条之前先想清楚这一点，否则又是一条「只有读代码能证明」的完成标准。
- **`OPT-001`（选项对话框框架）是 5 条里落 4 条**：分类树、搜索框、底部三按钮 +
  未应用时的切换提示、每项 tooltip、以及「读写集中在设置仓库」都在
  `Views/Options/optionsdialog.{h,cpp}` 里，并有 `Tests/OptionsDialog` 覆盖。
  **第 1 条只落得动一部分**：`categories()` 现在返回 4 个分类
  （`general` / `display` / `logging` / `storage`），而规格列了 12 个——
  其余 8 个分类的**页面**分别属于 OPT-003 ~ OPT-011，不是本条目能自己补齐的。
  因此它该有的标签也是「部分完成」，理由是「框架齐、分类随各自的 OPT-* 条目长出来」。
- **`OPT-013 / OPT-014` 的处境要按源码核对**：`optionsrepository` 里已经有
  `importFile` / `exportFile` 与「导出外观类 / 含机器相关路径」两个开关，
  而 `Tests/Options` 里已经有对应的导入预览与逐项选择用例——
  也就是说这两条的**服务层可能也已经落地了**，别按标签直接开工。

下面这一轮（02:2x）在同一节的基础上推进了 **`FILT-004` 内容过滤器的服务层**
（`Services/Filter/contentfilter.{h,cpp}` + `Tests/ContentFilter` 85 个用例函数）。
第 1、2 条已落，第 3 条落了一半（顺序定死并有用真比对引擎的证据，接进 `Services/Text`
的那一步未做），第 4 条缺状态栏、第 5 条缺 Filters 页的写入入口（宿主「文件格式定义」
**已经落地**，见 §1.19；本文档此前把它写成「缺文件格式定义」是错的）。它**没有**解阻塞
任何界面类条目——剩下的三半都需要别处的宿主，详见 §1.15 与 §4.1。因此下面第 1~5 条的
排序不变，另外新增了第 6 条（内容过滤剩下的三处接线）。

夜间那 24 个工作流已经把**服务层与视图层的主体骨架铺满了**（`SingleInstance` 104 条、
`Snapshot` 114 条、`Media` 378 条、`Cli` 108 条、`Script` 112 条……），
因此 §4 下面那张「从哪条 issue 开始」的表**有相当一部分已经过期**：它建议的
`PLAT-006` 第 1、2、4 条、以及若干「下一个该做哪条」的判断，都落在夜间已经落地的
范围里了。接手时**先按 `docs/development/team-*.md` 与各 issue 的「落地说明」评论
核对实际进度**，不要按下面那几条直接开工。

本轮之后，按性价比排序的下一步是：

1. **把「界面接通」当一批来做**——这是目前最大的、也是唯一成片阻塞的缺口。它一口气
   卡着六条：`SESS-007` 第 2、3 条（设置目标下拉的去向提示、关标签丢弃提示）、
   `FILT-005` 第 2、3 条（每层启用开关、「查看最终生效过滤」面板）、
   `FILT-003` 第 5 条与 `FILT-002` 第 3 条的界面部分、`FILT-004` 第 4 条
   （状态栏那句「内容过滤已启用，比较速度会降低」）、以及 `SESS-006` 的 Rules 按钮
   接线。**它们的共同住所是设置页 `OPT-*`**，所以先落 `OPT-*` 的设置页与 Filters 页，
   再回头把这些条目的界面部分一次性勾掉，比逐条零敲碎打划算得多。
   **本批已开工**：`OPT-005`（文件操作页）已闭环，做法见本节开头与 §1.17，可直接照抄。
2. ~~**决定那几条启动自检的去留**~~ **已决定（本轮）**：**不加回 `main.cpp`**，
   并在 `architecture.md` §5 与 §2 里删掉「启动时打印」的说法。理由是这些表的校验
   已由各自套件里「拿故意写坏的表喂同一个判定」的用例覆盖，接回启动路径只会每次多打
   几行恒为「0 项问题」的日志。处于「有实现、有单测、无生产调用点」的 `validate*()` 共
   五个（`validateSessionSettingsCatalog` / `validateFilterLayerTable` /
   `validateAttributeConditionTable` / `validateNameFilterTables` /
   `validateContentFilterTables`）。**这是显式决定，不是遗漏**——不要顺手接回去。
3. **补 `VCS-012` 缺的两个选项**（「忽略空白改动」「跨重命名追溯」）需要先扩
   `Vcs::Backend::blame()` 的签名——这是跨层改动，不要只改视图层假装做了。
4. **收紧 `tools/check_icons.py` 的裸名引用盲区**（见 §6 新增的那条）。
5. 平台的「写了但没在目标平台编译/运行过」清单仍是 Windows 与 Linux 的薄层
   （`filesystem_win.cpp`、`trash_win.cpp`、`iconservice_win.cpp`、`registrystore_win.cpp`、
   `shellintegration` 的 Win32 路径、`iconservice_linux.cpp`），**这些一条都不得勾选完成标准**。
6. **把内容过滤真正接上（`FILT-004` 剩下的三处）**：一是比对入口在应用忽略规则**之前**
   先过一遍 `LineFilter`（`Services/Text` 侧，本模块已经把顺序与证据准备好，见 §1.15）；
   二是 Filters 页启用开关与状态栏提示（等 `OPT-*`）；三是把
   `line-filter` / `key-byte-filter` / `content-filter-enabled` 三个键写进文件格式定义
   ——**这一处的宿主已经存在**（`FMT-001` 已落地，见 §1.19；`FormatDefinition::settings`
   本就是原样往返的不透明袋），但它同样要等 Filters 页来提供入口。三处的落地位置互不相同，
   **不要**为了「凑一条完整闭环」把它们塞进同一轮。

| 对话 | 工作流 | 从哪条 issue 开始 | 交付什么 |
| --- | --- | --- | --- |
| **B 会话框架** | 继续（**`SESS-001`、`SESS-002`、`SESS-006`、`SESS-007` 都已落地；SESS-007 剩下的全是「界面接通」，而界面要等第一个真正的会话类型**） | 先落一个具体会话类型（`TEXT-*` 或 `FOLD-*`），或改做 `SESS-008`（会话文件保存与加载） | 这里有一个**新产生的依赖链**：SESS-007 第 2、3 条被「第一个真正的会话类型」卡住（对话框要有设置目标才谈得上去向提示，标签要有持有设置的会话才谈得上关标签丢弃）。因此不要再往「框架」方向加条目 |
| **H 过滤与格式** | **本模块的「纯判定、本机可完整闭环」条目已经全数落地**（`FILT-001` / `FILT-002` / `FILT-003` / `FILT-005` / `FILT-004` 的服务层都在），继续往 H 加条目只会继续攒界面接口 | `FILT-006` 第 4 条（构造隐藏条目并断言两个选项下的行为差异，纯逻辑）；`FILT-007` 仍缺别的模块 | 仍在 `Services/Filter/` 内，纯逻辑，复用 `splitLines()` 那套与「表当参数的自检」做法；**不要**再去碰 `FILT-004` 剩下的三处——它们分别在 `Services/Text`、状态栏与 `Format/` |
| **A 平台底座** | **本模块的「纯判定、本机可完整闭环」条目也已见底**（`PLAT-006` 已落地，见 §1.16 与 §4.1） | 改做「界面接通」那一批（本节第 1 条）；`PLAT-004` / `PLAT-005` 第 1 条只能在真的 Windows 上做 | 服务层都已就绪；**不要再往 `Services/Platform/` 里加条目**——剩下的全是「界面取用」或「目标平台验证」 |
| **M 选项与外观** | **本批已开工**（`OPT-005` 已闭环见 §1.17；`OPT-001` 框架 5 条里落 4 条；`OPT-002` 实为 2/5） | 接着落下一张设置页。`OPT-002` 的正确最小集是「给第 3 条补一条能起真窗口的测试」（落在 `Tests/AppIntegration` 那一类套件上，`Tests/Options` / `Tests/OptionsDialog` 够不到 `MainWindow` 的关闭路径） | 仓库与对话框都已就绪，**加一张页 = 三步**：① 服务层加一个 `fromValues(QVariantMap)` 式策略对象（`fileopsoptions` 是范式）② `OptionsRepository::definitions()` 里登记键 ③ `OptionsDialog::buildPage()` 里加分支。**注意**：`OptionsDialog::categories()` 现在返回 **5** 个分类（`general` / `display` / `logging` / `storage` / `fileops`），规格列了 12 个；`Tests/OptionsDialog` 里有一条断言分类数，加页时要一起改 |

**为什么 H 的下一步不再是「先接 FILT-005 / FILT-003 / FILT-002 / FILT-004 的界面」**：这四条的
服务层都已经落地（三层叠加、启用状态、合并表达式、面板数据、落点路由；大小/时间/
属性位/所有者四类条件与三态结论；名称侧的模式/组合语义/超时保护；内容侧的行过滤与
关键字节判定），但它们剩下的全都是**界面接通或其他模块的宿主**（设置页的过滤 Tab、
Filters 页、面板的控件宿主、属性条件与名称过滤的输入框、状态栏那一行提示、
比对引擎里应用忽略规则的那一步、Filters 页的写入入口——**不是**文件格式定义，
那个宿主 `FMT-001` 已经落地，见 §1.19）。`OPT-*` 一天不落地，这几件事一天接不上，
再往 `Services/Filter/` 里加条目也只是继续攒界面接口。因此转 A 工作流。

**为什么 A 工作流的 `PLAT-006` 已经不再是一个「该开工」的条目**：它在前两轮被写成
本机唯一能整条闭环的候选（这一条的三条完成标准——跨平台单实例、参数转发与退出码、
首个实例无响应时超时降级——只依赖本机就能真跑的东西），但夜间那批工作流**已经把它
实现完了**，本轮只是验证并闭环（issue #327 现在是「部分完成」，第 3 条等一个把置前
策略暴露给用户的地方）。因此现在 A 工作流里已经没有「纯判定、本机可完整闭环」的条目，
下一步转「界面接通」那一批。**这一段的教训值得记**：那份「下一个该做哪条」的判断
是在 `docs/development/team-*.md` 与各 issue 的落地说明**之外**写出来的，
它们跟不上夜间那批产出；选条目前先核对源码。

**FILT-005 留下的确切接口**：`FilterStack` 收三层声明（`setDeclaration`）与启用标志
（`setLayerEnabled`），`decide()` / `accepts()` 出结论，`combinedExpression()` /
`preview()` / `buildEffectiveFilterPanel()` 出界面要的东西；三层的**读写落点**统一走
`FilterLayerBinder`（`setViewStore` / `setSessionStore` / `setBuiltinDeclaration` /
`loadInto` / `saveLayer` / `discardViewLayer`）。接界面时注意三条：过滤声明**不能**用
`ScopedSessionSettings::value()` 读（那是覆盖链，会丢掉会话层），视图层的声明必须经
`setViewStore()` 给进来（**不接上时 `saveLayer` 返回 false 而不是退而写会话层**），
以及三层共用一个键名 `filter-declaration`——它们住在三个不同存储里，不会互相覆盖。

**FILT-003 留下的确切接口**：`AttributeFilter::parseDeclaration(text)` 把一段声明文本
（一行一个条件、`#` 注释、`\n`/`\r\n`/`\r` 都认）解析成条件对象 + 问题清单，
`ConditionProblem` 带 `line` / `column` / `hint`，界面据此就地标红；`toDeclarationText()`
是它的逆（往返一致，可以直接写回设置项）；`attributeFilterDeclarationKey()` 返回
`"attribute-filter"`，是它将来在设置里占的键名。判定入口是
`decideEntry(const FilterStack &, const AttributeFilter &, const EntryMetadata &)`——
**名称侧与属性侧合成「与」的那一处就在这里**，返回的 `EntryFilterDecision` 同时保留
两侧的结论、起决定作用的那一侧与原因文案。`EntryMetadata` 是元数据快照，
构造方式是链式的（`forName(…).withSize(…).withLastModified(…)`），
**`AttributeFilter` 永远不碰文件系统**（有读源码的护栏盯着）。
接扫描器时注意两条：**`hasSize` / `hasLastModified` / `knownAttributeBits` 要如实填**，
「不知道」与「是 0」在判定上完全不同（前者放行并报 `undecided`）；以及
**`TimeCondition::referenceTime()` 必须由调用方设置**，模块自己不取当前时间。

**FILT-002 留下的确切接口**：`NameFilter::parseDeclaration(text)` 把一段声明文本
（一行一条表达式、`#` 注释、`\n`/`\r\n`/`\r` 都认）解析成表达式对象 + 问题清单；
每条表达式的模式由**行首前缀**决定（`=` 精确 / `*` 通配 / `~` 正则，无前缀默认按通配），
组合语义用 `setCombineMode(NameCombineMode::{AnyOf,NoneOf,AllOf})` 设定。
`NameFilterProblems` 带 `line` / `column` / `length` / `severity` / `kind`，界面据此就地标红；
`toDeclarationText()` 是它的逆（往返逐字一致，可以直接写回设置项）；
`nameFilterDeclarationKey()` 返回 `"name-filter"`，是它将来在设置里占的键名。
判定入口是 `NameFilter::decide(const QString &name)`，返回 `NameFilterDecision`
（`outcome` 是**三态** `Matched` / `NotMatched` / `Undecided`）。接的时候注意三条：
**`Undecided` 一律按放行处理但必须报出来**（`problems()` 里会有 `Timeout` 条目），
不要把它当 `NotMatched`；**超时保护默认是开着的**（`matchBudget().perEntryMs == 200`、
`consecutiveTimeoutLimit == 2`，可用 `setMatchBudget()` 调），它只保护正则模式，
精确与通配不走线程；**`analyzeNameFilterLine()` 是解析、实时校验、复验的唯一实现**——
界面的实时校验必须调它，不要再写第二份正则去解析同一行（这一点有读源码的护栏钉住）。

**为什么下一步是 `SESS-007` 而不是各会话类型**：`SESS-001` 定下了「会话」这个概念
（契约、状态机、三个出口、设置接口），`SESS-002` 定下了「会话类型」的登记方式
（内置类型的字段、ID 稳定性、按掩码的注册顺序优先查询、创建工厂），`SESS-006` 定下了
「一个会话类型有哪些设置项」以及这些项怎么读写与落盘（声明模型、草稿、询问策略）。
**唯一还没有定义的是「一次改动生效到哪一层」**——对话框底部那个作用域下拉现在能选，
但它选了之后什么都不影响；而「视图临时过滤不写入会话」（FILT-005 第 4 条）、
预设库、以及各会话类型读设置时的取值顺序，全都要依赖这条覆盖链。`SESS-007` 的四条
完成标准里有三条（优先级、覆盖链、提示文本）都是纯逻辑，本机就能做完整闭环。

**为什么 SESS-007 之后仍不是「先接界面」**：`SESS-006` 的对话框已经能按声明生成界面，
`SESS-007` 的作用域链也已经在服务层跑通并测好了，但**没有任何一份声明可登记**——
「文本比对该有哪些设置」是 TEXT-* / FOLD-* 的产品决定，而那批条目本身还没实现。
现在接通 Rules 按钮，打开的会是一个空的设置对话框；SESS-007 第 2 条那个「本次修改
将保存到 X」的去向提示也一样——它要有一个「当前会话的设置目标」才谈得上显示。
先落一个具体会话类型（`TEXT-*` 或 `FOLD-*`），对话框、注册表的工厂、作用域链
与设置声明才**同时**有东西可用。

**SESS-007 留下的确切接口**：`ScopedSessionSettings::setLayer(scope, store)` 收三份
借用的 `SessionSettings*`，`setSchema(const SettingsSchema*)` 收一份声明（出厂默认的来源），
`setWriteScope()` / `writeDestinationText()` 对应对话框底部那个下拉，
`viewScopeKeys()` / `discardViewScope()` / `planViewScopeClose()` 对应关标签这一条路径。
接的时候注意两条：三层存储只能经本类读写（本类不转发三层的 `changed`，
外部直接改某一层它不会察觉），以及写入目标层缺失时 `setValue()` 返回 false ——
不要为了「让它能存」而在调用点换成别的层。

**`SESS-002` 留给下一轮的接口**：`SessionTypeRegistry::find(id)` 拿到条目之后再取
`factory`，就是「按类型 ID 造会话」的那一步（`Tests/Session` 的 G 组从注册表把会话
造出来并走完了整个生命周期，用的是合成类型）。要留意的是**内置的 14 种类型目前一个
工厂都没有**——`HomePage::sections()` 里那批硬编码 ID 现在与注册表逐字一致（有源码级
护栏钉住，见 §2），但点到它们还造不出会话。等第一个真正的会话类型落地时，那一行
`factory` 才有人填；在那之前「双击 Home 页卡片打开会话」在真实数据上仍走不通。

三个工作流的目录互不重叠，`services.pri` 的 include 已一次加齐（`exists()` 保护），
因此三方都不需要改共享文件。详见 [parallel-workstreams.md](parallel-workstreams.md) §1。

### 4.0.3 最新一轮（2026-09-21 11:41）：ENG-003 测试框架与测试运行器（issue #335）

**先读 §1.24**，那里有完整的交付记录（五条完成标准逐条对着源码核对的结果、
三个新增能力的设计理由、13 处变异、以及两处真漏检的成因）。这里只写
**下一轮该从哪儿开始**。

**为什么这一轮做 ENG-003 而不是 §4.0.1 里排在第一位的那件事**：那个清单（读
`Tests/Folder` 的 stderr、ASan 复现、Windows 两条系统性原因、action 版本）是
**上一轮**为它自己定的收尾，而本轮的任务是推进 issue 队列——`ENG-003` 是
**P0 且五条标准里有三条没做**（并行、超时、测试写仓库目录），本机可完整闭环、
可反向验证。**它没有解决也没有掩盖下面第 1 条那个仍然红着的 CI 问题。**

**本轮之后，按优先级排：**

1. **`Tests/Folder` 在 ubuntu 上的缓冲区越界**（`linksAreComparedWithoutFollowing()`，
   stderr 只有一行 `*** buffer overflow detected ***: terminated`）。
   按 §4.0.1 第 4 条给的三步走：**先在本机用 ASan 复现**（`-fsanitize=address -g`
   单独编一份 `Tests/Folder`，`_FORTIFY_SOURCE` 与 ASan 盯的是同一类错误，
   而 ASan 在 macOS 上可用）；静默的话给崩溃套件自动补跑 `-v2`；
   重点看 `Services/Files/filesystem_posix.cpp::linkTarget()` 的 `readlink` 增长循环。
   **不要先读代码猜。**
2. **Windows 腿的 26 个套件构建失败**（§1.22 的清单）：先只做两条系统性原因
   （`_WIN32_WINNT`、`winioctl.h`），推一次看剩多少，**再**动那两个真 bug。
3. **`actions/checkout@v4` / `upload-artifact@v4` → v5**（运行器已有 Node 20
   弃用告警）。本轮**刻意没碰**：它属于流水线维护，且改错 major 版本会让三条腿
   一起红，值得单独一轮配一次推送去验，不要和功能改动混在一个提交里。
4. **超时分支在 Windows 上未实测**（本轮新增的已知缺口）：CI 的 Windows 腿
   **显式跳过** `--self-test` 并打 `::warning::`。要补的话，先测「Git Bash 的
   `kill -TERM` 能不能终止一个原生 Windows 进程」。注意**并行那一半不需要补**——
   Windows 腿的测试步骤现在默认就在并行跑。
5. **回到「界面接通那一批」**（§4.0 第 1 条）与 `OPT-002` 的最小集
   （给第 3 条补一条能起真窗口的测试，落在 `Tests/AppIntegration` 那一类套件上）。
6. **三个测试工程没有 `isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)` 这一行**
   （`OptionsDialog` / `MediaView` / `SpecialPicture`，本轮核实时发现）：它们的可执行文件
   因此落在构建目录根而不是 `bin/`，于是 CI 的 `test-binaries-*` 产物
   （glob 是 `_test-build/*/bin/tst_*`）**收不到这三个套件**。不影响正确性——
   `run-tests.sh` 的 `find` 兜底照样能跑，`results.txt` 也照常上传——但
   「下载测试可执行文件手工重跑」这条路对这三个套件是断的。**一行的事，但那属于
   构建配置一致性，不属 ENG-003 的验收范围，所以本轮只记录不改。**

**本轮顺带给出的两条工具纪律**（后续条目的变异测试都该照着做）：

- **断言要盯「被测行为真正改变的那一个输出」**：M4 破坏的是**父进程的合计**，
  而当时的断言盯着**某个套件自己的 `Totals:` 行**——两者是不同的量，一条都不会红。
- **断言不要只比「名字集合」**：M13（截图写回工作目录）第一次没被检出，因为
  仓库根历史上就躺着同名的三个文件，写回只是**覆盖**它们，名字集合一模一样。
  改成比「名字 + 大小 + 修改时间」的指纹后立刻检出。
  两处都属于**验证本身有洞**，而不是实现有 bug——这正是「每条 issue 都要有反向验证」
  的全部理由（详见 §1.24 第四、七节）。
- **本机的删除守卫对「一个 turn 内的累积删除数」计数**，超限时会让**调用它的
  Python 进程 `SystemExit(1)`**（不是只打告警）。变异驱动脚本因此**不要删文件**，
  改用「把源文件 mtime 推到**当前时间之后**」来逼 `make` 重编——推到「当前时间」
  不够：`.o` 常与它在同一秒里，而 make 判的是「源比目标**新**」，相等不算新，
  于是变异编不进去、被报成假漏检。见 §6。

### 4.0.4 最新一轮（2026-09-21 11:53）：TXT-002 基础行对齐算法（issue #57）

**先读 §1.25**，那里有逐条对着完成标准核对的结果、三个新用例各自的设计理由、
9 处变异与 M5 那次真漏检的成因。这里只写**下一轮该从哪儿开始**。

**为什么这一轮做 TXT-002**：它是 P0，而且**我一开始把它判错了方向**——看到
`textdiff.{h,cpp}` 与 `Tests/Text` 都在，就归成「核对 + 关闭」。逐条对标准核完才发现
**是验证不对等**：第 3 条的一半（全相同 → 单个相同块）与整个第 4 条（逐字段相同 + 快照）
从来没有被断言过。**这说明「模块在不在」判不出「标准守没守住」**——§4.1 那张表
按「有没有宿主」分类，而这一条既有宿主、又有一半标准没人守，那张表看不出来。
**核对一条 P0 时，正确动作是逐条读完成标准、再去源码与测试里找对应的断言**，
不是先看目录在不在。

**本轮之后，按优先级排**（比 §4.0.3 只多了第 7 条，其余不变）：

1. **`Tests/Folder` 在 ubuntu 上的缓冲区越界**（`linksAreComparedWithoutFollowing()`，
   stderr 只有一行 `*** buffer overflow detected ***: terminated`）。
   按 §4.0.1 第 4 条给的三步走：**先在本机用 ASan 复现**（`-fsanitize=address -g`
   单独编一份 `Tests/Folder`，`_FORTIFY_SOURCE` 与 ASan 盯的是同一类错误，
   而 ASan 在 macOS 上可用）；静默的话给崩溃套件自动补跑 `-v2`；
   重点看 `Services/Files/filesystem_posix.cpp::linkTarget()` 的 `readlink` 增长循环。
   **不要先读代码猜。**
2. **Windows 腿的 26 个套件构建失败**（§1.22 的清单）：先只做两条系统性原因
   （`_WIN32_WINNT`、`winioctl.h`），推一次看剩多少，**再**动那两个真 bug。
3. **`actions/checkout@v4` / `upload-artifact@v4` → v5**（运行器已有 Node 20 弃用告警）。
   它属于流水线维护，改错 major 版本会让三条腿一起红，值得单独一轮配一次推送去验。
4. **超时分支在 Windows 上未实测**（§4.0.3 第 4 条）：CI 的 Windows 腿**显式跳过**
   `--self-test` 并打 `::warning::`。要补先测「Git Bash 的 `kill -TERM` 能不能终止一个
   原生 Windows 进程」。**并行那一半不需要补**。
5. **回到「界面接通那一批」**（§4.0 第 1 条）与 `OPT-002` 的最小集。
6. **三个测试工程缺 `isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)`**
   （`OptionsDialog` / `MediaView` / `SpecialPicture`，§4.0.3 第 6 条）。一行的事，
   但属于构建配置一致性，单独一轮做。
7. **还有多少 P0 是「一半标准没人守」这种状态？** 这是本轮真正带出来的问题：
   `TXT-002` 不是「没写」，而是「写了但验收点没被断言」。**同一批夜间工作流产出的
   条目只被核对过「有没有宿主」，没有被核对过「每一条完成标准有没有对应的断言」。**
   建议挑下一批 P0 时优先挑那些「实现明显在、但测试只覆盖了一部分标准」的
   （`TXT-003` / `TXT-005`~`TXT-008`、`DIR-002`~`DIR-012` 都属这一类），
   每一条的成本都很低（只加测试），收益是让**已经存在的**实现第一次真正被守住。
   做法照着 §1.25：先逐条列标准 → 在源码与测试里找对应断言 → 缺的补上 →
   变异测试证明补的断言真的会红。

### 4.0.5 最新一轮（2026-09-21 13:0x）：TXT-003 耐心对齐（issue #58）

**先读 §1.26**，那里有逐条对着完成标准核对的结果、六条设计取舍的成因、以及 9 处变异
的清单与一处**按设计漏检**（`<` 与 `<=` 在候选下标互不相同的前提下等价，改它不会红）。
这里只写**下一轮该从哪儿开始**。

**为什么这一轮做 TXT-003**：它是 P0，`textdiff.{h,cpp}` 里**一行 Patience 都没有**，
而且四条完成标准里**没有一条指向别的工作流**——第 3 条甚至明确把「算法选择控件与
可用算法清单」划给 TXT-004。这与 §4.1 里那些被界面阻塞的条目正好相反，
是同一条判据（**看完成标准里有没有动词指向一个还不存在的模块**）的两个方向。

**本轮之后，按优先级排**（比 §4.0.4 只多了第 8 条，其余不变）：

1. **`Tests/Folder` 在 ubuntu 上的缓冲区越界**（`linksAreComparedWithoutFollowing()`，
   stderr 只有一行 `*** buffer overflow detected ***: terminated`）。
   按 §4.0.1 第 4 条给的三步走：**先在本机用 ASan 复现**（`-fsanitize=address -g`
   单独编一份 `Tests/Folder`，`_FORTIFY_SOURCE` 与 ASan 盯的是同一类错误，
   而 ASan 在 macOS 上可用）；静默的话给崩溃套件自动补跑 `-v2`；
   重点看 `Services/Files/filesystem_posix.cpp::linkTarget()` 的 `readlink` 增长循环。
   **不要先读代码猜。** 本轮**仍未做**（它不闭环任何 issue，而本轮的目标是闭环条目）。
2. **Windows 腿的 26 个套件构建失败**（§1.22 的清单）：先只做两条系统性原因
   （`_WIN32_WINNT`、`winioctl.h`），推一次看剩多少，**再**动那两个真 bug。
3. **`actions/checkout@v4` / `upload-artifact@v4` → v5**（运行器已有 Node 20 弃用告警）。
   它属于流水线维护，改错 major 版本会让三条腿一起红，值得单独一轮配一次推送去验。
4. **超时分支在 Windows 上未实测**（§4.0.3 第 4 条）：CI 的 Windows 腿**显式跳过**
   `--self-test` 并打 `::warning::`。要补先测「Git Bash 的 `kill -TERM` 能不能终止一个
   原生 Windows 进程」。**并行那一半不需要补**。
5. **回到「界面接通那一批」**（§4.0 第 1 条）与 `OPT-002` 的最小集。
6. **三个测试工程缺 `isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)`**
   （`OptionsDialog` / `MediaView` / `SpecialPicture`，§4.0.3 第 6 条）。一行的事，
   但属于构建配置一致性，单独一轮做。**本轮新加的 `Tests/Alignment` 已经按新写法写了**
   ——这条清单因此不再增长。
7. **还有多少 P0 是「一半标准没人守」这种状态？** 这是 §1.25 带出来的问题：
   `TXT-002` 不是「没写」，而是「写了但验收点没被断言」。**本轮把它又推进了一步**：
   `TXT-003` 是「真的没写」，而且**只有真去 `grep` 才发现**——`textdiff.h` 里
   连 `Alignment` 这个枚举都不存在。两条教训合起来是：核对一条 P0 的正确动作是
   **先逐条读完成标准，再去源码与测试里找对应的断言或实现**，目录在不在、issue 什么标签，
   两个都判不出来。
8. **同一族里还剩两类可做条目**（都属「本机可完整闭环」）：
   - **「实现明显在、测试只覆盖一部分标准」**：`TXT-005` ~ `TXT-008`、`DIR-002` ~ `DIR-012`。
     其中 `TXT-008`（忽略大小写）**值得优先**：`Change::Ignored` 与
     `CompareOptions::ignoreCase` 都已经落地并被 `Tests/Text` 摸到，
     但 issue 第 3 条（统一规范化链）与第 4 条（土耳其语 `i`/`İ` 的明确取舍）
     大概率没有对应的断言——**去做之前先逐条核对，不要按本行直接开工**。
   - **`TXT-004`（对齐样式选择与持久化）**：它是 TXT-003 的下一环，
     但它的入口在「Compare 页的算法下拉」上，**属界面**；本轮的 `AlignmentDescriptor`
     与 `defaultAlignment()` 已经把它需要的数据备好（标识符、可选清单、默认值），
     缺的是控件与设置键。要动它之前先确认界面那条路现在能不能走通。

### 4.0.6 最新一轮（2026-09-21 14:2x）：TXT-008 忽略大小写差异（issue #63）

**先读 §1.27**，那里有逐条对着完成标准核对的结果、规范化链的四条契约、
以及 8 处变异的清单与一处**按设计漏检**（链的两步可交换，调换顺序不会红）。

**为什么这一轮做 TXT-008**：§4.0.5 第 8 条点名过它，而 §1.25 / §1.26 两条教训合起来的
判据是「**先逐条读完成标准，再去源码与测试里找对应的断言或实现**」——目录在不在、
issue 什么标签，两个都判不出来。核对的结果是：**四条标准没有一条是「真的没写」**，
缺的全是断言；另外「规范化链」这件事当时**连名字都没有**（匿名命名空间里的一个
`static normalized()`），于是第 3、4 条从外面根本无从验证。这一条因此是本项目
第一次做「实现明显在、测试只覆盖一部分标准」那类条目，做法可以照抄：
**补断言 + 把被要求「记录」的东西变成有名字的、能被指着说的接口**。

**本轮之后，按优先级排**（前 3 条与 §4.0.5 相同，第 4 条起为新顺序）：

1. **`Tests/Folder` 在 ubuntu 上的缓冲区越界**（`linksAreComparedWithoutFollowing()`，
   stderr 只有一行 `*** buffer overflow detected ***: terminated`）。
   按 §4.0.1 第 4 条给的三步走：**先在本机用 ASan 复现**（`-fsanitize=address -g`
   单独编一份 `Tests/Folder`，`_FORTIFY_SOURCE` 与 ASan 盯的是同一类错误，
   而 ASan 在 macOS 上可用）；静默的话给崩溃套件自动补跑 `-v2`；
   重点看 `Services/Files/filesystem_posix.cpp::linkTarget()` 的 `readlink` 增长循环。
   **不要先读代码猜。** 已经连续两轮没做（它不闭环任何 issue，而每轮的目标是闭环条目）。
2. **Windows 腿的 26 个套件构建失败**（§1.22 的清单）：先只做两条系统性原因
   （`_WIN32_WINNT`、`winioctl.h`），推一次看剩多少，**再**动那两个真 bug。
3. **`actions/checkout@v4` / `upload-artifact@v4` → v5**（运行器已有 Node 20 弃用告警）。
   它属于流水线维护，改错 major 版本会让三条腿一起红，值得单独一轮配一次推送去验。
4. **同一族里还剩的可做条目**（都属「本机可完整闭环」，都按「先逐条核对再动手」办）：
   - **`TXT-009` ~ `TXT-012`、`TXT-015` ~ `TXT-017`**（忽略空白 / 行尾 / 注释 /
     行号数字日期 / BOM / 行尾规范化 / 制表符宽度）：与 TXT-008 **同一部引擎、同一类判据**，
     而 `CompareOptions` 里那几个开关（`whitespace` / `ignoreEol` / `ignoreFinalNewline`）
     **都已经在链里生效**。按 TXT-008 的做法先核对：哪些标准已有断言、
     哪些一次都没被问过。**`TXT-009`（忽略空白）优先**——它的 `Whitespace` 三值
     就在同一条链上，且 TXT-008 的链用例已经顺带碰过 `IgnoreChanges`。
   - **`DIR-002` ~ `DIR-012`**：`Services/Folder/foldercompare.{h,cpp}` 已经存在
     （见 §3 目录树），先逐条核对再决定。
5. **超时分支在 Windows 上未实测**（§4.0.3 第 4 条）：CI 的 Windows 腿**显式跳过**
   `--self-test` 并打 `::warning::`。要补先测「Git Bash 的 `kill -TERM` 能不能终止一个
   原生 Windows 进程」。**并行那一半不需要补**。
6. **回到「界面接通那一批」**（§4.0 第 1 条）与 `OPT-002` 的最小集。
7. **三个测试工程缺 `isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)`**
   （`OptionsDialog` / `MediaView` / `SpecialPicture`，§4.0.3 第 6 条）。一行的事，
   但属于构建配置一致性，单独一轮做。**TXT-008 这一轮没有新建套件，这条清单因此没有增长**。
8. **锁文件在收工前删除**（本轮已确认删除）。

### 4.0.7 最新一轮（2026-09-21 14:4x）：TXT-009 忽略空白变化（issue #64，**部分完成**）

**先读 §1.28**，那里有逐条对着完成标准核对的结果、五条标准的落地表、
10 处变异的清单，以及两条新踩出来的坑。

**为什么这一轮做 TXT-009**：§4.0.6 第 4 条点名过它（同族里优先）。
结论与 TXT-008 同类但多一层：**除了「实现早就在、只是没断言」，
还有两条属于「完成标准指的地方根本不存在」**——第 4 条后半句要的 View 页
（`OPT-007` / `OPT-003`）没落地，第 3 条的隐患在**界面与枚举按序号对应**上
（不是枚举不互斥）。这两类在核对阶段就能分辨：**动词指向的模块在不在仓库里**。
因此本轮按规矩只做能独立验证的部分、把第 4 条记成**部分完成**、标签留「部分完成」，
**不替它硬凑一个界面**。

**这一轮多出来的方法论收获**（比条目本身更值钱）：
「把界面文案提到公开静态接口」这件事是为了让**第二条链**可验——
本轮第一版变异测试漏检了一处，原因是断言只钉了「序号 → 值」，
而真正会烂掉的方式是「文案与序号脱钩」。**漏检的成因是断言选错了量，
不是实现有错**（与 §1.24 里 M4「盯错了量」同一类）。
判据可以带走：**界面上「用户看到的」与「实际生效的」是两条链，一条都不能少。**

**本轮之后，按优先级排**（第 1~3 条与 §4.0.6 相同，第 4 条起为新顺序）：

1. **`Tests/Folder` 在 ubuntu 上的缓冲区越界**（`linksAreComparedWithoutFollowing()`，
   stderr 只有一行 `*** buffer overflow detected ***: terminated`）。
   按 §4.0.1 第 4 条给的三步走：**先在本机用 ASan 复现**（`-fsanitize=address -g`
   单独编一份 `Tests/Folder`，`_FORTIFY_SOURCE` 与 ASan 盯的是同一类错误，
   而 ASan 在 macOS 上可用）；静默的话给崩溃套件自动补跑 `-v2`；
   重点看 `Services/Files/filesystem_posix.cpp::linkTarget()` 的 `readlink` 增长循环。
   **不要先读代码猜。** 已经连续三轮没做（它不闭环任何 issue，而每轮的目标是闭环条目）。
2. **Windows 腿的 26 个套件构建失败**（§1.22 的清单）：先只做两条系统性原因
   （`_WIN32_WINNT`、`winioctl.h`），推一次看剩多少，**再**动那两个真 bug。
3. **`actions/checkout@v4` / `upload-artifact@v4` → v5**（运行器已有 Node 20 弃用告警）。
   它属于流水线维护，改错 major 版本会让三条腿一起红，值得单独一轮配一次推送去验。
4. **同族里还剩的可做条目**（都属「本机可完整闭环」，都按「先逐条核对再动手」办）：
   - **`TXT-010` ~ `TXT-012`、`TXT-015` ~ `TXT-017`**（行尾 / 注释 /
     行号数字日期 / BOM / 行尾规范化 / 制表符宽度）：与 TXT-008 / TXT-009
     **同一部引擎、同一类判据**，`CompareOptions` 里 `ignoreEol` /
     `ignoreFinalNewline` 两个开关**都已经在链里生效**。按这两轮的做法先核对。
     **`TXT-010`（忽略行尾差异）优先**——它的两个开关就在同一条链上，
     且 `Tests/Text` 现有的行尾用例可能已经顺带碰过，先去看而不是先写。
   - **`TXT-004`（对齐/模式选择与持久化）**：本轮与上一轮已经把它的数据备齐
     （`AlignmentDescriptor` / `WhitespaceDescriptor` 两套标识符 + 可选清单 + 默认值），
     而 §1.28 末尾记着的那处 `qBound(0, value, 2)` 序号耦合正落在它的射程内。
     注意它**属于界面**（Compare 页的下拉），先看 §4.1。
   - **`DIR-002` ~ `DIR-012`**：`Services/Folder/foldercompare.{h,cpp}` 已经存在
     （见 §3 目录树），先逐条核对再决定。
5. **超时分支在 Windows 上未实测**（§4.0.3 第 4 条）：CI 的 Windows 腿**显式跳过**
   `--self-test` 并打 `::warning::`。要补先测「Git Bash 的 `kill -TERM` 能不能终止一个
   原生 Windows 进程」。**并行那一半不需要补**。
6. **回到「界面接通那一批」**（§4.0 第 1 条）与 `OPT-002` 的最小集。
   **`TXT-009` 第 4 条的后半句在等这儿的 `OPT-007`**（见 §4.1 新增那行）。
7. **三个测试工程缺 `isEmpty(DESTDIR): DESTDIR = $$clean_path($$OUT_PWD/bin)`**
   （`OptionsDialog` / `MediaView` / `SpecialPicture`，§4.0.3 第 6 条）。一行的事，
   但属于构建配置一致性，单独一轮做。**TXT-009 这一轮同样没有新建套件，清单没有增长**。
8. **锁文件在收工前删除**（本轮已确认删除）。

### 4.0.8 最新一轮（2026-09-21 15:0x）：TXT-005 相似度阈值与相似行对齐（issue #60，**已完成**）

**先读 §1.29**，那里有逐条对着完成标准核对的结果、四条标准的落地表、
「一处改动」这个新口径的由来、顺带拔出的那个**合并丢数据**缺陷，以及 12 处变异的清单。

**为什么这一轮做 TXT-005**：上一轮（§4.0.7 第 4 条）把它排在同族的可做条目里，
而它是其中**唯一「四条标准全都指向一个还不存在的模块」**的那一条
（`TXT-010` 的 `ignoreEol` / `ignoreFinalNewline` 已经在同一条链里生效，属「先核对」；
TXT-005 属「从零写」）。判据仍是那句：**先逐条读完成标准，再 `ls` 源码与测试目录**。
**核对结果**：`Services/Text/` 下只有 `textdocument.*` / `textdiff.*`，
没有任何相似度相关的源码，`Tests/` 下也没有对应套件——四条标准全部要新写。
（若只 grep `similarity`，会看到 `CompareOptions::similarityThreshold` 这个字段与
它注释里的「本字段尚未被引擎读取」——**那是声明，不是实现**，别当成「已经有了」。）

**这一轮多出来的方法论收获**（比条目本身更值钱）：
**一个「夹具与手推分值都错」的假红，会被误读成「实现错了」。**
TXT-005 落地后四个既有套件一起变红，第一反应是「引擎把块拆错了」；
真实原因是那批夹具里某个文件恰好有两处差异、以及某对行在出厂阈值 50 下**根本配不上对**
（实测 `bravo Two` / `BRAVO two` 只有 33 分、`a` / `left` / `right` 只有 22 分）。
→ 判据可以带走：**凡断言里出现具体数字，那个数字要么有独立来源、要么是实测的**，
不能是「看起来差不多」；改行为之前**先用一个独立小程序把真实数值 dump 出来**
（本轮用的是 `/tmp/lqdbg/`，不进仓库）。
这与 §1.28 那条「两条链都要有断言」互补：那条讲**盯错了量**，这条讲**量本身算错了**。

**本轮之后，按优先级排**（第 1~3 条与前两轮相同，第 4 条起为新顺序）：

1. **`Tests/Folder` 在 ubuntu 上的缓冲区越界**（`linksAreComparedWithoutFollowing()`，
   stderr 只有一行 `*** buffer overflow detected ***: terminated`）。
   按 §4.0.1 第 4 条给的三步走：**先在本机用 ASan 复现**（`-fsanitize=address -g`
   单独编一份 `Tests/Folder`，`_FORTIFY_SOURCE` 与 ASan 盯的是同一类错误，
   而 ASan 在 macOS 上可用）；静默的话给崩溃套件自动补跑 `-v2`；
   重点看 `Services/Files/filesystem_posix.cpp::linkTarget()` 的 `readlink` 增长循环。
   **不要先读代码猜。** 已经连续四轮没做（它不闭环任何 issue，而每轮的目标是闭环条目）。
2. **Windows 腿的 26 个套件构建失败**（§1.22 的清单）：先只做两条系统性原因
   （`_WIN32_WINNT`、`winioctl.h`），推一次看剩多少，**再**动那两个真 bug。
3. **`actions/checkout@v4` / `upload-artifact@v4` → v5**（运行器已有 Node 20 弃用告警）。
   它属于流水线维护，改错 major 版本会让三条腿一起红，值得单独一轮配一次推送去验。
4. **同族里还剩的可做条目**（都属「本机可完整闭环」，都按「先逐条核对再动手」办）：
   - **`TXT-010`（忽略行尾差异）优先** —— `ignoreEol` / `ignoreFinalNewline` 两个开关
     就在同一条链上，且 `Tests/Text` 现有的行尾用例可能已经顺带碰过，先去看而不是先写。
   - 其次 **`TXT-011` / `TXT-012` / `TXT-015` ~ `TXT-017`**（注释 / 行号数字日期 /
     BOM / 行尾规范化 / 制表符宽度）：与 TXT-008 / TXT-009 同一部引擎、同一类判据。
     **注意 `TXT-005` 已从这张单子上划掉**（本轮闭环）。
   - **`TXT-004`（对齐/模式选择与持久化）**：三套 `*Descriptor`（算法 / 空白，
     加本轮的值域与默认值）已把它的数据备齐，而 §1.28 末尾记着的那处
     `qBound(0, value, 2)` 序号耦合正落在它的射程内。它**属于界面**（Compare 页的下拉），
     先看 §4.1。
   - **`DIR-002` ~ `DIR-012`**：`Services/Folder/foldercompare.{h,cpp}` 已经存在
     （见 §3 目录树），先逐条核对再决定。
5. **超时分支在 Windows 上未实测**（§4.0.3 第 4 条）：CI 的 Windows 腿**显式跳过**
   `--self-test` 并打 `::warning::`。要补先测「Git Bash 的 `kill -TERM` 能不能终止一个
   原生 Windows 进程」。**并行那一半不需要补**。
6. **回到「界面接通那一批」**（§4.0 第 1 条）与 `OPT-002` 的最小集。
   **`TXT-009` 第 4 条的后半句在等这儿的 `OPT-007`**（见 §4.1 那行）。
7. **测试工程的 `DESTDIR`**：`OptionsDialog` / `MediaView` 两处还差一行
   （§4.0.3 第 6 条）。**TXT-005 本轮新建的 `Tests/Similarity/SimilarityTests.pro`
   一开始就带了这一行，清单没有增长。**
8. **锁文件在收工前删除**（本轮已确认删除）。

### 4.0.9 最新一轮（2026-09-21 17:3x）：TXT-001 文本比对会话与双窗格视图骨架（issue #56，**已完成**）

**为什么选它**：按要求「优先 P0」，在 59 条 P0 里挑。TXT-001 的决定性事实是
**这份文档里从头到尾没有出现过 `TXT-001` 一次**（`grep -c` 为 0），而
`Views/Text/` 下的会话与双窗格早就在、一直被 `Tests/TextView` 的 20 条用例跑着。
→ §4.1 那张表只登记「被讨论过」的条目，**一条从未被讨论过的条目在文档里查不到**，
它的「待实现」标签因此从没被质疑过。详细记录见 §1.30。

**做了什么**：**不新增模块**（骨架已经在），四条完成标准各补一条用例函数
（`Tests/TextView` 20 → 24），生产改动只有 `TextPane::lineNumbers()` 一个访问器——
理由见 §1.30（正文与号码是两条链，只钉正文会让号码链零覆盖）。

**验证数据**：单套件 **24 / 0 / 0**；全量 **3653 / 0 / 2**（此前 3649）；
五道护栏全过；主程序 **0 条本仓 warning** + 离屏启动正常；
**8 处变异 8 处检出、0 处漏检**（`/tmp/txt001_mutate.py`）。

**下一轮起点**（优先级不变，TXT-001 已从单子上划掉）：

1. `Tests/Folder` 在 ubuntu 上的 `*** buffer overflow detected ***` →
   **先在本机用 ASan 复现**（`-fsanitize=address -g` 单独编一份 `Tests/Folder`；
   glibc 的 `_FORTIFY_SOURCE` 与 ASan 盯同一类错误，而 macOS 可用）。
   **不要先读代码猜。** 已连续五轮没做（它不闭环任何 issue）。
2. Windows 腿 26 个套件构建失败：先只做两条系统性原因（`_WIN32_WINNT`、`winioctl.h`）。
3. `actions/checkout@v4` / `upload-artifact@v4` → v5（改错 major 会让三条腿一起红，
   值得单独一轮配一次推送去验）。
4. **同族里还剩的可做条目**（都按「先逐条核对再动手」办）：
   - **`TXT-010`（忽略行尾差异）优先** —— `ignoreEol` / `ignoreFinalNewline`
     两个开关就在同一条链上（`textdiff.cpp` 的规范化段），会话侧还会在状态栏加一句
     「Line endings ignored」，而 `Document::eolDescription()` 已经会给出
     `LF` / `CRLF` / `CR` / `Mixed (LF x / CRLF y / CR z)` / `No line ending`。
     **但它的第 3、4 条都在状态栏上**，先按 §4.1 判断「状态栏」算不算已有住所
     （`CompareSession::setStatusText()` → `MainWindow::refreshStatusBar()` 这条链是在的，
     与 TXT-009 第 4 条后半句缺「View 页设置界面」**不是同一回事**，别混为一谈）。
   - 其次 **`TXT-011` / `TXT-012` / `TXT-015` ~ `TXT-017`**（注释 / 行号数字日期 /
     BOM / 行尾规范化 / 制表符宽度）。
   - **`TXT-004`（对齐/模式选择与持久化）**：数据（三套 `*Descriptor` 的标识符 /
     可选清单 / 默认值）已备齐，§1.28 末尾那处 `qBound(0, value, 2)` 序号耦合
     正落在它的射程内。它**属于界面**（Compare 页 Alignment 组的下拉）+ 「大文件进度与取消」，
     先看 §4.1。
   - **`DIR-002` ~ `DIR-012`**：`Services/Folder/foldercompare.{h,cpp}` 已经存在，
     先逐条核对再决定。
5. 超时分支在 Windows 上未实测（CI 显式跳过并打告警）。
6. 回到「界面接通那一批」+ `OPT-002` 最小集。
   **`TXT-009` 第 4 条的后半句在等这儿的 `OPT-007`**（见 §4.1 那行）。
7. 三个测试 `.pro` 补 `DESTDIR`（`OptionsDialog` / `MediaView` / `SpecialPicture`）。
   **TXT-001 本轮没有新建套件，清单没有增长。**
8. **锁文件在收工前删除**（本轮已确认删除）。

### 4.0.10 最新一轮（2026-09-21 18:0x）：TXT-010 忽略行尾差异（issue #65，**已完成**）

**做了什么**：补上第 4 条后半句真正缺的那个东西——**状态栏警告图标**。做法是给会话加一条
**独立的严重程度通道**（`CompareSession::StatusSeverity` + `statusSeverityChanged`），
容器转发并在切标签时重播，窗口在状态栏放一个永久控件 `statusWarningIcon`
（装配时设一次 pixmap、之后只切可见性）。配套把 `Document::hasMixedEndings()` 做成谓词
（**末尾无换行不算一种风格**）并与 `eolDescription()` 共用同一个 `countEndings()`。
另补 9 个用例函数（`Tests/Text` 34 → 38、`Tests/TextView` 24 → 26、
`Tests/Session` 50 → 52、`Tests/AppIntegration` 12 → 13）。详见 §1.31。

**顺带删掉一处冗余路径**：图标原本有两条刷新路（会话的严重度信号 + `refreshStatusBar()`），
切标签时两条必然同进同出 → 两处变异**互相遮蔽、双双漏检**。删掉 `refreshStatusBar()` 里那次调用
（保留容器重播，与 `statusTextChanged` 的既有约定对称）之后两处各自都被检出。

**下一步优先级（TXT-010 已划掉）**：

1. **`Tests/Folder` 在 ubuntu 上的 `*** buffer overflow detected ***`** → **先在本机用 ASan 复现**
   （`-fsanitize=address -g` 单独编一份；**不要先读代码猜**）。**已连续五轮排在第一位而没做**
   ——它不闭环任何 issue，但它是「本机全绿 ≠ 代码没问题」的那一类，必须单独安排一轮。
2. Windows 腿 26 个套件构建失败：先只做两条系统性原因（`_WIN32_WINNT`、`winioctl.h`）。
3. `actions/checkout@v4` / `upload-artifact@v4` → v5（改错 major 会让三条腿一起红，
   值得单独一轮配一次推送去验）。
4. **同族里还剩的可做条目**（都按「先逐条核对再动手」办）：
   - 其次 **`TXT-011` / `TXT-012` / `TXT-015` ~ `TXT-017`**（注释 / 行号数字日期 /
     BOM / 行尾规范化 / 制表符宽度）。**注意 TXT-015 与本轮的 TXT-010 是邻居**
     （一个管「怎么比较行尾」，一个管「怎么写回行尾」），先看有没有重叠。
   - **`TXT-004`（对齐/模式选择与持久化）**：数据（三套 `*Descriptor` 的标识符 /
     可选清单 / 默认值）已备齐，§1.28 末尾那处 `qBound(0, value, 2)` 序号耦合
     正落在它的射程内。它**属于界面**（Compare 页 Alignment 组的下拉）+ 「大文件进度与取消」，
     先看 §4.1。
   - **`DIR-002` ~ `DIR-012`**：`Services/Folder/foldercompare.{h,cpp}` 已经存在，
     先逐条核对再决定。
5. 超时分支在 Windows 上未实测（CI 显式跳过并打告警）。
6. 回到「界面接通那一批」+ `OPT-002` 最小集。
   **`TXT-009` 第 4 条的后半句在等这儿的 `OPT-007`**（见 §4.1 那行）。
7. 三个测试 `.pro` 补 `DESTDIR`（`OptionsDialog` / `MediaView` / `SpecialPicture`）。
   **TXT-010 本轮没有新建套件，清单没有增长。**
8. **锁文件在收工前删除**（本轮已确认删除）。

### 4.0.11 本轮（2026-09-21 19:2x）：TXT-012 内置替换规则（issue #69，**部分完成**）

**做了什么**：新模块 `Services/Text/linereplacements.{h,cpp}` + `CompareOptions::replacements`
（加在末尾）+ 新套件 `Tests/TextRules`（19 个用例函数）。四条内置规则、一张唯一事实来源的表、
一个按表顺序应用的开关集合。细节、四个设计选择与「反误伤」的成对断言见 §1.32。

**两条留给下一轮的经验**（都是本轮真踩到的）：

1. **「顺序正确」这件事需要有语料才可观察**。两条规则的区间不相交时，顺序错了也不会红
   ——本轮为此造了一条重叠语料（见 §1.32）。同族里凡是「多个规则/多个层按某顺序应用」的
   条目（`TXT-011` 的注释剥离链、`TXT-016` 的行尾规范化）都该照这个手法检查一遍。
2. **变异脚本把「构建失败」读成了「漏检」**：一处变异给 `const int` 赋值导致编译不过，
   套件没有产出统计行，而判据只看 `合计：N passed, M failed` 里的失败数，于是报成漏检。
   脚本的判据必须区分「跑了但全绿」与「根本没跑起来」——已记进 §6。

**下一步优先级**（`TXT-012` 已划掉，其余沿用 §4.0.10 的排法）：

1. **`Tests/Folder` 在 ubuntu 上的 `*** buffer overflow detected ***`** → 先在本机用 ASan 复现
   （`-fsanitize=address -g` 单独编一份；**不要先读代码猜**）。**已被连续六轮排在第一位而没做**
   ——它不闭环任何 issue，但它是「本机全绿 ≠ 代码没问题」的那一类，必须单独安排一轮。
2. Windows 腿 26 个套件构建失败：先只做两条系统性原因（`_WIN32_WINNT`、`winioctl.h`），
   清单在 §1.22。
3. `actions/checkout@v4` / `upload-artifact@v4` → v5（单独一轮配一次推送去验）。
4. **同族里还剩的可做条目**：`TXT-011`（忽略注释差异——注意它的边界条款要求注释语法
   **来自文件格式定义**，而内建 `FormatDefinition.settings` 里现在**一条 syntax 数据都没有**，
   先决定这份数据放哪，别在引擎里硬编码）、`TXT-015` ~ `TXT-017`、`TXT-004`（属界面）。
5. 回到「界面接通那一批」+ `OPT-002` 最小集——`TXT-012` 第 2 条与 `TXT-009` 第 4 条后半句
   都在等这里。**本轮的 `Tests/TextRules` 是新建套件，所以 §2 末尾那份「三个测试 `.pro`
   缺 `DESTDIR`」的清单没有增长**（新套件的 `.pro` 已经按惯例写好 `DESTDIR`）。

### 4.0.12 本轮（2026-09-21 19:4x）：DIR-008 二进制逐字节比对（issue #116，**部分完成**）

**做了什么**：接手第 22 轮留下的 `.workbuddy/wip/DIR-008-partial-bytes.patch`（它**从未被编译过**），
`git apply` → 逐处复核 → 补 6 + 1 个用例函数 → 走完整闭环。落地的是
「只比较前 N 字节」这条快速模式的引擎、设置键（`folder.compareFirstBytes`）、视图往返
与它三条边界（限 == 文件长度 / 差异在限内 / 差异刚好在限外）的断言。三条设计理由
（复用 `Unknown` 而不新增主状态、预算卡在大小比较之后、上界提成公开常量）已进
`architecture.md` §4 与 §3.3。细节见 §1.33。

**顺带修掉一处**在 `HEAD` 上就红着的**护栏**：`tools/check_spec.py` 的「文档条目数」网被
上一轮 handoff 里那句「新套件 `Tests/TextRules`」后面的用例数（写成全角括号 + `19` + 条）撞上。**上一轮「五道护栏全绿」的结论因此不成立**，
成因是那个循环用 `| tail` 收尾吃掉了退出码（本轮的护栏循环已经改成把 `$?` 单独打出来）。
处置是改写法而不是放宽护栏，理由见 §1.33 与 §6。

**两条留给下一轮的经验**：

1. **「没被断言过的实现细节」要当缺功能对待**。DIR-008 的第 3、4 条实现早就在，
   但一个常量藏在 `.cpp` 里、一个偏移量进报表的路从没被测过——这两条标准当时**实际上是裸的**。
   凡是「完成标准里的那个动词已经写出来了」的条目，接手时都要再问一句「它是**怎么**被验的，
   验的是那个量还是它旁边的量」。
2. **写测试用例数的措辞会撞护栏**：`（N 条）` 是 `check_spec.py` 的保留写法（专指规格条目数）。
   测试用例一律写「（N 个用例函数）」。
3. **完成标准里带副词的那个词，要单独找一遍「谁在守它」**。DIR-008 第 1 条是
   「遇首个不同字节即可**提前**判定不同」——「逐字节」与「判定不同」都有断言，
   唯独「**提前**」没有：把 `compareFile()` 里的 `break` 删掉，3687 条用例一条都不红
   （既有夹具都是「大小相同、只有一处差异」，命中之后剩下的块两侧全同）。
   这一族的词还有「**只**比较前 N 字节」「**不**读完整文件」「**默认**关闭」「**不**一次性读入内存」，
   每一个都要问一句**「把它反过来写，谁变红」**；答不出来就说明这个词还没被验过。
   **注意还有一种更隐蔽的情形：那个词是验过的，但验的是「它旁边的量」**——
   `Tests/Folder` 原本断言了首个差异的**偏移量**，看起来把第 1 条钉住了，
   实际钉的是「哪个字节不同」而不是「在哪停下」。

**下一步优先级**（`DIR-008` 已划掉，其余沿用 §4.0.11 的排法，只调前两条的措辞）：

1. **`Tests/Folder` 在 ubuntu 上的 `*** buffer overflow detected ***`**。**已被连续七轮排在第一位
   而没做**，但本轮起它**不再是未知项**：第 22 轮已经用 ASan 在本机试过，ASan **静默**
   （`libclang_rt.asan_osx_dynamic.dylib` 确认已链上），CI 产物显示 `results.txt` 逐条打印到
   `recursionLimitIsExplicitUnknown()` 就断、而下一条正是 `linksAreComparedWithoutFollowing()`，
   且 `results.xml` 是 0 字节 → **崩溃点在那个用例的执行期内，且是硬 abort**。
   **要做的只有一件事**：把「崩溃套件自动补跑一遍 `-v2`」落进 `run-tests.sh`，
   让「跑到哪一条才崩」变成日志里直接看得见的东西。**改 `run-tests.sh` 必须跑
   `run-tests.sh --self-test`（24 断言 / 13 变异）。** **不要**再去猜具体哪一行：
   Docker 守护进程没起、本机没有 Linux，`_FORTIFY_SOURCE=3`（Ubuntu 24.04 默认）与
   `__builtin_dynamic_object_size` 的差异在本机结构性不可复现。
2. Windows 腿 26 个套件构建失败：先只做两条系统性原因（`_WIN32_WINNT`、`winioctl.h`），
   清单在 §1.22。
3. `actions/checkout@v4` / `upload-artifact@v4` → v5（单独一轮配一次推送去验）。
4. **同族里还剩的可做条目**：`TXT-011`（忽略注释差异——它的边界条款要求注释语法
   **来自文件格式定义**，而内建 `FormatDefinition.settings` 里现在**一条 syntax 数据都没有**，
   先决定这份数据放哪，别在引擎里硬编码）、`TXT-015` ~ `TXT-017`、`TXT-004`（属界面）。
5. **`DIR-002` ~ `DIR-012` 剩下的十条**：`Services/Folder/foldercompare.{h,cpp}` 已经存在
   且本轮刚被认真过了一遍，**先逐条核对再动手**（本轮证明了「实现早就在、标准却是裸的」
   是这一族的常态）。`DIR-011`（主状态分类法）与 §1.33 里那句「复用 `Unknown`」直接相关，
   开工前先读那两条设计决策。
6. 回到「界面接通那一批」+ `OPT-002` 最小集——`DIR-008` 第 2 条的输入框、`TXT-012` 第 2 条
   与 `TXT-009` 第 4 条后半句都在等这里。**本轮没有新建套件，所以 §2 末尾那份
   「三个测试 `.pro` 缺 `DESTDIR`」的清单没有增长。**

### 4.0.13 本轮（2026-09-21 21:0x）：崩溃套件自动补跑 `-v2`（ENG-003 / ENG-004 的基础设施，**新功能**）

**做了什么**：把 §4.0.12 第 1 条里那件「要做的只有一件事」真做掉了——`run-tests.sh` 现在对
**崩溃**（没产出 `Totals:` 行、且不是超时）的套件自动补跑一遍 `-v2`，日志里直接打出
「崩在用例：`Class::func()`」与 `-v2` 结尾 20 行，产物里留 `verbose.txt` / `verbose.stderr.log`
并加进 CI 上传清单。顺手把「启动二进制 + 看门狗」抽成 `run_binary_with_timeout()`（补跑要走
逐字相同的路径）。三条边界（只对崩溃补跑 / 不覆盖 `results.txt` / 补跑跑完就是「偶发」证据）
都有断言。细节见 §1.34。

**它没有勾掉任何一条完成标准**：`Tests/Folder` 在 ubuntu 上到底撞了哪一行仍然不知道，
要等下一次 CI 的 `verbose.txt`。**ubuntu 那个崩溃不能因为「诊断工具就位了」而被当作已解决**。

**两条留给下一轮的经验**：

1. **反向验证会暴露「断言盯错了量」**，而且这次是同族错误的第三次：`M4`（补跑覆盖
   `results.txt`）第一次**整条漏检**，因为判据是 `grep 'Loc:'`，而那个探针里**一条断言都没有**、
   `-v2` 输出里根本没有 `Loc:`。判据要选「这份夹具真正会变的那个量」，不是「理论上
   -v2 才有、但这份夹具产不出来的那个量」。
2. **自测本身出现了一条偶发红点，机制未查明**（约 2/13 次，HEAD 版 0/11）。它会把「漏检」
   伪装出来（多余的红点混进红点集合），所以驱动里显式识别它的签名做一次重跑，
   但**只要出现签名之外的红点就一律按真红点处理、绝不重试**。这条债记进 §4.1。

**下一步优先级**（第 1 条已划掉，其余顺延）：

1. **Windows 腿 26 个套件构建失败**：先只做两条系统性原因（`_WIN32_WINNT`、`winioctl.h`），
   清单在 §1.22。**本机无法编译 Windows 分支，只能靠 CI 回答**，所以一次只动一处、配上一次推送。
2. `actions/checkout@v4` / `upload-artifact@v4` → v5（单独一轮配一次推送去验）。
3. **同族里还剩的可做条目**：`TXT-011`（忽略注释差异——它的边界条款要求注释语法
   **来自文件格式定义**，而内建 `FormatDefinition.settings` 里现在**一条 syntax 数据都没有**，
   先决定这份数据放哪，别在引擎里硬编码）、`TXT-015` ~ `TXT-017`、`TXT-004`（属界面）。
4. **`DIR-002` ~ `DIR-012` 剩下的十条**：`Services/Folder/foldercompare.{h,cpp}` 已被认真过了一遍，
   **先逐条核对再动手**（「实现早就在、标准却是裸的」是这一族的常态）。`DIR-011`（主状态分类法）
   与 §1.33 里那句「复用 `Unknown`」直接相关，开工前先读那两条设计决策。
5. 回到「界面接通那一批」+ `OPT-002` 最小集——`DIR-008` 第 2 条的输入框、`TXT-012` 第 2 条
   与 `TXT-009` 第 4 条后半句都在等这里。
6. **新记一条债**：`run-tests.sh --self-test` 的偶发红点（见上）。它是整套验证链的地基——
   在它稳定之前，「本轮变异 7/7 检出」这类结论都要附带「驱动已对偶发签名做了一次重跑」这句限定。

### 4.0.14 本轮（2026-09-21 21:5x）：ubuntu 腿自测连续 13 次红的真因（ENG-003 / ENG-004 的基础设施）

**做了什么**：上一轮把「崩溃套件自动补跑 `-v2`」落进 CI 之后，自测这一步在 ubuntu 上
**连续 13 次**红在同一条断言上。本轮先定性、再修，细节见 §1.35。三句话版本：

1. **根因不在 `Tests/Folder` 那条崩溃线上，而在运行器自己的累加逻辑里**：Linux 的 Qt Test
   接住 `SIGTERM` 后会**自己补写一份统计行**再以 `SIGABRT` 收尾（`status=134`、
   `has_summary=1`），macOS 则直接被 `TERM` 带走、什么也不写。于是「有没有统计行」这个判据
   在两个平台上是**两种形状**，而累加那一步没有排除超时 ⇒ 挂死探针的 `1 passed, 1 failed`
   被算进了合计。修法一行：`if [[ ${hs} -eq 1 && ${to} -eq 0 ]]`。
2. **连带效应比这条红本身更贵**：自测步骤一红，紧随其后的「运行测试套件」被 **skipped**，
   ubuntu 腿**一个套件都没跑过**。所以「ubuntu 腿只剩 1 个崩溃」这句旧结论，其实是那个
   崩溃**最后一次真的跑起来**时的数据。
3. **光改实现不够**：这条边界在 macOS 上恒不可达（挂死探针不写统计行），**任何本地变异
   都 redden不了它**。所以让挂死探针**自己先写一份统计行再挂死**，两个平台形状一致之后，
   边界才真正可验。另加两条诊断：失败断言印出**期望与实际**、证据**按遍快照**。
4. **顺带拿到了那条崩溃线的账**（§1.36）：自测修好 ⇒ ubuntu 腿第一次真的跑起套件 ⇒
   上一轮那条「崩溃自动补跑 `-v2`」第一次在真实 CI 上产出 `verbose.txt`，于是
   **`Tests/Folder` 崩在哪一条用例、哪一次调用，现在有据可查**：崩在
   `linksAreComparedWithoutFollowing()` 里 `Folder::compare()` 那一次调用，且**补跑也崩**
   ⇒ 不是偶发。它仍然**没有被修好**，也**没有勾掉任何完成标准**。

**验证数据**：自测 **32 条断言全绿**（30 → 32）；全量 **3688 / 0 / 2（69 套件）**、`EXIT=0`；
五道护栏逐个 `exit=0`；主程序 `MAKE_EXIT=0` + 本仓 0 warning + 离屏三行 + 重签；
**5 处变异 5 处检出、0 漏检**。**未勾掉任何完成标准。**

**下一步优先级**（第 1 条仍顺延，`Tests/Folder` 的 `verbose.txt` 这次终于会产出）：

1. **修 `Tests/Folder` 那条只在 ubuntu 上炸的崩溃——它已经不是谜了**（§1.36）：崩在用例
   `linksAreComparedWithoutFollowing()`、崩在 `Folder::compare()` 这一次调用里，
   该用例是全仓唯一造「指向自己的目录符号链接 `cycle`」+「悬空符号链接 `dangling`」的地方。
   下一步要么**二分夹具**（先在 scratch 分支上只去掉 `cycle` 再跑 ubuntu 腿），要么给
   ubuntu 腿加一次 `gdb -batch -ex run -ex bt` 把 `_chk` 失败的那一帧指出来。
   **拿到结论之前不要改 `foldercompare.cpp`**（那一串调用里既有本仓代码也有 Qt/glibc）。
2. **Windows 腿 26 个套件构建失败**：先只做两条系统性原因（`_WIN32_WINNT`、`winioctl.h`），
   清单在 §1.22。**本机无法编译 Windows 分支，只能靠 CI 回答**，一次只动一处、配一次推送。
3. `actions/checkout@v4` / `upload-artifact@v4` → v5（单独一轮配一次推送去验）。
4. **同族里还剩的可做条目**：`TXT-011`（忽略注释差异——边界条款要求注释语法**来自文件格式
   定义**，而内建 `FormatDefinition.settings` 里现在**一条 syntax 数据都没有**，先决定这份
   数据放哪，别在引擎里硬编码）、`TXT-015` ~ `TXT-017`、`TXT-004`（属界面）。
5. **`DIR-002` ~ `DIR-012` 剩下的十条**：先逐条核对再动手（「实现早就在、标准却是裸的」
   是这一族的常态）。
6. 回到「界面接通那一批」+ `OPT-002` 最小集。
7. **仍未查清的债**：`run-tests.sh --self-test` 的偶发红点（§1.34 记过一次，**本轮又撞到
   一次**——基线整体红、并行那一遍什么都没产出、`实测峰值 0`）。机制未查明，
   已在 §4.1 里单列一行。

### 4.0.15 本轮（2026-09-22 14:4x）：DIR-011 条目状态判定与语义（issue #120，**已完成**）

新模块 `Code/Services/Folder/entrystatus.{h,cpp}` + **新套件** `Tests/EntryStatus`
（27 个用例函数，纯 QtCore）+ `Tests/Folder` 35 → 45，详见 §1.37。要点：

- 主状态表 9 档（末尾两档「两侧均改 / 冲突」只由有效基线推导），
  内容证据 8 档、时间关系 4 档各一张表，**每张表都带一个把表当参数的校验函数**
  （自己读内部表的自检喂坏表进去也永远绿）。
- `Entry::partialComparison` 由布尔字段改成 `contentEvidence == Partial` 的视图——
  DIR-008 那一轮留下的两份说法合并成一份。
- 基线细化只在**叶子**上把「相同 / 不同」改成「两侧均改 / 冲突」；
  坏基线既不置 `baselineApplied` 也不产生这两档。
- 父子一致靠**共用** `aggregateChildren()` 保证，真值表在纯函数那侧跑 17 行，
  引擎那侧断言「喂回同一个函数结论逐字相同」且**反过来喂一遍也相同**。
- 视图：状态图标列、按表铺的状态筛选下拉、越界整数回落、右键「为什么是这个状态」
  （三节恒定出现）、9 个中性灰描边状态图标。
- **验证**：全量 3724 / 0 / 2（70 个套件）、五道护栏全绿、主程序 0 warning +
  离屏启动正常、**15 处变异 15 处检出**（其中 2 处必须一次改两个地方才可观察，
  见 §6 新增的那条「同一判据写了两遍」）。

**下一轮从哪里接**：§4 开头那份清单里 1～3 条（`Tests/Folder` 的 ubuntu 崩溃线、
Windows 腿 26 个套件、`actions/*@v4` → v5）都还没有结论或还没做；`DIR-011` 之后
`DIR-002` ~ `DIR-012` 还剩九条（`DIR-002` / `003` / `004` / `005` / `006` / `007` /
`009` / `010` / `012`）。**`DIR-005`（时间戳比较）这一轮被顺带解除了服务层的前置**：
`timeRelationFor()` / `compareTimes()` 已经就位，缺的只是「比较时间戳」这个开关
与容差的界面入口——那条的完成标准里第 4、5 条要的是列与排序，属界面，可独立闭环。

### 4.0.16 本轮（2026-09-22 15:2x）：DIR-003 递归子目录策略（issue #111，**已完成**）

新模块 `Code/Services/Folder/recursionstrategy.{h,cpp}` + `Tests/Folder` 45 → 51，
无新套件，详见 §1.38。要点：

- **不新增 `Options` 字段**：三档（仅根目录直属条目 / 递归深度 1 / 完全递归）在引擎里
  已经全部可达（`recursive` + `maximumDepth` 的组合），反查**按行为归类**、
  `setOptions()` **不做归一化**——`.lqc` 里「不递归 + 深度 7」这种合法存档值原样保住。
- 档位表 + 七项自检（顺序 / 标识符机器可读 / 唯一 / 「不递归当且仅当上限为 0」/
  递归档范围 / **反查穷尽性** / `Full` 缺省值必须等于 `Options::maximumDepth` 初值）。
- 深度边界上的解释**只有一份实现**，印的是**实际生效**的上限（`qBound` 之后那个数）。
- 循环符号链接判据：解析目标 == 链接自身 **或** 是它的**严格**上级；相对目标按
  **链接所在目录**解析；指向兄弟 / 下级子树的链接刻意放过。
- 视图：两态复选框 → 档位下拉（按表铺，`objectName = folderRecursionTier`）+
  深度数字框（`folderMaximumDepth`，只在「完全递归」档可改）；换档 `rescanRequested()`
  → 会话重扫（未开始的会话只记档位、不弹提示）。
- **删掉一处遮蔽防线**：`applyTierToControls(tier, bool fromUser)` 的 `if (fromUser)`
  在程序性路径上恒被覆盖或恒等，删掉它没有任何用例变红 → 收成「深度控件只有两个写入者」。
- **验证**：全量 **3730 / 0 / 2**（70 个套件）、五道护栏全绿（`check_winapi` 扫 347 个源
  文件 / `check_spec` 369 条 P0 59 / 41 个图标 / 16 个 shell 脚本）、主程序 0 warning +
  `codesign -f -s -` + 离屏启动正常、**15 处变异 15 处检出**（两处是补断言之后才抓得住的，
  见 §1.38 与 §6）。

**下一轮从哪里接**：`DIR-003` 划掉之后，`DIR-002` ~ `DIR-012` 还剩八条
（`DIR-002` / `004` / `005` / `006` / `007` / `009` / `010` / `012`）。注意 **`DIR-005`
（时间戳判定与容差）现在只需要界面入口**——`timeRelationFor()` / `compareTimes()`
已在 `entrystatus` 里，第 1、2、3 条要的是 `Options` 新字段 + 设置入口，第 4、5 条要视图列。
`Tests/Folder` 的 ubuntu 崩溃线（§1.36）**仍未修好**，它一直排在第一位。

### 4.0.17 本轮（2026-09-24 00:5x）：DIR-012 状态着色与图标（issue #122，**已完成**）

- **第 0 步判锁**：`run.lock` 的 `pid=36005` 已死（`kill -0` 失败）、`pgrep` 里没有别的
  `--session-id`、`Code/` `docs/` `tools/` 下 12 分钟内无文件改动 ⇒ 判为「上一轮被中断」，
  **接手**。pid 写 `$PPID`（沙箱里 `ps` 被禁，`$$` 拿到的 pid 立刻变死、会被误判成死锁）。
  **收工前已删锁。**
- **接手时做了什么**：上一轮留下 `Services/Folder/statuspalette.{h,cpp}` +
  `Code/Tests/StatusPalette/` + 视图接线 + `architecture.md` §3 目录行，**但新套件构建不过**：
  头文件声明了 `anIdentifierThatIsNotMachineReadableIsRejected()` 与
  `anIdentifierIsJudgedOnItselfNotOnlyAgainstItsSiblings()`，`.cpp` 里没有实现
  （`ld: Undefined symbols`）。先把这两个用例实现掉，再继续核对五条标准。
- **闭环 issue**：#122 DIR-012（P0），五条标准全勾，标签 待实现 → **已完成**。
- **交付**：
  - 服务层 `statuspalette.{h,cpp}`（798 行）：三套配色（默认 / 高对比 / 色盲友好）各带
    深浅两套色值 + 两个参考背景 + 自报的对比度门槛；`colorSchemeTable()` 与**表当参数**的
    `validateColorSchemeTable(table)`；WCAG 相对亮度与对比度（非法输入返回 `-1` 而不是 `0`）；
    Viénot/Brettel 色觉模拟与 `colorBlindSeparation()`；`.lqcolors` 的序列化 / 解析 / 文件往返。
  - 视图层：`themedStatusIcon()` 用 `CompositionMode_SourceIn` 给中性灰描边图标着色
    （深色主题必须明显更亮，由用例正面钉住）；配色下拉与「配色…」菜单（导出 / 导入）；
    `setColorScheme()` 只发**逐行** `dataChanged` 重绘、**刻意不发出 `rescanRequested`**；
    导入的自定义配色只活在本次会话、第二次导入是替换而不是追加。
- **测试**：**新套件** `Tests/StatusPalette`（**29 个用例函数**，刻意 `QT -= gui`）；
  `Tests/Folder` **51 → 55**（J 组四条守界面侧的第 1 / 3 / 4 / 5 条）。
  全量 **3730 → 3763 passed / 0 failed / 2 skipped（70 → 71 个套件）**。
- **验证**：五道护栏全绿（`check_icons` 41 个图标 / `check_winapi` 351 个源文件 /
  `check_spec` 369 条、P0 59 条 / `check_shell` 16 个脚本 / 分层检查通过）；主程序全量重编
  **0 条本仓 warning** + `codesign -f -s -` + 离屏三行日志；
  **30 处变异 30 处检出、0 处漏检、0 处无效**。
- **本轮最值钱的一条（同族通用）**：把校验函数里**每一条** `problems <<` 的中文片段当针、
  在整个测试集里搜一遍——**搜不到的就是「没人喂过输入」的分支**。本轮一次扫出五处
  （缺展示名 / 缺说明 / 参考背景不是 `#rrggbb` / 两档背景不呈一明一暗 /
  「已排除」弱化色不是 `#rrggbb`）。它们不是写错了，而是没有任何夹具能触发；
  补法是**掰夹具**，不是删分支。
- **探针抓到一个真缺口**：`ColorScheme::coversEveryStatus()` 的第二句
  「行数也要相等」改成 `return true;` 之后**29 条用例一条都不红** ⇒ 补三条断言后当场变红。
- **下一轮从哪里接**：`DIR-012` 划掉之后，`DIR-002` ~ `DIR-012` 还剩七条
  （`DIR-002` / `004` / `005` / `006` / `007` / `009` / `010`）。`DIR-005`（时间戳判定与容差）
  现在只缺界面入口；`DIR-002`（名称大小写策略）与 `DIR-009` / `DIR-007`（规则 / CRC 比对）
  都还没有独立核对过。**排在第一位的一直是 `Tests/Folder` 在 ubuntu 上的
  `*** buffer overflow detected ***`（§1.36，崩在 `Folder::compare()`）**。

### 4.1 选下一步之前先看这一节：哪些条目被谁阻塞

**为什么单独写一节**：本项目的推进方式是「一次闭环一条 issue」，
而**被阻塞的条目照样能提交一堆漂亮的代码**——它们只是永远无法把完成标准勾上，
因为验收依赖的东西还不存在。等发现时已经写了几百行没人用的代码。
所以选条目前先在这里查一次。

| 条目 | 被谁阻塞 | 现在能做多少 |
| `DIR-005` 文件修改时间的判定与容差 | **本轮不再被阻塞**（DIR-011 已经把服务层那一半做出来了，见 §1.37） | `timeRelationFor(entry, toleranceMs)` 与 `compareTimes(left, right, toleranceMs)`（容差是**闭区间**、无效时间戳优先返回「未知」）已在 `entrystatus.{h,cpp}` 里，`Entry::timeRelation` 也已经在每条条目上。第 1、2、3 条（开关、UTC 比较、快捷预设）要的是 `Options` 新字段 + 界面入口，第 4、5 条（列中显示差值 / 排序、跨时区不产生虚假差异）要的是视图列与固定语料——**都能独立闭环**。注意第 3 条「忽略时间，只比大小与内容」的预设属 DIR-010，两者会碰同一个下拉 |
| --- | --- | --- |
| ~~`SESS-001` 会话抽象基类~~ | **已落地**（提交见 §3.1，issue #36） | 四条完成标准**全部已勾**：第 2 条的「并注册」那一半在下一轮由 `Tests/Session` 的 G 组补上，理由见 §1.8。它现在**不再阻塞任何人** |
| ~~`SESS-002` 会话类型注册表~~ | **已落地**（提交见 §3.1，issue #37） | 四条完成标准全部已勾，见 §1.9。**它解除了三处的阻塞**：`SESS-003` 第 1 条、`SESS-005` 第 1 条，以及「在 `Views/` 里新增会话入口的条目」。但它**没有**解除 `PLAT-006` 第 3 条与 `CLI-001` 的执行部分——原因见下面那两行 |
| ~~`SESS-006` 会话设置对话框框架~~ | **已落地**（提交见 §3.1，issue #39） | 四条完成标准全部已勾，见 §1.10。它**解除了 `SESS-007` 的阻塞**（作用域下拉就住在它的对话框底部，`SettingScope` 的三个取值与说明也已定好）。它**没有**解除「界面接入」那一类——框架里一项具体设置都没有，现在接通 Rules 按钮打开的是空对话框；要等第一个真正的会话类型登记声明 |
| `SESS-007` 设置作用域语义 | **已落地**（提交见 §3.1，issue #42） | 第 1、4 条已勾；第 2、3 条的服务层逻辑（去向文案、切换提示、丢弃计划）已落地并被 12 处变异逐一验证，**剩下的是界面接通**。它**解除了一处阻塞**：`FILT-005` 第 4 条（「视图临时过滤不写入会话」）现在有 `SettingScope::View` 可用了——该条已随后落地（见下一行）。它**没有**解除自己那两条——它们要等第一个真正的会话类型 |
| `PLAT-006` 第 3 条（收到参数后首个实例**创建会话**并把窗口置前） | 从「缺第一个真正的会话类型」**降级为「缺一个把置前策略暴露给用户的地方」** | 「创建会话」这一半**已经接上**：`main.cpp` 把 `relayReceived` 连到 `MainWindow::openRequest()`（它按类型/掩码造会话并显示），窗口置前由 `activationRequested` 的结论驱动。剩下的是括注里那句「不抢焦点导致用户中断输入时**可配置**」：`ActivationPolicy` 三值与「距上次输入多久」的注入点都在服务层、有 6 条用例，但没有任何设置项或命令行开关暴露它，生产路径只用默认的 `Always`。要做的话它的住所是「比较与界面」那一组选项（OPT-003 / OPT-007），**不要**在 `general` 下另起一个键 |
| `SESS-003` Home 视图 | 部分解除：第 1 条不再被阻塞 | 第 1 条（按会话类型分组的「新建会话」入口卡片）现在**可做**——注册表能按分组枚举，且每条带显示名、英文名与一句话说明。第 2、3 条（最近会话 / 最近比较）等 `SESS-009`，第 4 条（会话树）等 `SESS-004` 与 `SESS-008`，第 5 条（关完会话回 Home）只依赖 `SessionArea` |
| `SESS-005` 新建会话向导 | 部分解除：第 1 条不再被阻塞 | 第 1 条（按「文本类 / 文件夹类 / 数据类 / 高级」分组列出全部可用类型）现在**可做**：四个分组枚举与「按分组枚举可用类型」正是为此准备的。第 2 条要等各类型的路径模型，第 3 条（剪贴板作数据源）要等 `SESS-012` |
| `PLAT-006` 第 5 条（单实例行为可由选项关闭） | ~~`OPT-002`（设置框架）未落地~~ **已解除**（本轮勾选） | 设置键 `general.singleInstance` 已在 `OptionsRepository::definitions()` 里登记，`OptionsDialog` 会为它的改动给出「单实例行为将在下次启动时生效」的文案，`main.cpp` 读它决定 `setEnabled()`。另有命令行开关表（`--single-instance` / `--no-single-instance` / `--new-instance` / `--wait`） |
| ~~`PLAT-006` 第 1、2、4 条~~ | **已落地**（提交见 §3.1，issue #327） | 第 1、2、4、5 条已勾，第 3 条只勾一半，因此标签是「部分完成」，理由与两条环境限制见 §1.16。**注意**：它在上两轮一直被写成「下一轮该开工的条目」，而夜间那批工作流其实已经把它实现完了——**「issue 还是待实现」不等于「代码没写」**，接手时先核对 issue 与源码，别按 §4.0 的旧推荐直接开工 |
| ~~`OPT-005` 文件操作选项~~ | **已落地**（提交见 §3.1，issue #317） | 五条完成标准全部已勾，做法与验证数据见 §1.17。它**不阻塞任何人**，但它证明了「一张设置页怎么落」这件事：服务层纯数据策略对象 + 仓库登记键 + 对话框只搬值不重写规则。**它没有解除** `FILT-005` 第 2、3 条与 `FILT-004` 第 4 条——那几处要的是 **Filters 页与状态栏**，不是「文件操作」页 |
| 其余 `OPT-*` 设置页（`OPT-003` / `OPT-006` ~ `OPT-011`） | 页面各自被规格里的分类决定，**互不阻塞**；但其中承载 `FILT-*` 界面的那几张（Filters 页）一到，就能同时勾掉 §4.0 第 1 条列的那六条 | 可做。加页三步见 §4.0 的 M 行；`OPT-001` 的框架与 `OPT-005` 的范式都已在仓库里 |
| `TXT-009` 第 4 条后半句（**被忽略的空白差异的标记可由 View 页开关关闭**） | `OPT-007`（文本编辑与视图选项）/ `OPT-003`（比较规则默认值）——**两张设置页都还没落地**，那句开关没有住所 | **只落了前半句**（被忽略的空白差异在视图里有可视标记，已有 `Tests/TextView` 用例钉住）。后半句**不要**为了勾掉它而在 `general` 下另起一个键（与 `PLAT-006` 第 3 条同一处置）：正解是等 `OPT-007` 那张页，或按 §4.0 的「加一张页 = 三步」自己先落那张页——后者是一条独立 issue，不是 TXT-009 的收尾 |
| `TXT-009` 的遗留序号耦合（`textcomparesession.cpp` 的 `qBound(0, value, 2)`） | 不阻塞任何人，但**不属于 TXT-009 的射程** | 读回时的合法区间写死成 `0..2` 而不是从模式表推。要改得连命令行那条同样以整数传模式的路径一起改，更贴近 `TXT-004`（算法/模式选择与持久化）。已记在 §1.28 末尾 |
| `PLAT-004` 第 1 条、`PLAT-005` 第 1 条 | 需要在真的 Windows 上编译 + 在资源管理器里看 | 只能等有 Windows 机器 |
| `PLAT-004` / `PLAT-005` / `ENG-006` / `FILT-001` 的界面接入 | `OPT-001` / `OPT-002`（设置页）未落地 | 服务层已就绪，界面接口留好即可（`ENG-006` 的接收者机制、`FILT-001` 的 `maskSyntaxReferenceText()` 与 `preview()` 都已备好） |
| 任何在 `Views/` 里新增真正会话界面的条目 | ~~`SESS-001`（会话基类）~~ **已解除** | 基类与类型注册表都已落地，会话界面现在可以真正开工；缺的只是各自具体的会话类型实现。**一个反例值得记住**：`SESS-006` 的设置对话框外壳已经在 `Views/Session/` 里落地并跑通了 44 条用例，但它**没有**被容器接起来——框架层可以先于具体视图落地，只要它的输入（声明）是纯数据 |
| ~~`FILT-001` 掩码解析器~~ | **已落地**（提交见 §3.1，issue #228） | 已完成，其余 FILT 条目都复用它 |
| `FILT-005` 过滤器的层级与作用域 | **已落地**（提交见 §3.1，issue #233） | 第 1、4、5 条已勾；第 2、3 条的服务层部分（启用状态与四态文案、面板数据与全文）已落地并被测试，**缺的是显示控件**——两者的住所都是设置页（`OPT-*`），因此标签是「部分完成」。它**解除了一处阻塞**：`FILT-003` 第 4 条（「与名称过滤构成整体的与关系」）现在有一张三层的表可以挂。它**没有**解除任何「界面接入」类条目——那需要设置页 |
| ~~`FILT-003` 属性过滤~~ | **已落地**（提交见 §3.1，issue #230） | 第 1~4 条已勾；第 5 条只勾得动一半——「与内容比对解耦」由 `Tests/AttributeFilter` 的读源码护栏钉住了，「扫描阶段提前丢弃」那半句要等 `Folder/` 的扫描器，因此标签是「部分完成」，理由与接手方式写在 §1.13。它**为 `Folder/` 的扫描器留好了接口**：扫描器拿到 `EntryMetadata` 后调 `decideEntry()`，`accepted == false` 直接跳过 |
| ~~`FILT-002` 名称过滤器（正则与超时保护）~~ | **已落地**（提交见 §3.1，issue #229） | 第 1、2、4、5 条已勾（三种模式、**200ms 超时保护 + 连续超时断路器**、就地实时校验且非法表达式不生效、具名预设与导出往返）；第 3 条只勾得动服务层一半（组合语义与文案已落，「界面明确显示当前语义」要等界面），因此标签是「部分完成」，理由与接手方式写在 §1.14。它**解除了「Qt 版本卡住」这处阻塞**：超时走的是自建的工作线程 + 截止时间，不依赖任何 Qt 6 API。它**为 `Folder/` 的扫描器留好了接口**：扫描器拿到名字后调 `decide()`，`NotMatched` 跳过、`Undecided` 照常进入 |
| ~~`FILT-004` 内容过滤器（行过滤与关键字节）~~ | **已落地（第 1、2 条）**（提交见 §3.1，issue #231） | 第 1 条（行过滤器：给一段文本，排除匹配模式的行）与第 2 条（关键字节：给一段字节，判断是否含指定序列）**已完全落地**，85 个用例函数、纯 QtCore，理由与接手方式写在 §1.15。第 3 条只勾得动一半——顺序已定死成「先过滤行、再应用忽略规则」，`LineFilter` 的入参也已经是原始行，并用**真的** `Text::compare` 给出了证据；缺的是**比对入口在应用忽略规则之前先过一遍行过滤**（`Services/Text` 侧）。第 4 条要状态栏、第 5 条要 Filters 页的写入入口（宿主 `Format/` 模块**已落地**，见 §1.19——此前写成「要 `Format/` 模块」是错的）。它**没有**解除任何阻塞——那三处分别在 `Services/Text`、状态栏与 Filters 页，都不是本模块能自己接上的 |
| ~~`FMT-001` 文件格式定义模型与存储~~ | **已落地**（代码由夜间那批工作流产出，本轮**核对并闭环**，issue #240） | 五条完成标准全部已勾，证据见 §1.19。**它解除了两处被误记的阻塞**：`FILT-004` 第 5 条与 `FILT-005` 的「格式层」（此前都写成「等文件格式定义」）。注意它的**后续条目仍未做**：格式管理器 UI（`FMT-002` / #360）、语法高亮引擎、格式转换执行、归档解压器——**不要把这两件事混为一谈**，本文档之前就是这么错的 |
| `FILT-006` 过滤结果的可见性与批量操作安全 | 第 4 条可做；其余要扫描器/状态栏 | 第 4 条（构造隐藏条目并断言两个选项下的行为差异）是纯逻辑，可做；「当前可见项」这类数量来源要等 `Folder/` 扫描器，状态栏常显要界面 |
| `CLI-001` 起的命令行条目 | `SESS-002` 已落地，但**内置类型都没有工厂** | 解析部分可做（`ShellIntegration::parseShellInvocation()` 已是例子）；`--list-session-types` 这类**列出**类型的子命令现在也可做（注册表可枚举）。「执行」（真造出会话）仍要等第一个真正的会话类型 |
| `ENG-004` 持续集成流水线 | ~~「没有 CI」~~、~~「从来没跑过构建与测试」~~ 均已修；**三条腿的真实结果也已拿到（run `35546164218`）** | 四个阶段、三平台矩阵、产物上传都已写进 `.github/workflows/build.yml`；`run-tests.sh` 的**七项**跨平台问题已修（见 §1.20）。第一次真跑的证据：三条腿都跑完了四个阶段、上传了 6 个 artifact，**流水线本身按设计工作**（拿不到 LqRibbon 时正确跳过主程序构建与打包、测试照跑）。**三条腿现在都是红的，红的不是流水线而是被测对象**：`Archive` 套件在 ubuntu/macOS 上因 CI 的 Python 3.14 抛 `UnicodeDecodeError`（**已修，见 §1.21**，但要等下一次 CI 确认）；ubuntu 17 个 / Windows 26 个套件构建失败（`build.log` 已上传，**下一轮读它定位**）；Windows 的 8 个套件没产出 `Totals:` 行（已由本轮的单列机制暴露）。另有一处**结构性限制**：主程序构建、打包、主程序产物上传这三步都要私有仓 LqRibbon，没有 `MYCLASS_TOKEN` 时在公开 CI 上永远走不到，Windows 腿的打包分支（`7z`）因此是**未验证代码**。~~`ENG-003` 还差的两项（单套件超时、并行执行）不属本条~~ **该说法已过期——这两项已在下一行补齐** |
| ~~`ENG-003` 测试框架与测试运行器~~ | **已落地**（issue #335，记录见 §1.24） | 五条完成标准**全部已勾**：套件结构（66 个各带 `.pro`）原本就具备；本轮补上**并行**（默认 4，串行基线 3 分 53 秒 → 1 分 21 秒）、**单套件超时**（600s，自建轮询，不依赖 GNU `timeout`）、**JUnit XML** 沿用既有实现，并修掉第 5 条的违反（`Tests/OptionsDialog` 曾把截图写进仓库根）。新增 `run-tests.sh --self-test`（五个临时探针 / 两遍 / **32 条断言**，§1.34 从 24 涨到 30、§1.35 涨到 32），CI 里非 Windows 腿会跑它。**§1.34 给运行器加了一条诊断能力**：崩溃（没产出 `Totals:` 行）的套件自动补跑一遍 `-v2` 并**点名崩在哪一条用例**，产物留 `verbose.txt` / `verbose.stderr.log` 并随 CI 上传（7 处变异 7 处检出）。**§1.35 修掉了它在 ubuntu 上连续 13 次红的真因**（累加没排除超时套件，连带让 ubuntu 腿一个套件都没跑过）并让这条边界在两个平台上都可观察（5 处变异 5 处检出）。**它不阻塞任何条目**，但它是所有条目共用的验证入口——改它要跑自测。**一处已知缺口**：超时分支未在 Windows 上实测（CI 显式 `::warning::` 跳过；那一步在 CI 页面上显示 success 是**设计意图**，不是漏配）。**另有一条未清掉的债**：自测本身出现过偶发红点（§1.34 记过一次，**§1.35 那一轮又撞到一次**：整个并行遍什么都没产出、`实测峰值 0`、于是十余条断言一起红）——它会把「漏检」伪装出来，因此**在它查明之前，凡「N 处变异 N 处检出」的结论都要附带一句「驱动已对这条偶发签名做过一次重跑」** |
| ~~`TXT-002` 基础行对齐算法（Myers）~~ | **已落地**（issue #57，记录见 §1.25） | 四条完成标准**全部已勾**。算法（`textdiff.{h,cpp}`，192 行）与 `Tests/Text` 原本就在，但**第 3 条的一半与整个第 4 条从来没有被断言过**：既有用例只断言 `differences` 为空（对「有几个块」毫无约束）、只比过两遍的 `rows[i].leftLine/rightLine`。本轮只加测试（`Tests/Text` 22 → 25 条，全量 3598 → 3601）、**不动一行生产代码**：全相同 → 单个相同块的边界、把 `Result` 摊成文本的逐字段快照（五个手推黄金串）、以及专为 `alignmentLimited` 造的输入。9 处变异 9 处检出。**它对任何条目没有阻塞**，也不被任何条目阻塞 |
| ~~`TXT-003` 耐心对齐（Patience）~~ | **已落地**（issue #58，记录见 §1.26） | 四条完成标准**全部已勾**：唯一行优先（计数 + 严格递增最长子序列挑锚点）、无唯一行时**与 Myers 逐字段相同**的回退、可选算法表（`availableAlignments()` 只返回已实现的 / `validateAlignmentTable()` 报出规格被降级）、固定语料上切换算法改变块的数量与位置。9 处变异 9 处检出。**它对任何条目没有阻塞**，也不被任何条目阻塞。**它把 TXT-004 需要的数据备齐了**（算法标识符、可选清单、默认值）——但 TXT-004 的入口是 Compare 页的算法下拉，属界面，**不要**把这两条混为一谈 |
| ~~`TXT-008` 忽略大小写差异~~ | **已落地**（issue #63，记录见 §1.27） | 四条完成标准**全部已勾**。这一条的特殊之处是**实现早就在**（`ignoreCase` / `Change::Ignored` / Rules 勾选框 / `text.ignoreCase` 键 / `colorFor(Ignored)` 的弱化底色全在仓库里），缺的只是断言与「链」这个名字——本轮的产出因此主要落在**测试**上（7 个新用例函数）加一次把 `normalizedLine()` 提到公开接口的生产改动。**它对任何条目没有阻塞**，也不被任何条目阻塞；**但它给 TXT-025 留了一条硬约束**：字符级高亮可以直接拿规范化后的下标当原文下标用，前提是折叠长度守恒——`Tests/Text` 有一条用例专门把这个前提钉住，改成 full folding 会立刻红 |
| ~~`TXT-005` 相似度阈值与相似行对齐~~ | **已落地**（issue #60，记录见 §1.29） | 四条完成标准**全部已勾**。**这一条是「从零写」而非「先核对」**：核对阶段 `Services/Text/` 下只有 `textdocument.*` / `textdiff.*`，相似度相关源码一行都没有（`CompareOptions::similarityThreshold` 只是声明，注释里自己写着「尚未被引擎读取」）。新增纯函数模块 `linesimilarity.{h,cpp}` 与「一处改动」的归并层 `DifferenceRun` / `differenceRuns()`，`Tests/Similarity` 15 条。**它对任何条目没有阻塞**，也不被任何条目阻塞。**它给同族的 TXT-010 ~ TXT-017 留了一份可复用的判据**：「先逐条读完成标准，再 `ls` 源码与测试目录；`grep` 到的**字段声明**不算实现」。**它还暴露了一条排序纪律**：多个套件的夹具里若出现「两个不同的量恰好相等」或「某对行在出厂阈值下根本配不上对」，先怀疑夹具而不是实现——见 §1.29 那条教训 |
| ~~`TXT-001` 文本比对会话与双窗格视图骨架~~ | **已落地**（issue #56，记录见 §1.30） | 四条完成标准**全部已勾**。它是**本表的一个反例**：此前既不在本表、也从未出现在本文档任何位置，而实现（`TextCompareSession` / `TextCompareView`）从会话框架落地起就在仓库里、被 20 条用例跑着。本轮补 4 条用例函数把四条标准逐条钉住（分栏方向 / 填充行与号码链 / 单侧为空两个方向 / 三组无退化输入），生产改动只有 `TextPane::lineNumbers()`。**它对任何条目没有阻塞**，也不被任何条目阻塞。**它给同族的 TXT-010 ~ TXT-017 留了同一份判据，并额外加了一条**：文档里 `grep` 不到条目号时，要把「从没被核对过」当成一种可能，别默认它「已经有人看过了」** |
| ~~`TXT-010` 忽略行尾（EOL）差异~~ | **已落地**（issue #65，记录见 §1.31） | 四条完成标准**全部已勾**。两条开关（`ignoreEol` / `ignoreFinalNewline`）**早就接在同一条键函数里**，缺的是组合断言；**第 4 条的「警告图标」是本轮唯一真正新增的能力**——会话多出一条独立的严重程度通道（`StatusSeverity` + 信号，与状态文本**各自去重**），容器转发并在切标签时重播，窗口在状态栏放永久控件 `statusWarningIcon`。9 个新用例函数，18 处变异 18 处检出。**它对任何条目没有阻塞**，也不被任何条目阻塞。**它给同族的 TXT-011 ~ TXT-017 留了两条判据**：① 一个「删掉之后没有任何用例变红」的分支不是纵深防御，是没人知道的死代码（本轮两处变异互相遮蔽就是它暴露的）；② 断言里的具体字符串必须去读实现印出来的那一句（三处计数是「总是都印、用不到的为 0」） |
| ~~`TXT-012` 内置替换规则~~ | **已落地（第 1、3、4 条）**（issue #69，记录见 §1.32） | 第 1、3、4 条**全部已勾**；第 2 条只落服务层一半——四条规则都能单独开关，且「正则 / 说明」都是服务层可读数据（界面不必再抄一份），**缺的是把它们画到界面上**，住所是设置页 `OPT-*`（与 `TXT-009` 第 4 条后半句同一处置）。**它不阻塞任何条目**，也不被任何条目阻塞。**它给同族的 `TXT-011` / `TXT-016` 留了两条判据**：① 「多个规则按某顺序应用」这句话只有**区间重叠的语料**才让它可观察；② 命中之后走的是既有的 `Change::Ignored` 通道，别把它断言成 `Equal` |

| ~~`DIR-008` 二进制逐字节比对~~ | **已落地（第 1、3、4、5 条）**（issue #116，记录见 §1.33） | 第 1、3、4、5 条**全部已勾**；第 2 条落了引擎、设置键（`folder.compareFirstBytes`）与视图往返，**缺的是画到界面上的那个输入框**（`FolderCompareView` 目前与 `maximumDepth` 同法原样保留该值而不显示）。住所是目录选项那一组界面，与 `TXT-009` 第 4 条后半句、`TXT-012` 第 2 条同一处置：**不要**为了勾掉它在 `general` 下另起一个键。**它不阻塞任何条目**，也不被任何条目阻塞。**它给同族的 `DIR-002` ~ `DIR-012` 留了三条判据**：① 「实现早就在」不等于「标准被守住了」——第 3 条的分块上界原本藏在 `.cpp` 里（测试只能去匹配源码字面量），第 4 条的偏移量进报表**从没有用例见过**，这两条标准当时是裸的（本轮把上界提成公开常量并补了报表侧断言）；② 「已经证明的结论不许降级成不确定」是这一族里反复出现的一种错法（大小不等、差异在限内都属此列）；③ **带副词的那个词要单独找一遍「谁在守它」**——第 1 条的「**提前**」原本无人守（把那处 `break` 删掉，3687 条用例一条不红），而且既有断言**看起来像**把它守住了（其实钉的是偏移量）。判据一句话：**把那个词反过来写，谁变红？** |

| ~~`DIR-012` 状态着色与图标~~ | **已落地**（issue #122，记录见 §1.39） | 五条完成标准**全部已勾**：三套配色表（默认 / 高对比 / 色盲友好）与记表自检、WCAG 对比度、「色盲友好」由 Viénot/Brettel 模拟距离守着、图标随主题着色、切换只重绘不重扫、`.lqcolors` 导出导入且**失败时一个字段都不改**。**入口住所**：配色下拉落在文件夹比对页的「显示」工具条上，规格说的 View 页 Coloring 组属 `OPT-*`（未落地）——与 `DIR-003` 的档位下拉同一处置，因此**不阻塞任何人**。**它给同族留了一条判据**：本轮扫出五处「没人喂过输入」的校验分支，说明**「分支写了」也不等于「判据被守住了」**，判据仍然是那句「把它删掉，谁变红？」 |

**`FILT-002` 第 2 条当时为什么需要先决策（已被解决，留作记录）**：那条要求
「正则匹配有超时保护（默认 200ms/条），超时记录为错误条目并继续」。但
**Qt 5.15 的 `QRegularExpression` 没有匹配超时**——`setMatchTimeout()` /
`matchTimeout()` 是 Qt 6.0 才加进来的（已在本机 Qt 5.15.2 的头文件里核对过：
`qregularexpression.h` 里没有这两个成员）。当时有两条路：一是把匹配放到工作线程上等
一个截止时间，二是直接调 PCRE2 的 match limit。**本轮选了第一条**：
`ThreadNameMatchRunner` 起一个工作线程，用 `QWaitCondition` 等 `perEntryMs`，
超时就把整个线程丢弃（`deleteLater`）并换一个新的，同时把结论记成 `Undecided` +
一条 `Timeout` 问题；连续超时达到 `consecutiveTimeoutLimit`（默认 2）时断路器打开，
后续条目不再派活、直接按 `Undecided` 放行。**没选第二条**的原因是把 PCRE2 带进交付物
与「一个 exe 分发」相冲，而且本机的 Qt 并没有随包提供 `libpcre2`（Qt 静态编进去了）。

判断方法很简单：**看完成标准里有没有动词指向一个还不存在的模块**。
「创建会话」「在设置界面里」「显示在差异视图里」都指向别的工作流的产物；
「解析」「生成」「转发」「超时降级」「写注册表」都只依赖本模块。

**但光查这张表不够（TXT-001 这一轮补的教训）**：这张表只登记**被讨论过**的条目。
`TXT-001` 从来没人讨论过，于是它既不在这张表里、也不在 §1.x / §4.0.x 的
任何地方——「标签是待实现」与「文档里查不到」互相印证了同一个错误结论，
直到这一轮才被发现（它的实现其实早就在 `Views/Text/` 下跑着，见 §1.30）。
所以开工前的核对动作是**三件事一起做**，缺一个都可能白写或漏做：

1. 读 issue 的完成标准（逐条，不要只看标题）；
2. `ls` 对应源码目录与测试目录，**在源码与用例里找得到对应断言**才算真的落过
   （`grep` 到的**字段声明**不算实现——TXT-005 那次就是踩在这上面）；
3. 在本文档里 `grep` 一次该条目号：**一次都没出现**是要当真的信号，
   它意味着这条从来没有被任何人核对过。

**A 工作流的九件要紧事：**

1. `trash_linux.cpp`、`trash_win.cpp`、`filesystem_win.cpp`、`iconservice_win.cpp`、
   `registrystore_win.cpp` 需要在各自的平台上首次构建并修正。它们是当前唯一
   **从未被编译过**的代码，PLAT-002 / PLAT-003 / PLAT-004 / PLAT-005 / PLAT-007
   的各一条完成标准因此未勾选。拿到 Windows 机器时一次性过一遍——几份文件共用
   同一套手写常量 + `static_assert` 模式，错法也相似（先看
   `NSFileManagerUnmountBusyError` 写成 768 那件事就知道，这类错误编译期会直接报出来，
   不必等运行）。
   `iconservice_linux.cpp` 未在 Linux 上跑过，但它只依赖 QtGui 的 `QMimeDatabase`
   与 `QIcon::fromTheme`，风险低于 Windows 那几个。
2. **删除只有一条入口：`TrashService`**。`FileSystem::deleteToTrash` 已经移除，
   不要再加回来——理由写在 `filesystem.h` 的注释里（无状态的 `FileSystem` 留不住
   撤销点，用户点撤销会永远报「没有可还原的删除」）。
   调用方要自己持有 `TrashService` 的实例，生命周期要跨越「删除」与「撤销」两次操作。
3. 界面上接删除功能时，**先调 `availabilityFor()` 再决定是否动手**，不可用时用
   `decideTrash()` 拿到的 `reason` / `advice` 让用户选「取消 / 永久删除」。
   `TrashFallback` 的默认值是 `Cancel`——这个默认值会被当成「用户还没回答」时的行为，
   把它定成 `DeletePermanently` 会让一次界面卡顿变成一批文件的永久消失。
4. 界面上显示文件名时**统一走 `PathName::forDisplay()`**，不要直接把名字塞进控件。
   换行会撑破列表行、首尾空格完全看不见，用户会在「看起来一样」的两个名字里挑错。
   同时记住两条纪律：读回与写回一律用**原始名字**（转义只发生在显示层）；
   把文本编码回系统字节必须用 `PathName::toNativeBytes()`，
   **绝不能用 `QString::toUtf8()`**（它会把承载原始字节的未配对代理换成 `?`，
   而且从返回值上看不出发生过什么）。
5. **报错只用一条出口**：拿到系统错误码的地方用 `fromSystemError()` /
   `fromWindowsError()` / `fromCocoaError()`，**不要**用 `classify*()`——后者只回答
   「属于哪一类」，原始码会被丢掉，PLAT-008 第 5 条就落空了。
   显示时用 `errorReport(code, path)`（分类文案 + 原始码）配 `errorAdvice(category)`。
   反过来说，`ErrorCode` 与 `FileSystemError` 之间有双向隐式转换，
   所以 `*error = FileSystemError::None` 与 `error == FileSystemError::Busy`
   这类既有写法都仍然有效，不需要为了类型变化去改调用点。
6. **批量文件操作走 `BatchOperation`，不要自己写循环**。
   理由不是「少写几行」：长任务的重试必须知道「哪些条目已经成功」，
   自写的循环迟早会把这个状态算错，然后表现为「已经成功的文件被再做一遍」。
   默认策略是 `SkipAndContinue`（PLAT-008 第 4 条）；有序批次才用 `StopOnFirstError`。
   界面上给用户两条出路时对应的是：`retryFailed()`（重试失败项）与
   直接接受当前报告（跳过并继续）——**两条路都不要重跑整批**。
7. **界面取图标走 `IconService`，不要在视图里自己调 `QFileIconProvider`**。
   理由不是「统一风格」，而是三件会真实发生的事：一是按路径取图标没有缓存，
   滚动一个 5000 个文件的目录就是 5000 次系统调用；二是同步取会让界面
   卡在文件名上；三是取不到图标时视图要自己决定「显示什么」，
   于是每个用到图标的地方都会长出自己的一套回退逻辑。
   接线的正确姿势是：构造期给 `IconService` 一个 `setBaseSize()` / `setDevicePixelRatio()`，
   列表里调 `requestIcon(path)`，收到 `iconReady(cacheKey)` 后用**条目自己记住的 cacheKey**
   去 `iconForPath()`（命中即 O(1) 哈希查找）刷新那一行。
   **不要**把「哪个键对应哪些行」维护在服务里——那是第二份事实来源。
   另外记得 `IconService` 是从 `Platform::` 拿 `Files::PathUtils::Style` 的，
   与 `Files/` 的路径规则共用同一个事实来源，别在视图里自己拼扩展名。
8. **Shell 集成的三个入口要一起接，不要只接「安装」**。
   `installedState()`（现在是什么状态）、`previewInstall()`（这次会改什么）、
   `install()` / `uninstall()`（动手）——`previewInstall()` 用一个内存存储
   把「会覆盖你现有的 `.patch` 关联」这类结论在动手之前算出来，
   而 `uninstall()` 之后**必然**会跑一次残留检查并把结果写进同一个报告。
   三条容易踩的纪律：
   一是**不要**绕过 `buildShellIntegrationPlan()` 自己拼注册表路径，
   计划是唯一的事实来源，安装/卸载/校验/残留四件事都从它推导；
   二是安装要允许「重新配置」（用户改了选项再装一次），
   `install()` 的步骤 0 已经会先按**记录的**选项拆干净，
   否则旧选项留下的项卸载不掉，而报告会说「残留检查通过」；
   三是**非 Windows 平台不要试图给一个「能用的替代实现」**——
   `capability().available` 为假时照 `advice` 给用户可执行的替代路径就行，
   把「写不进去」做成「假成功」比直接说不行糟得多。
9. **输出面板接日志要走一次排队跳转**，不要直接把面板挂成日志接收者。
   接收者在**记录日志的那个线程**上被调用（`Tests/Logging` 里有一条跨线程用例
   把这个事实钉住），而图标解析跑在 `IconService` 的后台线程上——
   从那里碰控件会崩，且崩的位置与「我只是接了个日志」看起来毫无关系。
   正确姿势：接收者只 `emit` 一个信号，用 `Qt::QueuedConnection` 连到面板的槽。
   另外三个容易踩的点：`addSink()` 传空函数对象会返回 0（无效句柄），
   面板析构时要按句柄 `removeSink()`（`clearSinks()` 会把别人的接收者一起清掉，
   例如诊断包导出）；清理顺序是**先移除接收者再关日志文件**，
   反过来的话关文件时若还挂着接收者，某些实现会顺手写一条「日志文件已关闭」，
   而这条落到了下一个使用者头上。

三个并行对话不是硬性数量，也可以只开两个（A + B），或把 B 换成 **O 工程与文档**
（`ENG-002` 模块构建守卫、`DOC-001` 用户手册）。**H 建议早做**：掩码解析器是纯算法，
输入输出都是字符串，不需要任何平台能力就能写出完整测试，是性价比最高的一块。

## 5. 接手时必须遵守的约定

1. **先读 issue，再写代码**。issue 正文里的「入口、作用对象与行为边界」「完成标准」
   就是验收条件；不要按自己的理解扩大范围。
2. **不要改 `docs/PRD-actions.md`**。它是生成物，改 `tools/spec/` 里的数据后重新生成。
3. **不要手改各顶层 `.pri` 与 `LqCompare.pro`**。共享 include 已一次加齐（`exists()` 保护），
   新建模块只需要新建目录与自己的 `.pri`。
4. **提交消息必须带 ACTION-ID 与 issue 号**，格式见 parallel-workstreams.md §3。
5. **破坏性操作必须可逆**：删除走回收站、覆盖先备份、批量前预演。
6. **每个条目配测试**，且测试要能在 `-platform offscreen` 下跑。
7. **静态检查脚本必须能自证会报错**。加一个 `--self-test`，用「该报的」与
   「不该报的」两类样本各跑一遍。一个从不报错的护栏比没有护栏更糟——它会让人
   以为这块已经被守住了。`check_winapi.py --self-test` 是现成的例子。
8. **写平台代码时先问「这段逻辑能不能抽成纯函数」**。能抽就抽：Windows 的路径
   规则、Linux 的回收站规则、UTF-8 的字节校验都因此能在开发机上被真实执行。
   抽不出来的只剩真正的系统调用，那部分如实标注「未在目标平台验证」，
   并在 issue 上写清楚——**不要**把「写了」当成「做完了」。

## 6. 本仓库的「坑」记录（新增坑请追加到本节）

| 坑 | 现象 | 处理 |
| --- | --- | --- |
| 日志宏形参命名为 `level` | 宏体里的 `LqCompare::Log::level()` 被一起替换掉，编译报「called object type … is not a function」 | 形参改用 `lvl` |
| `Q_OBJECT` 在头文件里却又写 `#include "xxx.moc"` | `No rule to make target 'xxx.moc'` | 去掉 `.moc` include；确保有 `QTEST_MAIN`，否则链接报 `undefined _main` |
| 测试工程写 `QT -= gui` | `QKeySequence` 头文件找不到（它属于 QtGui） | 保留 `QT += gui` |
| 图标校验只认 `:/Pictures/x.svg` | 用 `icon("x.svg")` 辅助函数的地方被误判为未引用 | 校验同时认裸文件名 || Qt 5.15.2 在 macOS 26 SDK 上 | qmake 报 SDK 版本不支持的警告 | 只是警告；本项目已实测可编译运行 |
| 优先级有两个事实来源 | 规格条目自带 `prio` 字段（恒为 P1），策略却写在 `publish_issues.P0_RANGES`；索引读前者，会把 59 条 P0 全标成 P1，且不报任何错 | 删掉条目里的 `prio` 字段，优先级只由策略推导；`check_spec.py` 新增护栏核对策略前缀 |
| 单跑 `publish_issues.py prd` | 用空映射覆盖 `prd-issues.json`，把「哪条规格对应哪个 issue」这个唯一反查入口清空（issue 还在远端躺着，但本地查不到了） | `prd` 与 `--dry-run` 一律沿用已有映射（`load_existing_mapping`） |
| GitHub 二级限流（内容创建） | 403 `secondary rate limit`；脚本按 2/4/8/16/32 秒退避，5 次重试合计 62 秒必然再次撞墙，首轮白跑并丢掉 16 条 | 对二级限流单独等待 ≥60 秒；脚本幂等，直接重跑即补齐 |
| `gh` 的 token 缺 `workflow` scope | 推送 `.github/workflows/` 下的文件被拒（`refusing to allow an OAuth App to create or update workflow`） | **改走 SSH**：`git push git@github.com:LorenHan/LqCompare.git main` |
| 手写文档里的条目数会过期 | 加两条规格后，README/CHANGELOG/PRD/架构/交接 5 处仍写着旧的 367；没有任何机制会主动发现 | `check_spec.py` 新增 `check_doc_counts`，扫描手写 markdown 里的条目数并与实际比对 |
| 脚本用了 `mapfile` | macOS 自带 bash 3.2 没有这个内建，测试运行器直接崩（`mapfile: command not found`） | 改成可移植的 `while read` 循环；`run-tests.sh` 头部写明「不得用 bash 4 语法」 |
| 脚本用 GNU sed 的 `\+` | BSD sed 不支持，解析**静默失败**：每行显示「14 passed」而合计是 0，看起来还挺正常 | 改用 `[0-9][0-9]*`；这是最危险的一类——不报错，只给错数字 |
| 测试过滤器无匹配时仍报「全部套件通过」 | 套件改名或过滤器拼错 → CI 绿，但 0 个用例执行 | 无匹配视为失败并 `exit 2` |
| CI 只跑 Linux，却声称支持三平台 | bash 4 内建与 GNU 扩展在 Ubuntu 上全绿、到 macOS 崩，CI 发现不了 | 新增 `tools/check_shell.py` 静态护栏并接入 CI；`build.yml` 里写明这个覆盖盲区 |
| macOS 与 Linux 的 `stat` 时间字段名不同 | macOS 是 `st_mtimespec` / `st_atimespec`，Linux 是 `st_mtim` / `st_atim`。写 `st_mtim` 在 macOS 上编译失败 | 在 `filesystem_posix.cpp` 里用条件编译分别取名；**不要**用自定义映射抹平，那会变成「看起来一样、实际只有一边被测到」 |
| `AT_FDCWD` / `AT_SYMLINK_NOFOLLOW` 未声明 | 忘了 `#include <fcntl.h>`，只引 `<sys/stat.h>` 不够 | 补 `<fcntl.h>`；用 `utimensat` 时它和 `<sys/stat.h>` 都要引 |
| `QFile::decodeName` 没有 `(const char*, int)` 重载 | 从 `readlink` 的缓冲解码时报「no matching function」 | 先构造 `QByteArray(ptr, len)` 再传。**必须按实际长度截断**——`readlink` 不写结尾 `'\0'`，直接解整个缓冲会把未初始化内容读进来 |
| 把 `C:` 当成根目录 | `isRootPath("C:")` 写成期望 true，被用例纠正：`C:` 是「C 盘当前目录」，属**驱动器相对**路径；根是 `C:\` | 判根时必须要求盘符后跟分隔符。当成根会让「向上递归到根」提前停下，操作作用到完全不同的位置 |
| 接口把写操作标 `const`，实现却没标 | 派生类的非 const 方法与基类 const 声明不匹配，报「does not override」 | `setTimes` / `setAttributes` / `deleteToTrash` 在基类与所有实现里统一标 `const`——它们改的是**文件系统**这个外部状态，不是对象自身 |
| 测试替身要在 `const` 方法里改数据 | `const` 方法中 `QHash::find` 返回 const_iterator，赋值编译失败 | 把容器标为 `mutable`（与调用日志同理），并提供单个返回 `FileInfo*` 的 `findEntry() const` |
| 手写 Cocoa 错误码 `NSFileManagerUnmountBusyError` | 写成 768（从「Unmount 在 Busy 之前的直觉」排下来），真实值是 **769**；768 是 `NSFileManagerUnmountUnknownError`，含义完全不同 | `trash_mac.mm` 里的 `static_assert` 在编译期直接报出来。这是「把平台常量放在平台无关层」必须配套的补偿，与 Win32 那套做法一致 |
| 在 `FileSystem` 上留 `deleteToTrash` 便捷转发 | `FileSystem` 无状态，「撤销最近一次删除」的撤销点只能每次调用现场重建 → 撤销永远报「没有可还原的删除」。删除本身成功，只有撤销不工作，很难查 | 删掉接口，删除只在 `TrashService` 上。**不要**再加回来 |
| Windows 上 `FOF_ALLOWUNDO` 在网络盘被静默忽略 | 无回收站的位置上 `SHFileOperation` 会直接永久删除，**并且返回成功**——「删除成功」四个字背后是一次不可逆操作 | 可用性探测是唯一的安全闸门：先按驱动类型（`DRIVE_REMOTE` 等）判，再 `SHQueryRecycleBinW` 确认。绝不能靠失败反推 |
| 回收站搬移与写元数据的顺序 | 先写 `.trashinfo` 而搬移失败 → 回收站里留下指向不存在条目的幽灵记录；搬移成功而写元数据失败 → 文件在回收站里但还原不回去，用户以为删成功了 | 先 `rename` 再写 info；写失败必须把 rename 回滚 |
| 撤销时按原名拼回收站路径 | 回收站里已有同名条目时，macOS 与 XDG 都会自动改名（`a.txt` → `a 2.txt` / `a.txt.2`），拼出来的路径根本不存在 | 必须用实现返回的 `TrashedRecord.trashedPath`。测试替身刻意模拟了改名，让「按原名拼」的写法当场失败 |
| 撤销成功后没有清空撤销点 | 第二次点撤销会把已经回到原处的文件再挪进回收站，而用户以为自己只是在「取消上一次撤销」 | 全部还原成功后清空撤销点；部分失败时保留，让用户修掉障碍后重试 |
| 测试替身的可用性注入按精确路径匹配 | 注入 `/mnt/net` 后查询 `/mnt/net/a.txt` 查不到，静默落到默认答案「可用」→ 那些「不可用时不删」的测试其实在测「可用时删除」，且仍然是绿的 | 按祖先逐级查找（真实世界回收站可用性是按卷的）；`lookupByAncestor()` 里写明了这个失效方式 |
| XDG `.trashinfo` 的两处格式细节 | `Path` 必须百分号编码（含空格与中文的名字不编码会被解析方截断）；分隔符 `/` 要保留；`DeletionDate` 是**本地时间且不带时区后缀**（写成 UTC 或带 `Z` 部分实现解析失败） | 抽成 `xdgTrashInfoContents()` / `parseXdgTrashInfoPath()` 纯函数，在 macOS 上就往返测过了 |
| `shellapi.h` 必须在 `windows.h` 之后 | 反过来会因缺少基础类型而编译失败 | `trash_win.cpp` 里已按顺序 include 并注明 |
| 真实往返测试的清理守卫 | 断言失败会直接 `return`，后面的清理代码不执行 → 用户的废纸篓里留下测试造出来的垃圾文件。而**失败恰恰最容易发生** | 用 RAII 守卫（`TrashCleanupGuard` / `TrashFileRemover`）而不是顺序清理代码；守卫在两次危险操作**之间**声明，覆盖所有失败路径 |
| `QString::toUtf8()` 会把未配对代理换成 `?` | 承载原始字节的私存码位（U+DC80..DCFF）经 `toUtf8()` 变成 `3f`，原始字节**无声丢失**——返回值看起来完全正常，只是一段合法的 UTF-8 | 编码回字节一律用 `PathName::toNativeBytes()`。有一条专门的用例（`toUtf8WouldLoseRawBytes`）把这个差异钉死，有人换回 `toUtf8()` 会立刻失败 |
| `QString::fromUtf8` 无法区分「无效字节」与「真的是 U+FFFD」 | 它把无效字节替换成 U+FFFD，而 U+FFFD 本身是合法字符（`EF BF BD`）。于是「原文里有一个 U+FFFD」与「这里有个坏字节」从结果上完全一样，逆向编码时却要走不同分支 | UTF-8 校验必须自己写（`decodeUtf8Sequence`），按码位范围判定。几十行，换来判断能力 |
| UTF-8 **过长编码**不判会凭空产生 NUL | `C0 80` 是 U+0000 的过长编码。放行它会让解码结果里出现一个 NUL，而 NUL 在路径里是终止符语义——系统调用在那里截断 | 校验里必须有「码位 < 该长度的下界即拒绝」这一条；同理要拒绝代理区码位（与私存方案会撞车）与超过 U+10FFFF 的值 |
| `QString::normalized()` 保留未配对代理 | 这是**实测结论**（Qt 5.15.2：`DC80 0061 DCFF` 规范化后一个都不变），整套字节保真方案依赖它。属外部依赖，Qt 改了行为会导致原始字节静默变成 U+FFFD | 用 `normalizationKeepsRawBytes` 用例钉住，同时把实测过程写进注释——将来 Qt 升级后这条会失败，提醒重新评估 |
| 转义前缀被 `toUpper()` 一起大写 | 先 `.arg(...).toUpper()` 得到 `\XFF`。`\X` 不是任何语言认的转义写法，用户看到只会觉得这个界面输出的东西不能直接用 | 只大写十六进制部分：`QStringLiteral("\\x") + QString::number(v, 16).rightJustified(2, '0').toUpper()` |
| 静态护栏从不报错也没人发现 | 一个永远 `exit 0` 的检查脚本会让人以为这块已经被守住了，比没有护栏更糟 | 凡是静态检查脚本都要能自证会报错：`check_winapi.py --self-test` 用 17 个样本（含「注释里提到 ANSI API」这类**不该**报的）验证两边都对 |
| 护栏不认条件编译 | `filesystem.cpp` 在 `#ifdef Q_OS_WIN` 块里包含 `<windows.h>` 用 `static_assert` 核对手写常量——那正是刻意设计的护栏，却被「非 Windows 文件不得包含 Windows 头」这条判成违规 | 护栏里维护预处理条件栈，只对**不在 `Q_OS_WIN` 块里**的包含报错 |
| `SHFileOperationW` 的返回值不是 `GetLastError()` | 它返回 Shell 的 `DE_*` 系列（如 `0x7C` = `DE_INVALIDFILES`），拿它去查 Win32 错误码表会查出含义完全不同的东西 | 仍然把它当原始码留下来（能搜到 `DE_INVALIDFILES`），但在注释里写明来源；`rawErrorName()` 对 `DE_*` 返回 `nullptr`，界面显示成数字而不是编一个假名字 |
| `QFile::open` / `write` 失败后 `errno` 不可靠 | Qt 内部会调若干系统调用，失败时**不一定**把 `errno` 设成有意义的值，读到的是上一次调用残留的。一个「磁盘满」会被报成「没有权限」——比不给原始码更误导 | 进 `QFile` 之前先 `errno = 0`，之后只在 `errno != 0` 时才 `fromSystemError(errno)`，否则报一个明确不带原始码的 `Unknown`。PLAT-008 要的是「原始码**确实是这次失败的原因**」 |
| 两次失败分类相同就以为原因相同 | `EPERM`(1) 与 `EACCES`(13) 都归 `PermissionDenied`，但前者常是不可变标志或安全模块拦截、后者才是 `chmod` 能解决的。只留分类会让用户按错误的建议去改权限 | 出参改成 `ErrorCode`（分类 + 域 + 原始值），并在测试里专门用一条用例断言这两个码仍然可区分 |
| `QTest::toString` 的自定义版本必须写 `template <>` | 写成普通重载时，`QCOMPARE` 内部用的是 `toString<T>(x)` 这种带显式模板实参的调用，普通重载**不参与**重载决议 → 拿不到值，失败信息退回「Compared values are not the same」。而那个重载本身还能编译，看不出任何异常 | 一律写成 `template <> char *toString(const T &)`；每个套件里都有现成例子 |
| 派生类的重写声明不会继承基类的默认参数 | 默认实参只写在基类声明上（`ErrorCode *error = nullptr`），派生类重写时不再重复。于是拿**派生类的静态类型**调用 `fakeFileSystem.stat(path)` 会报「too few arguments」，而通过 `const FileSystem&` 调用却正常 | 测试里加一层接受基类引用的辅助函数（如 `isReadOnly(const FileSystem&, ...)`）——它顺带让同一段断言对真实实现与替身都适用 |
| **两套「三层」是同形不同义的** | 架构里同时有 `SettingScope`（视图/会话/类型，**覆盖**：只有一个胜出）与 `FilterLayer`（格式/会话/视图，**叠加**：全部生效）。用前者的读法取过滤声明——`ScopedSessionSettings::value("filter-declaration")`——只会拿到优先级最高（视图层）那一条，**会话层的过滤从此静默消失**。界面上两条设置看起来都在，用户只会觉得「会话级的过滤怎么不起作用」 | 过滤的三层声明一律**按存储分别读**（各层的存储里同一个键名），`FilterLayerBinder::loadInto()` 是唯一实现、不要绕过。`Tests/FilterStack` 有一条用例**同时**断言「作用域链确实只给一条」与「绑定器把三层都装进了栈」，理由写在 `filterstack.cpp` 顶部 |
| 一个 `.pri` 里有两份同样的源文件 | 两个模块互相 `include` 对方的 `.pri`（Session ↔ Filter）时，`mask.cpp` 会以两份 `SOURCES` 进到同一个 Makefile 里。**qmake 不会去重**——现象是重复符号，或同一份代码被编译两次 | 跨模块只加**搜索路径**（`exists(...): INCLUDEPATH += $$PWD/../X`），不 `include` 对方的 `.pri`。代价是单独构建该模块的测试工程要自己再 include 一次对方的 `.pri`（`Tests/Filter` 与 `Tests/FilterStack` 都照做了并在 `.pro` 里写了原因） |
| 拼表达式时重复包一层括号 | `joinAtoms()` 在多于一个原子时已经产出 `(a \|\| b)`，若取反时再包一层会得到 `!((a \|\| b))`——语义没错，但面板上看起来像程序拼错了，用户会怀疑整个表达式不可信 | 取反走一个 `negateExpression()`：已经在圆括号里的就直接前缀 `!`，否则才包一层。`Tests/FilterStack` 里 `expressionOfPureExcludesIsANegation` 钉住形状 |
| `qmake <路径> \| tail` 会把 qmake 的退出码吞掉 | 路径写错时 qmake 只印一行 `Cannot find file: …` 并**非零退出**，但管道给 `tail` 之后整条命令的退出码是 `tail` 的 0，于是 `&& make` 照跑；而 `make` 会用**上一次的 Makefile** 成功构建——看起来完全正常，只是新文件根本没编进去 | 构建命令写对路径（本仓是 `Code/LqCompare.pro`，不是仓库根的 `LqCompare.pro`）；确认新文件真的进了构建就看链接行里有没有 `<新文件>.o`。另外记住：**改了某个 `.pri` 之后不必手工重跑 qmake**——qmake 生成的 Makefile 把 `.pri` 列为自身依赖，`make` 会自动重跑（且用的是 Makefile 里记着的正确路径） |
| 启动自检不可反向验证 | `validateFilterLayerTable()` 若直接吃固定表，测试就拿不到「故意写坏的表」，这条护栏永远不会红——而它查的恰恰是「写错了也没有任何运行期现象」的几件事，最需要被验证。同理，只断言「问题列表非空」也不够：一个把所有输入都判成有问题的实现同样能过 | 把**表当参数**传进来（默认值给真实表），测试就能拿一份写坏的表跑同一个判定；且断言**问题的内容**（「视图层的落点」/「2 次」/「实际 4 行」）而不只是长度。与 `check_winapi.py --self-test` 同一条纪律 |
| 用「重试后全部成功」验证「只重试失败项」 | 一个「整批重跑」的实现同样会全部成功，断言照样通过——而这正是 PLAT-008 第 2 条要防的（对批量复制就是覆盖用户刚确认过的结果） | 断言必须落在**调用次数**上：`callsFor(成功路径) == 1`、`callsFor(失败路径) == 2`。替身记录调用日志就是为了这个 |
| 同一件事的两种错误文案实现 | `filesystem_win.cpp` 里原本自己拼 `"Win32 错误码 %1（%2）"`，与新的 `errorDetail()` 是同一件事的两份实现；两份必然演化成界面上同时出现「Win32 错误码 32（busy）」与「Win32 32（ERROR_SHARING_VIOLATION）」，用户以为是两个不同故障 | 删掉自拼的那份（它当时还没有任何调用点），统一用 `errorDetail(fromWindowsError(code))` |
| 给认不出的错误码编一个「名字」 | 拼出 `UNKNOWN_1234` 这类字符串会让用户拿一个根本不存在的符号去搜，比只看到数字更糟；同理「原始错误码：（无）」也只是噪声 | `rawErrorName()` 认不出就返回 `nullptr`；`errorDetail()` 在没有原始码时返回**空串**，由界面决定不显示这一段 |
| `QImage(uchar*, w, h, bpr, fmt)` 是**浅引用** | 它不接管那块内存。用完就 `release` 源缓冲（如 `NSBitmapImageRep`）之后，QImage 指向已释放的内存——表现为「图标偶尔是花的」，只在内存被复用时出现，且几乎不可复现 | 立刻 `.copy()` 一份再释放源；`iconservice_mac.mm` 里写明了这一条。**任何**用这个构造函数包外部缓冲的地方都要这样处理 |
| `NSBitmapImageRep` 的像素格式要配 `Format_RGBA8888_Premultiplied` | 配成 `Format_ARGB32_Premultiplied`（直觉上「更 Qt」）会在小端机器上把通道读反，图标变成「蓝脸」。而灰度图标上完全看不出来 | 用 `RGBA8888` 与 NSBitmapImageRep 的内存布局逐字节对应。这类错误要靠**彩色**测试样本才能发现 |
| 后台线程里用 AppKit 不开 `@autoreleasepool` | 图标解析跑在 `QThreadPool` 的线程上，Qt 不会为它建池。`NSWorkspace` 返回的自动释放对象一直不释放，控制台有抱怨，表现为「滚动大目录时内存一直涨」 | 解析函数体整体包在 `@autoreleasepool { }` 里 |
| `UTType` 要 macOS 11+，部署目标却是 10.13 | 编译器为函数体内每个 `UTType` 各报一条 `-Wunguarded-availability-new`（本次 7 条）。`@available` 检查明明写在调用点，编译器却看不到——因为用的是**独立函数**，警告报在函数体内 | 给该函数加 `API_AVAILABLE(macos(11.0))`，把契约写进签名：漏检查时报错落在**调用行**。**不要**用 `#pragma clang diagnostic ignored` 压掉——那会连同「调用点漏检查」一起静音，而后果是用户在 10.15 上点一下列表就崩 |
| `SHGetFileInfo` 默认会去**访问磁盘** | 对没有对应文件的名字（枚举一个还没访问过的目录）它会去实际查找，网络盘上一次卡几百毫秒、U 盘没插时直接失败。而这里问的只是「`.cpp` 该长什么样」 | 加 `SHGFI_USEFILEATTRIBUTES` 只按名字问；Linux 侧同理用 `QMimeDatabase::MatchExtension` 而不是 `MatchDefault` |
| `SHGetFileInfo` 只能给 16/32/48/256 几档 | 请求 20 会拿到 32。把「接近的尺寸」当成「就是这个尺寸」写进结果，调用方会以为拿到了精确尺寸，拼高 DPI 图集时用错比例 | 如实报出 `actualPixelSize`；需要精确尺寸的地方自己再缩一次。Windows 侧 48/256 还需要 `IImageList` COM，本次没做，代码里已注明 |
| 用 `QCache` 当图标缓存 | 它的淘汰策略**没有对外契约**（文档只说「某条策略」），于是「缓存上限 256 个类型」这个约束无法写测试，也无法预测谁被淘汰 | 自己写有界 LRU，带 `keysByRecency()` 与 `Stats`，淘汰行为可断言。图标缓存总量本来就不大（上界是扩展名数），几十行的代价换来确定性 |
| 图标缓存键只写扩展名 | `notes.txt` 这个**目录**会拿到文本文件图标——因为「是否目录」没进键。这类错误只在「目录名带扩展名」时出现，测试里如果只用 `foo.txt` 这种文件样本，永远发现不了 | 键形如 `f|txt` / `d|<dir>`，把两个事实都编进去；并专门写一条「目录与同名扩展名的文件拿到不同格子」的用例 |
| 缓存命中时也走异步发信号 | 命中率高的目录（全是 `.cpp`）里绝大多数条目本来能同步拿到图，一律异步会让滚动时可见闪烁：先放占位、几十毫秒后换真图 | 命中就同步返回并同步发信号；只有未命中才排后台 |
| 服务里维护「哪些路径引用了这个键」 | 一次解析产出一个**类型**的图标，可能 200 行在等它。按路径发信号就要维护这份映射，即第二份事实来源，且随视图增删要同步改——改漏了就是「部分行永远不刷新」 | 信号只带 `cacheKey`，视图按需重查。列表项自己记住 cacheKey，刷新是 O(1) 哈希查找 |
| 图标请求不去重 | 一次解析的代价与「多少文件引用这个类型」无关，却与「多少文件」成正比。滚动一个全 `.cpp` 的目录就是几百次系统调用换回同一张图 | `IconRequestQueue` 按缓存键去重，把在途请求数从文件数压到扩展名数 |
| 让缓存层直接吃 `QIcon` | 缓存淘汰顺序、去重、命中统计是最容易写错的部分，而一旦它们依赖 `QIcon`，测试就得有一个能跑图形栈的环境 | `IconEntry::payload` 用不透明的 `QVariant`：生产放 `QIcon`，测试放 `QString`。于是这批逻辑能在只链接 QtCore 的套件里完整覆盖。代价是 `usable()` 只回答「有没有」，所以真实图标源另有三条走真机的用例 |
| 图标解析与其他后台工作共用线程池 | 一个耗时任务（枚举大目录、读压缩包）会把所有图标请求排到它后面，界面上表现为「整个列表都不出图标」——用户会以为是图标功能坏了，而不是「有个任务在跑」 | `IconService` 用**专属**的单线程 `QThreadPool`；池容量设 1 也顺便免除「提供者实现各自考虑并发」的负担 |
| 对真实系统图标源做「一定不是回退图标」的断言 | macOS 对未知扩展名（`.zzzznope`）会给**通用文档图标**，来源是 `System` 而不是 `Builtin`——与 Finder 的行为一致，这是正确行为。断言 `hasFallback() == true` 会失败，而看起来像代码有 bug | 按来源分支断言：`System` 时断言 `usable()` 且 `actualPixelSize > 0`；`Builtin` 时才断言 `hasFallback()`。**教训**：对「外部系统会怎么回答」的断言，先确认外部系统的真实行为，不要按自己的直觉写期望值 |
| `check_spec.py` 的文档计数护栏会误报「N 个条目」这种口语 | 我在架构文档里用「几千文件的目录会占几千格」举例时，最初写成了「N 个条目」的形状（数字紧跟「个条目」），护栏直接报错；改完这行**引用它的坑表本身**又踩了第二次——本轮写 FILT-001 的分组说明时第三次踩到，因为「三组（N 条）」与「（N 条，每条带样本）」都是同样的形状 | 该护栏是刻意宽进严出的（宁可误报也不漏报过期数字）。**它的模式不止「个条目」与「条规格」，写文档前先看一眼 `check_spec.py` 的 `patterns`**：数字紧跟「个条目」、数字紧跟「条规格」、以及两种括号形状（数字 + 条 + 右括号、数字 + 条 + 逗号）；最后还有一条针对表格里的「合计」行。所以数量一律写成「33 个用例」「这 21 行」这种形状，**不要在括号里用「数字 + 条」**。反过来也不要把这些模式改窄：它们正是靠宽匹配才抓到了 5 处真正的过期数字 |
| `git push --force-with-lease` 在本仓永远报 `stale info` | `origin` 是 https URL，推送走的是 SSH URL，两者是不同远端；git 找不到对应的 remote-tracking 引用，无法核对租约，于是保守拒绝。**报错信息读起来像「远端被别人推过」**，会被误判成协作冲突而不敢继续 | 显式写期望值：`git push --force-with-lease=main:<远端当前提交> git@github.com:LorenHan/LqCompare.git main`，前置一次 `git fetch origin`。更根本的做法是能不强推就不强推——先回填提交号再推 |
| 32 位进程写注册表却不加 `KEY_WOW64_64KEY` | 交付目标是 32 位 MinGW 构建。默认视图下写入会落到 `WOW6432Node` 影子副本，而 64 位资源管理器**看不见**那里。现象是安装、校验、卸载全都报成功，只有右键菜单「没有变化」——用户唯一能看到的证据就是什么都没发生 | 全部注册表访问固定带 `KEY_WOW64_64KEY`。判定依据不是「我们的程序多宽」，而是「谁要读它」——读它的是 64 位资源管理器 |
| 用「某个值一直写不进去」的注入测回滚 | `failOnValue()` 把该值的**删除**也一起挡住，于是回滚必然也失败，用例只留下一句「回滚未完全成功」。而「回滚逻辑写错了」与「注册表真的删不掉」从结果上完全分不开，这个用例等于什么都没测 | 另加一次性注入 `failNextWriteOnValue()`，只挡这一次写入、不影响删除。真实世界的写入失败大多是瞬时的（被杀毒软件短暂锁住），这才是想模拟的那一类。断言才能落在 `rollbackClean` 上 |
| 把 `QStringLiteral` 写进 `= {…}` 初始化列表 | 报一句**与真实原因毫无关系**的 `use of overloaded operator '=' is ambiguous (with operand types 'QStringList' and 'void')`。成因：Qt 5.15 的 `QStringLiteral` 展开成一个**含 `static` 局部变量**的 lambda，出现在 `= {…}` 里时 clang 会把整个列表判成 `void`（C++17 起列表元素的类型不必一致，于是推导出来的是 `void`，而 `QStringList` 没有接受 `void` 的赋值运算符） | 写 `QStringList{QStringLiteral("a"), QStringLiteral("b")}`，让元素类型由显式类型 + 花括号推导定下来。本仓里凡是要塞进容器的 `QStringLiteral` 都照这个写；`attributefilter.cpp` 的条件表上方有一行注释专门记这件事 |
| 中文单位认不出来：`const char *` + `QLatin1String` | 「字节」这种单位写成 `const char *name` 再用 `QLatin1String(row.name)` 比较时，UTF-8 字节被当成 Latin-1 逐个字符解释，**永远不相等**。现象是 `formatSizeText()` 的输出 `1.00 KB` 能解析回来、而 `0 字节` 解析不了——自己的输出自己认不出 | 表里的名字用 `QString`，比较写成 `unitText == row.name`。附带一条更宽的原则：**凡是可能含非 ASCII 的字面量表，字段别用 `const char *`** |
| 大小单位只收 `[A-Za-z]`，中文单位收不进来 | 与上一条同源但独立：正则 `([A-Za-z]*)` 直接把「字节」挡在门外，且失败方式是「解析失败 → 条件不生效 → 放行全部」。看起来过滤在工作，实际什么都没过滤 | 单位部分放宽到 `(\S*)` 再查表；**表里没有的单位报错**，不要静默当字节。放宽的代价是 `1 xyz` 会进到查表分支并报错——这正是想要的行为 |
| 只填日期的**上限**按 `00:00` 比较 | 用户写 `2026-09-10` 指的是「含这一天」。按 `00:00` 比较会把这一天下午 3 点改过的文件全排除掉，而用户看到的是「我明明加了到今天，它却少了一批」 | 解析结果带一个 `dateOnly` 标志，上限是它时扩到**当天最后一刻**。且要用**同一天的 `23:59:59`** 构造，不要 `addSecs(86399)`——夏令时切换的那一天有 25 小时，加固定秒数会跨错一个小时 |
| 相对天数用正则解析前后缀 | 正则的最左匹配会把 `7days` 的 `days` 当成前缀（从位置 0 起匹配），剩下一个空数字 → 报「没有数字」。而 `7days` 恰恰是最常见的写法之一 | 用两张显式的表：前缀（`最近`/`过去`/`近`）与后缀（`天内`/`日内`/`天`/`日`/`days`/`day`/`d`，**长串优先**）。表是谁都看得懂的，报错时也能说出「后缀不认识」而不是「语法错误」 |
| 变异测试脚本用 `shutil.copy2` 还原源文件 | `copy2` 会把**旧的 mtime 一起还原**。于是 `make` 认为被改过的那个 `.o` 还是最新的，**不会重新编译**，下一轮跑的还是上一轮被变异的二进制。现象是「下一处变异莫名其妙地被检出/漏检」或「某种子虚乌有的失败」，而源文件看起来完全正常——本次就因此凭空造出一个 `declarationRejectsConflictingTimeKeys` 失败 | 每轮**改之前**与**还原之后**都要删掉目标 `.o`（本次的 `drop_objects()`），不要依赖 mtime 判断。这条与「删掉 `.o` 再验证变异」是同一件事的两半——少做一半，另一半也不会生效 |
| 变异脚本的锚点在文件里不唯一 | 拿 `{"k", 1024ULL},` 当锚点，而这一行在前一轮已经被改写成 `QStringLiteral("k")`，替换静默失败，于是那一处「变异」其实什么都没改，报漏检。同类问题还有「锚点指向的分支根本不可达」（`claimFamilyKey` 的重复键分支：大小键走的是**另一处**重复检查） | 每处变异替换后**断言替换确实发生了**（计数或 `!=` 原串）；锚点要挑唯一且可达的分支。不可达的分支不值得变异，应该改成对可 reach 的那个分支做变异 |
| 失败记录只打印「错误分类」 | 报告写成「失败 ……：找不到 该路径」，用户看不出找不到的是**哪一项**、期望它是什么值。而「缺失」与「值不对」的处置不同（一个要补、一个要改），全部区分信息都在 `detail` 里，却被丢掉了 | `ShellChangeRecord::describe()` 失败分支必须带上 `detail`。**凡是「成功/失败」两类共用一条格式化路径的地方，都要检查失败那一路有没有把诊断信息丢掉** |
| 登记子树里的下标键不在静态计划里 | `Backup\1`、`Backup\2` 是运行时生成的，卸载时按静态计划「键下有别人的内容就保留」会把这些备份键误判成**外来内容**从而拒绝删除 —— 卸载报「残留」，而用户什么都没做错 | 登记子树是唯一允许递归删除的地方（`removeKey` 整棵删）。回滚与卸载共用同一份 `removeInstallation()`，两处都写明了这个例外 |
| 「重新配置」被实现成「再叠一层」 | 用户取消勾选某项后再点安装，旧选项留下的注册表项仍在，而卸载的依据（登记里的选项）已经变成新选项——那些项永不被删，**报告却说「残留检查通过」**。这类错误只在「改过选项」时才出现 | 安装的步骤 0：已安装则先按**记录的**选项 `removeInstallation()` 拆掉再装；拆不干净就中止。凡是「安装记录 + 当前配置」两处状态的系统，都要先想清楚改配置时旧记录谁来负责 |
| `QString::SkipEmptyParts` | Qt 5.15 起弃用（`-Wdeprecated-declarations`），Qt 6 里被删。它在**只在 Windows 编译**的文件里不会在本机报出来，于是主程序 0 warning 而 Windows 构建会多 4 条 | 一律写 `Qt::SkipEmptyParts`（行为相同，两个大版本都不报警告）。**没被编译过的平台文件也要跟着改**，否则「0 warning」这个结论只对本机成立 |
| 用 `key.contains("\\shell\\")` 判「这是不是右键菜单项」 | 文件关联侧的命令键（`Software\Classes\LqCompare.PatchFile\shell\open\command`）同样含 `\shell\`，但它挂在 **ProgID** 下，不是加在右键菜单里的项。用它判会导致「关掉右键菜单」这条断言必然失败，而失败原因与它想验证的事实无关 | 按**挂在哪个类键下**判：菜单项挂在 `*` / `Directory` / `Directory\Background` 三个目标类下，关联挂在 ProgID 下。测试里把 `menuPrefix(target)` 提成辅助函数，让「菜单项」这个概念只有一个定义 |
| 用动词键去查命令条目 | 菜单文字挂在 `...\shell\LqCompare.compare`，命令挂在它**下面一层**的 `...\shell\command`。用前者调 `entriesFor()` 一条命令都取不到，断言 `foundCommand` 永远为假 | 命令条目要按 `verbKey + "\\shell\\command"` 查。测试里配了 `commandKey()` 辅助函数，避免第二次写错 |
| 「值名/键名大小写不敏感」被顺手做成了「存储也转小写」 | 归一后直接拿去创建键，会把 `LqCompare.DiffFile` 写成 `lqcompare.difffile`。功能上没问题，但用 regedit 打开时看起来像随手敲的乱码，下一个人会以为这是 bug 而去「修」它 | **归一仅用于比较，存储保留原拼法**。真实实现与内存实现都按这条写，且各有一条断言拼法的用例 |
| 认不出的注册表值类型被当成「没有值」 | 只记「有个值」而不记类型与字节，备份就等于记成「本来没有值」；卸载时会把用户原本那个我们看不懂的值**删掉**——这是不可逆的数据丢失，而报告会说「已还原」 | `RegistryValueKind::Unsupported` 把原始类型码与字节一起带上，`operator==` 逐字节比较；用例断言往返后字节完全一致。**凡是「读旧值 → 覆盖 → 还原」的流程，都要先问「我看不懂的旧值会怎样」** |
| `--log-level` 原先**只对宏生效、对直接调用无效** | `logging.h` 的 `LQCOMPARE_*` 宏会先比级别再调 `write()`，但 `Log::write()` 自己完全不过滤。`main.cpp` 里有 5 处直接调 `Log::write(...)`（含启动横幅），于是 `--log-level error` 下它们照样打印，日志文件也没法靠调级别瘦身——与 `logging.h` 写的「低于该级别的日志被丢弃」相反 | 过滤收进 `isEnabled()`，宏、`write()`、`Stopwatch` 三处共用它；`main.cpp` 的 5 处改成宏。**注意副作用**：默认级别是 `warning` 而启动横幅是 `info`，所以不带参数启动时日志里不再有启动横幅。想改回去要调**默认级别**（OPT-010），不是让 `write()` 不过滤 |
| 级别过滤写在多处 | 宏、`write()`、`Stopwatch` 各自写一遍 `<=` 比较的话，把方向或级别顺序弄反只会发生在其中一处，而现象是「某个级别偶发不输出」——这种不一致最难查 | 比较只有 `isEnabled()` 一份实现。加新级别时也只改一处 |
| 日志接收者在记录日志的那个线程上被调用 | 界面输出面板若直接挂成接收者，图标解析（`IconService` 的后台线程）记一条日志时就会从非 GUI 线程碰控件——崩溃位置与「我只是接了个日志」看起来毫无关系 | 面板必须自己加一次排队跳转（接收者只 `emit` 信号，用 `Qt::QueuedConnection` 连槽）。`Tests/Logging` 里有一条跨线程用例把这个事实钉住 |
| 在持锁期间调用日志接收者 | 接收者里顺手记一条调试日志（很自然的写法）会**死锁**，现象是「界面卡住」，与日志模块毫不相干；接收者在回调里增删接收者还会让容器在遍历中变动 | 进临界区前把接收者清单**拷贝**一份，出临界区再调用。代价是并发移除时那个接收者可能还收到这一条——比死锁好得多 |
| 日志接收者的句柄用函数对象当键 | lambda 之间没有可靠的相等比较，按值 `remove` 会**静默失效**——接收者以为自己被摘掉了，其实还在收 | `addSink()` 返回整数句柄，`removeSink(handle)` 按句柄移除。传空函数对象时返回 **0**（无效句柄）：空 `std::function` 不是「什么都不做的接收者」，而是调用时崩溃 |
| `setLogFile()` 会创建一个空文件 | 它要探一次可写性（以追加方式打开再关掉），所以「只配了路径、还没写日志」时文件已经存在。用例若断言「文件不存在」会失败，而失败原因是自己理解错了契约 | 断言应该是「关掉输出之后**不再写入**」（文件大小仍为 0），而不是「文件被删掉」——关掉输出不等于丢掉用户已有的日志。这个代价是刻意付的：宁可配置的那一刻就知道写不进去 |
| 日志文本格式里线程名的可选段 | 线程名只有主线程之外少数情况才有。直接拼 `[t:<id> <name>]` 时，名字为空会在行里留一个孤立空格，按空格切分日志的工具会多切出一段空字段 | 名字为空时整段不输出（`[t:<id>]` 与 `[t:<id> <name>]` 两种形态）。定宽短名（`"INFO "` / `"WARN "` 都占 5 格）则是为了级别列之后的内容能对齐——日志是给人竖着扫的 |
| 耗时辅助用 `start()` / `stop()` 两个调用 | 中途 `return`、抛异常、或忘了写 `stop()` 的路径都会漏记——而漏记的那条恰恰最可能是「为什么这里有时很慢」的答案 | 改成 RAII：作用域开头构造一个 `Log::Stopwatch`，离开作用域自动记一条。`finish()` 可重复调用（手动结束过就只记一条）；**级别在析构时判断**，这样「先放计时器、再用命令行调级别」也能出结果 |
| 日志套件链接 QtGui | 别的套件都是 `QT += gui`（`QKeySequence` 属于 QtGui）。日志只用 QtCore，若也跟着加，等哪天有人往 `logging.cpp` 里加图形依赖就没人会发现 | `LoggingTests.pro` 写 `QT -= gui`，让「混进 QtGui 依赖」变成构建失败。这也是 `QTEST_MAIN` 在这里展开成 `QCoreApplication` 的原因（无需 offscreen 平台） |
| `\` 转义用「不认识的转义就原样保留」这种宽容处理 | Windows 用户把路径分隔符敲进掩码（`build\out`）时，它会被静默解释成 `buildout`——过滤看起来生效了、只是漏了一批文件，而这类偏差在界面上完全无法自查 | 转义白名单是**封闭**的（`* ? [ ] - # \`），其余一律报错，并且提示里明确写「如果这是 Windows 路径分隔符，请改写成 `/`」。宁可报错，也不要给出一个看起来生效的过滤器 |
| 掩码里的分隔符跟着平台走 | 会让「预设库导出给团队共享」（FILT-007）带上平台色彩：`build/out` 在一台机器上排除子目录、在另一台上排除一个名字里带 `/` 的条目 | 掩码里 `/` 恒为分隔符、`\` 恒为转义，与平台无关。顺带让「`*` / `?` / 字符集都不可能吃掉 `/`」成为结构性事实——段是按 `/` 切出来的，段里根本没有 `/` |
| 只按 `\n` 切过滤声明的行 | Windows 上编辑过的预设文件是 CRLF，于是每行末尾多一个 `\r`，`*.tmp` 悄悄变成 `*.tmp\r`——**静静地对不上任何文件**，而掩码本身看起来完美无缺 | 切行同时认 `\n`、`\r\n`、`\r`（`\r\n` 算一个换行，否则空行的行号会整体偏大）。注意修复层在**声明解析**而不是掩码解析：行尾是文本文件的属性，不是掩码语言的属性 |
| 大小写不敏感时把区间端点也折叠 | `[A-_]` 折叠后是 `a`..`_`，而 `a`(U+0061) 比 `_`(U+005F) 大——区间**反了**，这个字符集从此永远匹配不到任何东西，表面上却一切正常 | 不折端点，改成「拿反转大小写的字符再试一遍」。这样 `[a-z]` 命中 `A`、`[A-Z]` 命中 `a` 都自然成立，也不必为区间维护两套边界 |
| 段级匹配写成递归回溯 | `**` 每一步都有「吃零段」与「吃一段」两个选择，`**/**/**/…` 对上有 N 段的路径时有 2^N 条路径。一个手抖敲出来的掩码就足以让扫描停在那里不动，而现象是「程序卡死」而不是「结果不对」 | 用「可达掩码段」表做 NFA 模拟，复杂度 O(路径段数 × 掩码段数)。`**` 的 ε 闭包只朝后传播，所以一次顺序扫描就到不动点，不需要反复迭代 |
| `**` 的语义不定就开写 | 不定下来的话，`a**b` 与 `a*b` 的行为没有任何可预期的区别，用户只能靠试；而两种实现都能自圆其说，谁也不会发现自己在改别人的规则 | 明确「`**` 只有**独占一段**时才跨目录，段内等同于 `*`」（gitignore / ant / ripgrep 的共同规则），并把它写进速查表——文档与行为同源，改一头另一头会红 |
| 不含 `/` 的掩码直接拿整条相对路径匹配 | `*.txt` 对不上 `src/a.txt`（因为 `*` 不跨 `/`），而「文件掩码」在所有人心里都是「任意目录下的 .txt」——用户会认为掩码功能坏了 | 由**掩码自己**决定按哪一侧匹配：不含 `/` 按名字匹配，含 `/` 按相对路径从起点匹配。这样 `*.txt` 处处生效、`src/*.txt` 又不会误伤别处的同名子树 |
| 把「速查表」写成 markdown 表格 | 文档里的示例与实现必然在某次修改后分家，而错误方式是「帮助里说 `[!a]` 是取反、程序其实不认」这种用户完全无法自查的偏差 | 速查表做成**数据**（`maskSyntaxReference()`），每条带可执行样本，测试逐条跑一遍；纯文本速查由数据生成。FILT-011 的「文档与测试语料同源」由此变成一条会红的用例 |
| 未闭合的 `[` 当成字面量 | 用户把 `[abc` 漏掉一个 `]` 时，掩码会静静变成「匹配字符串 `[abc`」——过滤看起来还在工作，只是永远不命中，界面上完全看不出问题 | 报错并给出列号与建议（补 `]`，或写成 `\[`）。「永远匹配不到任何东西」的掩码要和语法错误一样被拦下来 |
| Qt 5.15 的 `QRegularExpression` **没有**匹配超时 | FILT-002 第 2 条要求「正则匹配 200ms/条超时保护」，而 `setMatchTimeout()` / `matchTimeout()` 是 **Qt 6.0** 才加的（已在本机 `qregularexpression.h` 里核对）。照直觉把它当成「顺手加个参数」会做到一半才发现做不到 | 开 FILT-002 之前先定路线：走工作线程 + 截止时间（超时就放弃那一条并记错，注意线程无法真正杀掉），或直接调 PCRE2 的 match limit。两条都不是小活 |
| 声明的槽/构造函数没有实现，失败发生在**链接**期 | `moc_tst_filter.o` 引用了 `TstFilter::hiddenIsTotalMinusIncluded()`，报错是 `Undefined symbols` 且指向 moc 生成的文件，看起来像 moc 出了问题而不是「少写了一个函数」；`Mask::Mask()` 只声明未定义同理 | 头里声明的槽必须在 .cpp 里有实现；构造函数声明了就要定义（或写 `= default`）。看到 `Undefined symbols ... referenced from ... moc_*.o` 时，第一个要查的就是「哪个声明漏了实现」 |
| 报告「0 warning」时不说范围 | `_build-lqcompare` 是增量构建，看到的是 0 条；一旦 `make clean` 重编，第三方 LqRibbon 会冒出 `LqRibbon.cpp: unused function 'nativeWindowScaleFactor'`。于是「0 warning」这个结论到底指什么就说不清了 | 说「本仓库自己的代码 0 warning」，并注明第三方那一条不属于本仓（它在 MyClass 仓库里）。清过构建目录之后要重新数一遍 |
| 在构造函数里调虚函数（`createSettings()` 这类工厂） | 构造函数期间虚函数只派发到**基类**版本，子类的覆写永远不生效。现象是「设置改了不生效」——看起来像设置没保存，其实是工厂根本没被换掉 | 惰性构造：第一次 `sessionSettings()` 时才调工厂。`Tests/Session` 有一条用例把这件事钉住：「构造之后工厂调用次数为 0，首次取用时恰好 1 次」 |
| 会话基类用裸指针记住视图 | 容器删标签时会连带析构视图，而裸指针仍然指着那块内存。下一次 `createWidget()` 把一个已析构的对象交给界面，崩溃位置与真正的错误毫不相干 | 用 `QPointer`：视图被析构时它自动变成 `nullptr`，于是「视图不在了」成为可判定的状态，可以就地重建。有一条用例专门删掉容器再断言重建（`widgetFollowsTheContainersLifetime`） |
| 「基类不得依赖具体视图」只靠代码评审 | 这类违规是「加一行 `include`」级别的改动，评审极易漏掉；而更糟的是**主构建编得过**（它的 INCLUDEPATH 里有 `Views/Shell`、`Views/Page`），于是没有任何东西会红 | 把**测试工程**的 INCLUDEPATH 收敛到只剩本模块需要的两个目录（`Views/Session` 与 `Services/Session`），违规立刻变成测试工程构建失败；再由一条源码级用例做第二层（主构建的 CI 也会跑到它）。**注意这个办法的前提是测试工程不共享主构建的 INCLUDEPATH** |
| 自定义结构体/枚举只用于同线程直连 | 忘了 `qRegisterMetaType` 时 `QObject::connect` 只在**运行期**抱怨一句，编译期什么都看不出来；而进度上报迟早会来自后台线程，那时的现象是「进度条一直不动」 | 在会话构造时用函数内静态做一次性注册，并写一条用例断言 `QMetaType::type("…") != QMetaType::UnknownType`。凡是进信号的结构体都按这条办 |
| 用脚本做变异测试时，复原源文件后 `make` 未必重建 | 观察到「复原之后那些变异用例仍然红」，看起来像代码没改回来；其实只是构建产物没更新（时间戳判定），于是很容易得出「用例不能反向验证」的错误结论 | 复原之后显式 `touch` 一次被测文件（或删掉对应 `.o`）再 `make`，并在宣布「全绿」之前确认那次构建**真的编译了**。变异测试的结论只在「每一轮都确认过构建确实发生」时才可信 |
| 交接文档里「扫描 N 个源文件」这类数字会随新增模块过期 | `check_winapi.py` 数的是 `Code/` 下全部源文件（含 `Tests/`），新加一个套件这个数字就变一次。本轮它从 71 变成了 79，而文档里还写着 63——没有任何机制会主动发现 | 每次跑护栏时把输出里的数字与 §2 的表格核对一遍；`check_spec.py` 的文档计数护栏只管规格条目数，管不到这些 |
| `setDirty(false)` / `setStatusText()` 这类「设成某个值」的入口不去重 | 界面常见的写法是「按当前状态重写一遍」，每次都发信号会让状态栏在批量过程中反复重排、标签上的「*」反复重绘，看起来在抖。而这类抖动很难归因到某一次赋值 | 「值没变就不发信号」写在这几个入口里（与 `MemorySessionSettings` 的后三个入口同一条纪律）；错误是例外——它是**事件**而不是状态，同一个原因连报两次要收到两条 |
| `check_icons.py` 的引用正则**会扫注释** | 我在注释里写下一个完整的资源路径字面量（`":/Pictures/xxx.svg"` 的形状），护栏立刻报「代码引用了它，但 `Pictures.qrc` 未声明」。它认的是**形状**，不区分这行是代码还是注释 | 注释里要举例就写成 `:Pictures/<名字>.svg`（尖括号占位，不构成一个真实引用）。同理，测试里要**故意造一个不存在的图标键**时，必须把它拆成三段拼接——否则那个「假」键会被护栏当成真的 |
| 测试头文件只 `#include <QObject>` | 首次构建一次性报出三个 `no function template matches function template specialization 'toString'` 加几十行 `use of undeclared identifier 'QVERIFY2'/'QCOMPARE'/'QFAIL'`，看起来像是整个 QtTest 没接上 | 测试头文件里写 `#include <QtTest>`（既有套件都是这么写的，照抄即可）。**报错条数与真实原因严重不成比例**，见到这种规模的「未声明」先怀疑头文件而不是语法 |
| 用脚本做变异测试时只构建/运行了**一个**套件 | 变异明明生效、目标用例也确实会红，脚本却报「没检出」——因为那条用例住在**另一个**套件里。第一版就因此把三处变异误判成「用例不能反向验证」 | 变异测试脚本必须显式列出**所有**可能覆盖该变异的套件并逐个跑。另外单独复验一次那一条变异（打印「变异生效：True 出现次数：1」并肉眼确认 FAIL），别让脚本的结论单独决定「用例没写到位」 |
| 把源文件改回原样后紧接着写下一轮变异 | 与上面那条**构建时序**坑叠加：`finally` 里刚 `copy` 回来，下一轮立刻写入并 `make`，时间戳粒度不够时 `make` 判定「没有变化」→ 这条变异其实没被编译进去 → 报「没检出」 | 每轮之间强制重建（`make clean`，或至少 `os.utime` 一下被测文件 + 确认那次构建**真的编译了**）。这两条一起看：**变异测试的结论只在「每一轮都确认过构建确实发生」时才可信** |
| 变异测试脚本累积删除文件触发沙箱护栏 | 脚本跑到一半被 `[safe-delete][SAFE_DELETE_BULK_CONFIRM_REQUIRED]` 打断（本轮累计 97 次、阈值 50），看起来像脚本写崩了 | 这不是脚本的问题，是每次 `os.remove(...bak)` 都在计数。**要确认的是「源文件是否已复原」**：把复原放在 `finally` 里**先 copy 再 remove**，这样中途被打断也是干净的；打断后手工核一遍 `find Code -name "*.bak"`、`git status` 与关键内容计数 |
| `session.pri` 里嵌套 `include(../Filter/filter.pri)` | `services.pri` 已经把 Session 与 Filter 两个 `.pri` 各 include 一遍（各自在 `exists()` 保护下），再嵌套一次会让 `mask.cpp` 以**两份 SOURCES** 进同一个 Makefile。qmake 不去重——现象是重复符号或同一份代码编两次 | 子模块的 `.pri` 只加**搜索路径**（`exists($$PWD/../Filter/mask.h): INCLUDEPATH += $$PWD/../Filter`），不嵌套 include 兄弟模块的 `.pri`。代价是单独构建该模块的工程要自己再 include 一次，把它写在 `.pri` 的注释里 |
| 服务层需要「创建界面层对象」的工厂 | 注册表在 `Services/Session/`，而工厂返回的 `CompareSession` 定义在 `Views/Session/`。直接 `#include` 界面头会撞上 `check_layering.py`；退一步写成 `QObject *` 又丢掉了类型安全，且让「制造一个会话」这件事在类型上无从检查 | **只前向声明** `class CompareSession;` + `std::function<CompareSession *(QObject *)>`。前向声明不会被分层检查的 include 正则匹配，于是既守住分层又保住类型安全。顺带的收益很大：`Tests/SessionType` 因此是**纯 QtCore** 的套件（`QT -= gui`），本仓库第一次有「服务层的会话框架测试」 |
| `validate()` 里重查 `add()` 已经把住的规则 | ID 格式、ID 重复、显示名为空、掩码编译失败都是 `add()` 就会拒绝的，于是 `validate()` 里那几条判断**永远走不到**。而「一条永远不会红的护栏比没有护栏更糟」——它会让人以为这块已经被守住了 | `validate()` 只查**登记时没把住**的几项（英文原名缺失、图标键不以 `.svg` 结尾、掩码含大写、已编译掩码数与声明数不一致）。测试相应改成「内置表 `validate()` 为空」+「用合成表逐条验证它**能**报出来」，而不是断言一个不可达的分支 |
| 测试里真的弹出模态对话框 | 变异测试时把「没有改动就不问」去掉，`Tests/SettingsDialog` **挂住**而不是变红——offscreen 平台上 `QMessageBox::exec()` 是一个永远等不到输入的模态循环。挂住的代价比一条红断言大得多：CI 上表现为超时，本地表现为「测试卡住了」，排查方向完全错 | 把「问一句、拿个答案」抽成可替换的 `SettingsInquiryHandler`，并在测试里定义一个基类，构造时就装一个**默认答案恒为「返回」**的处理器。于是「意外被问到」变成断言失败。顺带得到一条更强的结论：加固**之前**那 45 条用例能跑完，说明它们当中没有一条走到过真实对话框——所以这次加固对它们是恒等变换 |
| `QDialog` 没 `show()` 就断言 `isVisible()` | 从未显示过的对话框本来就 `isVisible() == false`，于是「问题标签显示了没有」「对话框还在不在」这类断言**怎么改实现都成立**（测试里改错了方向也不会红） | 凡是断言可见性/关闭状态的用例先 `show()` 一次（配一个 `showDialog()` 辅助函数并 `processEvents()`）；这类「恒真断言」比没有断言更危险，因为它看上去有覆盖 |
| 用 `QSignalBlocker` 挡住控件信号来「回滚选中项」时，只处理了一半的入口 | Tab 列表的 `currentRowChanged` 直连切换逻辑，而公开的 `goToTab()` 自己也会问一次——两条路径行为不一致：点列表**不问**，调函数**问**。现象是「点 Tab 直接就过去了，未保存的东西跟着走到了下一页」 | 让列表的信号也走 `goToTab()`，被拒绝时用 `QSignalBlocker` 把选中项回滚到旧行。**一个用户动作只应有一条实现路径**；两条路径就一定会出现「点得动、调不动」这种只在其中一个入口复现的缺陷 |
| `SettingValidation::describe()` 用一个单位描述所有控件类型 | 掩码清单的上下界数的是「条数」，写成「最多 5 个字符」会让用户以为在限制单条掩码的长度 | 单位随控件类型走：文本类「个字符」、掩码清单「条」。这类「文案与数据对不上」的错误不会让任何断言失败，只有人看得出来——所以它值得一条用例盯住（`maskListLengthRulesCountEntriesNotCharacters`） |
| 宿主环境：**未做 ad-hoc 签名**的 x86_64 可执行文件无法启动 | 2026-09-20 下午起，`qmake`（Qt 5.15.2 clang_64）、全部 `tst_*` 与主程序都启动不了：进程进入 `U`（不可中断等待）状态、CPU 时间恒为 0、`SIGKILL` 与 `SIGALRM` 都进不去，**既跑不完也超时不了**。看起来像「测试挂住了」或「qmake 坏了」，上一轮因此把它误判成「宿主/沙箱的 x86_64 执行路径断了」并只做了记录 | **真正的成因是签名，不是 Rosetta 坏了**：`clang -arch x86_64` 编出的最小程序（`int main(){return 42;}`）未签名时卡住、执行 `codesign -f -s -` 之后立刻正常返回 42；`arch -x86_64 /bin/echo` 一直正常，因为它在 dyld 共享缓存里且由 Apple 签名；arm64 程序全程正常。**判据**：`codesign -dv <二进制>` 报 `code object is not signed at all` 就是它。**处理**：给 `~/Qt/5.15.2/clang_64` 下 232 个 Mach-O 各补一次签名，并在 `Code/Tests/run-tests.sh` 构建成功之后加一行 `codesign -f -s -`（`uname -s` 为 Darwin 才做、失败不阻断）。遇到「`U` 状态 + CPU 时间 0」先跑一次 `codesign -dv` 与被签名前后的最小实验，不要在测试与代码里找原因 |
| 只按 mtime 判断「要不要重编」在变异测试里会漏编 | 用 `os.utime()` + `sleep` 躲时间戳粒度仍然不可靠：复原动作用的是 `shutil.copy2`，它会**把备份的旧 mtime 一起还原**，于是「这条变异其实没被编译进去」，脚本却报「用例没检出」——结论正好反过来 | 变异脚本改成**先删掉被测文件的 `.o` 再 `make`**（`os.remove(obj)`），重编与否就没有歧义；再断言 `make` 日志里出现了被测文件名。本轮第一版脚本就因此在 12 处变异里漏报了 1 处（M11），改成删 `.o` 后 12 处全部检出 |
| 变异脚本传 `-o -` 与单独的 `txt` 两个参数 | QTest 的写法是 `-o -,txt`（一个参数），拆成两个会被当成未知参数，二进制直接以非 0 退出。而脚本把「非 0 退出」理解成「用例失败」，于是基线就被判定成「不是全绿」而中止 | `-o -,txt` 必须是一个参数。另外「非 0 退出」与「有用例失败」是两件事，脚本要分开判（构建失败、参数错误、用例失败） |
| 交接文档里的 qmake 命令路径写错过 | 交给无人值守任务的那份说明里写的是 `qmake ../LqCompare.pro`，而 `LqCompare.pro` 在 `Code/` 下，直接跑报 `Cannot find file: ../LqCompare.pro.`（qmake 以 exit 2 结束，`make` 因此根本没跑） | 正确命令是 `cd _build-lqcompare && qmake ../Code/LqCompare.pro`（§2 的表格里一直是对的）。qmake 失败时**它不会生成 Makefile**，所以紧随其后的 `make` 会报找不到文件——看到「make 立刻失败且没有任何编译输出」先看 qmake 的 stdout |
| 组合式设置存储直接转发各层的 `changed` 信号 | 一次写入会先由被写的那一层发出、再由组合层发出，上层收到**两条**；而层的 `clear()` 用**空键**表示「全变了」，直接转发会把「某一层清空了」说成「所有设置都变了」——状态栏与标签上的脏标记会白抖一次 | 组合层（`ScopedSessionSettings`）**不连接**三层的 `changed`，自己按「**有效值**有没有变」发一次。代价是外部绕过组合层直接改某一层时它不会察觉——因此约定三层存储只经组合层读写，并把这条写进类注释 |
| 把「写入成功」当成「用户看得见变化」 | 往会话层写、而视图层已经有同一条时，值确实存进了会话层（用户点的就是「保存到当前会话默认值」），但有效值仍是视图层那个。此时发 `changed` 或改写视图层都是错的：前者让脏标记白抖，后者让这次改动活不过关标签而用户以为存下来了 | 两者分开：值存进目标层、`changed` 按有效值发，并另给 `resolvedFromLayer()` / `describeResolution()` 让界面显示「当前生效值来自「仅当前视图」」。**凡是「按作用域分层存储」的系统都会有这个错位**，要在接口上留出表达它的地方 |
| 用 `QCOMPARE` 直接比 `enum class` | `QTEST_MAIN` 的 `QCOMPARE` 需要 `toString<T>`，对 `enum class` 没有现成特化；补一个特化当然可以，但那会让断言依赖枚举**序号**——在枚举中间插一个值，快照与断言的含义就整体错位 | 比较机器标识字符串（`QString::fromLatin1(settingScopeIdentifier(scope))`）。本仓既有套件都是这么写的，且字符串是稳定的对外事实（它会进会话文件与命令行）。本轮 `Tests/SettingsScope` 全篇按这条办 |
| 函数内构造的临时容器被返回，指针随即悬垂 | `QVector<Row> matchModeTable()` 按**值**返回临时对象，`matchModeRow()` 又返回指向其中一行的指针 → 函数一返回指针就指向已析构的存储。现象很误导：`nameMatchModeLabel()` 返回空串、`QCOMPARE` 失败，紧接着下一条用例直接 **SIGSEGV**（栈里看不出与表有关） | 三张表一律改成**函数内静态**并返回引用（`static const QVector<Row> table = …; return table;`）。**「返回容器的指针」等于返回悬垂指针**，除非容器本身是静态的 |
| 以为 `QVector` 有 `QStringList` 的成员 | 写 `QVector<QString>{…}.join("；")` 报 `no member named 'join'`——`join` / `filter` 这类方便成员只在 `QStringList` 上 | 加一个 `joinProblems()` 辅助先转成 `QStringList` 再 `join`。**`QVector` 与 `QStringList` 不是同一个容器**，别按后者用前者 |
| 自检函数自己去读模块内部的表 → 一条**永远不会红**的护栏 | `validateNameFilterTables()` 最初从模块内的静态表取数据，于是测试把一份故意写坏的表传进去也影响不到它：变异 M27（改坏组合语义表）漏检。**测试全绿但守的东西没被守住**，这正是 §5 第 7 条说的那种最坏的护栏 | 自检**把表当参数**（`validateNameFilterTables(modeTable, combineTable)`），坏表才影响得到它；并补一条「对故意写坏的表跑同一判定必须报出条数」的用例。**凡是「自检/校验」函数，数据来源必须是参数而不是内部常量**，否则它测的是自己 |
| 大小写选项只在精确 / 通配路径上生效，正则漏了 | 自查发现的**真实功能缺陷**：`= *.CPP` 在 `Qt::CaseInsensitive` 下能命中，`~\.CPP$` 却始终区分大小写——因为 `setPatternOptions(… CaseInsensitiveOption)` 只加在了非正则分支上 | 构造正则时按需带上该选项（`decide()` 里按值捕获前设置），并新增 `regexHonorsCaseSensitivity` 用例钉住。**「同一个开关要管三条路径」时最容易漏掉最后加的那一条** |
| 用 `anchoredPattern()` 做整名匹配 | `QRegularExpression::anchoredPattern()` 会把模式包成 `\A(?:…)\z`，用户自己写的 `^a` / `a$` 与包装后的锚点叠加，语义被悄悄改掉（不同写法下表现为永不匹配或行为不定）。而用户以为「正则模式」就是普通正则 | 不要用 `anchoredPattern()`；改判 `match.capturedStart() == 0 && match.capturedLength() == name.length()`。**包装用户输入前先想清楚它会不会改变用户表达的意思** |
| 解析结果里的对象在测试里被当成 `const` | `const NameFilterParseResult parsed` 上调 `parsed.filter.setCombineMode(...)` 报 `'this' argument to member function … has type 'const NameFilter'`。这不是库的问题，是测试自己写错了限定 | 去掉那个 `const`。**解析结果是可变的工作对象**，不要顺手加 `const`（本仓的 `ParseResult` 类都是这个语义） |
| 预设往返多出末尾空行 | `serializeNamedNameFilters()` 吐 `"*.cpp\n*.h\n"`，往返后与期望 `"*.cpp\n*.h"` 不等——只差一个看不见的换行，两个字符串在终端里长得一模一样 | `flush()` 里丢掉 `body` 末尾的空行；并且**只有 `bodyStarted` 之后的空行才进正文**——记录标题之前的空行会让元信息块提前结束。**「逐字一致」的往返断言必须包含首尾空白**，否则查不出这类差异 |
| 新套件忘了写 `QTEST_MAIN` | 链接报 `Undefined symbols: "_main"`。§6 早有一条同类记录（`Q_OBJECT` 那行），本轮又犯一次 | `.cpp` 末尾必须有 `QTEST_MAIN(Tst_Xxx)`。**新建套件的第一条构建失败，十有八九是它或 `.moc` include，先查这两个再查别的** |
| `QCOMPARE(a, QStringList{"x","y"})` | 报 `too many arguments provided to function-like macro invocation`——宏参数里**只有圆括号能保护逗号，花括号不算**，预处理器把 `{"x"` 与 `"y"}` 当成两个实参了 | 外面再包一层：`QCOMPARE(a, asList(QStringList{"x","y"}))`（或先赋给一个局部变量）。表驱动的断言尤其容易踩 |
| `.pri` 已经引用了还没写出来的 `.cpp` | 并行开发被中途打断（额度/崩溃）时，最常见的残留形态是**接线先写完、实现没落盘**。`services.pri` 会 include 各模块 `.pri`，于是**整个应用工程编译不过**，而报错指向的是一个「不存在的文件」，很容易被当成环境问题 | 接手一次被打断的并行开发时，先做一次**引用完整性扫描**：把 `Code/**/*.pri` 与 `*.pro` 里 `HEADERS/SOURCES/FORMS/RESOURCES` 的每一项（**解析掉 `$$PWD`**）逐个判存在，缺失的就是断点。2026-09-21 用这个办法在 60+ 个 `.pri` 里只捞出 1 处（`patchapply.cpp`），比逐个模块读代码快得多 |
| `check_icons.py` 看不见「文件在磁盘、裸名被引用、但 qrc 里被摘掉」 | 裸名引用（`icon("ribbon_copy.svg")` 这种由 App 补前缀的写法）只在**名字已在 qrc 里**时才被计入「已使用」，因此把某个图标从 `Pictures.qrc` 里摘掉之后，静态护栏**仍然是绿的**（31 个图标，一致） | 目前靠 `Tests/AppIntegration` 的运行期断言兜住（它真的去 `QFile::exists` 每个命令的图标）。这是**护栏的已知缺口**，不是「已经安全」——收紧它要扫描裸名引用并反向校验「磁盘存在但未声明」，尚未做 |
| 命令的图标路径指向不存在的资源 | 界面上一排 Ribbon 按钮是空图标，但**编译期与静态护栏都不报错**，只有两种途径能发现：离屏启动日志里刷 `qt.svg: Cannot open file ':/Pictures/xxx.svg'`，或 `Tests/AppIntegration` 的 `QFile::exists(command.icon)` 断言 | 新增带图标的命令时，要么真的按同一套视觉语言补一张 SVG 并登记进 `Pictures.qrc`，要么指向一张已有的、语义合适的图。`check_icons.py` 只校验「qrc 声明 ↔ 文件 ↔ 源码引用」三者一致，**不校验「命令引用的路径真的存在」**——这一条只有运行期能抓 |
| 离屏启动的「成功」有两种含义，退出码 10 不是失败 | 单实例机制默认开着。若上一轮那个离屏实例**还活着**（不带管道重定向地跑 `LqCompare`，它会一直待在事件循环里，不会自己退出），再启动一次会走**转发**路径：立刻退出、**日志文件是空的**（转发那一侧什么也不打印）、退出码 **10**。看起来像「启动失败」或「日志没接上」，实际是 `RelayStatus::Delivered`（参数已转交给正在运行的实例） | 退出码含义看 `relayExitCodeTable()`：0 未发生转发 / **10 已转交** / 11 被拒绝 / 12 无人应答 / 13 握手超时。要做一次干净的启动确认，就带 `--new-instance`（日志里会印「单实例机制已由选项关闭」）并在确认后把进程杀掉；或先 `pgrep -fl 'MacOS/LqCompare'` 看一眼有没有残留实例 |
| 重写 `main.cpp` 会静默带走启动自检 | 夜间把 `main.cpp` 从 233 行删到 91 行增添之后，之前几轮加的四条 `validate*Tables()` 启动自检**没有任何调用方了**，但函数本体还在、文档也还写着「启动时会打印」，于是文档与行为悄悄分家 | 重构 `main.cpp` / 装配路径时，**顺手 grep 一下那些 `validate*()` 还有没有调用方**（`grep -rn 'validate.*Table' Code/`）。自检函数留在模块里不等于它会跑；这类「文档说会跑、实际不跑」的漂移没有任何机制会自动发现 |
| 在入口处「顺手」对空行提前返回 | `LineFilter::excludes()` 开头写了一句 `if (rawLine.isEmpty()) return false;`（看着像无害的快速路径），它把精确与正则模式**也**一起挡掉了——于是 `re:^$` 永远不生效，而 `re:^$` 恰恰是报错提示里教用户写的「去掉空行」的**唯一**写法。用户按提示写了、没有任何报错、空行照旧留着 | 入口不做任何模式无关的提前返回，让每种模式自己决定（空行只有通配模式命中不了，因为掩码要求名字非空）。这类 bug 的共性是**「快速路径」的判据与真正的语义无关**；写这种分支前先问「它在所有模式下都成立吗」 |
| 行首前缀匹配没跳过缩进 | `= EXACT LINE` 缩进成 `  =   EXACT LINE  ` 之后，前缀要求「行首是 `= `」的实现匹配不上，于是那一行被解析成**通配模式**。它照样解析成功、照样产出一条表达式、界面上看不出任何异常——只是它从此变成「匹配字面量 `= EXACT LINE` 的行」，永远不命中。内容过滤的声明常常是从日志里抄下来的一行（带着缩进） | 前缀匹配前先 `ltrimAscii` 再匹配，并把跳过的长度记进 `textOffset` 让列号算回原始行。注意这与 `namefilter.h` **刻意不一致**（那里的表达式是用户从头写的，没有这个来源），改动前先读两边的模块头注释 |
| 源码级护栏扫到自己的注释 | 新加的「本模块不许读文件」护栏直接报错，而它扫到的是 `contentfilter.h` **自己说明里**逐字写的 `QFile` / `readAll`。与 `check_icons.py` 那条同源（护栏认的是**形状**，不区分代码与注释），但这次的代价更隐蔽：人会顺手把护栏放宽或删掉 | 护栏先 `stripComments()`（去 `//` 与 `/* */`）再判；并且**成对**断言「原文里有 `QFile`、去注释后没有」——这样 `stripComments()` 写错时用例会红，而不是**静默通过**（一个去注释做过头、把所有内容都丢掉的实现同样能让「去注释后没有 `QFile`」成立） |
| `check_spec.py` 的文档计数护栏扫到 gitignored 目录 | 护栏原本 `os.walk(REPO_ROOT)` 扫全仓 `.md`（含 `.md` 的宽匹配模式），于是扫到了 `.workbuddy/` 下的工作日志——那里写的数字是**测试用例条数**，不是规格条目数。结果是**本机红、CI 绿**：CI 只 checkout 被跟踪的文件，本地多出的工作日志只在开发机上触发误报 | `doc_count_candidates()` 优先用 `git ls-files -z '*.md'`（退化到「仓库根 + docs/」），只扫被跟踪的文档。**「本机与 CI 结论不一致」的护栏要优先修**——本机红会让人习惯性忽略它，而那一天之后它连真问题也拦不住了 |
| `QRegularExpression::patternErrorOffset()` 指向出错点**之后** | 用它算列号（`issue.column = textColumn + offset`）会指向出错字符的下一位：`(` 报 `offset == 1`，而 `(` 在位置上只占 0 那一格。断言写成「列号等于 3」时才看得出这半格偏差 | 要么按「下一格」的语义写注释与断言（本模块目前就是这么处理的，并在用例里注明），要么减 1 再交出去。**先测一次再定语义**，不要照直觉写期望值 |
| **本机的 `QSharedMemory` 根本建不起来** | `SingleInstanceGuard` 会退回到「进程锁 + 本地套接字」，并在日志里如实写「共享内存不可用（QSharedMemory::create: out of resources），已由进程锁保证单实例」。于是 `takePrimaryIdentifier()` 里 `error() == AlreadyExists` 那个分支**从不进入**，只在那里被调用的 `recoverStaleIdentifier()` 因此**不可达**，它的三条用例在本机是「跳过式通过」。**这不会让任何用例变红**——只会让「共享内存这条腿」的结论凭空成立 | 实测办法（30 行的小程序就够）：先 `nm -u ~/Qt/5.15.2/clang_64/lib/QtCore.framework/Versions/5/QtCore \| grep -E 'shmget\|shmat\|ftok'` 确认本机 Qt 走的是 **SysV** 后端，再直接调一次 `shmget()` ——本机返回 `ENOMEM`（errno 12），Qt 报 `OutOfResources`。**凡是「有回退路径」的功能，都要问一句：本机跑的是主路径还是回退路径**；只看「测试全绿」会把回退路径通过当成主路径验证过 |
| 两处**纵深防御**里去掉其中一处，整套测试仍全绿 | `splitFrame()` 校验「声明的载荷长度 == 实际长度」，`decodeRelay*()` 又在读完字段后校验「载荷必须读尽」——两道都拦「粘帧/截断」。变异把前一处去掉后**零条用例变红**：原有两条用例构造的输入恰好被后一处兜住（一条少一个字节 → 字段读不完；一条多一个字节 → 读不尽）。**「去掉任意一道防线都不该全绿」这件事没人保证** | 补的用例要专门构造「只有那一道拦得住」的输入：声明比实际**多一个字节**，而**已有的字节本身仍是一个完整可解析的请求**。写纵深防御时，每一道都要问「有没有哪一类输入是只有我拦得住的」；有，就为它写一条用例，否则那道检查随时可能被人当成冗余删掉 |
| **「issue 还是「待实现」」不等于「代码没写」** | 夜间 24 个工作流一次落地 454 个文件，其中 `PLAT-006`（单实例与进程间通信）的实现与其测试套件全都在，但 issue #327 仍是「待实现 / 0 个勾 / 0 条评论」，而 `current-handoff.md` §4 还在推荐「下一轮做它」。照那份推荐开工就会**把已经写完的东西再写一遍**。同类情形在 OPT-001 / OPT-002 上也存在（选项对话框与设置仓库都已落地，issue 还挂着「待实现」） | 换工作流推进、或接手一次大批量产出之后，**选条目前先核对源码**：`gh issue list` 拿到的标签是滞后信号，`Code/` 下的文件与 `Code/Tests/<套件>/` 是否已存在才是当下的状态。核对成本很低（一次 `ls`），重复实现的成本极高 |
| 给**按位置聚合初始化**的结构体插字段，只能插在末尾 | `OptionDefinition` 的定义表是按位置写的（每行只写到 `maximum`），本轮把新字段 `unit` 插在 `maximum` 与 `machineSpecific` 之间 → 所有行的值整体错位一格（`false` 落进了 `QString unit`）。报错是 `no matching constructor for initialization of 'const QVector<OptionDefinition>'`，**指不到任何一行**；因为是不定长参数式的聚合初始化，前几行还能歪打正着地编过 | 新字段一律加在**结构体末尾并给默认值**；头文件里写明「为什么必须放在最后」。看到那条「没有构造函数」的错误而定义表又确实有对应构造函数时，先按位置错位去数 |
| 变异驱动脚本把 **`.o` 的 mtime 推到未来** | 想让 `make` 重编却推错了目标：`make` 认为目标比源新，于是**整轮跳过编译**，25 处变异全部报「未确认重编」——**一次看起来像「25 处全漏检」的假阴性**，而实际是变异根本没编进去 | 推**源文件**的 mtime（`os.utime(src)` 或 `touch -m`），并在跑用例前**断言变异确实被编译了**（例如检查新 `.o` 的 mtime 或产物里的符号）。**「失败」与「没跑」必须能区分**，否则护栏会给出方向完全相反的结论 |
| 套件二进制的落点不统一 | 变异驱动按 `_test-build/<Suite>/bin/tst_x` 找产物，`Tests/OptionsDialog` 的却在 `_test-build/OptionsDialog/tst_optionsdialog`（少一层 `bin/`），于是 `FileNotFoundError` 被当成「变异未被检出」 | 驱动脚本两个路径都试（`bin/tst_x` 与 `tst_x`），找不到就**报错退出**而不是记成漏检。这类「工具自己找不到文件」的失败最容易被混进结果里 |
| 把与安全**无关**的选项也算进「安全契约违规」 | `safetyContractViolations()` 最初写成「覆盖策略 != 询问 即违规」。但「跳过已存在的目标」虽然也不问，它**不覆盖任何东西**；把它算成违规会让这条契约退化成「必须等于 Ask」这个与安全性无关的同义反复，「保守」一词就失去了边界。而我先写的用例断言的是「Skip 不算违规」，于是**实现与用例对不上，用例红** | 契约的判据要落到**真正有害的那一个取值**上（只有 `Overwrite` 直接覆盖），而不是「不等于某个值」。写这类「把一句话变成函数」的契约时，先列出「哪些取值踩了这条线」再写条件 |
| 路径脱敏的**右边界方向搞反**（真实隐私缺陷） | 判据最初写成「前缀后面必须紧跟路径分隔符」，于是 `/Users/loren `（后面是**空格**）——也就是句子里最常见的写法——**不会被替换**，导出的诊断包原样带着用户家目录。这是新写的测试抓出来的，不是 review 看出来的 | 脱敏的两种错误代价**不对等**：多替换一次（`/Users/lorenx` 被误替换）只是看着怪，漏替换一次是**真的泄露**。所以判据取「后面不能是名字的延续字符（`_ - .` 或字母数字）」，朝「宁多勿漏」偏。写这类判定时先问一句「两种错法各自的代价是什么」，再决定往哪边偏 |
| 用 `QDir` 的**名字通配**去枚举「某某文件的兄弟文件」 | 滚动历史按 `entryList(日志名 + ".*")` 找。日志名是**用户可配**的，含 `[` `*` `?` 时通配符会被解释：`a[1].log` 既**漏掉**真正的 `a[1].log.1`，又**误收**无关的 `a1.log.12345`。前者让诊断包少一半材料，后者把无关文件打进包里（可能顺带带出隐私）。测试是**先红再改实现**的 | 别用通配符匹配用户可控的字符串。列全部条目、自己按前缀 `startsWith` + 后缀解析判断（`logfiles.cpp` 的 `existingHistory()` 就是这么写的，注释里写了理由） |
| 变异打在**头文件**上时，只推源文件 mtime 不够 | 变异落在 `.h` 上（例如改默认值），依赖它的 `.cpp` 由 `make` 从 `.d` 文件推导依赖，**看似**会重编；但上一轮用过的 `shutil.copy2` 会把源文件的 mtime 一起还原，于是部分 `.o` 仍然「比源新」，变异编不进去而被记成「漏检」——和 §6 上一条是同一类假阴性，但触发路径不同 | 变异落在头文件上时**把整个套件的构建目录重建**（实现里叫 `purge_suites()`），不要只删单个 `.o`。判定标准仍是「变异确实进了产物」而不是「我推过 mtime 了」 |
| `make -B` 会连 app bundle 的 `PkgInfo` / `Info.plist` 一起删掉重建 | 本机沙箱有删除守卫，拦住这两个文件的删除，于是 `make` 报 `*** [../dist/…/PkgInfo] Error 1` 并中止。**但编译与链接其实都已经成功了**——`MacOS/LqCompare` 的 mtime 是最新的，产物可用。若照着「报错了所以没构建成功」去排查，会白找很久 | 先看产物的 mtime 再判断成败；确认是这一条之后 `touch` 那两个文件再 `make -j8` 即可（它们的内容没变，只是 mtime 落后）。**这类「工具层的拒绝」与「代码层的失败」必须分开判断**，否则会把一次成功构建记成失败 |
| 全局状态的测试必须逐个复位，**新加的开关也要算进去** | `Services/Log` 里日志级别 / 日志文件 / 接收者都是进程全局的，本轮又加了滚动策略与性能计时开关。若 `init()` 只复位旧的那几个，用例之间就会互相影响（前一条把计时开关打开、后一条断言关着）——而失败顺序看起来随机的 | 给模块加新的全局状态时，**同一轮里**把它补进该模块所有套件的 `init()` 与 `cleanupTestCase()`。`Tests/Logging` 的 `initTestCase` 现在会逐个断言初值，就是为了让「漏复位」立刻红 |
| 「某模块未落地」这句话**本身会传染**，而且看起来像是「多处独立佐证」 | 本文档至少四处写「文件格式定义模块未落地」（§1.15、§4.0、§4.1 的两行），而 `docs/development/team-format.md` 一直躺在那儿说它落了、`Code/Services/Format/` 与 `Tests/Format` 也一直在磁盘上。第一处写错，后面几轮**复制**它——于是同一句错话在四个地方出现，读者会以为有四处独立证据 | 看到「X 模块未落地 / 没有宿主 / 要等 X」时，**先动手查三条**：`ls Code/Services/<模块>/`、`ls Code/Tests/<模块>/`、`grep -l <模块> docs/development/team-*.md`。夜间那批工作流有 **24 路**，本文档对它们的覆盖从来不完整；`team-*.md` 才是各路自己写的交付记录。改判之后，**把引用过这句话的每一处一起改掉**，否则它会继续传染 |
| 把「该模块的**后续**条目没做」读成「该模块没做」 | `team-format.md` 结尾明确列了「未完成且不计为本轮实现」的一串（格式管理器 UI、语法高亮引擎、格式转换执行、归档解压器…）。这些是 `FMT-002` **及其后**条目的范围，与 `FMT-001` 的完成标准毫无重叠。一句话读快了就变成「Format 模块没做」——而这正是上面那条错话的源头 | 判断一个模块落没落，**看完成标准的逐条对应**，不要看「有没有后续条目没做」。issue 编号（`FMT-001` vs `FMT-002`）本来就是范围边界：`FMT-001` 只管**模型与存储**，识别算法的细节归它自己，界面归 `FMT-002` |
| **编辑正在运行的 bash 脚本** | bash 是**按字节偏移惰性读**脚本文件的，不是一次性全读进内存。脚本跑到一半时改动它（哪怕只是往中间插几行），后续读到的就是错位的内容。本轮实测到的现象有两种：一句乱码的 `����: command not found`（正中文里打了字节），以及 `syntax error near unexpected token \`done'`。两次都出在**看起来完全无关的行号**上，很容易被当成「脚本被写坏了」 | 长任务（全套测试跑十几分钟）跑起来之后**不要再碰那个脚本**；要改就先停掉再改。心爱的备用做法是把待改的行先改好再启动。**判断依据**：`bash -n <脚本>` 在同一个文件上语法通过、而运行时却报括号错，就说明是「边跑边改」，不是语法问题 |
| BSD grep 不支持 `\|` 交替（与 `\+` 同族） | `grep -rn "LqRibbon\|lqribbon" Code/` 在 macOS 上**静默返回空**——BRE 里 `\|` 不是交替符，整条模式被当成「一个字面量反斜杠」去找。本轮差点据此写下「没有任何代码引用 LqRibbon」这个**错误结论**（真实情况是 `App/RibbonWindow.{h,cpp}`、`Views/Page/ribbonlayout.{h,cpp}` 等六处引用它） | 交替一律用 `grep -E` 或 `grep -e A -e B`；跨平台脚本里更稳的是「不用交替，分两次搜」。这一条与坑表里那条 `sed` 的 `\+` 是同一族（**不报错，只给错答案**），`check_shell.py` 拦的是脚本，**命令行上手工敲的它管不到** |
| **「没 grep 到」不等于「不存在」**——依赖清单必须实测 | CI 配置里曾断言「`Code/Tests/*/*.pro` 一个都不 include `Views/`，所以全部测试套件都不依赖 LqRibbon」。实测（对每个 `.pro` 跑一次 `LQCOMPARE_MYCLASS_ROOT=/nonexistent qmake`）**66 个里有 2 个依赖**：`Tests/AppIntegration`（include 了 `Views/views.pri`）与 `Tests/CommandActions`。两句错话（「都不 include Views/」+「都不依赖 LqRibbon」）叠在一起，差点让流水线的降级范围写错 | 问「谁依赖 X」时，**能跑就跑，不能跑才去读**：`qmake` 探一遍不会漏（它按的是真实的 include 链），grep 会漏（它只能看见写出来的字面量，看不见 `.pri` 的 `exists()` 条件 include、变量拼接、以及**你自己写错的正则**）。这条比它看起来更重要——同一个错误在这一轮里出现过两次，第一次是 grep 正则写错，第二次是推断代替实测 |
| GitHub Actions 里 Windows 的 `run:` 默认走 **pwsh** | 每一步都写的是 bash 语法（`case` / `[ -n ]` / `>> "$GITHUB_OUTPUT"` / 多行 `if`），在 Windows 腿上会被 pwsh 解释，报的错与真实原因无关 | 作业级写 `defaults: { run: { shell: bash } }`（Windows 的 bash 由 Git for Windows 提供，运行器自带）。**三平台共用一份脚本时，shell 必须显式声明**，不能靠默认值 |
| GitHub 托管 macOS 运行器的标签选错 | `macos-13`（原来的 x86_64 标签）已于 **2025-12-04 退役**，用了直接失败；`macos-latest` / `macos-14` / `macos-15` 都是 **arm64**，而 GitHub 托管的 arm64 运行器**没有预装 Rosetta 2**——本项目的 Qt 5.15.2 只有 x86_64（clang_64）官方包，qmake 在上面跑不起来 | 用 **`macos-15-intel`**（官方为「需要 x86_64 的标准运行器用户」新加的标签）跑这个 Qt 基线；它可用到 **2027-08**，之后托管运行器不再有 x86_64，届时必须整体迁 arm64 并自建 Qt（Qt 5.15.2 没有官方 arm64 macOS 包）。**推断「最新的就是最好的」在这件事上会直接翻车** |
| 把「跳过依赖缺失的套件」做成**静默** | 「排除掉 2 个拿不到依赖的套件」与「静默跳过这 2 个」在日志上只差一句话，却让「64 个套件通过」被读成「全部 66 个都验证过」——而事实是那 2 个**从来没在公开 CI 上编译过**。这是 CI 里最危险的那种绿 | 排除机制（`LQCOMPARE_TEST_SKIP`）生效时：开头打一行排除清单，末尾把「全部套件通过」换成「通过（已排除 N 个套件、未验证）：…」，**全部被排除时仍以退出码 2 报「一个测试都没跑」**。4 处变异 4 处检出，其中一处专门变异「汇总掩盖排除」——**「报告缺口」这件事本身也要被反向验证**，否则它会随下一次重构悄悄消失 |
| 用 Qt 的 `-o results.xml,xml` 当作 CI 的测试报告 | Qt 的 `xml` 是它**自己的私有格式**（根节点 `<TestCase>`），没有任何 CI 的测试报告解析器认它。看起来「有一个 XML 了」，实际上 CI 拿到它什么也做不了，而失败用例清单（ENG-004 第 3 条要的）也就无从消费 | 用 `-o results.xml,junitxml`：根节点是 `<testsuite failures=… tests=…>`，这才是 JUnit。**「有个同名文件」不等于「格式对」**——生成之后 `head -3` 看一眼根节点，成本两秒 |
| 用 `-o -,txt` 把结果写到 stdout | Qt 的 `-o <文件>,<格式>` 允许把文件名写成 `-` 表示 stdout。在本机（macOS）**照常输出**，在 **Windows（Git Bash）上一行都不输出**，而同一轮的文件产物写得好好的。后果是 Windows 腿的日志里既没有 `Totals:` 也没有任何失败用例名，合计还被算成 `0 passed`——看起来像「所有套件都通过了，只是数字是 0」。**这是第一次真实 CI 跑完才暴露的**，本机怎么试都试不出来 | 别依赖 stdout 约定：**只写文件，脚本再 `cat` 回来**（`run-tests.sh` 现在就是这么做的），并显式 `rm -f` 上一轮的产物——否则某套件崩在初始化阶段时，脚本会把**上一次跑出来的** `results.txt` 当成这一次的结果 |
| **合计与「红了几个套件」对不上** | Windows 腿上 `34 个套件红` 而合计只有 `26 failed`。原因是 Qt 的 `Totals:` 行只在套件正常跑完时才写；**崩在 `initTestCase()`（或构建失败）的套件一条用例都不写**，于是它们的用例数既不在 passed 也不在 failed 里。两个数字都对，只是口径不同——而日志里没有任何一句话说明这一点 | 合计之外**单独点名没产出统计行的套件**（`run-tests.sh` 现在会打「另有 N 个套件没有产出统计行（其用例数不计入上面的合计）：…」）。凡是「总数」与「明细条数」并列出现的地方，都要能解释两者的差从哪来 |
| 构建输出丢进 `/dev/null`，失败时**一个字的原因都没有** | `run-tests.sh` 原本是 `qmake … >/dev/null 2>&1`。第一次 CI 上 ubuntu 有 17 个、Windows 有 26 个套件构建失败，而 artifact 里只有「哪个套件红了」，**连一条编译器错误都看不到**——排查只能靠猜。同一件事在本机也发生过一次：`make: command not found` 被吞掉之后，26 个套件看起来都像「代码编不过」 | 构建输出**落盘**（`<套件>/build.log`），失败时打末尾 20/30 行，并把 `build.log` 加进 CI 的上传产物。**`>/dev/null` 只允许出现在「结果已经被别的方式记下来」的地方**；失败路径的原始输出是唯一的证据，不能丢 |
| **覆盖标准库的私有方法时，要留意基类在调用它之前改了什么** | `generate_fixtures.py` 用 `RawName` 覆盖 `zipfile.ZipInfo._encodeFilenameFlags()`，按自己的 `raw_flags` 决定 UTF-8 位。Python 3.13 及更早的 `_open_to_write()` 先把 `zinfo.flag_bits` 置成 `0x00`，所以「我说的算」；**3.14 改成无条件置成 `_MASK_UTF_FILENAME`**，于是那句 `self.flag_bits \| self.raw_flags` 被覆盖，`cp437.zip` 被写成「置了 UTF-8 位、字节却不是 UTF-8」——**夹具的语义被换掉了**，而症状出现在很久以后、指向别的地方（生成器结尾回读校验抛 `UnicodeDecodeError`） | ① 覆盖私有方法时，**返回值要完整表达意图，不要依赖基类保持某个字段没被动过**（这里是先 `& ~UTF8_NAME_FLAG` 再按 `raw_flags` 置回）；② 写完**断言落盘的字节**，别只断言对象上的意图——两者如何相互作用只有在记录里才看得见；③ 具体到 `zipfile`：Python ≥3.14 给**每一个**成员都置 UTF-8 位（连 `z.txt` 也置），所以任何依赖这一位的夹具都必须在生成侧自己钉住 |
| **报错的那一步不一定是错的那一步** | `Archive` 在 CI 上抛 `UnicodeDecodeError`，位置在生成器**结尾的回读校验**。当时的结论是「夹具没错，是回读校验太严」，于是「修复方向」写成「把 `cp437.zip` 从回读名单里去掉」。**真相相反**：3.14 把夹具**写**坏了，回读校验是唯一发现它的机制——照那个方向修，等于拆掉唯一会报警的仪表 | 校验失败时先问一句「**是被检查的东西错了，还是检查错了**」，判别办法是**去看被检查的那个产物本身**（这里 `struct.unpack_from` 把 53 个夹具的 flag 打出来，五分钟就定位了），而不是先怀疑判据。**尤其当「修正」的方向是「少检查一点」时，先停下来**：让校验变松永远能让红变绿，也永远能掩盖真问题 |
| **按 EOCD 扫中央目录会被注释里的假签名骗到** | 新加的写入后断言最初用 `eocd(data)`＝`data.rfind(b"PK\x05\x06")` 定位中央目录，结果在 `comment-signatures.zip` 上直接 `struct.error`——那个夹具的注释里**故意**放了一个假 EOCD 签名（它的用途正是验证读侧不被骗），`rfind` 先找到假的，记录长度读成垃圾 | 定位中央目录用 `ZipFile` 自己的 **`start_dir` + `filelist`**：可 seek 与不可 seek（数据描述符）两种写模式下都正确，而且不经过任何签名启发式。**夹具会故意喂给你畸形输入——给夹具自己用的辅助函数必须比被测试的代码更严格**，否则你会在写夹具的路上踩到夹具本来要测的那个坑 |
| **「只负责把别的失败讲清楚」的护栏，单独变异它必然漏检** | 本轮新增的写入后断言单独关掉（M5）时，三个组件全部与基线一致——**检出 0 个变化**。看起来像漏检，其实是等值变异：这个护栏没有自己的行为，它的作用是把**别的**故障讲成一句能读懂的话。把「回归」与「护栏」一起变异（M8）才看得出：失败性质从「成员 `b'caf\x82.txt'` 的 UTF-8 位与声明不一致：声明=0x0000 实际=0x0800」退回成 `UnicodeDecodeError: 'utf-8' codec can't decode byte 0x82` | 遇到报错/诊断型护栏（`assert…, "message"`、`QVERIFY2` 的第二参数、断言调用方传进来的表），**不要指望单独变异能检出它**；用**成对变异**交代它：行为变异 + 护栏失效，断言「坏消息变回难读的那个」。这样既说明护栏有用，又不会把「设计上的漏检」误记成测试缺口 |
| **CI 解释器版本与本机不同**：先假设它会改你产物的字节 | 同一个生成器，本机 Python 3.13 绿、CI 的 Python 3.14 红（`Archive` 在 ubuntu 与 macOS 两条腿上都红）。而且差异不只是「报不报错」——**92 个产物文件里有 72 个字节不同**，因为 3.14 给每个成员都置了 UTF-8 位。本机无法装 3.14（`install_binary` 不可用），于是写了个**行为复现器**：`inspect.getsource()` 取本机源码、把关键那一行换成目标版本的写法、`exec` 回模块再跑被测试脚本——拿到的失败与 CI **逐字一致** | ① **「本机全绿」推不出「CI 全绿」**，凡是走系统解释器/工具链的地方（Python、make、7z、shell）都要假设版本不同；② 差一个版本又装不上时，**照着目标版本的源码把那几行换掉做复现器**比猜快得多，也很容易证明复现器没失效（锚点行必须唯一出现，否则立刻报错退出）；③ 产物的字节要**跨版本可复现**——夹具/快照/生成代码一旦随解释器变，所有基于它的绿灯都要打问号 |
| **「等价变异」有两类，别把第二类记成第一类** | 本轮 M4（去掉「本轮开头清理旧产物」那句 `rm -f`）**连报两次漏检**，两次原因完全不同：第一次是真的等价——`2>file` 与 `-o file` 自己就会截断/覆盖，所以在「二进制真的被启动」的路径上删不删毫无区别，**那句 `rm` 当时确实没有任何可观察作用**；第二次是**观测点选错了**——探针场景选成「构建失败」，而陈旧产物的危害不在控制台，**在上传的产物里**（构建失败时脚本根本不会去读 stderr），于是输出里当然什么都看不到。把清理挪到本轮最前面、观测点改成「陈旧文件是否残留」之后，M4 才成为一个真变异（`stale_survived: False → True`） | 宣布「这是等价变异」之前，**把观测点换成三种不同粒度各试一遍**（控制台输出 → 产物文件内容 → 退出码/副作用），否则会把「探针写漏了」误判成「代码没问题」。判据：如果是真等价，**换任何观测点都不会变**；只要换一个观测点就检出，那就是探针的问题。另外——真等价的代码要么删掉、要么在注释里写明它为什么留着（本轮选择保留并把清理位置前移，因为**失败路径上它确实有用**） |
| **「清理上一轮产物」放在构建成功之后，等于失败路径上不清理** | `run-tests.sh` 原本在建完、启动二进制之前 `rm -f results.txt results.xml`，注释还写着「否则崩溃会被旧结果掩盖」。但那三行**执行不到**：构建失败、找不到可执行文件、qmake 失败时流程直接 `continue`，于是**失败的那一轮反而带着上一轮的产物**去上传——一份看起来正常的旧结果配一条「构建失败」的日志。这类「旧结果冒充新结果」是 CI 里最难被发现的一种假信号，因为它两份证据都能自圆其说 | 清理动作放在**本轮最开头**（`mkdir -p "${build_dir}"` 之后、任何可能 `continue` 的动作之前）。判断标准很简单：**问「这个清理在哪些路径上跑不到」，而不是「它写了没有」**。凡是要「先清后写」的产物，清理与写入之间不能夹任何可能提前退出的步骤 |
| **macOS 上跑得好好的代码，Linux 上可能被 `_FORTIFY_SOURCE` 直接 `abort`** | `Tests/Folder` 的 `linksAreComparedWithoutFollowing()` 在 macOS 上过、在 ubuntu 上把整个套件带走，`stderr` 只有一句 **`*** buffer overflow detected ***: terminated`**——这是 glibc 的 `__chk_fail()` 打印的，即**抓到了一处缓冲区越界**。`_FORTIFY_SOURCE` 是 **glibc 独有**的机制（Ubuntu 默认开，24.04 上是 `=3`，用 `__builtin_dynamic_object_size`，连一部分**堆**分配也查得出来），**macOS 的 libc 里根本没有**。所以「本机全绿」在这里不是「代码没问题」，而是「本机少一道本来该有的检查」 | ① 遇到同一个用例「macOS 过、Linux 崩」，**先假设是内存安全问题**，而不是平台差异——`buffer overflow detected` / `stack smashing detected` 这类 glibc 的话一出现就可以直接定性；② 在本机用 **ASan**（`-fsanitize=address -g`）复现，它盯的是同一类错误、而且 macOS 上可用；③ 这类失败**不可能**靠读代码 + 推理在本机定位，必须让工具说话；④ 写「读进固定/增长缓冲」的代码时（本项目是 `linkTarget()` 里的 `readlink`），**把长度判断写清楚并加一条越界用例**——本机看不见的那一类错误只能靠读代码时更严 |
| **崩溃时不打印「正在执行哪一句」，等于把定位工作留给下一个人** | `Tests/Folder` 崩了以后 `results.txt` 只剩前面 7 行 `PASS`，**最后执行到哪一句完全不知道**。Qt 的 `-v2` 会把每条 `QVERIFY/QCOMPARE` 都打出来，于是「最后一条打印出来的语句」就是崩之前那一句——但脚本平时不用 `-v2`（日志会大很多），崩溃时又不补跑 | 对「没产出 `Totals:` 行」的套件，**自动重跑一遍带 `-v2`**，把末尾若干行贴出来（`run-tests.sh` 已经有这条判据，加几行即可）。**注意 `-v2` 只在崩溃时用**：它是诊断工具，不是默认输出 |
| **子 shell 里对全局变量赋值不会再传回父 shell——串行时看不出来，并行时是灾难** | `run-tests.sh` 原本把「通过/失败/跳过」累加在主循环的全局变量上。并行化之后每个套件在子 shell（`&`）里跑，那些赋值**全部丢弃**、父进程一个数字都拿不到。症状是最难查的一类：66 个套件都真的跑了、每个套件的 `Totals:` 一行不差、**而合计是 `0 passed`** | 子 shell 的结果必须**落到文件**（`<套件>/summary.env` 一行六个数），父进程 `read` 回来再累加。凡是「并行工作单元要汇报的东西」，只能经文件或管道回到主进程——**不要指望内存里的变量** |
| **并发度不能用「跑得快了」证明；而且要只统计有头有尾的区间** | 探针把 `epoch毫秒 名字 start/end` 追加进一个共享轨迹文件，事后算最大并发。第一版把**所有** `start` 都记进去，于是串行跑也报出「并发 2」——因为「挂死」与「硬退出」两条探针永远走不到 `end`，它们的 `+1` 只增不减。**一个测量误差就这样冒充成「并行生效了」** | 先求出「同时写下 `start` 与 `end` 的标签集合」，只对它们做前缀和；同一毫秒内先处理 `end` 再处理 `start`（结果只偏保守）。并且**成对断言**：并行时必须 ≥2、串行时必须恰为 1——只断言一边，等于在测量自己的探针 |
| **`wait -n` 与 GNU `timeout` 在 macOS 上都不存在** | `wait -n`（等任意一个子进程）是 **bash 4.3** 才有的，macOS 自带 bash 3.2 上没有；GNU `timeout` 在 macOS 上默认也没有（`perl -e alarm` 之类的外挂会把「跑测试」变成「跑测试 + 一个解释器」）。这两个写法都很自然，而结果是脚本在本机能跑、换台机器直接报错 | 并行调度用「PID 队列 + `wait` 队首（最老的那个）」——副作用是**打印顺序 = 启动顺序**，日志反而更可读；超时用自建轮询（`kill -0` 判存活 + 到点 `TERM` 再 `KILL` + 落一个标记文件）。这一条已经在 `check_shell.py` 的护栏范围里，但**护栏只拦得下它认识的写法**，写之前先问「这命令在另一个平台上有没有」 |
| **判「子进程还活着」用 `kill -0` 是可行的，但别用看门狗子 shell** | 两件事都容易搞反：一是担心 `kill -0` 在子进程退出后仍返回真（僵尸），实测**不会**——bash 会异步回收后台子进程，`kill -0` 立刻为假，而 `wait` 仍能取回**真实退出码**（`sh -c 'exit 7' &` 那条实验里拿到 7）；二是想用 `( sleep "$T" ; kill … ) &` 做定时打断，那会在**每个**套件上留一个孤儿 `sleep`（66 个套件就是 66 个进程） | 用轮询 + `wait`：`while kill -0 "${pid}"; do … sleep 1 …; done` 然后 `wait "${pid}"; status=$?`。代价是每个套件最多多等 1 秒，收益是**没有残留进程**、而且退出码是真的。**这两个行为都写进了 `--self-test`，不是靠读文档记住的** |
| **`EXIT` trap 里引用函数内的 `local` 变量，报的是「未定义变量」而不是「清理失败」** | 自测的临时目录清理写成 `trap 'rm -rf "${tmp}"' EXIT`，而 `tmp` 是函数里的 `local`。函数返回后 trap 才触发，那时 `local` 已经不存在，在 `set -u` 下直接报未定义变量——**清理静默失败，临时目录留在盘上**，而脚本其余部分一切正常 | trap 里引用的变量必须是**全局**的（`selftest_tmp=""` 放文件顶层）。凡是在 `trap`/`finally`/析构里要用的东西，都不该是局部生命周期的 |
| **qtestlib 的 `Totals:` 把 `initTestCase` / `cleanupTestCase` 也算作「用例」** | 自测探针只有 2 个真正的用例，日志里却是 `Totals: 4 passed`，JUnit XML 也写 `tests="4"`。它看上去像「多算了两个」。更阴的是：当时那条断言写的是 `Totals: *2 passed`，**它匹配上的是另一个探针（失败探针）那一行**——一句「名字与实物不符、却恰好为真」的断言 | 断言要带足够的上下文（把 `4 passed, 0 failed` 整段写上，而不是只匹配一个数字）。这一条也是「断言必须指名道姓」的一个实例：**只匹配一个数字的模式，很容易在日志里撞上别人的数字** |
| **断言要盯「被测行为真正改变的那一个输出」，不是它旁边那个看起来像的** | 变异 M4 把「父进程累加用例数」改成 `+0`，自测**全绿**。因为当时的断言盯的是**某个套件自己的** `Totals:` 行（由子 shell 里那个套件写、与父进程的累加无关），而 M4 破坏的是**父进程的合计**——两个不同的量。补上 `^合计：6 passed, 1 failed, 0 skipped$` 之后立刻检出 | 写断言前先问「这个变异会让**哪个**输出变」，再去匹配那一个。与本节另一条（等价变异换观测点）是同一类错误的两个方向：**一个是观测点粒度不对，一个是盯错了量** |
| **断言比「文件名集合」而不比内容，会让「覆盖写」这一类退化完全隐身** | 「测试不许把截图写进仓库根」那条断言的第一版比的是 `entryList("options-*.png")`：所有**名字**。而变异（把落点改回 `QDir::current()`）**没被检出**——被防的文件名是固定的三张，仓库根历史上就躺着同名的三个文件，写回工作目录只是**覆盖**它们，名字集合一模一样（实测三张截图的 mtime 已经变成 11:28:11，断言依然全绿）。改成比**指纹**（名字 + 大小 + 修改时间）后立刻检出 | 凡是「不许把东西写到 X」的断言，都要问一句「**如果它写回 X 并且覆盖了一个已经存在的同名文件，我这条断言还看得见吗**」。看得见的是内容/时间戳/句柄，看不见的是「有没有多出几个名字」。更普遍地说：**凡是可能已经存在同名产物的场景，比集合都要带上指纹** |
| **本机删除守卫按「一个 turn 内的累积删除数」计数，超限时会让调用它的进程 `SystemExit(1)`** | 变异驱动脚本删到第 N 个文件时整个 Python 进程被带走，栈里只有 `sitecustomize.py` 的 `_exit_bulk_guard_control → raise SystemExit(1)`。**它看起来像「脚本自己崩了」**，实际是宿主环境的守卫，与要验证的东西无关（删除数少时它只打一条 `SAFE_DELETE_BULK_CONFIRM_REQUIRED` 告警） | 变异驱动**不要删文件**：删 `.o` 的唯一目的是「逼 `make` 重编」，把**源文件 mtime 推到当前时间**同样能达到（这也正是本节另一条的做法）。另外，驱动脚本要捕获 `BaseException`（`SystemExit` 不是 `Exception`）并打栈，否则「环境拒绝了」会被读成「代码有问题」 |
| **Qt Test 对 `SIGSEGV` / `SIGABRT` 自己装了处理，用它复现「没写结果就死了」会复现不出来** | 自测需要一条「套件在写完结果前就死掉」的探针（CI 上 `Tests/Folder` 被 glibc 的 `_FORTIFY_SOURCE` abort 掉就是这一类）。第一版用 `std::raise(SIGSEGV)`，Qt Test 接住后**补写了一份结果文件**并把用例记为失败——于是它走的是「有统计行」那条路，验不到目标路径 | 用 **`std::_Exit(3)`**：不跑 atexit、不给任何 handler 机会，稳定复现「硬退出、无结果、stderr 有内容」。**选定探针手段之前，先确认它复现的是目标路径而不是相邻的某条路径**——否则那条断言测的是另一件事，而且它还会绿 |
| **顺手写下的夹具会让两个不同的量恰好相等，于是一个真变异变成等价变异** | 快照测试里四个夹具的每个块都只占一行，于是「前面已累积的行数」恰好等于「块序号」——把 `block.firstRow = result.rows.size()` 变异成 `= index`，**9 处变异里唯一没被检出的就是它**（9 处里 8 处检出）。而报告出来的是「断言没抓住」，看起来像测试有洞 | 补一个**中间块占两行**的夹具（`"a\nb\nc\n"` vs `"a\nX\nY\nb\nc\n"`），`firstRow` 与序号立刻分开、当场检出。**写夹具时问一句「这里有没有两个量在当前数据上恰好相等」**：等价变异会伪装成漏检，而两者的修法完全相反（一个该改数据、一个该改断言） |
| **手推的黄金串与「把实现跑出来的结果抄回去」是两件事** | 新写的快照测试里，五个黄金串是在纸上枚举 Myers 的 `ranges` 再算块与行推出来的，第一次跑就全绿——这算一次独立交叉验证（实现与手推互不相干地得到同一个值）。若改成先跑一遍、把输出粘进测试，得到的东西**只能证明「今天和昨天一样」，证明不了「今天是对的」**，而且它会把当前的 bug 一起固化成「期望」 | 写快照/黄金文件时，**期望值要有一个独立的来源**（手推、另一个实现、规格文档里的例子）。做不到独立来源时，至少在注释里写明「这是从当前行为录制的，不是推导出来的」，让下一个人知道它的证明力只有那么多 |
| **改一处「看起来该有区别」的比较符，可能是等价变异——先看上层不变量，再看本处** | `longestStrictlyIncreasing()` 里把 `<` 改成 `<=` 跑不出任何红。原因不在这个函数：候选的右侧下标**天然互不相同**（每个候选来自一个两侧都唯一的键，唯一键在右侧只出现一次，两个不同的键不可能落在同一行），于是严格与非严格在可达输入上无差别。若按「这里有漏检」去改，方向是反的 | 遇到跑不出来的变异，先问「**有没有一条上层不变量让这对取值不可能同时出现**」，把那条不变量写成注释（本轮写在 `textdiff.cpp` 的 `longestStrictlyIncreasing` 上方），并说明**上层一旦放宽会怎么坏**（重复取值会让锚点循环的下一段右侧长度算成负数）。等价变异不是漏检，但**不写清楚就会在下一轮被当成漏检去修** |
| **`QCOMPARE` 打印不出枚举向量，失败信息会退化成「值不同」** | `QCOMPARE(availableAlignments(), QVector<Alignment>{...})` 能编译，但失败时两侧都打成 `<unknown>`：「清单里多了一项」与「顺序反了」这两件事于是分不出来——而它们在本仓里后果完全不同（前者是算法没实现，后者是**默认值会漂移**，因为默认值取「第一条已实现项」） | 比**可读的投影**而不是原对象：本轮把清单摊成标识符串（`myers,patience`）再 `QCOMPARE`。凡是要比容器 / 枚举的地方都先问「失败时我看得见差在哪一项吗」 |
| **自检返回的问题数是多条规则的并集，凭印象数会数错** | `validateAlignmentTable({})`（空表）第一版断言写成 3 条，实际是 **4** 条：空表本身 1 条、一条都没实现 1 条、两条规格点名的算法各 1 条。测试红了，而**实现是对的**——数错的是断言。这类「自检报告几条」的断言很容易凭印象写，而它一旦写成「恰好 N 条」，就同时变成了对**规则条数**的隐式约束 | 写这类断言前**逐条枚举规则**，再把得到的数字写进测试并配一句说明（本轮在用例里写明「这三类问题各有各的修法，合并成一条会让人只修一处就以为干净了」）。顺带一条：只断言 `problems.size() == N` 还不够，要同时断言**内容**（含不含某个标识符/关键词），否则一个「恒定报 N 条但报错内容不对」的实现照样能过 |
| **`QString::toCaseFolded()` 是 simple folding，不是 Python 的 `casefold()`** | 照「大小写无关」的通用直觉写断言 `STRASSE == straße` 会失败：Qt 5.15.2 实测 `ß`(U+00DF) 折成它自己、`ﬁ`(U+FB01) 折成它自己、`İ`(U+0130) 折成它自己——**都不做多字符展开**（`ẞ`(U+1E9E) 倒确实折成 `ß`）。而失败原因**不在本项目里**，看起来像实现写漏了一条规则 | 拿实测表写断言，不要拿别的语言的折叠语义写（表见 §2）。不展开的收益是**长度守恒**：BMP 逐码位 1:1（实测 1189 个码位会变、0 个长度变化）、非 BMP 的代理对进出都是两个 `QChar`，于是规范化后的下标可以直接当原文下标用。**改成 full folding 之前必须先做偏移映射**，否则 TXT-025 的字符级高亮整段错位 |
| **判等用 `toLower()` 会漏掉希腊语词尾 sigma** | `toLower("ΟΔΟΣ")` = `οδοσ` 而 `toLower("οδος")` = `οδος`（`ς` 保留），两者被判成不同——同一段希腊文只要大小写差异落在词尾就永远忽略不掉。`toCaseFolded()` 把 `Σ`/`σ`/`ς` 一律折成 `σ`。ASCII 上两种写法**完全一样**，所以这个错只有非 ASCII 语料才暴露，而本工具的常见用法恰好是跨平台比同一份文件树 | 大小写判等一律走 `normalizedLine()`（内部用折叠），不要图省事用 `toLower()`。`Tests/Text` 有一行样本专门钉住 `ΟΔΟΣ`/`οδος` |
| **C++ 的十六进制转义取最长匹配，会吃掉后面的十六进制字符** | 语料里写 `"stra\xC3\x9Fe"`（要表达 `straße`）直接编译失败：`hex escape sequence out of range`——`\x9F` 后面紧跟着的 `e` 被当成十六进制位，转义值超出 `char`。报错位置盯着那一行，看起来像字符串本身有问题 | 断成两段：`"stra\xC3\x9F" "e"`。**凡是 `\xHH` 后面紧跟 `[0-9a-fA-F]` 的字面量都要断开**，而不是只在报错时才改 |
| **规范化链的两步可交换：调换顺序是等价变异，不要按「漏检」去改方向** | 把 `normalizedLine()` 里的「先折叠、后空白」改成「先空白、后折叠」跑不出任何红。原因是两者**交换律成立**：折叠既不造出空白、也不吃掉空白（没有任何码位折成空白，也没有空白折成非空白）。这与本节里 TXT-003 那条 `<`/`<=` 是同一类：**真等价变异**与**真漏检**的修法完全相反（一个什么都不用改、一个要补断言），判错方向会白改一轮 | 顺序**不是**契约的一部分，只在注释里写明「调换不该、也不会让用例变红」。真正是契约的是「两步都做、两侧都做、做完才判等」，那三条各有独立用例（`M4` 与 `M6` 两处变异证明它们真的会红） |
| **夹具的两侧必须都含「会被折叠改动」的字符，否则「只折一侧」的变异与正确实现重合** | 反向验证里有一处变异是「只对左侧应用选项、右侧按默认选项算」。若夹具写成左侧 `Alpha` / 右侧 `alpha`（右侧恰好已经是折好的小写），那么「折叠过的左」与「本就没变的右」**在数据上完全一致**，变异后的结果与正确实现逐字段相同，用例静默放过它。这与本节里那条「两个不同的量恰好相等」同源，但方向不同：不是断言抓不住，而是**变异后的输入与原输入重合** | 夹具里刻意留一行让两侧都要被折：`Alpha` / `BRAVO`、`Alpha` / `ALPHA`。**写夹具时问的第二句话是「如果只做一半，这份数据还看得出区别吗」** |
| **对渲染结果断言时，不要另抄一份调色板常量** | 「被忽略的行仍有弱化标记」只能对画出来的东西验，很容易顺手在测试里把几个 RGB 抄一遍。抄一份就有了第二份事实来源：改了视图的调色板，测试仍然用旧常量比对，两边不会同时红。同理，只断言「有标记」还不够——一个把所有行都标记的实现同样能过 | 直接读控件的 `extraSelections`（只有真正进了那里的行才会被填色），并把 Insert / Delete / Replace 三种差异色**从真实渲染里采出来**再断言「忽略色与每一种都不同、且彩度更低」。顺带得到一条反例判据：真正相同的行**不该**有标记 |
| **`Result::differences` 数的是「变更块」不是「行」：夹具缺分隔行会让三种模式的块数分不出来** | 为空白三模式挑语料时写成左侧 `alpha beta` / `gamma delta`、右侧 `alpha  beta` / `gammadelta`——两行都是 Replace，**相邻的两处替换被并成一个块**，于是 `differences.size()` 在 `Exact` 下是 1 而不是 2，三种模式的块数也分不开（`Exact`/`IgnoreChanges`/`IgnoreAll` 期望 2/1/0，实际拿到 1/1/0，断言红了而实现是对的）。这与块边界就是界面上分隔线位置这件事同源 | 两处替换之间**插一行逐字符相同的行**（本轮用 `keep`），块数立刻与期望对齐；顺带这一行还提供了一个免费的反例断言——真正相同的行不该有标记。**写多块夹具时先问「这些变更会不会被合并」**（TXT-009） |
| **界面上「用户看到的」与「实际生效的」是两条链，只钉一条会漏检** | TXT-009 第一版把界面的第 i 行钉在模式表第 i 项上——但它断言的是「选第 i 行，引擎用的就是表第 i 项」，**没有断言第 i 行显示的是什么字**。于是把下拉的铺法改成倒序（文案与序号脱钩）**10 处变异里唯一没被检出的就是它**：值那条路是 `whitespaceAt(index)` 按表取的，倒序只改变了用户看到的字。用户选「忽略全部空白」得到「比较空白」的行为，而 54 条用例全绿 | 两条链都要钉：值那条（第 i 行 ↔ 表第 i 项 ↔ 会话选项 ↔ 落盘值）+ **文案那条**（第 i 行**显示的字** ↔ 表第 i 项的文案）。取「期望文案」不能靠把三行英文抄进测试（那又是第二份事实来源），正解是把标签函数提成**公开静态接口**（`TextCompareView::whitespaceLabel()`），顺序仍由模式表决定，所以不是同义反复；再补一条「三条文案彼此不同」，否则三条一样的文案也能过逐项比对。**判据可以带走：凡 UI 值与 UI 文案同源不同路，两条都要有断言** |
| **`enum class` 成员的类外定义不能落在匿名命名空间里；`tr` 在类外定义处要写限定名** | 把视图里的自由函数提成 `TextCompareView::whitespaceLabel()` 时，定义写在了原位置——而那个位置在匿名命名空间**里面**，报 `cannot define or redeclare 'whitespaceLabel' here because namespace '' does not enclose namespace 'TextCompareView'`（后续 5 条错误都是它的连锁）。改到命名空间外之后，`return tr(...)` 又报 `use of undeclared identifier 'tr'`：`Q_OBJECT` 提供的静态 `tr` 只在**类作用域内**可见，类外定义处必须写 `TextCompareView::tr(...)` | 成员的类外定义一律放在匿名命名空间的**结束括号之后**；`tr` 用限定名。**报错位置（第几行、哪几个标识符）看着像函数体有问题时，先看它到底在不在这块命名空间里**（TXT-009） |
| **批量删 `.o` 会撞本机沙箱的批量删除守卫；`run-tests.sh` 在有套件失败时仍然退 0** | 变异驱动为了让 `make` 重编而删掉所有构建目录下的 `.o`（56 个），被沙箱拦下并**把驱动自己弄死**（`SAFE_DELETE_BULK_CONFIRM_REQUIRED`，阈值 50/轮），表现为「驱动突然只输出一行就结束」。另一处：把 `run-tests.sh` 的退出码当判据会**把「用例全红」读成「变异漏检」**——实测套件失败时它依然 `EXIT=0` | ① 不删文件，改成 `os.utime` 把目标 `.o` 的时间戳**推回一小时前**，同样逼 `make` 重编（源码一侧用普通写入，mtime 自然是当前时刻，避开 `shutil.copy2` 带回旧 mtime 那条坑）；② 判据一律**解析合计行里的失败数**（`合计：N passed, M failed`），不看退出码。两条都只在「本机无人值守跑反向验证」时出现，正常开发踩不到（TXT-009） |
| **变异驱动里「这次真的重编了吗」的判据，必须在**还原源码之前**取** | 驱动为了让 `make` 重编会先 `os.utime(target, (0, 0))` 把 `.o` 的 mtime 回拨到纪元，然后才去读 `.o` 的 mtime 判「重编发生与否」——于是 12 处变异**全部报成「重编发生=False」**，而每一处的 `failed` 都大于 0、构建确实发生了。这个假阴性会让整份反向验证报告在「构建到底有没有带上变异」这一栏上完全失去意义 | 取 mtime 的时机挪到**还原源码之后、回拨之前**：先记录变异前 mtime → 回拨 → 构建 → **立刻**读 mtime 判定 → 才还原源码与 mtime。更稳的写法是干脆不依赖 mtime，改为在变异体里塞一个**编译期可见的标记**（本轮没走到那一步，但下次可以）。**判据：只要报告里出现一整栏恒定的「False/0」，先怀疑判据本身，不要当成结论**（TXT-005） |
| **把「一处改写」拆成多块之后，一切按**块**粒度读的东西都会悄悄错——合并引擎会**丢行**** | TXT-005 把 `Change::Replace` 拆成「配对的修改块 + 未配对的删除 / 新增块」之后，三方合并引擎仍按**块**解读两侧编辑：「左把 B 改成 C、右把 B 改成 D」在 `A B C` / `A D C` / 基线 `A B C` 上被读成**两组互不重叠**的编辑（一组覆盖 `B`、另一组覆盖 `B`），于是基线行 `B` 从结果里消失——输出 `A\nC\n` 而不是 `A\nB\nC\n`。**这是丢数据**，比多报一个冲突严重得多，而它只在 TXT-005 落地之后才可能出现。两方合并的输出行→块映射同样按块粒度读，会让纯新增行挂到错误的旧行上（现象是「点了接受，改动落到了别处」） | 在 `mergeengine` 里加 `changeRunLength()`，把「**基点相接**的同侧相邻非 Equal 块」归并成**一处**改写再交给合并判定；`textmergesession` 的归属计算改用 `differenceRuns()` 建 `runOfBlock` / `runFirstOld` / `runOldCount` / `runFirstNew` / `runFinalOwner` 五张表。判据是「基点相接」（`next.leftStart == previous.leftStart + previous.leftCount`，且两块不能都是纯新增）而**不是**块类型：`addBlock` 已经保证删除排在新增之前、相邻同类块会合并。**可带走的规则：把一个粗粒度单位拆细之后，要把所有「按这个单位计数或操作」的调用点列一遍**——本轮正是靠这条找出四处（状态栏 / 导航 / 复制 / 命令行摘要）与两处合并归属（TXT-005） |
| **`html.contains(payload)` 对「不含正则元字符的普通载荷」是恒真的，转义断言会变成假的** | `Tests/Report` 里那条「HTML 转义每一个非信任字段」的用例原本写死 `model.rows[3]` 并断言 `!html.contains("<script>...")`。TXT-005 改了行序之后下标指到了别的行，于是改成「按内容查找第一个两侧非空的变化行」——第一版改完仍然红，原因是那一行两侧都是**普通中文**（载荷成了 `末行旧` 这种不含任何元字符的串），`html.contains()` 对「根本没被转义的原文」也为真，断言失去区分力 | 断言必须把**载荷自己注入**到待测单元格里（用例先把两侧文本设成 `"<script>alert(1)</script>"`、`"a & b \" c"` 这类真载荷，再导出并逐条查「转义后的形态在、原文不在」），而不是指望夹具里恰好有一行含元字符。**判据：凡断言形如 `!输出.contains(X)`，先问「X 在**未经转义**的输出里还在不在」**——不在的话这条断言什么也没测（TXT-005） |
| **变异让套件**段错误**时，排在它前面的用例会先把进程带走，驱动会把「按顺序还没轮到的那条」错报成漏检** | TXT-001 有一处变异是「两侧都用右侧的行映射」。它让左窗格去索引右文件的第 N 行，而左文件更短 → `QVector::operator[]` 越界 → `refresh()` 里段错误。QtTest 的输出里确实有 `FAIL! : …Received a fatal error.`，但**红的是声明在前面的 `emptySessionAndRealFiles`**（它先跑、先在同一个越界上崩掉），于是进程死在那里、**本轮新写的那条用例根本没机会运行**。驱动按「预期用例名出现在 FAIL 行里」判定，于是把它报成「漏检」——而事实上这个变异被清清楚楚地检出了 | 判定要接受**两种**信号，但必须分开写：① 预期用例名出现在 FAIL 行里；② 整个进程**非正常退出**（`Test crashed` / `Received signal` / 退出码 134 等）。报告里要能看出是哪一种，不能都说成「检出」。更干净的做法是**先挑一个不会越界的变异**（本轮改用「丢掉填充行」——左窗格只显示自己那几行、下标永不越界，于是目标用例自己红、现象干净）。**判据：驱动报「漏检」时，先去看那份 results.txt 里到底有没有 FAIL 行，别信「预期名字没出现」这一个信号**（TXT-001） |
| **用例里的具体数字要么有独立推得出的来源，要么必须是实测的——「看起来差不多」的那个数会红，而且红得莫名其妙** | TXT-001 标准 4 的第②个输入是「两侧各 4000 行、每隔一行共有一行」。第一版断言 `result.rows.size() == 4000`，实测 **4005**。多出来的 5 行与实现正确性毫无关系：两侧相邻的**唯一行**配对时会先算相似度，`left i` / `right i` 这一对的分值取决于末尾数字共有几个字符，于是 2000 对里有 5 对恰好落在出厂阈值 50 的**另一侧**，从「一个替换块」变成「删除块 + 新增块」，每对多一行。这与 §1.29 里那条「四个套件一起变红、真因是夹具与手推分值都错」是**同一条纪律的第二次出现**：夹具里的数字与分值不是「随便填一个」 | 把那个断言换成**可推出来的量**：两侧公共行只有那 2000 行 `shared i`（`left i` 与 `right i` 互不相同），因此最长公共子序列长度就是 2000，任何正确的对齐都必须把这 2000 行报成 `Equal`——断言 `equalRows == 2000`，并在注释里写明推导过程与「预算若被调小这个数会变小，那时先确认是不是真被改好了」。**判据：断言里出现具体数字时，先问「这个数我是怎么知道的」，答案不能是「我刚跑出来的」**（TXT-001） |
| **同一件事的第二条实现路径会让两处变异**互相遮蔽**，两边都测不出来** | TXT-010 第一版把状态栏警告图标的刷新同时挂在两条路上：会话的严重度信号（`SessionArea` 转发 + `MainWindow` 收到后重算）与 `MainWindow::refreshStatusBar()` 里的顺手一刷。理由听起来完全合理——「刷新状态栏时把图标也重算一遍」。但切标签时这两条路**必然同进同出**（`QTabWidget::currentChanged` 里连着 `activeSessionStateChanged → refreshStatusBar()` 与容器对严重度的重播），于是删掉其中任何一条，图标的表现**一点变化都没有**：另一条把它兜住了。18 处变异跑完，M13 / M14 双双报「漏检」，而它们各自都是真变异。**注意这两处漏检既不是探针的问题、也不是等价变异**——它是一个**结构**问题（冗余），而且「再补一条用例」补不出来：两条路同进同出，任何行为断言都同时被两条路满足 | 处置是**删掉冗余的那一条**，把写入点收敛成一个。删的是 `refreshStatusBar()` 里那次调用，保留容器重播（它与 `statusTextChanged` 在切标签时同样重播的既有约定对称）。删完之后两处变异**各自都被检出**。**可带走的判据：一个「删掉之后没有任何用例变红」的分支不是纵深防御，是没人知道的死代码**；写代码时若冒出「顺手也刷一遍」的念头，先问「这一遍能不能被单独打红」。另一条操作要点：**报告里出现「漏检」时，先问「这两处漏检之间有没有关系」**——同一轮里成对出现的漏检，往往共享一个结构原因（TXT-010） |
| **断言里的具体字符串，必须去读实现**印出来的那一句**，不要按印象拼** | TXT-010 的第一版把状态栏期望值写成 `Mixed (LF 1 / CRLF 1)`（照着「括号里列已出现的风格」去推），实测是 `Mixed (LF 1 / CRLF 1 / CR 0)`——`Document::eolDescription()` 的三个计数**总是**都印出来，用不到的那一档是 0。这一条与本轮另一处（`hasMixedEndings()` 的谓词必须与文案的 `Mixed` 分支一致）合起来说明：「按语义推出来的字符串」与「实现真的印出来的字符串」是两件事，而只有后者能当期望值 | 期望值从实测的失败信息里取（QtTest 的 `QVERIFY2(..., qPrintable(session.statusText()))` 会把实参原样打出来，一行就够），并在注释里写明「三个计数总是都印」。**判据：凡断言里出现**拼接出来的**字符串（不是单个常量词），先跑一次把实参打出来再写进去。**这已经是同一条纪律在本文档里的第三次出现**（前两次见 §1.29 的「手推分值」与 §1.30 的「4005 行」）——三次的共同形式都是：**我用推理代替了观测，而推理里少了一个我没注意到的机制**（TXT-010） |
| **「这个是 X 不是 Y」的空侧语义，要按完成标准的动词选块类型，而不是选「能表示」的那个** | 单侧为空时，引擎完全可以输出一个 `Replace` 块（左侧 0 行、右侧 N 行）——它在数学上同样表达了「这 N 行是新的」，界面上也能画对。但规格写的是「标记为**新增**/删除」，而 `Replace` 在界面上表达的是「左边原来有内容、被换掉了」。这一处差异**没有任何既有用例守着**，反过来把 `Insert` 写成 `Delete`（新增的文件显示成被删除）也一样没人发现 | 断言要同时钉**块类型**（`Change::Insert` / `Delete`）、**覆盖计数**（`leftCount == 0` / `rightCount == 0`）、**每一行的两侧归属**（`leftLine == -1`）与**两个方向**。两个方向都验的理由是：只验一侧时，把插入与删除写反不会红。这一类的普遍形式是**「同一个语义有好几个都能表达的编码，必须钉住规格点名的那一个」**（TXT-001） |
| **变异脚本把「构建失败」读成了「漏检」** | TXT-012 的 M2 是「让 `tableIndexOf()` 对表外取值返回 0」。第一版写成 `if (index < 0) index = 0;`，而 `index` 是 `const int` → **编译不过**。套件因此没有产出统计行，而驱动的判据只解析 `合计：N passed, M failed` 里的失败数（本仓库必须这样，因为 `run-tests.sh` 在有套件失败时仍然退 0），于是拿到 `0 failed` 并报成「漏检」——一个真变异被记成了「测试没守住」，方向还是反的 | 驱动要显式区分三种结局：**跑了且全绿**（有统计行且 0 failed）、**跑了且有红**、**根本没跑起来**（没有统计行 / 汇总里出现「另有 N 个套件没有产出统计行」）。第三种一律按「变异无效」处理并重写该变异，不计入漏检。**判据：报告里出现漏检时，先确认那一轮套件真的跑起来过**（TXT-012） |
| **「两份文件相同 ⇒ 只有一个 `Equal` 块」在**有忽略维度命中**时不成立** | TXT-012 的集成用例第一版断言 `blocks.size() == 1` 且 `change == Equal`，实测是 `[Ignored(左2/右2), Equal(左1/右1)]`——被替换规则改写过的那两行，键相同而原文不同，收尾循环把它们判成 `Change::Ignored`（TXT-008 留下的那条通道），同一个相同段因此被切成两个块。红的不是实现，是断言 | 这类用例要断言**块的构成**（把 `Result` 投影成 `change(左N/右N)` 串，个数对了而构成错了才查得出来），并显式接受 `Ignored`；注意 `ignoredBlocks` 数的是**块**不是行（两行合并之后是 1）。**判据：一旦开了「忽略某个维度」的功能，`Equal` 就不再是「相同」的唯一编码**——同族的 TXT-009 / TXT-010 / TXT-011 都适用（TXT-012） |
| **用 `\| tail` 收尾的护栏循环会把退出码吃掉，护栏红了也看不见** | 自查五道护栏的惯用写法是 `for g in check_layering …; do $PY tools/$g.py 2>&1 \| tail -6; echo "exit=$?"; done`。这里的 `$?` 是 **`tail` 的**退出码（恒为 0），于是 `check_spec.py` 明明报了「发现 1 处规格问题」并 `exit=1`，循环却一行行印着 `exit=0`。上一轮 TXT-012 的「五道护栏全绿」就是这么来的——**结论写在文档里，红点还留在仓库里**，直到本轮顺手复查才发现 | 自查五个护栏时**不要把输出管进 `\| tail` 就完事**：要么直接 `$PY tools/$g.py`（退出码就是护栏自己的），要么先把输出收进变量、再单独打 `$?`（本轮的循环已改成后者，五个 `exit=` 一个一个打出来）。**判据：任何「批量跑检查」的循环，最后那句 `echo exit=$?` 里的 `$?` 必须来自被检查的程序本身，不能来自管道里的最后一个命令。** 顺带一条：`check_spec.py` 这类护栏**只在本地跑**，CI 的三条腿不跑它们——红了不会有任何远程信号，只能靠自查 |
| **`tools/check_spec.py` 把「全角括号 + 数字 + 条」当成规格条目数，测试用例数不能用这种写法** | 它的职责是核对**手写文档里写的规格条目数 == 369**，网是「全角括号 + 数字 + 条」/「数字 + 个条目」/「数字 + 条规格」（只扫手写文档，排除生成物与两份竞品测绘文档）。于是 handoff 里一句「新套件 `Tests/TextRules`」后面的用例数（**19** 加一个「条」字）直接把它打红——那句话说的是**用例数**，与规格条目数毫无关系。它**只在本地跑**（CI 的三条腿不跑这五道护栏），所以红了不会有任何远程信号 | 测试用例数一律写**仓库既有的那两种写法**：「（N 个用例函数）」或「（N 条用例函数，…）」（后者之所以安全，是因为「条」后面跟的不是右括号也不是逗号）。**不要**为了省几个字写「（N 条）」。**判据：写测试计数之前先看一眼仓库里别处是怎么写的，别自创缩写**（DIR-008） |
| **「实现早就在」不等于「完成标准被守住了」：藏在 `.cpp` 里的常量等于那条标准裸着** | DIR-008 有五条标准，核对时四条都能在源码里找到对应的动词，于是很容易直接勾掉。但第 3 条的「分块大小有上界」把 `256 * 1024` 藏在 `foldercompare.cpp` 的循环里，第 4 条的「首个差异偏移量在报表中可查看」虽然写进了 `Report::fromFolder`，**却从来没有任何用例见过它**。这两条标准当时的实际状态是：实现是真的，**守住它们的断言一根都没有**——把常量改成 1 GiB、把 `report.cpp` 里那句 `if (entry.firstDifference >= 0)` 整条删掉，全套 3681 条用例一条都不会红 | 处置分三步：① 把上界提成公开常量 `kMaximumCompareBlockSize`（测试要能引用它，否则只能去匹配源码字面量，那是第二份事实来源）；② 给报表侧补一条「偏移量在导出里可见 + 没有结论的条目不许长出偏移量」；③ 用变异反向验证这两条新断言**真的会红**（M7 打上界、再由 `Tests/Report` 那条挡住）。**可带走的判据：完成标准里的动词能在源码里找到时，紧接着问第二句——「它是**怎么**被验的，验的是这个量还是它旁边的量」**（DIR-008） |
| **完成标准里带副词的那个词最容易没人守；更隐蔽的是「看起来守住了，其实 ASSERT 的是它旁边的量」** | DIR-008 第 1 条是「遇首个不同字节即可**提前**判定不同」：「逐字节」有断言、「判定不同」有断言，唯独「**提前**」没有。把 `compareFile()` 里「命中即 `break`」那一句删掉，**3687 条用例一条都不会红**——既有夹具全是「大小相同、只有一处差异」，命中之后剩下的块两侧全同，循环继续跑也改写不了 `firstDifference`，于是这处 `break` 是没人知道的死代码。**更隐蔽的一层**：`exactBytesAcrossChunkBoundary` 断言了首个差异的偏移量（500000）并刻意跨了分块边界，读起来像把第 1 条钉住了，实际钉的是「哪个字节不同」而不是「在哪停下」。同族的副词还有「**只**比较前 N 字节」「**不**读完整文件」「**默认**关闭」 | 判据只有一句话：**把那个词反过来写，谁变红？** 答不出来就是没守。补法往往不需要改生产代码，而是要**把夹具掰到能让那个词产生可观察差异的形状**——本轮给测试替身加了 `physicalPaths`（逻辑路径 → 真正被打开的路径）与 `frozenInfo`（冻结的申报元数据），造一个「申报尺寸 600000、实际只有 100 字节」的对手，于是「命中之后会不会继续读」第一次有了可观察量（`firstDifference` 是 100，还是被改写成 262144 / 524288）。**要记住的一般规律是：默认夹具形状会天然掩盖某些性质**——「等长、只差一处」让「停 / 不停」两种实现产出完全相同的结果，所以「这条一直没被验过」通常不是没人想到，而是没人把夹具掰到看得出差别的位置（DIR-008） |
| **`-v2` 的输出只写文件、不写 stdout；而进程被硬杀时文件里**仍然留着尾巴** | 两件事都是本机实测的（探针 `/tmp/vprobe`，不进仓库）。① `-o <file>,txt` 下 **stdout 是 0 字节**——与文件头第 6 条同源，所以诊断产物一律「写文件、再由脚本读回来」。② 更要紧的一条：`std::abort()` 与连 `atexit` 都不跑的 `std::_Exit(3)` 之下，verbose 文件里**最后一行 QDEBUG 与 `INFO : … entering` 都还在**（Qt 的纯文本日志器逐条写盘）。⇒ 「跑到哪一条用例才崩」是**可回答的**，不需要依赖任何一次 flush，也不需要给崩溃的进程加什么钩子。**「崩溃时机」这类怀疑要先用最小探针测一次再下结论**——想当然地认为「缓冲区没冲、尾巴一定丢」会让这个功能根本不敢做（ENG-003 / ENG-004） |
| **断言要盯「这份夹具真正会变的那个量」，不是「理论上只有它才会有的那个量」** | 同族错误第三次（前两次见 §1.24 的 M4 与 M13）。本轮守「补跑没有改写首轮的 `results.txt`」用的判据是 `grep 'Loc:'`——听着很对，`Loc:` 确实只有 `-v2` 才会写。但 `ZZProbeHardExit` 这个探针里**一条断言都没有**，它的 `-v2` 输出里根本没有 `Loc:`，于是「结果文件被补跑覆盖了」这个事实（实测确实被覆盖，`INFO :` 行就在里面）**在断言上完全看不见**，变异整条漏检。改成「-v2 独有的行首标记」（`INFO` / `DEBUG` / `QDEBUG` / `QWARN`）之后立刻检出。**判据的写法要跟着夹具的形状走**：先问「这份夹具跑一遍，哪些字节真的会不一样」，再挑其中的一个当判据 |
| **验证工具自己偶发一次红，会把「漏检」伪装成一个结论** | 本轮 `run-tests.sh --self-test` 出现过偶发红点（约 2/13 次；HEAD 版 11 次里 0 次）：某一次跑里 `ZZProbePass` 的产物与合计对不上（`results.xml` 里没有 `<testsuite>`、`stderr.log` 非空、两遍合计不同）。6 次空跑循环 + 3 次 CPU 负载下对比 + 8 次 HEAD 对照都**没能复现**，机制未查明。**危险的地方在于它的表现形状**：在反向验证里，「变异体多红了几条」会被判成「漏检」，而实证是工具在抖。**处置**：驱动里显式识别这个签名——基线撞上就重跑一次基线，某条变异的多余红点**全部**落在签名里才重跑一次，出现签名之外的红点一律按真红点处理、**绝不重试**。**要记住的一般规律是：一个「偶尔报红」的验证工具比一个「总是报红」的更危险**——后者立刻会被修，前者会被当成噪声，直到某天它替一个真的漏检背了锅（ENG-003） |
| **平台分歧的「超时」形状：Linux 上超时的套件**也会**留下 `Totals:` 行** | 「超时」在两个平台上是两种形状。Linux 上 Qt Test 接住 `SIGTERM` 后会**自己补写一份统计行**再以 `SIGABRT` 收尾（`status=134`、`has_summary=1`）；macOS 上进程直接被 `TERM` 带走，什么也不写（`has_summary=0`）。于是以「有没有 `Totals:` 行」为判据的任何逻辑，在 ubuntu 上会把超时套件当成「正常跑完」——实测后果：超时探针的 `1 passed, 1 failed` 被算进合计，自测连续 13 次红，**并且因为自测红了、后面的「运行测试套件」被 skip，整条 ubuntu 腿一个套件都没跑过**（§1.35） | 判据写成「有统计行 **且** 没超时」（`hs -eq 1 && to -eq 0`）；凡涉及「进程怎么死的」的判定，都要问一句「这个信号在两个平台上是不是同一种死法」 |
| **一条只在一个平台上成立的边界 = 本机永远绿的漏检** | 修好上一条之后发现：**本机根本验不到这条边界**——macOS 上挂死探针不写统计行，`hs` 恒为 0，累加那一步压根不会被走到，于是**任何本地变异都 redden不了它**（真实的漏检机制：断言写在 CI 上、本机永远绿，而 CI 红了又被当成「环境问题」）。**光把实现改对是不够的，得让边界在所有平台上都可观察** | 改**夹具形状**而不是改断言：让挂死探针从 `QCoreApplication::arguments()` 里取出 `-o <file>,junitxml` 的路径、截掉逗号后面的格式，**自己先写一行 `Totals:` 再挂死**。两个平台形状一致之后，删掉 `to -eq 0` 这半句，合计立刻变大（5 处变异 5 处检出） |
| **断言失败只说「不相等」，CI 上就无从定性** | `st_line_matches()` 一开始只报一句「不相等」，而它断言的期望值是**硬编码的一整行**文本。CI 上那条红连续 13 次没人能一眼定性——因为日志里既没有实际值、也没有「差在哪」，只能靠起一条 scratch 分支专门跑一次去捞证据 | 失败时**同时打印期望与实际**（`st_line_matches` 三个字段：期望 / 实际 / 没匹配上时的原话）。**推而广之：凡是「整行相等」这类断言，失败信息必须自带两边的值**，否则它只是在说「你猜」 |
| **「两遍跑同一个目录」的验证：证据必须按遍当场快照** | 自测的两遍（并行 4 / 串行 1）共用同一个构建目录，第二遍会把第一遍的 `results.txt` / `stderr.log` / `results.xml` 全部覆盖。于是「第一遍到底产出了什么」在断言阶段**已经拿不到了**——上一轮那条偶发红点（`ZZProbePass` 的产物与合计对不上）就是因为这个而无法定位 | 每一遍结束**立刻**快照证据到 `evidence-jobs4.txt` / `evidence-jobs1.txt`（合计行、运行器自己报的异常清单、每个探针的 `summary.env` 字段与 `Totals:` 行），断言与排查都读快照。**验证「两遍的差异」之前，先确保两遍的产物不会互相覆盖** |
| **`QTEST_APPLESS_MAIN` 的探针里没有 application 对象** | 新挂死探针第一版要读命令行参数（`-o <file>,junitxml` 的落点），于是 `QCoreApplication::arguments()` 报 `Please instantiate the QApplication object first`，**三个探针同时失败**、一个字都没写出来 | 改用 `QTEST_MAIN`（本工程 `QT -= gui`，于是创建的是 `QCoreApplication`）。**探针一旦要用 `QCoreApplication` 的任何静态设施，`QTEST_APPLESS_MAIN` 就不成立** |
| **同一判据在仓库里写了两遍时，单摘一道是「等价变异」，会被误报成漏检** | DIR-011 的变异 M1 只摘掉 `applyBaselineStatus()` 的 `entry.isDirectory()`，全套 27 条用例**一条都不红**——因为 `sideDiffersFromAncestor()` 里还有一道 `side.kind == Kind::Directory → return false`。M4 同形：只摘掉 `timeRelationFor()` 的 `exists()` 判断也不红，因为 `compareTimes()` 开头还有一道 `!isValid()`（不存在的侧时间戳本来就是无效的）。两道防线互相遮蔽，**任何一道单独消失都不可观察** | 这**不是缺口**（行为确实被守着），但报「漏检」会把方向指反。判据：**先问「这个行为是不是有第二个实现处」，有就一次改多处**——驱动因此支持 `edits` 列表（M1 / M4 各改 2 处，改完立刻红）。**代价要一并记下**：两道防线里有一道是冗余的，冗余本身有维护成本；这里选择保留，因为两道守的不是同一件事（一道守「目录不适用基线」，一道守「目录自身的时间戳不算内容证据」），只是在这一条上重合 |
| **参数化的分支（`bool fromUser` 之类）在「程序性路径」上可能恒被覆盖或恒等，于是它是一段没人知道的死代码** | DIR-003 的 `applyTierToControls(tier, bool fromUser)` 把「写深度控件」放在 `if (fromUser)` 里，注释写着「恢复存档时一个值都不动」，读起来是纵深防御。实测把它整段删掉**没有任何用例变红**（变异 M13 第一次跑就是 green）：程序性路径（`setOptions()`）里那条写入紧跟着就被 `setValue(qBound(options.maximumDepth, …))` 覆盖；而深度控件自己的处理器那条路上，由档位反推出来的深度**恒等于**控件当前值（`probe.recursive` 默认 `true`，于是 `Full/4→Full`、`OneLevel/1→OneLevel`、`DirectChildren/0→DirectChildren` 都是恒等写入） | 与上一条**同一个判据**：**一个「删掉之后没有任何用例变红」的分支不是纵深防御，是没人知道的死代码**。这里的处置与 TXT-010 那条一致——**收敛成一个写入点**（深度控件的写入者只剩「用户换档处理器」与 `setOptions()`），收敛之后 M13 立刻变红。**参数化布尔开关尤其要怀疑**：它把「两种调用者」编码进同一个函数，而其中一种调用者的行为往往恰好是恒等或恒被覆盖 |
| **「实现里明明正确处理了某个边界，但没有任何输入会走到那条路」——这类正确需要一行表格才能守住** | DIR-003 里两处都中了这个形态。① `recursionBoundaryExplanation()` 印的是 `qBound(0, maximumDepth, kMaximumRecursionDepth)` 而不是原值，写得对；但所有既有输入都是界内值（2 / 4 / 128），把 `qBound` 换成原值**全绿**。② `isStrictAncestor()` 里 `ancestor.endsWith('/') ? ancestor : ancestor + '/'` 这个三元专门处理「上级是文件系统根」——不写它的话 `/` 会拼成 `//`；但表格里所有用例的上级都是普通目录，把三元换成 `ancestor + '/'` 也**全绿** | 判据还是那句「**把那个词反过来写，谁变红？**」，只是这次要问的是「**这条处理有没有输入能走到**」。补法极便宜：纯函数各加一行——文案那条断言「传 300 进去必须含 256、不含 300」；路径那条加一行 `{"cycle", "/", true}`。加完之后两处变异（M10 / M09）双双被检出。**一般规律：凡是「写了条件分支来兜一个少见输入」的地方，那个输入必须在表格里出现一次**，否则那条分支是替未来的某个 bug 提前写好的注释，而不是防线（DIR-003） |
| **「分支写了」不等于「判据被守住了」：把校验函数里每一条报错文案当针去搜测试集，搜不到的就是没人喂过输入的分支** | DIR-012 的 `validateOneColorScheme()` / `validateColorSchemeTable()` 里有五条分支（缺展示名 / 缺说明 / 参考背景不是 `#rrggbb` / 两档背景不呈一明一暗 / 「已排除」弱化色不是 `#rrggbb`）在整个测试集里**一句断言都搜不到**。它们不是写错了，而是没有任何夹具能触发。判据仍是那句「把它删掉，谁变红？」，只是这次要先问「有没有输入能走到这里」；做法是把每条 `problems <<` 的中文片段当针 `grep` 一遍。补法是**掰夹具**（新加一个用例逐条把表弄坏），**不是删分支**——删掉会让一段本来就正确的校验变成没人知道它存在过 | DIR-012 |
| **探针式变异要专门挑「看着像冗余、其实在守第二句话」的行** | DIR-012 里 `ColorScheme::coversEveryStatus()` 的 `return highlights.size() == statuses.size();` 挂在 `&&` 之后，看着像一句多余的兜底；换成 `return true;` 之后 **29 条用例一条都不红**。既有夹具只有「九档齐全」这一种形状，而这一句查的是「**没有多余的行**」。补齐三种形状的夹具（十行九档 / 八行缺一档 / 十行含表外状态）之后当场变红。同族教训见上面「完成标准里带副词的那个词」那一条 | DIR-012 |
| **本仓没有 `-MMD` 依赖跟踪：改头文件的变异不会触发重编，而「把 `.o` 的 mtime 推回一小时」这个手法对它静默失效** | 实测 `grep -c MMD _test-build/StatusPalette/Makefile` = **0**，因此 make 只比 `.o` 与**它自己的 `.cpp`**，不知道 `.cpp` 依赖哪个头文件。于是「改头文件 + 把 `.o` 推回一小时」在别处好用的手法在这里失效（`.cpp` 的 mtime 是 23 小时前，比被推后的 `.o` 还旧 ⇒ make 判定不需要重编），表现为「变异没编进去 → 被误报成漏检」。两条可行做法：① 定点删除那几个 `.o`；② **把用到该头文件的 `.cpp` 原样重写一遍**刷新它的 mtime（内容一字不改，make 照样重编）——第 ② 条不删任何文件，因此在删除守卫已经触发过的轮次里仍然可用 | DIR-012 |
| **沙箱的批量删除守卫按轮累计：一轮里删够 50 个之后，后面任何一次 `unlink` 都会被拦** | DIR-012 的变异驱动连跑两轮，把 `.o` 的 mtime 推回一小时（不删）是安全的，但头文件那一处需要定点删 3 个 `.o`；第二轮跑到那里时守卫报了 `SAFE_DELETE_BULK_CONFIRM_REQUIRED {"count":109,"threshold":50,"scope":"turn"}`，脚本**在「源码还没还原」的位置上直接退出**。两点教训：① 只要有可能删文件，就把还原写在 `finally` 里（本轮因此没有留下变异残留）；② 能用「重写源文件刷 mtime」实现的，就不要用删除 | DIR-012 |
| **驱动读子进程输出必须写 `errors='replace'`** | 变异驱动的 `subprocess.run(..., text=True)` 在读到编译器回显的中文路径时抛 `UnicodeDecodeError`，**整轮在那一处中断**——M28 之后的三个变异根本没跑到，而报告里看起来只像是「跑完了 27 处」。中断时机恰好在「变异已写入、还没还原」之间，靠着 `finally` 才没留下残留。构建输出不保证是 UTF-8 这件事，在有中文路径的仓库里是常态 | DIR-012 |
