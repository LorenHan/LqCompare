#ifndef LQCOMPARE_COMPARECONCLUSION_H
#define LQCOMPARE_COMPARECONCLUSION_H

#include "textdiff.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace LqCompare { namespace Text {

// ===========================================================================
// 一、结论档（TXT-015 第 2 条）
// ===========================================================================
//
// 为什么「相同」必须拆成两档：规格的原话是「仅 BOM 不同且策略为忽略时，
// 比较结论为**规则相同**并说明已忽略 BOM；**不得标成字节完全一致**」。
// 只有「相同 / 不同」两档时，这句话里的那个区分**无法被表达**，
// 于是也无法被否定式地验证——一条既不能说真也不能说假的验收标准等于没有。
//
// 三档的含义刻意写成「有没有找到证据」而不是「字节是否相同」，
// 因为「字节完全一致」是一句**需要读完两侧全部字节才能说出口**的话，
// 而这一层拿到的事实只有行级比对结果与 BOM 这一维的观测。
// 于是：
//
//   * `Identical`     —— 按当前规则没找到任何差异，**也没有**任何「被规则忽略」
//                        的痕迹。它不是「字节完全相同」的断言；界面上也不要这么印。
//   * `RuleIdentical` —— 按当前规则没找到差异，但**有证据**表明两侧在原始层面
//                        并不相同（有被规则忽略的块，或 BOM 差异被规则忽略）。
//                        这正是规格点名的那一档。
//   * `Different`     —— 存在未被规则忽略的差异。BOM 被计为差异时也走这一档，
//                        即使行级一个差异块都没有。
//
// 取值一律追加在末尾：整数值会进设置、快照与（将来的）报表。
enum class Conclusion
{
    Identical,
    RuleIdentical,
    Different,
};

/// 面向用户的一句话档名。**唯一措辞来源**——界面、状态栏与将来的报表都取这里，
/// 各自写一份文案的话，「规则相同」与「相同」迟早会被写成同一个词。
QString conclusionLabel(Conclusion conclusion);

/// 档名的展开说明（为什么是这个档）。与 `conclusionLabel()` 分开：
/// 标签要短到能进列宽，说明要长到能解释「为什么不是那个档」。
QString conclusionDescription(Conclusion conclusion);

// ===========================================================================
// 二、BOM 处理策略（TXT-015 第 1 条）
// ===========================================================================
//
// `BomPolicy` 这个**枚举**定义在 `textdiff.h`（`CompareOptions` 要按值存它）；
// 本节是它的全部**行为**。这样分开之后，「引擎认哪几档」与「界面上能选哪几档」
// 各自只有一个来源，而 `validateBomPolicyTable()` 是那个把两边拉到一起的判定。

/// 策略表的一行。与 `AlignmentDescriptor` / `WhitespaceDescriptor` 同一个定位：
/// 表是**唯一的事实来源**，可选集合与默认值都从它推导。
/// 分开各写一遍的话，加档时会出现「下拉里有三项、默认值指向第二项、
/// 而第二项其实没实现」这类错位。
struct BomPolicyDescriptor
{
    BomPolicy policy = BomPolicy::Automatic;
    /// 机器可读标识（日志、快照、设置键都用它，不靠枚举序号）。
    const char *identifier = "";
    /// 引擎是否真的会算它。`false` 的条目不会出现在 `availableBomPolicies()` 里。
    bool implemented = false;
};

const QVector<BomPolicyDescriptor> &bomPolicyTable();

/// 机器可读标识。表里查不到时返回空指针，**不编一个假名字**。
const char *bomPolicyIdentifier(BomPolicy policy);

///
/// \brief 标识符 → 取值（设置键的反查）。
///
/// 存在的理由与「为什么用标识符而不是枚举序号当设置键」是同一件事：
/// 序号是隐式的第二事实来源，往枚举中间插一个取值就会让设置文件里那个
/// 整数**换一个含义**，而现象是「升级之后比较规则自己变了」。
/// 标识符不会。
///
/// 认不出来时返回 false，并把 `*result` 留在 `defaultBomPolicy()`（`result` 可为空）。
/// 刻意**不返回「表里第一项」**：设置文件里写着一个不认识的档，多半是
/// 手工改过或来自更新的版本，此时退回默认值是唯一能解释得通的行为，
/// 而默默用另一个档会让用户完全无从发现。
///
bool bomPolicyFromIdentifier(const QString &identifier, BomPolicy *result = nullptr);

