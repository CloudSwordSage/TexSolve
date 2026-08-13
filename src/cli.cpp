#include "internal.hpp"
#include "i18n.hpp"

#include <charconv>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <io.h>
#include <texsolve/texsolve.h>

namespace {

void print_help() {
    std::cout << texsolve::i18n::translate(
        "Usage: texsolve [options] [operation] [expression]\n"
        "       texsolve [options] --file <path>\n"
        "       texsolve --repl\n\n"
        "Options:\n"
        "  -h, --help       Show this help\n"
        "  -v, --version    Show version\n"
        "  -r, --repl       Start the interactive REPL\n"
        "  -f, --file PATH  Read an expression from PATH\n"
        "  -d, --debug      Write the parsed AST to stderr\n"
        "  -p, --precision DIGITS       Set decimal precision\n"
        "  -b, --backend CATEGORY NAME  Select a backend:\n"
        "      symbolic: auto, symengine, ginac\n"
        "      linear: auto, eigen, armadillo\n"
        "      integration: auto, gsl, boost\n"
        "      optimization: auto, ceres, nlopt\n"
        "  --                End option parsing\n\n"
        "Operations:\n"
        "  evaluate simplify expand factor differentiate integrate limit\n"
        "  sum product solve linear optimize ode define\n");
}

void print_repl_help() {
    std::cout << texsolve::i18n::translate(
        "REPL commands:\n"
        "  :help, :h                 Show this help\n"
        "  :help backend, :h backend List backend choices\n"
        "  :definitions              List saved definitions\n"
        "  :clear                    Clear saved definitions\n"
        "  :precision DIGITS         Set decimal precision\n"
        "  :backend CATEGORY NAME    Select a backend\n"
        "  :quit, :exit              Exit the REPL\n");
}

void print_backend_help() {
    std::cout << texsolve::i18n::translate(
        "Backend choices:\n"
        "  symbolic: auto, symengine, ginac\n"
        "  linear: auto, eigen, armadillo\n"
        "  integration: auto, gsl, boost\n"
        "  optimization: auto, ceres, nlopt\n");
}

int exit_code(texsolve_status status) {
    if (status == TEXSOLVE_STATUS_OK) return 0;
    if (status == TEXSOLVE_STATUS_INVALID_ARGUMENT || status == TEXSOLVE_STATUS_ABI_MISMATCH) return 2;
    if (status == TEXSOLVE_STATUS_INVALID_UTF8 || status == TEXSOLVE_STATUS_PARSE_ERROR ||
        status == TEXSOLVE_STATUS_SEMANTIC_ERROR || status == TEXSOLVE_STATUS_OPERATION_MISMATCH) return 3;
    if (status == TEXSOLVE_STATUS_UNSUPPORTED || status == TEXSOLVE_STATUS_NO_ANALYTIC_SOLUTION ||
        status == TEXSOLVE_STATUS_BACKEND_UNAVAILABLE || status == TEXSOLVE_STATUS_BACKEND_UNSUPPORTED) return 4;
    if (status == TEXSOLVE_STATUS_NOT_CONVERGED) return 5;
    if (status == TEXSOLVE_STATUS_RESOURCE_LIMIT || status == TEXSOLVE_STATUS_DEADLINE_EXCEEDED) return 6;
    return 70;
}

std::string text(texsolve_string_view view) {
    return view.data == nullptr ? std::string{} : std::string(view.data, view.size);
}

std::string kind_name(int32_t kind) {
    switch (kind) {
        case TEXSOLVE_RESULT_LIST: return texsolve::i18n::translate("list");
        case TEXSOLVE_RESULT_MAPPING: return texsolve::i18n::translate("mapping");
        case TEXSOLVE_RESULT_MATRIX: return texsolve::i18n::translate("matrix");
        case TEXSOLVE_RESULT_ROOT_SET: return texsolve::i18n::translate("roots");
        case TEXSOLVE_RESULT_ROOT: return texsolve::i18n::translate("root");
        case TEXSOLVE_RESULT_OPTIMUM: return texsolve::i18n::translate("optimum");
        case TEXSOLVE_RESULT_TRAJECTORY: return texsolve::i18n::translate("trajectory");
        case TEXSOLVE_RESULT_SAMPLE: return texsolve::i18n::translate("sample");
        default: return texsolve::i18n::translate("value");
    }
}

void print_result(const texsolve_result *result, std::size_t indent = 0) {
    const auto exact = text(texsolve_result_exact_latex(result));
    const auto approximate = text(texsolve_result_approximation(result));
    const auto name = text(texsolve_result_name(result));
    std::cout << std::string(indent * 2, ' ');
    if (!name.empty()) std::cout << name << ": ";
    if (!exact.empty()) {
        std::cout << exact;
        if (!approximate.empty() && approximate != exact) std::cout << " ~= " << approximate;
        std::cout << '\n';
        return;
    }
    if (!approximate.empty()) {
        std::cout << approximate << '\n';
        return;
    }
    std::cout << kind_name(texsolve_result_kind(result)) << '\n';
    for (std::size_t index = 0; index < texsolve_result_child_count(result); ++index) {
        print_result(texsolve_result_child(result, index), indent + 1);
    }
}

/** Print a named top-level metadata value when it is present. */
void print_metadata(const texsolve_result *result, std::string_view name) {
    const auto *metadata = texsolve_result_metadata(result);
    if (metadata == nullptr) return;
    for (std::size_t index = 0; index < texsolve_result_child_count(metadata); ++index) {
        const auto *item = texsolve_result_child(metadata, index);
        if (text(texsolve_result_name(item)) == name) {
            print_result(item, 1);
            return;
        }
    }
}

int execute(texsolve_context *context, std::string_view input, int operation, bool debug) {
    if (debug) {
        const auto parsed = texsolve::parse_for_debug(input, 128, 50000);
        if (parsed.ok) std::cerr << parsed.ast;
    }
    texsolve_request request{};
    request.struct_size = sizeof(request);
    request.abi_version = TEXSOLVE_ABI_VERSION;
    request.operation = operation;
    request.latex = {input.data(), input.size()};
    texsolve_result *result = nullptr;
    const auto status = texsolve_execute(context, &request, &result);
    if (status == TEXSOLVE_STATUS_OK && result != nullptr) {
        print_result(result);
        print_metadata(result, "domain");
        if (text(texsolve_result_exact_latex(result)).empty() &&
            !text(texsolve_result_approximation(result)).empty()) {
            const auto backend = text(texsolve_result_backend(result));
            if (!backend.empty()) std::cout << texsolve::i18n::translate("  backend: ") << backend << '\n';
            print_metadata(result, "precision_digits");
        }
    }
    if (result != nullptr) {
        for (std::size_t index = 0; index < texsolve_result_diagnostic_count(result); ++index) {
            texsolve_diagnostic diagnostic{};
            diagnostic.struct_size = sizeof(diagnostic);
            if (texsolve_result_diagnostic(result, index, &diagnostic) == TEXSOLVE_STATUS_OK) {
                std::cerr << "[" << diagnostic.begin_byte << ',' << diagnostic.end_byte << ") "
                          << text(diagnostic.message) << '\n';
            }
        }
    }
    texsolve_result_destroy(result);
    return exit_code(status);
}

/**
 * Parse and store one decimal precision value.
 *
 * Args:
 *     options: Context options to update.
 *     value: Decimal precision text in the supported range.
 * Returns:
 *     bool: True when value is valid and stored.
 */
bool set_precision(texsolve_context_options &options, std::string_view value) {
    uint32_t precision = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), precision);
    if (error != std::errc{} || end != value.data() + value.size() ||
        precision == 0 || precision > 10000) {
        return false;
    }
    options.precision_digits = precision;
    return true;
}

