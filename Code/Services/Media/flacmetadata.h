#ifndef LQCOMPARE_FLACMETADATA_H
#define LQCOMPARE_FLACMETADATA_H

#include "mediametadata.h"

class QFile;

namespace LqCompare { namespace Media { namespace Internal {

// Reads native FLAC metadata from an open, seekable file. Audio is never decoded.
// The caller supplies path and fileSize; diagnostics and parsed fields are filled here.
bool readFlac(QFile &file, Document *document, const ReadLimits &limits);

} } }

#endif
