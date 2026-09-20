#include "sessiontype.h"

#include <QCoreApplication>

namespace LqCompare {

// -----------------------------------------------------------------------------
// 枚举与标识
// -----------------------------------------------------------------------------

const char *sessionGroupIdentifier(SessionGroup group)
{
    switch (group) {
    case SessionGroup::Text:
        return "text";
    case SessionGroup::Folders:
        return "folders";
    case SessionGroup::Data:
        return "data";
    case SessionGroup::Advanced:
        return "advanced";
    }
    return "text";
}

QString sessionGroupLabel(SessionGroup group)
{
    switch (group) {
    case SessionGroup::Text:
        return QCoreApplication::translate("SessionType", "文本");
    case SessionGroup::Folders:
        return QCoreApplication::translate("SessionType", "文件夹");
    case SessionGroup::Data:
        return QCoreApplication::translate("SessionType", "数据");
    case SessionGroup::Advanced:
        return QCoreApplication::translate("SessionType", "系统");
    }
    return QString();
}

const char *sessionEditionIdentifier(SessionEdition edition)
{
    switch (edition) {
    case SessionEdition::Pro:
        return "pro";
    case SessionEdition::Standard:
        return "standard";
    }
    return "standard";
}

QString sessionEditionLabel(SessionEdition edition)
{
    // 不翻译：这是 Beyond Compare 自己的写法，也是研究基线与 issue 正文里
    // 一直在用的词。翻成「专业版」之后，规格与代码就对不上号了。
    return edition == SessionEdition::Pro ? QStringLiteral("Pro") : QStringLiteral("Standard");
}

const char *sessionPlatformScopeIdentifier(SessionPlatformScope scope)
{
    switch (scope) {
    case SessionPlatformScope::WindowsOnly:
        return "windows";
    case SessionPlatformScope::All:
        return "all";
    }
    return "all";
}

QString sessionPlatformScopeLabel(SessionPlatformScope scope)
{
    switch (scope) {
    case SessionPlatformScope::WindowsOnly:
        // REG-001 的边界原话是「必须在 Home 页明确标注「仅 Windows」」，
        // 就用这四个字，不要改写成「Windows 专用」「只支持 Windows」之类——
        // 那样同一个状态在三处界面会有三种说法。
        return QCoreApplication::translate("SessionType", "仅 Windows");
    case SessionPlatformScope::All:
        return QString();
    }
    return QString();
}

bool currentPlatformAllows(SessionPlatformScope scope)
{
    switch (scope) {
    case SessionPlatformScope::All:
        return true;
    case SessionPlatformScope::WindowsOnly:
#ifdef Q_OS_WIN
        return true;
#else
        // 非 Windows 返回 false，但**不要**在这里给「替代实现」的暗示。
        // PLAT-005 已经定过这条纪律：能力不可用时如实说不可用，
        // 把「做不到」包装成「假成功」比直接说不行糟得多。
        return false;
#endif
    }
    return false;
}

bool isValidSessionTypeId(const QString &id)
{
    // 手写而不是用 QRegularExpression：一是这条规则每登记一个类型就要跑一次，
    // 二是 Qt 5.15 的正则没有匹配超时（见交接文档 §6 里 FILT-002 的那条），
    // 一个手写的字符循环在这里更简单也更可预期。
    if (id.isEmpty()) {
        return false;
    }
    const QChar first = id.at(0);
    if (first < QLatin1Char('a') || first > QLatin1Char('z')) {
        return false;
    }
    for (int index = 1; index < id.size(); ++index) {
        const QChar character = id.at(index);
        const bool allowed = (character >= QLatin1Char('a') && character <= QLatin1Char('z'))
                || (character >= QLatin1Char('0') && character <= QLatin1Char('9'))
                || character == QLatin1Char('-');
        if (!allowed) {
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// 描述子
// -----------------------------------------------------------------------------

QString SessionType::describe() const
{
    const QString platform = sessionPlatformScopeLabel(platforms);
    QString text;
    text += QStringLiteral("%1（%2 / %3）\n").arg(id, displayName, englishName);
    text += QStringLiteral("  分组：%1　版本：%2　平台：%3\n")
                    .arg(sessionGroupLabel(group), sessionEditionLabel(edition),
                         platform.isEmpty() ? QCoreApplication::translate("SessionType", "全部")
                                            : platform);
    if (fileMasks.isEmpty()) {
        // 空掩码有四种完全不同的成因，读的人必须能分清是哪一种——
        // 否则「为什么这个类型从不被自动选中」会被当成缺陷反复排查。
        text += QStringLiteral("  掩码：（无——只由显式入口打开，或按排除法兜底）\n");
    } else {
        text += QStringLiteral("  掩码：%1\n").arg(fileMasks.join(QLatin1Char(' ')));
    }
    text += iconKey.isEmpty()
            ? QStringLiteral("  图标：（未指定）\n")
            : QStringLiteral("  图标：%1\n").arg(iconKey);
    text += QStringLiteral("  说明：%1").arg(summary);
    return text;
}

// -----------------------------------------------------------------------------
// 注册表条目
// -----------------------------------------------------------------------------

QString SessionTypeEntry::unavailableReason() const
{
    if (isAvailableHere()) {
        return QString();
    }
    switch (type.platforms) {
    case SessionPlatformScope::WindowsOnly:
        return QCoreApplication::translate(
                   "SessionType",
                   "仅 Windows 可用：%1 依赖 Windows 的系统接口，本平台没有对应实现。")
                .arg(type.displayName);
    case SessionPlatformScope::All:
        break;
    }
    // 走到这里说明 isAvailableHere() 与 platforms 的判断不一致——那是代码问题，
    // 不是用户的错。返回一句能直接显示的兜底话，而不是空串（空串会让界面显示
    // 一个没有任何解释的置灰项）。
    return QCoreApplication::translate("SessionType", "当前平台不支持该会话类型。");
}

// -----------------------------------------------------------------------------
// 注册表
// -----------------------------------------------------------------------------

SessionTypeRegistry::SessionTypeRegistry() = default;

void SessionTypeRegistry::clear()
{
    m_rows.clear();
}

bool SessionTypeRegistry::isEmpty() const
{
    return m_rows.isEmpty();
}

int SessionTypeRegistry::count() const
{
    return m_rows.size();
}

const SessionTypeRegistry::Row *SessionTypeRegistry::rowForId(const QString &id) const
{
    for (const Row &row : m_rows) {
        if (row.entry.type.id == id) {
            return &row;
        }
    }
    return nullptr;
}

bool SessionTypeRegistry::add(const SessionType &type, const SessionFactory &factory,
                              QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    if (!isValidSessionTypeId(type.id)) {
        return fail(QCoreApplication::translate(
                        "SessionType",
                        "类型 ID「%1」不合法：只允许小写字母、数字与连字符，且必须以字母开头"
                        "（例如 text、folder-merge）。ID 是保存进会话文件的机器键，"
                        "因此不接受大写与空格。")
                        .arg(type.id));
    }
    if (type.displayName.isEmpty()) {
        return fail(QCoreApplication::translate("SessionType", "类型「%1」没有显示名。")
                        .arg(type.id));
    }
    if (rowForId(type.id) != nullptr) {
        return fail(QCoreApplication::translate(
                        "SessionType",
                        "类型 ID「%1」已经登记过了。ID 一经发布不可改名，也不可重复——"
                        "重复会让按 ID 查找返回哪一个变成实现细节。")
                        .arg(type.id));
    }

    // 掩码在登记时就编译好：按掩码查询是文件夹扫描的热路径，
    // 每匹配一个文件重编译十四组掩码没有意义。
    QVector<Filter::Mask> compiled;
    compiled.reserve(type.fileMasks.size());
    for (const QString &pattern : type.fileMasks) {
        if (pattern.trimmed().isEmpty()) {
            return fail(QCoreApplication::translate(
                            "SessionType", "类型「%1」的掩码列表里有一个空条目。")
                            .arg(type.id));
        }
        const Filter::MaskParseResult parsed = Filter::Mask::compile(pattern);
        if (!parsed.ok()) {
            // 整条拒绝而不是丢掉这一条掩码：丢掉之后这个扩展名再也不会被自动选中，
            // 而界面上看不出任何异常——「掩码写错了」是能在启动时发现的，
            // 「某些文件双击没反应」不是。
            return fail(QCoreApplication::translate(
                            "SessionType", "类型「%1」的掩码「%2」无法解析：%3")
                            .arg(type.id, pattern, parsed.error.describe()));
        }
        compiled.append(parsed.mask);
    }

    Row row;
    row.entry.type = type;
    row.entry.factory = factory;
    row.masks = compiled;
    m_rows.append(row);
    if (error) {
        error->clear();
    }
    return true;
}

int SessionTypeRegistry::addBuiltInTypes(QString *error)
{
    if (error) {
        error->clear();
    }
    int added = 0;
    const QVector<SessionType> types = builtInSessionTypes();
    for (const SessionType &type : types) {
        QString failure;
        if (!add(type, SessionFactory(), &failure)) {
            if (error && error->isEmpty()) {
                *error = failure;
            }
            continue;
        }
        ++added;
    }
    return added;
}

QVector<const SessionTypeEntry *> SessionTypeRegistry::entries() const
{
    QVector<const SessionTypeEntry *> result;
    result.reserve(m_rows.size());
    for (const Row &row : m_rows) {
        result.append(&row.entry);
    }
    return result;
}

const SessionTypeEntry *SessionTypeRegistry::find(const QString &id) const
{
    const Row *row = rowForId(id);
    return row ? &row->entry : nullptr;
}

const SessionTypeEntry *SessionTypeRegistry::findByName(const QString &name) const
{
    if (name.isEmpty()) {
        return nullptr;
    }
    const Qt::CaseSensitivity insensitive = Qt::CaseInsensitive;
    // 三轮而不是一轮里比三个字段：ID 的匹配优先于显示名。
    // 否则一旦某个类型的显示名与另一个类型的 ID 撞上，命中哪一个就取决于
    // 登记顺序，而这属于「用户敲的名字」这件事，应该由名字的确定性决定。
    for (const Row &row : m_rows) {
        if (row.entry.type.id.compare(name, insensitive) == 0) {
            return &row.entry;
        }
    }
    for (const Row &row : m_rows) {
        if (row.entry.type.englishName.compare(name, insensitive) == 0) {
            return &row.entry;
        }
    }
    for (const Row &row : m_rows) {
        if (row.entry.type.displayName.compare(name, insensitive) == 0) {
            return &row.entry;
        }
    }
    return nullptr;
}

const SessionTypeEntry *SessionTypeRegistry::findByFileMask(const QString &fileName,
                                                            Qt::CaseSensitivity cs) const
{
    const Filter::MaskSubject subject = Filter::MaskSubject::forPath(fileName);
    if (!subject.isValid()) {
        return nullptr;
    }
    for (const Row &row : m_rows) {
        if (!row.entry.isAvailableHere()) {
            // 当前平台上不可用的类型不参与自动选择：在一个 macOS 机器上把 .exe
            // 落到「版本比较会话」，用户得到的是一句「此类型仅 Windows 可用」，
            // 而他本来可以拿到一个十六进制比对。不可用就不参选。
            continue;
        }
        for (const Filter::Mask &mask : row.masks) {
            if (mask.matches(subject, cs)) {
                return &row.entry;
            }
        }
    }
    return nullptr;
}

const SessionTypeEntry *SessionTypeRegistry::findByFileMask(const QString &fileName) const
{
    return findByFileMask(fileName, caseSensitivity());
}

QVector<const SessionTypeEntry *> SessionTypeRegistry::allByFileMask(const QString &fileName,
                                                                     Qt::CaseSensitivity cs) const
{
    QVector<const SessionTypeEntry *> result;
    const Filter::MaskSubject subject = Filter::MaskSubject::forPath(fileName);
    if (!subject.isValid()) {
        return result;
    }
    for (const Row &row : m_rows) {
        if (!row.entry.isAvailableHere()) {
            continue;
        }
        for (const Filter::Mask &mask : row.masks) {
            if (mask.matches(subject, cs)) {
                result.append(&row.entry);
                break; // 同一个类型命中两条掩码只算一次
            }
        }
    }
    return result;
}

QVector<const SessionTypeEntry *> SessionTypeRegistry::allByFileMask(const QString &fileName) const
{
    return allByFileMask(fileName, caseSensitivity());
}

QVector<const SessionTypeEntry *> SessionTypeRegistry::byGroup(SessionGroup group,
                                                               bool onlyAvailable) const
{
    QVector<const SessionTypeEntry *> result;
    for (const Row &row : m_rows) {
        if (row.entry.type.group != group) {
            continue;
        }
        if (onlyAvailable && !row.entry.isAvailableHere()) {
            continue;
        }
        result.append(&row.entry);
    }
    return result;
}

QVector<SessionGroup> SessionTypeRegistry::groups(bool onlyAvailable) const
{
    // 按枚举本身的固定顺序返回，而不是「首次出现的顺序」：目录里只有文本类时，
    // 界面上也应该先出现「文本」而不是让它变成第一组；顺序随数据变化会让
    // Home 页的分区在两次启动之间换位置。
    static const SessionGroup ordered[] = {SessionGroup::Text, SessionGroup::Folders,
                                          SessionGroup::Data, SessionGroup::Advanced};
    QVector<SessionGroup> result;
    for (SessionGroup group : ordered) {
        if (!byGroup(group, onlyAvailable).isEmpty()) {
            result.append(group);
        }
    }
    return result;
}

Qt::CaseSensitivity SessionTypeRegistry::caseSensitivity() const
{
    if (m_caseOverridden) {
        return m_case;
    }
    // 与 FILT-001 的大小写策略同源：Windows 的文件系统大小写不敏感，
    // 因此 `.TXT` 在那里就该命中 `*.txt`；Unix 上不是，而且 Unix 上
    // `Makefile` 与 `makefile` 真的可以是两个文件。
    return Filter::defaultCaseSensitivity(Filter::currentMaskPlatform());
}

bool SessionTypeRegistry::isCaseSensitivityOverridden() const
{
    return m_caseOverridden;
}

void SessionTypeRegistry::setCaseSensitivity(Qt::CaseSensitivity cs)
{
    m_case = cs;
    m_caseOverridden = true;
}

void SessionTypeRegistry::clearCaseSensitivityOverride()
{
    m_caseOverridden = false;
}

QStringList SessionTypeRegistry::validate() const
{
    // 只查**登记时没有把住**的几件事，也就是「手写那张表时容易写错、
    // 但写错了也不影响登记成功」的那些。add() 已经拦下的（ID 格式、ID 重复、
    // 显示名为空、掩码编译失败）不在这里重复：那些分支永远走不到，
    // 而一条永远不会红的护栏比没有护栏更糟——它会让人以为这块已经被守住了
    // （交接文档 §5 把这条写成了约定）。
    //
    // 这里是这些规则的**唯一**实现：`Tests/SessionType` 不再各写一份，
    // 而是断言「内置表 validate() 为空」并另用合成表逐条验证它能报出来。
    QStringList problems;
    for (const Row &row : m_rows) {
        const SessionType &type = row.entry.type;
        const QString prefix = QStringLiteral("类型「%1」").arg(type.id);

        if (type.englishName.isEmpty()) {
            // 英文原名不进任何运行时行为，只进命令行 /fv 与规格对照，
            // 因此 add() 不拦它——但缺了就等于命令行上没有这个类型。
            problems.append(prefix + QStringLiteral("缺英文原名（命令行 /fv 用得上）"));
        }

        if (!type.iconKey.isEmpty() && !type.iconKey.endsWith(QStringLiteral(".svg"))) {
            problems.append(prefix + QStringLiteral("的图标键不是 .svg：") + type.iconKey);
        }

        for (const QString &mask : type.fileMasks) {
            // 掩码写成大写不会报错，只会在大小写敏感的平台上一个都命中不到。
            // 大小写敏感性由统一策略决定，掩码自己不该带这层色彩。
            if (mask != mask.toLower()) {
                problems.append(prefix + QStringLiteral("的掩码含大写：") + mask);
            }
        }

        if (row.masks.size() != type.fileMasks.size()) {
            // 按构造不可能成立，留着是因为它对应「将来有人改 add() 时忘了同步
            // 编译结果」——那时它立刻会红，而不是让某个扩展名悄悄匹配不到任何类型。
            problems.append(prefix + QStringLiteral("的已编译掩码数与声明数不一致"));
        }
    }
    return problems;
}

QString SessionTypeRegistry::describe() const
{
    QStringList lines;
    for (SessionGroup group : groups(false)) {
        lines.append(QStringLiteral("[%1]").arg(sessionGroupLabel(group)));
        const QVector<const SessionTypeEntry *> entriesInGroup = byGroup(group, false);
        for (const SessionTypeEntry *entry : entriesInGroup) {
            QString line = QStringLiteral("  %1  %2").arg(entry->type.id, entry->type.englishName);
            if (entry->type.edition == SessionEdition::Pro) {
                line += QStringLiteral(" [Pro]");
            }
            const QString platform = sessionPlatformScopeLabel(entry->type.platforms);
            if (!platform.isEmpty()) {
                line += QStringLiteral(" [%1]").arg(platform);
            }
            if (!entry->hasFactory()) {
                line += QStringLiteral("（尚无实现）");
            }
            lines.append(line);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

// -----------------------------------------------------------------------------
// 内置表
// -----------------------------------------------------------------------------

namespace {

/// 一个小工具，让下面那张表能一行一个类型。
SessionType makeType(const char *id, const char *displayName, const char *englishName,
                     SessionGroup group, SessionEdition edition, SessionPlatformScope platforms,
                     const char *summary, const QStringList &fileMasks)
{
    SessionType type;
    type.id = QString::fromLatin1(id);
    type.displayName = QCoreApplication::translate("SessionType", displayName);
    // 英文原名不翻译：它是 Beyond Compare 的官方 UI 文案，也是 /fv 的取值。
    type.englishName = QString::fromLatin1(englishName);
    type.group = group;
    type.edition = edition;
    type.platforms = platforms;
    type.summary = QCoreApplication::translate("SessionType", summary);
    type.fileMasks = fileMasks;
    return type;
}

} // namespace

QVector<SessionType> builtInSessionTypes()
{
    QVector<SessionType> types;

    // 顺序 = 优先级 = Home 页展示顺序，见头文件说明。改动顺序会改变
    // 「多个类型同时命中一个扩展名时选中谁」，因此 Tests/SessionType 里
    // 有一条用例把顺序也钉住。
    //
    // 掩码一律写成小写：掩码语言本身是大小写敏感的（FILT-001），
    // 由大小写策略统一决定要不要忽略。写成 `*.TXT` 只会让它自己在
    // Unix 上命中不到任何东西。

    // --- 文本类 ---------------------------------------------------------------
    types.append(makeType("text", "文本比较会话", "Text Compare", SessionGroup::Text,
                          SessionEdition::Standard, SessionPlatformScope::All,
                          "并排或上下比对两个文本文件，高亮差异并支持编辑。",
                          {QStringLiteral("*.txt"), QStringLiteral("*.log"), QStringLiteral("*.md"),
                           QStringLiteral("*.ini"), QStringLiteral("*.cfg"), QStringLiteral("*.conf"),
                           QStringLiteral("*.xml"), QStringLiteral("*.json"), QStringLiteral("*.yml"),
                           QStringLiteral("*.yaml"), QStringLiteral("*.c"), QStringLiteral("*.h"),
                           QStringLiteral("*.cpp"), QStringLiteral("*.hpp"), QStringLiteral("*.cc"),
                           QStringLiteral("*.cs"), QStringLiteral("*.java"), QStringLiteral("*.py"),
                           QStringLiteral("*.js"), QStringLiteral("*.ts"), QStringLiteral("*.css"),
                           QStringLiteral("*.sql"), QStringLiteral("*.sh"), QStringLiteral("*.bat"),
                           QStringLiteral("*.ps1"),
                           // `*.html` 同时出现在表格比对里，这是**有意留的**一处重叠：
                           // 它是「按注册顺序返回首个命中」这条规则在真实数据上的唯一用例。
                           // 当前结论是文本胜出（文本先登记）。要让表格胜出是一次产品决定，
                           // 改的时候这条用例会红，从而强制做那次决定。详见
                           // allByFileMaskKeepsTheOnlyRealOverlap 与交接文档。
                           QStringLiteral("*.html"), QStringLiteral("*.htm")}));

    types.append(makeType("text-merge", "文本合并会话", "Text Merge", SessionGroup::Text,
                          SessionEdition::Pro, SessionPlatformScope::All,
                          "三路合并：左右为两个版本，中部为共同祖先，下部为可编辑输出。",
                          // 无掩码：三路合并需要三个及以上文件参数才会进入（CLI-010/011），
                          // 因此它不能被「两个文件」这种自动选择命中去。
                          QStringList()));

    types.append(makeType("text-edit", "文本编辑视图", "Text Edit", SessionGroup::Text,
                          SessionEdition::Standard, SessionPlatformScope::All,
                          "单窗格纯文本编辑器（不比对），可独立打开与保存文件。",
                          // 无掩码：这是「辅助视图」，只由显式入口打开
                          // （Tools > Edit Text Files / `/edit`）。
                          QStringList()));

    types.append(makeType("text-patch", "文本补丁视图", "Text Patch", SessionGroup::Text,
                          SessionEdition::Standard, SessionPlatformScope::All,
                          "打开 .diff / .patch 文件查看，并支持把补丁应用到原始文件。",
                          {QStringLiteral("*.diff"), QStringLiteral("*.patch")}));

    // --- 文件夹类 -------------------------------------------------------------
    types.append(makeType("folder", "文件夹比较会话", "Folder Compare", SessionGroup::Folders,
                          SessionEdition::Standard, SessionPlatformScope::All,
                          "以资源管理器风格并排比对两个文件夹树。",
                          // 目录类型不带文件掩码：它的 Specs 是两个目录，
                          // 用扩展名去猜一个目录该不该用文件夹比对没有意义。
                          QStringList()));

    types.append(makeType("folder-sync", "文件夹同步会话", "Folder Sync", SessionGroup::Folders,
                          SessionEdition::Standard, SessionPlatformScope::All,
                          "专用同步视图：先预览将执行的复制与删除操作，再一键执行。", QStringList()));

    types.append(makeType("folder-merge", "文件夹合并会话", "Folder Merge", SessionGroup::Folders,
                          SessionEdition::Pro, SessionPlatformScope::All,
                          "三路文件夹合并：逐文件决定保留哪一侧，冲突文件需人工选边。",
                          QStringList()));

    // --- 数据类 ---------------------------------------------------------------
    types.append(makeType("table", "表格比较会话", "Table Compare", SessionGroup::Data,
                          SessionEdition::Standard, SessionPlatformScope::All,
                          "按单元格比对 CSV / TSV / Excel / HTML 表格数据，支持多工作表。",
                          {QStringLiteral("*.csv"), QStringLiteral("*.tsv"), QStringLiteral("*.xls"),
                           QStringLiteral("*.xlsx"), QStringLiteral("*.xlsm"), QStringLiteral("*.ods"),
                           // 与文本比对重叠，见上面 text 的说明。
                           QStringLiteral("*.html")}));

    types.append(makeType("hex", "十六进制比较会话", "Hex Compare", SessionGroup::Data,
                          SessionEdition::Standard, SessionPlatformScope::All,
                          "逐字节比对二进制文件，以十六进制 dump 布局呈现。",
                          // 无掩码：它是**兜底**视图——「既不是文本、也没有任何文件格式
                          // 认领」时才落到这里。给它写一串扩展名反而会让它抢走别的类型。
                          QStringList()));

    types.append(makeType("picture", "图片比较会话", "Picture Compare", SessionGroup::Data,
                          SessionEdition::Standard, SessionPlatformScope::All,
                          "并排比对两张图片并按容差模式高亮像素差异。",
                          {QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                           QStringLiteral("*.gif"), QStringLiteral("*.bmp"), QStringLiteral("*.tif"),
                           QStringLiteral("*.tiff"), QStringLiteral("*.webp"), QStringLiteral("*.ico")}));

    types.append(makeType("media", "媒体比较会话", "Media Compare", SessionGroup::Data,
                          SessionEdition::Standard, SessionPlatformScope::All,
                          "比对 MP3 / FLAC / MP4 的标签与元数据差异（不比对音频波形）。",
                          {QStringLiteral("*.mp3"), QStringLiteral("*.flac"), QStringLiteral("*.mp4"),
                           QStringLiteral("*.m4a"), QStringLiteral("*.aac"), QStringLiteral("*.ogg")}));

    // --- 系统类 ---------------------------------------------------------------
    types.append(makeType("registry", "注册表比较会话", "Registry Compare", SessionGroup::Advanced,
                          // 研究基线里这一条同时带着 [Pro] 与 [Win] 两个标记，
                          // 是本表里唯一两者兼具的类型。
                          SessionEdition::Pro, SessionPlatformScope::WindowsOnly,
                          "比对本地或远程实时注册表，或注册表导出文件。",
                          {QStringLiteral("*.reg")}));

    types.append(makeType("version", "版本比较会话", "Version Compare", SessionGroup::Advanced,
                          SessionEdition::Standard, SessionPlatformScope::WindowsOnly,
                          "比对可执行文件（exe / dll / ocx）的版本信息资源。",
                          {QStringLiteral("*.exe"), QStringLiteral("*.dll"), QStringLiteral("*.ocx")}));

    types.append(makeType("archive", "压缩包比对", "Archive Compare", SessionGroup::Advanced,
                          SessionEdition::Standard, SessionPlatformScope::All,
                          "把压缩包当作文件夹展开并比对其中内容，支持多种归档格式。",
                          {QStringLiteral("*.zip"), QStringLiteral("*.jar"), QStringLiteral("*.7z"),
                           QStringLiteral("*.rar"), QStringLiteral("*.tar"), QStringLiteral("*.gz"),
                           QStringLiteral("*.tgz"), QStringLiteral("*.bz2"), QStringLiteral("*.tbz2"),
                           QStringLiteral("*.cab"), QStringLiteral("*.chm"), QStringLiteral("*.deb"),
                           QStringLiteral("*.rpm"),
                           // `.bcpkg` 是压缩包，同时也是**设置包**（CLI-013 里
                           // 命令行传入 `.bcpkg` 表示导入整套设置）。同一个扩展名
                           // 承担两件事是研究基线里就有的事实，两条路都会走到：
                           // 命令行参数由 CLI 优先判定为设置包，文件对自动选择
                           // 才落到这里。写在这里是为了让「双击 .bcpkg 会怎样」
                           // 有一个明确的答案，而不是空着。
                           QStringLiteral("*.bcpkg")}));

    return types;
}

QStringList builtInSessionTypeIds()
{
    QStringList ids;
    const QVector<SessionType> types = builtInSessionTypes();
    ids.reserve(types.size());
    for (const SessionType &type : types) {
        ids.append(type.id);
    }
    return ids;
}

} // namespace LqCompare
