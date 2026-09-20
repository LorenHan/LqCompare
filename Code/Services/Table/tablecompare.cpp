#include "tablecompare.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace LqCompare::Table {
namespace {

bool participates(const ColumnRule &column)
{
    return column.role == ColumnRole::Compare || column.role == ColumnRole::Key;
}

bool hasCell(const Document &document, int row, int column)
{
    return row >= 0 && column >= 0 && column < document.rows.at(row).size();
}

struct Decimal
{
    QString digits = QStringLiteral("0");
    qint64 exponent = 0;
    bool negative = false;
};

void normalize(Decimal *value)
{
    int first = 0;
    while (first < value->digits.size() && value->digits.at(first) == QLatin1Char('0'))
        ++first;
    if (first == value->digits.size()) {
        *value = {};
        return;
    }
    int last = value->digits.size();
    while (value->digits.at(last - 1) == QLatin1Char('0'))
        --last;
    value->exponent += value->digits.size() - last;
    value->digits = value->digits.mid(first, last - first);
}

bool number(const QString &text, Decimal *value)
{
    // QString::toDouble is locale independent, but this grammar additionally
    // excludes infinities, NaNs, grouping separators and hexadecimal numbers.
    static const QRegularExpression grammar(QStringLiteral(
        "^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$"));
    QString trimmed = text.trimmed();
    if (!grammar.match(trimmed).hasMatch())
        return false;
    bool ok = false;
    const double finiteValue = trimmed.toDouble(&ok);
    if (!ok || !std::isfinite(finiteValue))
        return false;
    *value = {};
    value->negative = trimmed.startsWith(QLatin1Char('-'));
    if (trimmed.startsWith(QLatin1Char('-')) || trimmed.startsWith(QLatin1Char('+')))
        trimmed.remove(0, 1);
    const int exponentAt = trimmed.indexOf(QLatin1Char('e'), 0, Qt::CaseInsensitive);
    if (exponentAt >= 0) {
        value->exponent = trimmed.mid(exponentAt + 1).toLongLong(&ok);
        if (!ok || value->exponent > std::numeric_limits<int>::max()
            || value->exponent < std::numeric_limits<int>::min())
            return false;
        trimmed.truncate(exponentAt);
    }
    const int decimalAt = trimmed.indexOf(QLatin1Char('.'));
    if (decimalAt >= 0) {
        value->exponent -= trimmed.size() - decimalAt - 1;
        trimmed.remove(decimalAt, 1);
    }
    value->digits = trimmed;
    normalize(value);
    return true;
}

int magnitude(const Decimal &left, const Decimal &right)
{
    if (left.digits == QStringLiteral("0"))
        return right.digits == QStringLiteral("0") ? 0 : -1;
    if (right.digits == QStringLiteral("0"))
        return 1;
    const qint64 leftOrder = left.digits.size() + left.exponent;
    const qint64 rightOrder = right.digits.size() + right.exponent;
    if (leftOrder != rightOrder)
        return leftOrder < rightOrder ? -1 : 1;
    for (int i = 0; i < std::max(left.digits.size(), right.digits.size()); ++i) {
        const QChar a = i < left.digits.size() ? left.digits.at(i) : QLatin1Char('0');
        const QChar b = i < right.digits.size() ? right.digits.at(i) : QLatin1Char('0');
        if (a != b)
            return a < b ? -1 : 1;
    }
    return 0;
}

Decimal multiply(const Decimal &left, const Decimal &right)
{
    if (left.digits == QStringLiteral("0") || right.digits == QStringLiteral("0"))
        return {};
    // One operand is always a tolerance (at most 17 digits); cost stays linear
    // in the source cell size.
    QVector<int> digits(left.digits.size() + right.digits.size(), 0);
    for (int i = left.digits.size() - 1; i >= 0; --i) {
        for (int j = right.digits.size() - 1; j >= 0; --j)
            digits[i + j + 1] += left.digits.at(i).digitValue() * right.digits.at(j).digitValue();
    }
    for (int i = digits.size() - 1; i > 0; --i) {
        digits[i - 1] += digits.at(i) / 10;
        digits[i] %= 10;
    }
    Decimal result;
    result.exponent = left.exponent + right.exponent;
    result.digits.clear();
    for (int digit : digits)
        result.digits.append(QChar(ushort('0' + digit)));
    normalize(&result);
    return result;
}

Decimal distance(const Decimal &left, const Decimal &right)
{
    const Decimal &larger = magnitude(left, right) >= 0 ? left : right;
    const Decimal &smaller = magnitude(left, right) >= 0 ? right : left;
    Decimal result;
    result.exponent = std::min(left.exponent, right.exponent);
    QString a = larger.digits + QString(int(larger.exponent - result.exponent), QLatin1Char('0'));
    QString b = smaller.digits + QString(int(smaller.exponent - result.exponent), QLatin1Char('0'));
    b = b.rightJustified(a.size(), QLatin1Char('0'));
    result.digits = a;
    int carry = 0;
    const bool add = left.negative != right.negative;
    for (int i = a.size() - 1; i >= 0; --i) {
        int digit = a.at(i).digitValue() + (add ? b.at(i).digitValue() : -b.at(i).digitValue()) + carry;
        if (add) {
            carry = digit / 10;
            digit %= 10;
        } else {
            carry = digit < 0 ? -1 : 0;
            if (digit < 0)
                digit += 10;
        }
        result.digits[i] = QChar(ushort('0' + digit));
    }
    if (carry > 0)
        result.digits.prepend(QLatin1Char('1'));
    normalize(&result);
    return result;
}

bool equalNumbers(const Decimal &left, const Decimal &right, const ColumnRule &column)
{
    if (left.negative == right.negative && magnitude(left, right) == 0)
        return true;
    if (column.absoluteTolerance == 0 && column.relativeTolerance == 0)
        return false;
    // Use the shortest decimal spelling that round-trips to the configured
    // double (for example 0.3), without expanding the accepted tolerance.
    const auto toleranceValue = [](double configured) {
        Decimal result;
        for (int precision = 1; precision <= 17; ++precision) {
            const QString spelling = QString::number(configured, 'g', precision);
            if (spelling.toDouble() == configured) {
                number(spelling, &result);
                break;
            }
        }
        return result;
    };
    const Decimal absolute = toleranceValue(column.absoluteTolerance);
    const Decimal relative = toleranceValue(column.relativeTolerance);
    const Decimal scaled = multiply(magnitude(left, right) >= 0 ? left : right, relative);
    const Decimal allowed = magnitude(absolute, scaled) >= 0 ? absolute : scaled;
    return magnitude(distance(left, right), allowed) <= 0;
}

bool equalValues(const QString &left, const QString &right, const ColumnRule &column)
{
    if (left == right)
        return true;
    if (!column.numeric || column.compareFormatting)
        return false;
    Decimal leftNumber;
    Decimal rightNumber;
    return number(left, &leftNumber) && number(right, &rightNumber)
        && equalNumbers(leftNumber, rightNumber, column);
}

CellStatus cellStatus(const Document &left, const Document &right,
                      int leftRow, int rightRow, const ColumnRule &column)
{
    if (column.role == ColumnRole::Ignore)
        return CellStatus::Ignored;
    if (column.role == ColumnRole::Display)
        return CellStatus::NotCompared;
    const bool leftPresent = hasCell(left, leftRow, column.left);
    const bool rightPresent = hasCell(right, rightRow, column.right);
    if (!leftPresent && !rightPresent)
        return CellStatus::Equal;
    if (!leftPresent)
        return CellStatus::RightOnly;
    if (!rightPresent)
        return CellStatus::LeftOnly;
    return equalValues(left.rows.at(leftRow).at(column.left),
                       right.rows.at(rightRow).at(column.right), column)
        ? CellStatus::Equal : CellStatus::Different;
}

void appendValue(QString *signature, const Document &document, int row, int column,
                 const ColumnRule &rule, bool raw)
{
    if (!hasCell(document, row, column)) {
        signature->append(QLatin1Char('M'));
        return;
    }
    QString value = document.rows.at(row).at(column);
    QChar kind = QLatin1Char('P');
    Decimal numericValue;
    if (!raw && rule.numeric && !rule.compareFormatting && number(value, &numericValue)) {
        kind = QLatin1Char('N');
        value = (numericValue.negative ? QStringLiteral("-") : QString())
            + numericValue.digits + QLatin1Char('e') + QString::number(numericValue.exponent);
    }
    signature->append(kind);
    signature->append(QString::number(value.size()));
    signature->append(QLatin1Char(':'));
    signature->append(value);
}

QString signature(const Document &document, int row, const QVector<ColumnRule> &columns,
                  bool leftSide, bool keyOnly)
{
    QString result;
    for (const auto &column : columns) {
        if (keyOnly ? column.role != ColumnRole::Key : !participates(column))
            continue;
        appendValue(&result, document, row, leftSide ? column.left : column.right,
                    column, keyOnly);
    }
    return result;
}

bool uniqueHeaders(const Document &document)
{
    QSet<QString> seen;
    for (const auto &header : document.headers) {
        if (seen.contains(header))
            return false;
        seen.insert(header);
    }
    return true;
}

bool mapColumns(const Document &left, const Document &right,
                const CompareOptions &options, Result *result)
{
    const int leftCount = left.columnCount();
    const int rightCount = right.columnCount();
    if (options.columnMode == ColumnMappingMode::Explicit) {
        result->columns = options.columns;
    } else if (options.columnMode == ColumnMappingMode::AutoName && left.hasHeader && right.hasHeader) {
        if (!uniqueHeaders(left) || !uniqueHeaders(right)) {
            result->error = QStringLiteral("Duplicate column headers require Position or Explicit column mapping.");
            return false;
        }
        QHash<QString, int> rightNames;
        for (int i = 0; i < right.headers.size(); ++i)
            rightNames.insert(right.headers.at(i), i);
        for (int i = 0; i < left.headers.size(); ++i) {
            const auto match = rightNames.constFind(left.headers.at(i));
            if (match != rightNames.constEnd())
                result->columns.append({i, match.value()});
        }
    } else if (options.columnMode == ColumnMappingMode::AutoName
               || options.columnMode == ColumnMappingMode::Position) {
        for (int i = 0; i < std::min(leftCount, rightCount); ++i)
            result->columns.append({i, i});
    } else {
        result->error = QStringLiteral("Unknown column mapping mode.");
        return false;
    }

    QSet<int> usedLeft;
    QSet<int> usedRight;
    for (const auto &column : result->columns) {
        if (column.left < -1 || column.left >= leftCount || column.right < -1
            || column.right >= rightCount || (column.left == -1 && column.right == -1)) {
            result->error = QStringLiteral("Column mapping contains an invalid column index.");
            return false;
        }
        if ((column.left >= 0 && usedLeft.contains(column.left))
            || (column.right >= 0 && usedRight.contains(column.right))) {
            result->error = QStringLiteral("Column mappings must be one-to-one; a source column was used more than once.");
            return false;
        }
        if (column.role != ColumnRole::Compare && column.role != ColumnRole::Key
            && column.role != ColumnRole::Ignore && column.role != ColumnRole::Display) {
            result->error = QStringLiteral("Unknown column role.");
            return false;
        }
        if (column.role == ColumnRole::Key && (column.left < 0 || column.right < 0)) {
            result->error = QStringLiteral("A key column must map a column on both sides.");
            return false;
        }
        if (!std::isfinite(column.absoluteTolerance) || column.absoluteTolerance < 0
            || !std::isfinite(column.relativeTolerance) || column.relativeTolerance < 0) {
            result->error = QStringLiteral("Numeric tolerances must be finite and nonnegative.");
            return false;
        }
        if (column.left >= 0)
            usedLeft.insert(column.left);
        if (column.right >= 0)
            usedRight.insert(column.right);
    }

    int unmapped = 0;
    for (int i = 0; i < leftCount; ++i) {
        if (!usedLeft.contains(i)) {
            result->columns.append({i, -1, ColumnRole::Display});
            ++unmapped;
        }
    }
    for (int i = 0; i < rightCount; ++i) {
        if (!usedRight.contains(i)) {
            result->columns.append({-1, i, ColumnRole::Display});
            ++unmapped;
        }
    }
    if (unmapped)
        result->warnings.append(QStringLiteral("%1 unmapped column(s) are shown but not compared.").arg(unmapped));
    return true;
}

struct Pairing
{
    QVector<int> rightForLeft;
    QVector<int> leftForRight;
    QVector<bool> leftDuplicate;
    QVector<bool> rightDuplicate;

