#pragma once

#include <filesystem>
#include <string>

// Tiny thread-safe diagnostic log. Off until init() opens a file; write() is a
// cheap no-op otherwise. Used to trace hard-to-reproduce runtime behaviour
// (streaming/account lifecycle) without a debugger.
namespace fastsm::log {

// Open (truncate) the log file. Safe to call once at startup.
void init(const std::filesystem::path& file);

// Append one timestamped line (thread-safe). No-op if init() wasn't called.
void write(const std::string& message);

} // namespace fastsm::log
