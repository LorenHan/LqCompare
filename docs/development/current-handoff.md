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
OPT-010 见 [issue #365](https://github.com/LorenHan/LqCompare/issues/365)。

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


## 2. 已验证的事实（不用再花时间确认）
| 项目 | 结论 | 验证方式 |
| --- | --- | --- |
| 构建 | Qt 5.15.2 clang_64 上 qmake + make 通过，产出 `dist/macos/LqCompare.app` | `qmake && make -j8` |
| 运行 | 主程序离屏启动正常，日志显示「Ribbon 构建完成：10 页 / 45 组 / 169 个按钮」 | `QT_QPA_PLATFORM=offscreen ./LqCompare --log-level info` |
| 测试（全量） | **3598 passed / 0 failed / 2 skipped，66 个套件**（2026-09-21 07:5x 实测，用了本轮改过的 `run-tests.sh`；全套 2 分 59 秒）。本轮没有增删用例，数字与上一轮一致是预期的。此前一轮是 3596；上上轮为 FMT-001 补了两条用例（`Tests/Format` 80 → **82**）。2 条跳过分别来自 `PathName`（40/0/1）与 `Registry`（61/0/1），都是按平台条件跳过的用例 | `Code/Tests/run-tests.sh` |
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
| Windows 宽字符 API | 通过（**332** 个源文件、清单内 43 个 API；自测 17 个样本）。本轮从 326 涨到 332，正是 OPT-010 新增的两个模块（`logfiles` / `diagnostics` 的 `.h` 与 `.cpp`） | `python3 tools/check_winapi.py [--self-test]` |
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

| `810262f` | **让流水线的输出真的可读**（ENG-004 的第二批修复，由第一次真实 CI 跑出来的结果驱动）：不再用 `-o -,txt` 从 stdout 拿测试结果（Windows 上一行都不输出、文件产物却正常，于是日志没有 `Totals:` 而合计被算成 0），改成只写文件再由脚本 `cat` 回来，并在跑之前 `rm -f` 上一轮产物（否则套件崩溃会被上一轮的旧结果掩盖）；崩溃的套件在合计之外单独点名（`34 个套件红` 与 `26 failed` 两个都对，但以前没人解释差从哪来）；构建输出落盘 `<套件>/build.log` 并加进上传产物（以前丢 `/dev/null`，17/26 个构建失败一个字的原因都没有）。10 处变异 10 处检出、另有 8 项行为断言 | ENG-004 |

> 上面这张表里，PLAT-005、FILT-001、SESS-001、SESS-002、SESS-006、SESS-007、
> FILT-005、FILT-003、FILT-002、FILT-004、`ac32665`、`c03dc79`、`bdede29`、`d084060`、
> `a1cd3ee`、`786cf46` 与 `810262f` 这十七行由**单独的纯文档提交**补写提交号，
> 原因见下一段——把一个提交的提交号写进它自己，会因为 `--amend` 每次都改变提交号而永远对不上。

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
3. 顺手把 `actions/checkout@v4` / `actions/upload-artifact@v4` 升到 v5
   （运行器已有 Node 20 弃用告警，现在是警告、将来是错误）。
4. 上面三条做完再回到功能条目。

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

### 4.1 选下一步之前先看这一节：哪些条目被谁阻塞

**为什么单独写一节**：本项目的推进方式是「一次闭环一条 issue」，
而**被阻塞的条目照样能提交一堆漂亮的代码**——它们只是永远无法把完成标准勾上，
因为验收依赖的东西还不存在。等发现时已经写了几百行没人用的代码。
所以选条目前先在这里查一次。

| 条目 | 被谁阻塞 | 现在能做多少 |
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
| `ENG-004` 持续集成流水线 | ~~「没有 CI」~~、~~「从来没跑过构建与测试」~~ 均已修；**三条腿的真实结果也已拿到（run `35546164218`）** | 四个阶段、三平台矩阵、产物上传都已写进 `.github/workflows/build.yml`；`run-tests.sh` 的**七项**跨平台问题已修（见 §1.20）。第一次真跑的证据：三条腿都跑完了四个阶段、上传了 6 个 artifact，**流水线本身按设计工作**（拿不到 LqRibbon 时正确跳过主程序构建与打包、测试照跑）。**三条腿现在都是红的，红的不是流水线而是被测对象**：`Archive` 套件在 ubuntu/macOS 上因 CI 的 Python 3.14 抛 `UnicodeDecodeError`（**已修，见 §1.21**，但要等下一次 CI 确认）；ubuntu 17 个 / Windows 26 个套件构建失败（`build.log` 已上传，**下一轮读它定位**）；Windows 的 8 个套件没产出 `Totals:` 行（已由本轮的单列机制暴露）。另有一处**结构性限制**：主程序构建、打包、主程序产物上传这三步都要私有仓 LqRibbon，没有 `MYCLASS_TOKEN` 时在公开 CI 上永远走不到，Windows 腿的打包分支（`7z`）因此是**未验证代码**。`ENG-003` 还差的两项（单套件超时、并行执行）不属本条 |

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
| 图标校验只认 `:/Pictures/x.svg` | 用 `icon("x.svg")` 辅助函数的地方被误判为未引用 | 校验同时认裸文件名 |
| Qt 5.15.2 在 macOS 26 SDK 上 | qmake 报 SDK 版本不支持的警告 | 只是警告；本项目已实测可编译运行 |
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
