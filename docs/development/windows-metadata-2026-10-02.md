# PLAT-002 / #325：Windows 当前元数据查询

## 依据与边界

Windows Qt 5.15.2 / MinGW 8.1 的 `256532d` 产物中，Snapshot 的
`captureMetadata` 通过，`captureHashes` 与 `saveLoadRoundTripWithoutContents`
在源目录稳定性复核时失败；`ac47de0` 中前者通过、后者仍失败。FolderMerge
五个直接从写入进入扫描的用例失败，先经过 `treeContents` 读遍目录的文件决策
用例通过。此分布与 NTFS 搜索索引缓存过时一致，但旧产物未记录具体差异字段，
不能把推断写成已测得的修改时间差。

[Microsoft FindFirstFileW 文档](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfilew)
明确说明 NTFS 搜索返回的属性可能不是当前值，并要求使用
`GetFileInformationByHandle` 获取当前属性；该搜索 API 也不能直接取得卷根属性。
[CreateFileW 文档](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)
说明 access=0 可查询元数据、`BACKUP_SEMANTICS` 用于目录句柄，
`OPEN_REPARSE_POINT` 阻止跟随重解析点。

后续 `b1e6786` 的正式 Windows 产物确认：WindowsFileTime 31 passed / 0 failed，
但 Snapshot 的保存加载采集、SyncBaseline 的真实往返仍失败，FolderMerge
仍有 5 处失败、FolderMergeView 仍有 3 处失败。因此 FILETIME 修复没有消除
这组目录一致性问题；本次句柄查询修改尚未进入该次运行，仍需后续 CI 核对。

这是与已经修正的 FILETIME 整数溢出/单位问题分开的查询契约修正。
本轮不修改 Snapshot 或 FolderMerge 的生产一致性判断，不忽略大小、修改时间、
属性、类型、不存在状态或错误，不通过延时、重试或预读让测试假绿。

## 实现

- Windows `stat` 使用 `CreateFileW(access=0, OPEN_EXISTING)` 与
  `GetFileInformationByHandle`；共享读/写/删除，不读取内容，不请求写权限
- 同时使用目录和不跟随重解析点标志；活链接、坏链接都查询链接自身
- `exists` 直接复用 stat，避免卷根可 stat 却报不存在；长路径、不跟随和
  原始错误/成功复位也共用同一行为，悬空链接仍算存在
- 大小、时间、属性和路径/名称转换维持现有映射，目录只读位仍按原生属性表示
- 查询失败在关闭句柄前保存 Win32 原始码；成功才清除错误
- 枚举由 FindFirst/FindNext 取得原始名称，再逐项调用 stat，类似 POSIX 的
  readdir+lstat；子项查询失败和 FindNext 中途失败都标记不完整，不用旧缓存兜底
- 每个子项额外打开一个元数据句柄，不能宣称性能不变。专项测试记录 1000 个
  条目的三次真实枚举耗时及平台，不设置受 CI 负载影响的时间阈值

## 回归设计

`FileMetadata` 独立工程：

- 四类全新目录夹具各重复八次，前后比较大小、修改时间、属性和类型；错误时
  输出 ASCII 字段名与精确原值，Windows 控制台编码不会抹掉差异数字
- Windows 写句柄保持打开，用直接 `GetFileInformationByHandle` 核对黄金时间，
  再检查 stat 与枚举；旧搜索缓存实现可能落后，正式原生 CI 判定具体表现
- 卷根目录：旧 FindFirst stat/exists 不支持卷根，当前句柄查询均应成功
- 文件/目录的活链接和悬空链接均核对 stat、exists 和枚举，只有明确缺少
  Windows 符号链接权限才跳过
- 原始错误码、成功复位、空错误输出指针；目录只读/隐藏位保持原义
- 1000 个 Unicode / 大小写名称完整枚举，计数/字节数精确核对并输出耗时

Snapshot 新增八行故障注入，独立改变目录大小、修改时间、属性、存在性、目录/
链接类型、时间有效性和 stat 错误，均须丢弃整个部分快照，原文件内容不变。

## 本地验证与限制

- Linux Qt 5.15.15 / GCC 14，独立 `make -j2`：Snapshot 122 passed / 0 failed /
  0 skipped；FolderMerge 31/0/0；SyncBaseline 116/0/0；FileMetadata 9/0/9，
  九个跳过都是原生 Win32 专属行（含数据行），不算 Windows 通过
- 独立副本把目录稳定性检查变异为恒真，新增八行全部精确失败（2 passed /
  8 failed）；最终生产源码未作该变异
- Linux 的 1000 项三轮枚举在本次运行约 0.9–1.3 ms，仅为本机记录
- 官方 Windows Qt 5.15.2 头、i686 GCC 14 / MinGW-w64 v12：完整
  filesystem_win.cpp、FileMetadata 与 Snapshot 测试翻译单元均以 `-Wall -Wextra -Werror`
  编译为 i386 COFF；不是正式 MinGW 8.1 或原生运行证据
- Windows API 宽字符、App→Views→Services 分层与空白差异护栏通过
- 本机没有 Windows 运行时；真实 NTFS 缓存、Win32 错误、符号链接权限与
  枚举性能仍须正式 Windows CI 验证。Linux 耗时不代表 Windows 性能