/// 默认策略：表里**第一条已实现**的条目；一条都没有时退回 `BomPolicy::Automatic`。
BomPolicy defaultBomPolicy(const QVector<BomPolicyDescriptor> &table = bomPolicyTable());

/// 可选策略：只含 `implemented == true` 的条目，顺序与表一致。
QVector<BomPolicy> availableBomPolicies(const QVector<BomPolicyDescriptor> &table = bomPolicyTable());

/// 策略表自检。参数化是为了让测试喂一份**故意写坏**的表进去，
/// 证明这个判定真的会报——一条永远不会红的护栏比没有护栏更糟。
/// `expected` 是规格点名的那三档，也是唯一不依赖这张表自身的期望值。
QStringList validateBomPolicyTable(const QVector<BomPolicyDescriptor> &table,
                                   const QVector<BomPolicy> &expected = {BomPolicy::Ignore,
                                                                        BomPolicy::TreatAsDifference,
                                                                        BomPolicy::Automatic});

/// 面向用户的一行档名。
QString bomPolicyLabel(BomPolicy policy);

// ===========================================================================
// 三、BOM 与编码：BOM 是不是「编码的一部分」
// ===========================================================================

///
/// \brief 这个编码的 BOM 是不是**字节序声明**（也就是「去不掉」的那种 BOM）。
///
/// 这条判定是 `BomPolicy::Automatic` 的全部内容，也是为什么这一档必须存在：
/// UTF-16 / UTF-32 的 BOM 不是可有可无的标记，它**就是**字节序本身——
/// 去掉之后同样的字节序列有两种读法，文件不再可读。而 UTF-8 的
/// `EF BB BF` 完全冗余（UTF-8 没有字节序问题），有它没它解出来的字符一样。
///
/// 于是一句「BOM 不同」在两种编码下**根本不是同一件事**：
/// 前者意味着两份文件真的被编成了不同的东西，后者只是一个多余标记的有无。
/// `Automatic` 就是按这条分界线决定算不算差异的。
///
bool bomIsEncodingCritical(const QByteArray &codecName);

/// 该编码要写进文件的那段 BOM 字节；不写 BOM 的编码返回空。
/// 表外编码返回空，**不猜**——猜出来的字节会把文件写坏。
QByteArray bomBytesForCodec(const QByteArray &codecName);

// ===========================================================================
// 四、BOM 差异的判定（TXT-015 第 1、2 条）
// ===========================================================================

///
/// \brief 判定要用到的**全部**观测，一次给齐。
///
/// 刻意不做成「传两个 `Document`」：`Document` 会随解码、编辑、保存而变，
/// 而这条判定要看的是**打开时那一刻的 BOM 事实**。把观测单独成结构，
/// 判定的输入就固定住了，测试也不必先造出两个真文件。
struct BomObservation
{
    /// 两侧是否都读到了文档。任一侧没有文档时**不下结论**——
    /// 「还没打开」与「两侧 BOM 状态相同」在界面上是两件事。
    bool leftAvailable = false;
    bool rightAvailable = false;
    bool leftHasBom = false;
    bool rightHasBom = false;
    QByteArray leftCodec;
    QByteArray rightCodec;

    bool complete() const { return leftAvailable && rightAvailable; }
};

/// 从两侧文档取一次观测。`available` 为假表示这次会话还没有可比的文档
/// （尚未打开 / 已关闭），此时观测不完整，`evaluateBom()` 不下结论——
/// 「还没打开」与「两侧 BOM 状态相同」在界面上是两件事。
BomObservation observeBom(const Document &left, const Document &right, bool available = true);

