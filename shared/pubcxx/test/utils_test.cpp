#include "tinytest.h"
#include "pubcxx/utils.hpp"

#include <filesystem>
#include <string>

static bool is_potentially_absolute(std::string const& path_str) {
    if (path_str.empty()) { return false; }
#ifdef _WIN32
    if (path_str.length() >= 2 && path_str[1] == ':') { return true; }
    if (path_str.length() >= 2 && path_str[0] == '\\' && path_str[1] == '\\') { return true; }
#else
    if (path_str[0] == '/') { return true; }
#endif
    return false;
}

suite("Executable Path Utilities") {
    namespace fs = std::filesystem;

    group("getCurrentExecutablePath") {
        it("returns valid absolute path") {
            std::string exePath = getCurrentExecutablePath();
            check_false(exePath.empty());

            fs::path fsExePath(exePath);
            check(is_potentially_absolute(exePath));
            check(fsExePath.is_absolute());
            check(fs::exists(fsExePath));
            check(fs::is_regular_file(fsExePath));
            check_false(fsExePath.filename().empty());
        }
    }

    group("getCurrentExecutableDirectory") {
        it("returns valid directory") {
            std::string exeDir = getCurrentExecutableDirectory();
            std::string exePathFull = getCurrentExecutablePath();
            fs::path fsExePathFull(exePathFull);

            if (exeDir.empty()) {
                check(fsExePathFull.parent_path().empty());
            } else {
                check_false(exeDir.empty());

                fs::path fsExeDir(exeDir);
                check(is_potentially_absolute(exeDir));
                check(fsExeDir.is_absolute());
                check(fs::exists(fsExeDir));
                check(fs::is_directory(fsExeDir));
                check(fsExePathFull.parent_path() == fsExeDir);
            }
        }
    }

    group("Consistency") {
        it("path and directory are consistent") {
            std::string exePath = getCurrentExecutablePath();
            std::string exeDir = getCurrentExecutableDirectory();

            if (!exeDir.empty()) {
                fs::path fsExePath(exePath);
                fs::path fsExeDir(exeDir);
                check(fsExePath.parent_path() == fsExeDir);
            } else {
                fs::path fsExePath(exePath);
                check(fsExePath.parent_path().empty());
            }
        }
    }
}