/**
 * Parse and store one category-specific backend selection.
 *
 * Args:
 *     options: Context options to update.
 *     category: Backend category name.
 *     name: Backend name within category.
 * Returns:
 *     bool: True when category and name form a valid selection.
 */
bool set_backend(texsolve_context_options &options, std::string_view category,
                 std::string_view name) {
    const auto parse = [&](std::string_view automatic, std::string_view first,
                           std::string_view second) {
        if (name == automatic) return 0;
        if (name == first) return 1;
        if (name == second) return 2;
        return -1;
    };
    int selected = -1;
    int32_t *target = nullptr;
    if (category == "symbolic") {
        selected = parse("auto", "symengine", "ginac");
        target = &options.symbolic_backend;
    } else if (category == "linear") {
        selected = parse("auto", "eigen", "armadillo");
        target = &options.linear_algebra_backend;
    } else if (category == "integration") {
        selected = parse("auto", "gsl", "boost");
        target = &options.integration_backend;
    } else if (category == "optimization") {
        selected = parse("auto", "ceres", "nlopt");
        target = &options.optimization_backend;
    }
    if (target == nullptr || selected < 0) return false;
    *target = selected;
    return true;
}

bool configure_repl(texsolve_context *context, texsolve_context_options &options,
                    std::string_view line) {
    const auto previous = options;
    bool valid = false;
    if (line.starts_with(":precision ")) {
        valid = set_precision(options, line.substr(11));
    } else if (line.starts_with(":backend ")) {
        std::istringstream input(std::string(line.substr(9)));
        std::string category;
        std::string name;
        std::string trailing;
        valid = (input >> category >> name) && !(input >> trailing) &&
                set_backend(options, category, name);
    }
    if (valid && texsolve_context_configure(context, &options) == TEXSOLVE_STATUS_OK) return true;
    options = previous;
    return false;
}

