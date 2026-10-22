#pragma once

#include <sys/types.h>

#include <optional>
#include <string>
#include <vector>

namespace cedit {

struct Captured {
    int exit_code;       // exit status, or 128 + signal
    std::string output;  // stdout and stderr interleaved
};

// Runs argv[0] (an absolute path) to completion with stdin closed.
std::optional<Captured> run_capture(const std::vector<std::string>& argv);

struct PtyChild {
    int master;  // non-blocking, close-on-exec
    pid_t pid;
};

// Starts `program` on a new pseudo-terminal in `directory` with the Turkish UTF-8 locale.
std::optional<PtyChild> spawn_in_pty(const std::string& program, const std::string& directory, unsigned short columns,
                                     unsigned short rows);

int exit_code_of(int wait_status);

// "0,23 sn": a duration in seconds, written the Turkish way.
std::string duration_text(double seconds);

// How a program ended, in plain Turkish: its exit code, or what stopped it (a crash, Control-C, ...).
std::string describe_exit(int wait_status, double seconds);

}  // namespace cedit
