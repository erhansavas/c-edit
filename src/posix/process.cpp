#include "process.hpp"

#include <fcntl.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <util.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>

extern char** environ;

namespace cedit {
namespace {

std::vector<char*> pointers(std::vector<std::string>& strings) {
    std::vector<char*> result;
    result.reserve(strings.size() + 1);
    for (std::string& s : strings) result.push_back(s.data());
    result.push_back(nullptr);
    return result;
}

std::vector<std::string> program_environment() {
    std::vector<std::string> variables;
    for (char** entry = environ; *entry != nullptr; ++entry) {
        const std::string_view variable(*entry);
        if (variable.starts_with("LANG=") || variable.starts_with("LC_") || variable.starts_with("TERM=")) continue;
        variables.emplace_back(variable);
    }
    variables.emplace_back("LANG=tr_TR.UTF-8");
    variables.emplace_back("TERM=dumb");
    return variables;
}

}  // namespace

int exit_code_of(int wait_status) {
    if (WIFEXITED(wait_status)) return WEXITSTATUS(wait_status);
    if (WIFSIGNALED(wait_status)) return 128 + WTERMSIG(wait_status);
    return -1;
}

std::string duration_text(double seconds) {
    char text[32];
    std::snprintf(text, sizeof text, "%.2f sn", seconds);
    std::string result(text);
    std::replace(result.begin(), result.end(), '.', ',');
    return result;
}

std::string describe_exit(int wait_status, double seconds) {
    const std::string after = "Program " + duration_text(seconds);
    if (!WIFSIGNALED(wait_status))
        return after + " içinde bitti, çıkış kodu " + std::to_string(WEXITSTATUS(wait_status)) + ".";
    // A crash takes two lines, what happened and why, so neither wraps in the 80-column console.
    const auto crashed = [&](std::string_view name, std::string_view reason) {
        return after + " sonra çöktü (" + std::string(name) + ").\r\n" + std::string(reason);
    };
    switch (const int signal = WTERMSIG(wait_status)) {
    case SIGSEGV: return crashed("segmentation fault", "İzin verilmeyen bir bellek alanına erişti.");
    case SIGBUS: return crashed("bus error", "İzin verilmeyen bir bellek alanına erişti.");
    case SIGFPE: return crashed("floating-point exception", "Aritmetik bir hata oluştu.");
    case SIGILL:
    case SIGTRAP: return crashed("trap", "Hiç çalışmaması gereken bir koda ulaştı.");
    case SIGABRT: return after + " sonra abort() ile kendini durdurdu, örneğin bir assert tuttu.";
    case SIGINT: return after + " sonra Control-C ile durduruldu.";
    case SIGKILL: return after + " sonra sonlandırıldı.";
    default: return after + " sonra " + std::to_string(signal) + " numaralı sinyalle durduruldu.";
    }
}

std::optional<Captured> run_capture(const std::vector<std::string>& argv) {
    int pipe_fds[2];
    if (::pipe(pipe_fds) != 0) return std::nullopt;
    // The child inherits only stdin, stdout and stderr, never another descriptor of this process.
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDERR_FILENO);
    std::vector<std::string> arguments = argv;
    std::vector<char*> argument_pointers = pointers(arguments);
    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, argv[0].c_str(), &actions, &attributes, argument_pointers.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    ::close(pipe_fds[1]);
    if (spawned != 0) {
        ::close(pipe_fds[0]);
        return std::nullopt;
    }
    Captured captured{-1, {}};
    char buffer[8192];
    for (;;) {
        const ssize_t got = ::read(pipe_fds[0], buffer, sizeof buffer);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        captured.output.append(buffer, static_cast<std::size_t>(got));
    }
    ::close(pipe_fds[0]);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    captured.exit_code = exit_code_of(status);
    return captured;
}

std::optional<PtyChild> spawn_in_pty(const std::string& program, const std::string& directory, unsigned short columns,
                                     unsigned short rows) {
    // Everything the child needs is prepared before fork: only async-signal-safe calls follow it.
    std::vector<std::string> arguments{program};
    std::vector<char*> argument_pointers = pointers(arguments);
    std::vector<std::string> variables = program_environment();
    std::vector<char*> variable_pointers = pointers(variables);
    struct winsize size {};
    size.ws_col = columns;
    size.ws_row = rows;
    const int descriptors = ::getdtablesize();
    int master = -1;
    const pid_t pid = ::forkpty(&master, nullptr, nullptr, &size);
    if (pid < 0) return std::nullopt;
    if (pid == 0) {
        // The program inherits only its terminal: a compiler pipe left open here would stall that compile.
        for (int fd = STDERR_FILENO + 1; fd < descriptors; ++fd) ::close(fd);
        if (::chdir(directory.c_str()) != 0) ::_exit(127);
        ::execve(program.c_str(), argument_pointers.data(), variable_pointers.data());
        ::_exit(127);
    }
    ::fcntl(master, F_SETFL, ::fcntl(master, F_GETFL) | O_NONBLOCK);
    ::fcntl(master, F_SETFD, FD_CLOEXEC);
    return PtyChild{master, pid};
}

}  // namespace cedit
