// mvx driver: compile MVX BASIC to objects, executables, or shared
// subroutine libraries.
//
//   mvx -c prog.b -o prog.o          compile only
//   mvx prog.b sub.b -o prog         compile and link an executable
//   mvx -shared subs.b -o libsubs    compile and link a shared library
//
// Errors go to stderr as "item:line: message" — parseable; the BASIC verb
// will consume this later, so treat the format as an interface.

#include "codegen.h"
#include "parser.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

namespace {

fs::path exeDir() {
#ifdef __APPLE__
    char buf[4096];
    uint32_t sz = sizeof buf;
    if (_NSGetExecutablePath(buf, &sz) == 0)
        return fs::canonical(buf).parent_path();
#else
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
#endif
    return fs::current_path();
}

// The runtime ships next to the compiler: <root>/bin/mvx, <root>/lib/*.
fs::path runtimeLibDir() {
    fs::path lib = exeDir().parent_path() / "lib";
    if (fs::exists(lib / "libmvxrt.a")) return lib;
    std::cerr << "mvx: cannot find runtime library near " << exeDir()
              << "\n";
    exit(1);
}

int usage() {
    std::cerr <<
        "usage: mvx [options] file.b [file.b|file.o ...]\n"
        "  -c           compile to object only (no link)\n"
        "  -o <path>    output path\n"
        "  -shared      produce a shared subroutine library\n"
        "  -O0|-O1|-O2  optimisation level (default -O2)\n"
        "  --emit-llvm  also write textual IR beside each object\n";
    return 2;
}

std::string shellQuote(const std::string &s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

} // namespace

int main(int argc, char **argv) {
    bool compileOnly = false, shared = false;
    mvx::CodegenOptions cg;
    std::string outPath;
    std::vector<std::string> sources, objects;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-c") compileOnly = true;
        else if (a == "-shared") shared = true;
        else if (a == "-o") {
            if (++i >= argc) return usage();
            outPath = argv[i];
        }
        else if (a == "-O0") cg.optLevel = 0;
        else if (a == "-O1") cg.optLevel = 1;
        else if (a == "-O2") cg.optLevel = 2;
        else if (a == "--emit-llvm") cg.emitLLVM = true;
        else if (!a.empty() && a[0] == '-') return usage();
        else if (a.size() > 2 && a.substr(a.size() - 2) == ".o")
            objects.push_back(a);
        else sources.push_back(a);
    }
    if (sources.empty() && objects.empty()) return usage();
    if (compileOnly && shared) {
        std::cerr << "mvx: -c and -shared are mutually exclusive\n";
        return 2;
    }
    if (compileOnly && sources.size() > 1 && !outPath.empty()) {
        std::cerr << "mvx: -c with -o takes a single source file\n";
        return 2;
    }

    // Compile each source to an object.
    std::vector<std::string> linkObjects = objects;
    fs::path tmpDir;
    for (const std::string &src : sources) {
        std::ifstream in(src);
        if (!in) {
            std::cerr << "mvx: cannot open " << src << "\n";
            return 1;
        }
        std::stringstream ss;
        ss << in.rdbuf();

        std::string obj;
        if (compileOnly) {
            obj = outPath.empty()
                      ? fs::path(src).stem().string() + ".o"
                      : outPath;
        } else {
            if (tmpDir.empty()) {
                std::string tmpl =
                    (fs::temp_directory_path() / "mvx-XXXXXX").string();
                std::vector<char> buf(tmpl.begin(), tmpl.end());
                buf.push_back('\0');
                if (!mkdtemp(buf.data())) {
                    std::cerr << "mvx: cannot create temp directory\n";
                    return 1;
                }
                tmpDir = buf.data();
            }
            obj = (tmpDir / (fs::path(src).stem().string() + ".o")).string();
        }

        try {
            mvx::Program prog = mvx::parse(ss.str(), src);
            mvx::compileToObject(prog, obj, cg);
        } catch (const mvx::CompileError &e) {
            std::cerr << e.item << ":" << e.line << ": " << e.what()
                      << "\n";
            return 1;
        }
        linkObjects.push_back(obj);
    }

    if (compileOnly) return 0;

    if (outPath.empty())
        outPath = shared ? "libmvxsubs" : "a.out";

    fs::path lib = runtimeLibDir();
    std::string cmd = "cc";
    if (shared) {
#ifdef __APPLE__
        cmd += " -dynamiclib -undefined dynamic_lookup";
#else
        cmd += " -shared";
#endif
    }
    for (const std::string &o : linkObjects) cmd += " " + shellQuote(o);
    if (!shared)
        cmd += " " + shellQuote((lib / "mvx_crt.o").string());
    cmd += " " + shellQuote((lib / "libmvxrt.a").string());
#ifndef __APPLE__
    cmd += " -ldl";                     // dlopen for storage drivers
#endif
    cmd += " -o " + shellQuote(outPath);

    int rc = std::system(cmd.c_str());

#ifdef __APPLE__
    // Mach-O keeps debug info in the object files; bundle it into a .dSYM
    // before the temp objects are removed, or BASIC-level debugging is
    // silently lost.
    if (rc == 0)
        std::system(("dsymutil " + shellQuote(outPath) +
                     " >/dev/null 2>&1").c_str());
#endif

    if (!tmpDir.empty()) {
        std::error_code ec;
        fs::remove_all(tmpDir, ec);
    }
    if (rc != 0) {
        std::cerr << "mvx: link failed\n";
        return 1;
    }
    return 0;
}