int repl(texsolve_context *context, texsolve_context_options options) {
    std::string line;
    while (std::cout << "> " && std::getline(std::cin, line)) {
        if (line == ":quit" || line == ":exit") return 0;
        if (line == ":help backend" || line == ":h backend") {
            print_backend_help();
            continue;
        }
        if (line == ":help" || line == ":h") {
            print_repl_help();
            continue;
        }
        if (line == ":clear") {
            texsolve_context_reset(context);
            continue;
        }
        if (line == ":definitions") {
            texsolve_result *snapshot = nullptr;
            if (texsolve_context_snapshot(context, &snapshot) == TEXSOLVE_STATUS_OK) {
                const auto *variables = texsolve_result_child(snapshot, 0);
                const auto *functions = texsolve_result_child(snapshot, 1);
                std::cout << texsolve_result_child_count(variables) << ' '
                          << texsolve::i18n::translate("variables, ")
                          << texsolve_result_child_count(functions) << ' '
                          << texsolve::i18n::translate("functions\n");
            }
            texsolve_result_destroy(snapshot);
            continue;
        }
        if (line.starts_with(":precision ") || line.starts_with(":backend ")) {
            if (!configure_repl(context, options, line)) {
                std::cerr << texsolve::i18n::translate("invalid REPL setting\n");
            }
            continue;
        }
        if (!line.empty()) execute(context, line, TEXSOLVE_OPERATION_AUTO, false);
    }
    return 0;
}

int operation_for(std::string_view name) {
    if (name == "evaluate") return TEXSOLVE_OPERATION_EVALUATE;
    if (name == "simplify") return TEXSOLVE_OPERATION_SIMPLIFY;
    if (name == "expand") return TEXSOLVE_OPERATION_EXPAND;
    if (name == "factor") return TEXSOLVE_OPERATION_FACTOR;
    if (name == "differentiate") return TEXSOLVE_OPERATION_DIFFERENTIATE;
    if (name == "integrate") return TEXSOLVE_OPERATION_INTEGRATE;
    if (name == "limit") return TEXSOLVE_OPERATION_LIMIT;
    if (name == "sum") return TEXSOLVE_OPERATION_SUM;
    if (name == "product") return TEXSOLVE_OPERATION_PRODUCT;
    if (name == "solve") return TEXSOLVE_OPERATION_SOLVE;
    if (name == "linear" || name == "linear-algebra") return TEXSOLVE_OPERATION_LINEAR_ALGEBRA;
    if (name == "optimize") return TEXSOLVE_OPERATION_OPTIMIZE;
    if (name == "ode") return TEXSOLVE_OPERATION_ODE_IVP;
    if (name == "define") return TEXSOLVE_OPERATION_DEFINE;
    return -1;
}

bool stdin_is_terminal() { return _isatty(_fileno(stdin)) != 0; }

}  // namespace

