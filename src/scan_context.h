#pragma once

// Internal to the core library (not part of the public API).
// Bundles what every scanning step needs: the byte source to read from and the
// report to fill in.

#include <string>
#include <utility>

#include "image_inspector.h"
#include "image_source.h"

namespace filerelic {

struct ScanContext {
    ReadOnlySource& source;
    InspectionReport& report;

    void trace(std::string line) { report.trace.push_back(std::move(line)); }
};

} // namespace filerelic
