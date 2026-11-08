// Program log: lytton.log next to the settings file, for analysing what happened on the game PC.
//
// Each start renames the last log to lytton.previous.log. Lines are time stamped and flushed at once, so the file
// is complete even if the program is killed. Safe to call from any thread; does nothing before open().
#pragma once

#include <string>

namespace applog {

void open(const std::string &dir);
void close();
std::string path();

// printf-style, one line (no trailing newline needed)
void write(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Buffer the lines this thread writes from now on; release(true) writes them out, release(false) drops them.
// For attempts that are repeated: only the first failure and the eventual success need to be in the log.
void hold();
void release(bool keep);

} // namespace applog

#define LOG(...) ::applog::write(__VA_ARGS__)
