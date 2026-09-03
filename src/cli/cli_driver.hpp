#pragma once

#include <iosfwd>

namespace cactus::cli {

/// Runs the Cactus compiler driver. Diagnostics go to `err`; emitted text goes
/// to `out` when no `--output` file is selected. Returns the process exit code.
int run(int argc, char** argv, std::ostream& out, std::ostream& err);

}  // namespace cactus::cli
