#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "generation.hpp"

namespace {

bool run_command(const std::string& command) {
    const int result = std::system(command.c_str());
    if (result == 0) {
        return true;
    }

    std::cerr << "Command failed: " << command << std::endl;
    return false;
}

std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string input_file;
    std::string output_name = "out";
    bool emit_c = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "Missing argument for -o" << std::endl;
                return EXIT_FAILURE;
            }
            output_name = argv[++i];
        } else if (arg == "--emit-c") {
            emit_c = true;
        } else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << std::endl;
            return EXIT_FAILURE;
        } else {
            if (!input_file.empty()) {
                std::cerr << "Multiple input files are not supported" << std::endl;
                return EXIT_FAILURE;
            }
            input_file = arg;
        }
    }

    if (input_file.empty()) {
        std::cerr << "Incorrect usage. Correct usage is..." << std::endl;
        std::cerr << "rivel <input.rivel> [-o <output>] [--emit-c]" << std::endl;
        return EXIT_FAILURE;
    }

    std::string contents;
    {
        std::stringstream contents_stream;
        std::fstream input(input_file, std::ios::in);
        if (!input) {
            std::cerr << "Failed to open input file: " << input_file << std::endl;
            return EXIT_FAILURE;
        }
        contents_stream << input.rdbuf();
        contents = contents_stream.str();
    }

    try {
        Tokenizer tokenizer(std::move(contents));
        std::vector<Token> tokens = tokenizer.tokenize();

        Parser parser(std::move(tokens));
        Program program = parser.parse_program();

        SemanticAnalyzer analyzer(program);
        SemanticContext semantics = analyzer.analyze();

        const std::string c_filename = output_name + ".c";
        const std::string build_c_filename = emit_c ? c_filename : output_name + ".tmp.c";
        CBackend backend(program, semantics);
        const BackendOutput output = backend.generate(build_c_filename);

        {
            std::fstream file(output.filename, std::ios::out | std::ios::trunc);
            if (!file) {
                std::cerr << "Failed to open output file: " << output.filename << std::endl;
                return EXIT_FAILURE;
            }
            file << output.source;
        }

        const std::string command = "clang -std=c11 " + shell_quote(output.filename) + " -o " + shell_quote(output_name);
        const bool compiled = run_command(command);
        if (!emit_c) {
            std::error_code remove_error;
            std::filesystem::remove(output.filename, remove_error);
        }
        if (!compiled) {
            return EXIT_FAILURE;
        }
    } catch (const CompileError& error) {
        std::cerr << error.what() << std::endl;
        return EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "Unexpected compiler error: " << error.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
