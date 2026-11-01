#pragma once

// Partition-table discovery: MBR, extended MBR (EBR chains) and GPT.
// Rules are documented in docs/MBR_VALIDATION.md.

#include "scan_context.h"

namespace filerelic {

// Reads the first sector and fills context.report with the partition table
// kind, its validation state, partition records, and warnings.
void scan_partitions(ScanContext& context);

} // namespace filerelic