    Pairing(int leftCount, int rightCount)
        : rightForLeft(leftCount, -1), leftForRight(rightCount, -1),
          leftDuplicate(leftCount, false), rightDuplicate(rightCount, false) {}

    void pair(int left, int right)
    {
        rightForLeft[left] = right;
        leftForRight[right] = left;
    }
};

void matchExact(const Document &left, const Document &right, const QVector<ColumnRule> &columns,
                const QVector<int> &leftRows, const QVector<int> &rightRows, Pairing *pairing)
{
    QHash<QString, QVector<int>> candidates;
    QHash<QString, int> consumed;
    for (int row : rightRows)
        candidates[signature(right, row, columns, false, false)].append(row);
    for (int row : leftRows) {
        const QString key = signature(left, row, columns, true, false);
        const auto found = candidates.constFind(key);
        if (found == candidates.constEnd())
            continue;
        int &position = consumed[key];
        if (position < found->size())
            pairing->pair(row, found->at(position++));
    }
}

double textSimilarity(const QString &left, const QString &right, qint64 *work)
{
    // Dice similarity of adjacent Unicode code units is content based and
    // bounded. Long cells use a prefix/suffix sample only for alignment; final
    // cell equality always compares the complete original value.
    const auto sample = [](const QString &value) {
        return value.size() <= 512 ? value : value.left(256) + value.right(256);
    };
    const QString a = sample(left);
    const QString b = sample(right);
    *work -= a.size() + b.size();
    if (a.size() < 2 || b.size() < 2)
        return 0;
    QHash<uint, int> pairs;
    for (int i = 1; i < a.size(); ++i)
        ++pairs[(uint(a.at(i - 1).unicode()) << 16) | a.at(i).unicode()];
    int common = 0;
    for (int i = 1; i < b.size(); ++i) {
        const uint pair = (uint(b.at(i - 1).unicode()) << 16) | b.at(i).unicode();
        auto found = pairs.find(pair);
        if (found != pairs.end() && found.value() > 0) {
            --found.value();
            ++common;
        }
    }
    // Distinct strings can have identical bigram multisets. Reserve 1.0 for
    // real cell equality, including numeric equality within the tolerance.
    return std::min(0.99, 2.0 * common / (a.size() + b.size() - 2));
}

double rowSimilarity(const Document &left, const Document &right, int leftRow, int rightRow,
                     const QVector<ColumnRule> &columns, qint64 *work)
{
    double score = 0;
    int count = 0;
    for (const auto &column : columns) {
        if (!participates(column))
            continue;
        --*work;
        if (column.numeric) {
            if (hasCell(left, leftRow, column.left))
                *work -= left.rows.at(leftRow).at(column.left).size();
            if (hasCell(right, rightRow, column.right))
                *work -= right.rows.at(rightRow).at(column.right).size();
        }
        // Numeric parsing and exact decimal arithmetic are linear in the
        // source length, so charge before doing them for a fuzzy candidate.
        if (*work < 0)
            return -1;
        ++count;
        if (cellStatus(left, right, leftRow, rightRow, column) == CellStatus::Equal) {
            score += 1;
        } else if (!column.numeric && hasCell(left, leftRow, column.left)
                   && hasCell(right, rightRow, column.right)) {
            score += textSimilarity(left.rows.at(leftRow).at(column.left),
                                    right.rows.at(rightRow).at(column.right), work);
        }
        if (*work < 0)
            return -1;
    }
    return count ? score / count : -1;
}

void alignContent(const Document &left, const Document &right, const CompareOptions &options,
                  Result *result, Pairing *pairing)
{
    const bool anyCompared = std::any_of(result->columns.cbegin(), result->columns.cend(), participates);
    if (!anyCompared) {
        result->warnings.append(QStringLiteral("No comparison columns are selected; content alignment cannot pair rows."));
        return;
    }
    QVector<int> leftRows;
    QVector<int> rightRows;
    for (int i = 0; i < left.rows.size(); ++i)
        leftRows.append(i);
    for (int i = 0; i < right.rows.size(); ++i)
        rightRows.append(i);
    matchExact(left, right, result->columns, leftRows, rightRows, pairing);

    struct Candidate { int left; int right; double score; };
    QVector<Candidate> candidates;
    qint64 remainingWork = 2000000;
    int remainingPairs = 200000;
    bool limitReached = false;
    for (int l : leftRows) {
        if (pairing->rightForLeft.at(l) >= 0)
            continue;
        for (int r : rightRows) {
            if (pairing->leftForRight.at(r) >= 0)
                continue;
            if (--remainingPairs < 0) {
                limitReached = true;
                break;
            }
            const double score = rowSimilarity(left, right, l, r, result->columns, &remainingWork);
            if (remainingWork < 0) {
                limitReached = true;
                break;
            }
            if (score >= options.minimumSimilarity)
                candidates.append({l, r, score});
        }
        if (limitReached)
            break;
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        if (a.score != b.score)
            return a.score > b.score;
        const int aDistance = std::abs(a.left - a.right);
        const int bDistance = std::abs(b.left - b.right);
        if (aDistance != bDistance)
            return aDistance < bDistance;
        if (a.left != b.left)
            return a.left < b.left;
        return a.right < b.right;
    });
    for (const auto &candidate : candidates) {
        if (pairing->rightForLeft.at(candidate.left) < 0
            && pairing->leftForRight.at(candidate.right) < 0)
            pairing->pair(candidate.left, candidate.right);
    }
    if (limitReached)
        result->warnings.append(QStringLiteral("Content alignment work limit reached; remaining rows are retained as unmatched, without positional fallback."));
}

