// Shared doctest entry point for all test binaries.
//
// Redirects test-written scratch data (content DBs, cooked projects, temp files - all created
// via CWD-relative paths like "scratch_xxx_db") into a single gitignored ".test-scratch"
// directory, so test runs never litter the repo root (or the build dir). Fixture READS are
// unaffected: they use absolute ${CMAKE_SOURCE_DIR} compile-definition paths.
#pragma once

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <filesystem>

int main(int argc, char** argv)
{
    std::error_code ec;
    std::filesystem::create_directories(".test-scratch", ec);
    std::filesystem::current_path(".test-scratch", ec); // scratch writes land here, not the CWD root

    doctest::Context context(argc, argv);
    return context.run();
}
