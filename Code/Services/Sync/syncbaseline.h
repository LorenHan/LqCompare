#ifndef LQCOMPARE_SYNCBASELINE_H
#define LQCOMPARE_SYNCBASELINE_H

#include "syncengine.h"
#include "snapshot.h"

namespace LqCompare { namespace Sync {

struct BaselineResult {
    Baseline baseline;
    QString error;
    bool cancelled = false;
    bool ok() const { return error.isEmpty() && !cancelled; }
};

struct BaselineSaveResult {
    QString error;
    bool cancelled = false;
    bool ok() const { return error.isEmpty() && !cancelled; }
};

// Both documents must be complete SHA-256 inventories of an equal common
// state. File metadata may differ, but names, kinds, sizes and hashes must agree.
// Roots bind the baseline to an ordered pair of independent local directories;
// scopeKey binds it to these exact synchronization rules and exclusions.
BaselineResult fromSnapshots(const Snapshot::Document &left, const Snapshot::Document &right,
                             const Options &options,
                             const std::atomic_bool *cancelled = nullptr);
QString validateBaseline(const Baseline &baseline);
BaselineSaveResult saveBaseline(const Baseline &baseline, const QString &path,
                                const std::atomic_bool *cancelled = nullptr);
BaselineResult loadBaseline(const QString &path);

} }
#endif