bool alignKeys(const Document &left, const Document &right, Result *result, Pairing *pairing)
{
    const bool anyKey = std::any_of(result->columns.cbegin(), result->columns.cend(),
        [](const ColumnRule &column) { return column.role == ColumnRole::Key; });
    if (!anyKey) {
        result->error = QStringLiteral("Key alignment requires at least one mapped key column.");
        return false;
    }
    QHash<QString, QVector<int>> leftGroups;
    QHash<QString, QVector<int>> rightGroups;
    QStringList keys;
    QSet<QString> seen;
    const auto add = [&](const Document &document, bool leftSide, QHash<QString, QVector<int>> *groups) {
        for (int row = 0; row < document.rows.size(); ++row) {
            const QString key = signature(document, row, result->columns, leftSide, true);
            (*groups)[key].append(row);
            if (!seen.contains(key)) {
                seen.insert(key);
                keys.append(key);
            }
        }
    };
    add(left, true, &leftGroups);
    add(right, false, &rightGroups);
    int duplicateGroups = 0;
    for (const QString &key : keys) {
        const auto leftRows = leftGroups.value(key);
        const auto rightRows = rightGroups.value(key);
        if (leftRows.size() > 1 || rightRows.size() > 1) {
            ++duplicateGroups;
            for (int row : leftRows)
                pairing->leftDuplicate[row] = true;
            for (int row : rightRows)
                pairing->rightDuplicate[row] = true;
        }
        matchExact(left, right, result->columns, leftRows, rightRows, pairing);
        int rightPosition = 0;
        for (int l : leftRows) {
            if (pairing->rightForLeft.at(l) >= 0)
                continue;
            while (rightPosition < rightRows.size()
                   && pairing->leftForRight.at(rightRows.at(rightPosition)) >= 0)
                ++rightPosition;
            if (rightPosition < rightRows.size())
                pairing->pair(l, rightRows.at(rightPosition++));
        }
    }
    if (duplicateGroups)
        result->warnings.append(QStringLiteral("%1 duplicate key group(s): every row was retained; exact content matches were paired before remaining rows in source order.").arg(duplicateGroups));
    return true;
}

