#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// Nim module names must be valid Nim identifiers (letters, digits,
// underscores; no leading digit). Script filenames often aren't
// (hyphens, dots, etc.), so we build a sanitized stand-in name.
std::string sanitize_identifier(const std::string &raw) {
    std::string out;
    for (char c : raw) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out += c;
        } else {
            out += '_';
        }
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) {
        out = "m_" + out;
    }
    return out;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: nimx <script> [args...]\n";
        return 1;
    }

    const std::string home_dir = getenv("HOME");
    const std::string cache_dir = home_dir + "/.nimx";
    if (!std::filesystem::exists(cache_dir)) {
        std::filesystem::create_directory(cache_dir);
    }

    std::filesystem::path script_path;
    try {
        script_path = std::filesystem::canonical(argv[1]);
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "nimx: cannot resolve script path '" << argv[1] << "': " << e.what() << "\n";
        return 1;
    }
    const std::filesystem::path original_dir = script_path.parent_path();

    // Derive a stable, valid module name from the real path (hash suffix
    // avoids collisions between differently-located scripts with the same
    // basename).
    const std::string base_name = sanitize_identifier(script_path.stem().string());
    std::hash<std::string> hasher;
    std::stringstream hash_hex;
    hash_hex << std::hex << hasher(script_path.string());
    const std::string module_name = base_name + "_" + hash_hex.str();

    // Nim resolves relative include/import paths against the directory of
    // the file that contains them, not against a generic search path. So
    // rather than moving the script (which breaks "include ./foo/bar.nim"),
    // build a "shadow" directory in the cache that symlinks every sibling
    // of the script back to the original directory, plus one symlink to
    // the script itself under a sanitized, valid module name. Compiling
    // that symlink gives Nim a valid identifier while every relative path
    // inside the script still resolves through to the real files.
    const std::filesystem::path shadow_dir = std::filesystem::path(cache_dir) / (module_name + "_env");
    try {
        if (std::filesystem::exists(shadow_dir)) {
            std::filesystem::remove_all(shadow_dir);
        }
        std::filesystem::create_directories(shadow_dir);
        for (const auto &entry : std::filesystem::directory_iterator(original_dir)) {
            if (entry.path().filename() == script_path.filename()) {
                continue; // handled separately below, under the sanitized name
            }
            std::filesystem::create_symlink(entry.path(), shadow_dir / entry.path().filename());
        }
        // The entry point must be a real file, not a symlink: nim resolves
        // symlinks back to the real path when deriving the module name,
        // which would just reintroduce the original invalid-identifier error.
        std::filesystem::copy_file(script_path, shadow_dir / (module_name + ".nim"),
                                    std::filesystem::copy_options::overwrite_existing);
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "nimx: failed to stage script environment: " << e.what() << "\n";
        return 1;
    }

    const std::filesystem::path compile_path = shadow_dir / (module_name + ".nim");
    const std::filesystem::path binary_path = std::filesystem::path(cache_dir) / module_name;

    // Compile only (nim c), not nim r: "nim r" runs the binary itself as a
    // monitored subprocess and wraps abnormal exits (including Ctrl-C)
    // with its own "execution of an external program failed" message.
    // Compiling separately and exec'ing the binary ourselves below means
    // the binary owns the terminal directly, so Ctrl-C produces Nim's own
    // clean SIGINT traceback instead.
    std::vector<std::string> compile_args = {
        "nim",
        "c",
        "--hints:off",
        "--warnings:off",
        "-d:release",
        "--nimcache:" + cache_dir,
        "--path:" + original_dir.string(),
        "-o:" + binary_path.string(),
        compile_path.string(),
    };
    std::vector<char *> compile_cargs;
    compile_cargs.reserve(compile_args.size() + 1);
    for (auto &a : compile_args) {
        compile_cargs.push_back(a.data());
    }
    compile_cargs.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "nimx: fork failed: " << std::strerror(errno) << "\n";
        return 1;
    }
    if (pid == 0) {
        execvp("nim", compile_cargs.data());
        std::cerr << "nimx: failed to exec nim: " << std::strerror(errno) << "\n";
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }

    // Compile succeeded; hand off the terminal to the binary directly.
    std::vector<std::string> run_args = {binary_path.string()};
    for (int i = 2; i < argc; ++i) {
        run_args.emplace_back(argv[i]);
    }
    std::vector<char *> run_cargs;
    run_cargs.reserve(run_args.size() + 1);
    for (auto &a : run_args) {
        run_cargs.push_back(a.data());
    }
    run_cargs.push_back(nullptr);

    execvp(binary_path.c_str(), run_cargs.data());

    // Only reached if execvp failed
    std::cerr << "nimx: failed to exec compiled binary: " << std::strerror(errno) << "\n";
    return 1;
}