int main(int argc, char **argv) {
    texsolve::i18n::initialize();
    texsolve_context *context = nullptr;
    if (texsolve_context_create(&context) != TEXSOLVE_STATUS_OK) return 70;
    bool debug = false;
    bool force_repl = false;
    texsolve_context_options options{};
    options.struct_size = sizeof(options);
    options.abi_version = TEXSOLVE_ABI_VERSION;
    bool configure_context = false;
    std::string file;
    std::vector<std::string> expressions;
    int operation = TEXSOLVE_OPERATION_AUTO;
    bool options_ended = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (options_ended) {
            expressions.emplace_back(argument);
        } else if (argument == "--") {
            options_ended = true;
        } else if (argument == "-h" || argument == "--help") {
            print_help();
            texsolve_context_destroy(context);
            return 0;
        } else if (argument == "-v" || argument == "--version") {
            std::cout << "texsolve " << TEXSOLVE_VERSION_MAJOR << '.'
                      << TEXSOLVE_VERSION_MINOR << '.' << TEXSOLVE_VERSION_PATCH << '\n';
            texsolve_context_destroy(context);
            return 0;
        } else if (argument == "-d" || argument == "--debug") debug = true;
        else if (argument == "-r" || argument == "--repl") force_repl = true;
        else if (argument == "-p" || argument == "--precision") {
            if (++index >= argc || !set_precision(options, argv[index])) {
                std::cerr << texsolve::i18n::translate("invalid precision value\n");
                texsolve_context_destroy(context);
                return 2;
            }
            configure_context = true;
        } else if (argument == "-b" || argument == "--backend") {
            if (index + 2 >= argc ||
                !set_backend(options, argv[index + 1], argv[index + 2])) {
                std::cerr << texsolve::i18n::translate("invalid backend selection\n");
                texsolve_context_destroy(context);
                return 2;
            }
            index += 2;
            configure_context = true;
        }
        else if (argument == "-f" || argument == "--file") {
            if (++index >= argc) {
                std::cerr << texsolve::i18n::translate("missing path after ") << argument << '\n';
                texsolve_context_destroy(context);
                return 2;
            }
            file = argv[index];
        } else if (const int selected = operation_for(argument); selected >= 0 && expressions.empty()) {
            operation = selected;
        } else if (argument.starts_with("--") ||
                   (argument.size() > 1 && argument.front() == '-' &&
                    std::isalpha(static_cast<unsigned char>(argument[1])))) {
            std::cerr << texsolve::i18n::translate("unknown option: ") << argument << '\n';
            texsolve_context_destroy(context);
            return 2;
        } else expressions.emplace_back(argument);
    }
    if (configure_context && texsolve_context_configure(context, &options) != TEXSOLVE_STATUS_OK) {
        std::cerr << texsolve::i18n::translate("invalid CLI setting\n");
        texsolve_context_destroy(context);
        return 2;
    }
    if (force_repl) {
        if (!file.empty() || !expressions.empty() || debug || operation != TEXSOLVE_OPERATION_AUTO) {
            std::cerr << texsolve::i18n::translate(
                "--repl cannot be combined with input, operation, or debug options\n");
            texsolve_context_destroy(context);
            return 2;
        }
        const int status = repl(context, options);
        texsolve_context_destroy(context);
        return status;
    }
    std::string input;
    if (!file.empty()) {
        if (!stdin_is_terminal() && std::cin.peek() != std::char_traits<char>::eof()) {
            texsolve_context_destroy(context);
            return 2;
        }
        std::ifstream stream(file, std::ios::binary);
        if (!stream) {
            std::cerr << texsolve::i18n::translate("cannot open input file\n");
            texsolve_context_destroy(context);
            return 2;
        }
        input.assign(std::istreambuf_iterator<char>(stream), {});
    } else if (!expressions.empty()) {
        for (std::size_t index = 0; index < expressions.size(); ++index) {
            if (index != 0) input.push_back(' ');
            input += expressions[index];
        }
    } else {
        if (stdin_is_terminal()) {
            const int status = repl(context, options);
            texsolve_context_destroy(context);
            return status;
        }
        input.assign(std::istreambuf_iterator<char>(std::cin), {});
        if (input.empty()) {
            texsolve_context_destroy(context);
            return 2;
        }
    }
    const int status = execute(context, input, operation, debug);
    texsolve_context_destroy(context);
    return status;
}