void appendRow(const Document &left, const Document &right, int leftIndex, int rightIndex,
               const Pairing &pairing, Result *result)
{
    Row row;
    row.left = leftIndex;
    row.right = rightIndex;
    row.duplicateKey = (leftIndex >= 0 && pairing.leftDuplicate.at(leftIndex))
        || (rightIndex >= 0 && pairing.rightDuplicate.at(rightIndex));
    row.status = leftIndex < 0 ? RowStatus::RightOnly
        : rightIndex < 0 ? RowStatus::LeftOnly : RowStatus::Equal;
    for (const auto &column : result->columns) {
        const auto status = cellStatus(left, right, leftIndex, rightIndex, column);
        row.cells.append(status);
        if (status == CellStatus::Ignored) {
            ++result->statistics.ignoredCells;
        } else if (status == CellStatus::Different || status == CellStatus::LeftOnly
                   || status == CellStatus::RightOnly) {
            ++result->statistics.differentCells;
            if (row.status == RowStatus::Equal)
                row.status = RowStatus::Different;
        }
    }
    switch (row.status) {
    case RowStatus::Equal: ++result->statistics.equalRows; break;
    case RowStatus::Different: ++result->statistics.differentRows; break;
    case RowStatus::LeftOnly: ++result->statistics.leftOnlyRows; break;
    case RowStatus::RightOnly: ++result->statistics.rightOnlyRows; break;
    }
    if (row.status != RowStatus::Equal)
        result->differences.append(result->rows.size());
    result->rows.append(row);
}

