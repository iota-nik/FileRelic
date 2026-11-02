#pragma once

// Builds InspectionReport::sections: the human-readable, interface-neutral view
// of the typed results. Internal to the core library.

#include "image_inspector.h"

namespace filerelic {

void build_sections(InspectionReport& report);

} // namespace filerelic