/// BOM 这一维的结论。`None` 表示「没有 BOM 差异可谈」，
/// 与「有差异但被忽略」是两件事，不能塌成一档——塌了之后状态栏
/// 就没法区分「两侧一样」和「两侧不一样但我不计较」。
enum class BomConclusion
{
    None,
    Ignored,
    Difference,
};

struct BomVerdict
{
    BomConclusion conclusion = BomConclusion::None;

    bool differs() const { return conclusion != BomConclusion::None; }
    bool countedAsDifference() const { return conclusion == BomConclusion::Difference; }
    bool ignored() const { return conclusion == BomConclusion::Ignored; }
};

BomVerdict evaluateBom(BomPolicy policy, const BomObservation &observation);

/// 面向用户的一句话结论。**唯一措辞来源**：状态栏、将来的报表与命令行摘要
/// 都取这里，否则同一次判定会在三处印出三种说法，而用户无从判断哪个算数。
QString bomConclusionSummary(BomConclusion conclusion);

// ===========================================================================
// 五、保存侧的 BOM 策略（TXT-015 第 3 条）
// ===========================================================================
//
// `BomSavePolicy` 这个**枚举**住在 `textdocument.h`（`Document` 要按值存它）；
// 本节是它的表、文案与判定。

struct BomSavePolicyDescriptor
{
    BomSavePolicy policy = BomSavePolicy::Preserve;
    const char *identifier = "";
    bool implemented = false;
};

const QVector<BomSavePolicyDescriptor> &bomSavePolicyTable();
const char *bomSavePolicyIdentifier(BomSavePolicy policy);
BomSavePolicy defaultBomSavePolicy(const QVector<BomSavePolicyDescriptor> &table = bomSavePolicyTable());
QVector<BomSavePolicy> availableBomSavePolicies(
    const QVector<BomSavePolicyDescriptor> &table = bomSavePolicyTable());
QStringList validateBomSavePolicyTable(
    const QVector<BomSavePolicyDescriptor> &table,
    const QVector<BomSavePolicy> &expected = {BomSavePolicy::Preserve,
                                              BomSavePolicy::AlwaysWrite,
                                              BomSavePolicy::NeverWrite});
QString bomSavePolicyLabel(BomSavePolicy policy);

///
/// \brief 这条策略在这个编码下是不是**真的能做到**。
///
/// `NeverWrite` 对 UTF-16 / UTF-32 不成立：那些编码的 BOM 就是字节序声明，
/// 去掉之后文件不再可读。所以保存时它对这些编码**降级成保留**，
/// 而不是照办——照办会写出一个打不开的文件，而用户按的是「不写 BOM」，
/// 不是「把文件弄坏」。
///
/// 这条判定抽出来是为了让上面那句话**可以被直接问**：
/// 把它写进 `bytes()` 的分支里，测试就只能去比对最终字节，
/// 而「因为降级所以写了」与「因为原文件本来就有所以写了」在字节上完全一样。
bool bomSavePolicyApplies(BomSavePolicy policy, const QByteArray &codecName);

////
/// \brief 这次保存到底要不要写 BOM。
///
/// `originalHasBom` 是文档**打开时**的 BOM 事实（`Document::hasBom()`），
/// 不是当前缓冲里的内容——BOM 不在行模型里。
bool bomShouldBeWritten(BomSavePolicy policy, bool originalHasBom, const QByteArray &codecName);

// ===========================================================================
// 六、把两件事合成一个结论
// ===========================================================================

///
/// \brief 行级比对结果 + BOM 这一维的判定 ⇒ 结论档。
///
/// 判据顺序不能换：
///   1. 有未被规则忽略的差异块，**或** BOM 被计为差异 ⇒ `Different`。
///      BOM 那一路必须单独判：BOM 不是行级现象，它不会让 `differences`
///      里多出一块，只看 `differences` 会把「两侧只差 BOM 且策略要求视为差异」
///      报成相同。
///   2. 否则，有被规则忽略的块，**或** BOM 差异被忽略 ⇒ `RuleIdentical`。
///   3. 否则 `Identical`。
///
Conclusion conclude(const Result &result, const BomVerdict &bom);

} }
#endif