void materialize(const Document &left, const Document &right, const Pairing &pairing, Result *result)
{
    // Anchor inserted right rows before their next matched right row. This
    // keeps ordinary inserts beside their neighbors while allowing reorders;
    // all paired/left-only rows preserve left source order.
    QVector<QVector<int>> insertBefore(left.rows.size());
    QVector<int> trailing;
    int nextLeft = -1;
    for (int r = right.rows.size() - 1; r >= 0; --r) {
        if (pairing.leftForRight.at(r) >= 0)
            nextLeft = pairing.leftForRight.at(r);
        else if (nextLeft >= 0)
            insertBefore[nextLeft].append(r);
        else
            trailing.append(r);
    }
    for (int l = 0; l < left.rows.size(); ++l) {
        const auto &insertions = insertBefore.at(l);
        for (auto it = insertions.crbegin(); it != insertions.crend(); ++it)
            appendRow(left, right, -1, *it, pairing, result);
        appendRow(left, right, l, pairing.rightForLeft.at(l), pairing, result);
    }
    for (auto it = trailing.crbegin(); it != trailing.crend(); ++it)
        appendRow(left, right, -1, *it, pairing, result);
}

} // namespace

Result compare(const Document &left, const Document &right, const CompareOptions &options)
{
    Result result;
    if (!std::isfinite(options.minimumSimilarity) || options.minimumSimilarity < 0
        || options.minimumSimilarity > 1) {
        result.error = QStringLiteral("Minimum content similarity must be between 0 and 1.");
        return result;
    }
    if (!mapColumns(left, right, options, &result))
        return result;
    const bool anyCompared = std::any_of(result.columns.cbegin(), result.columns.cend(), participates);
    if (!anyCompared && !left.rows.isEmpty() && !right.rows.isEmpty()) {
        result.error = QStringLiteral("No columns selected for comparison; map at least one comparison column.");
        return result;
    }
    constexpr qint64 maximumResultCells = 4000000;
    if (qint64(std::max(left.rows.size(), right.rows.size())) * result.columns.size() > maximumResultCells) {
        result.error = QStringLiteral("Table comparison exceeds the 4,000,000 result-cell limit; select a smaller data set.");
        return result;
    }
    int invalidNumericCells = 0;
    for (const auto &column : result.columns) {
        if (!participates(column) || !column.numeric)
            continue;
        Decimal value;
        for (int row = 0; row < left.rows.size(); ++row) {
            if (hasCell(left, row, column.left) && !number(left.rows.at(row).at(column.left), &value))
                ++invalidNumericCells;
        }
        for (int row = 0; row < right.rows.size(); ++row) {
            if (hasCell(right, row, column.right) && !number(right.rows.at(row).at(column.right), &value))
                ++invalidNumericCells;
        }
    }
    if (invalidNumericCells)
        result.warnings.append(QStringLiteral("%1 numeric cell(s) are not finite locale-neutral numbers; those values are compared as literal text.").arg(invalidNumericCells));
    Pairing pairing(left.rows.size(), right.rows.size());
    switch (options.alignment) {
    case RowAlignment::Position:
        for (int i = 0; i < std::min(left.rows.size(), right.rows.size()); ++i)
            pairing.pair(i, i);
        break;
    case RowAlignment::Content:
        alignContent(left, right, options, &result, &pairing);
        break;
    case RowAlignment::Key:
        if (!alignKeys(left, right, &result, &pairing))
            return result;
        break;
    default:
        result.error = QStringLiteral("Unknown row alignment mode.");
        return result;
    }
    const int unmatchedRight = std::count(pairing.leftForRight.cbegin(), pairing.leftForRight.cend(), -1);
    if (qint64(left.rows.size() + unmatchedRight) * result.columns.size() > maximumResultCells) {
        result.error = QStringLiteral("Table comparison exceeds the 4,000,000 result-cell limit; select a smaller data set.");
        return result;
    }
    materialize(left, right, pairing, &result);
    return result;
}

QString rowStatusLabel(RowStatus status)
{
    switch (status) {
    case RowStatus::Equal: return QStringLiteral("相同");
    case RowStatus::Different: return QStringLiteral("内容不同");
    case RowStatus::LeftOnly: return QStringLiteral("仅左");
    case RowStatus::RightOnly: return QStringLiteral("仅右");
    }
    return QStringLiteral("未知");
}

} // namespace LqCompare::Table
