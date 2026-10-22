#include "files.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

namespace cedit {
namespace {

bool write_all(int fd, std::string_view bytes) {
    while (!bytes.empty()) {
        const ssize_t written = ::write(fd, bytes.data(), bytes.size());
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        bytes.remove_prefix(static_cast<std::size_t>(written));
    }
    return true;
}

}  // namespace

std::optional<std::string> read_file(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return std::nullopt;
    std::string bytes;
    char buffer[1 << 16];
    for (;;) {
        const ssize_t got = ::read(fd, buffer, sizeof buffer);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) {
            ::close(fd);
            return std::nullopt;
        }
        if (got == 0) break;
        bytes.append(buffer, static_cast<std::size_t>(got));
    }
    ::close(fd);
    return bytes;
}

bool write_file_atomic(const std::string& path, std::string_view bytes) {
    mode_t mode = 0644;
    struct stat existing {};
    if (::stat(path.c_str(), &existing) == 0) mode = existing.st_mode & 07777;
    std::string temporary = path + ".cedit-XXXXXX";
    const int fd = ::mkstemp(temporary.data());
    if (fd < 0) return false;
    const bool ok = write_all(fd, bytes) && ::fchmod(fd, mode) == 0 && ::fsync(fd) == 0;
    ::close(fd);
    if (!ok || std::rename(temporary.c_str(), path.c_str()) != 0) {
        ::unlink(temporary.c_str());
        return false;
    }
    return true;
}

}  // namespace cedit
