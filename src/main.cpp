// Command-line driver: source file to native executable.
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "checker.hpp"
#include "codegen.hpp"
#include "optimizer.hpp"
#include "parser.hpp"

namespace {

void usage() {
    std::cerr << "usage: minic <file> [-o output] [-S] [--no-opt]\n"
                 "  -o output   executable to write (default a.out), or assembly with -S\n"
                 "  -S          write assembly instead of an executable\n"
                 "  --no-opt    turn off constant folding\n";
}

// Runs the system assembler and linker directly, with no shell, so file names can't be misread.
int assemble(const std::string& assembly_path, const std::string& output) {
    const pid_t child = fork();
    if (child < 0) {
        return 1;
    }
    if (child == 0) {
        execlp("clang", "clang", "-arch", "arm64", "-x", "assembler", assembly_path.c_str(), "-o", output.c_str(),
               static_cast<char*>(nullptr));
        std::perror("could not run clang");
        _exit(127);
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) {
        return 1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string input;
    std::string output;
    bool assembly_only = false;
    bool optimize_code = true;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            output = argv[++i];
        } else if (arg == "-S") {
            assembly_only = true;
        } else if (arg == "--no-opt") {
            optimize_code = false;
        } else if (!arg.empty() && arg[0] != '-' && input.empty()) {
            input = arg;
        } else {
            usage();
            return 2;
        }
    }
    if (input.empty()) {
        usage();
        return 2;
    }

    std::ifstream file(input);
    if (!file) {
        std::cerr << "minic: could not open " << input << "\n";
        return 2;
    }
    std::stringstream source;
    source << file.rdbuf();

    std::string assembly;
    try {
        minic::Program program = minic::parse(source.str());
        minic::check(program);
        if (optimize_code) {
            minic::optimize(program);
        }
        assembly = minic::generate(program);
    } catch (const minic::CompileError& error) {
        std::cerr << input << ":" << error.location.line << ":" << error.location.column << ": error: " << error.what() << "\n";
        return 1;
    }

    if (assembly_only) {
        if (output.empty()) {
            std::cout << assembly;
        } else {
            std::ofstream(output) << assembly;
        }
        return 0;
    }

    char assembly_path[] = "/tmp/minic-XXXXXX";
    const int fd = mkstemp(assembly_path);
    if (fd < 0) {
        std::cerr << "minic: could not create a temporary file\n";
        return 2;
    }
    close(fd);
    std::ofstream(assembly_path) << assembly;
    const int result = assemble(assembly_path, output.empty() ? "a.out" : output);
    std::remove(assembly_path);
    return result;
}
