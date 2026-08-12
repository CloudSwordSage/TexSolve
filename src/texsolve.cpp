#include <texsolve/texsolve.h>

#include "internal.hpp"
#include "evaluator.hpp"
#include "solver.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <map>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Config {
    uint32_t precision_digits = 15;
    uint32_t max_iterations = 10000;
    uint32_t deadline_ms = 5000;
    uint32_t max_input_bytes = 65536;
    uint32_t max_nesting_depth = 128;
    uint32_t max_ast_nodes = 50000;
    int32_t symbolic_backend = TEXSOLVE_SYMBOLIC_AUTO;
    int32_t linear_algebra_backend = TEXSOLVE_LINEAR_ALGEBRA_AUTO;
    int32_t integration_backend = TEXSOLVE_INTEGRATION_AUTO;
    int32_t optimization_backend = TEXSOLVE_OPTIMIZATION_AUTO;
};

struct DiagnosticStorage {
    int32_t severity = TEXSOLVE_SEVERITY_ERROR;
    int32_t code = TEXSOLVE_DIAGNOSTIC_NONE;
    size_t begin_byte = 0;
    size_t end_byte = 0;
    std::string message;
};

struct Definition {
    std::vector<std::string> parameters;
    std::string body;
};

constexpr bool in_range(uint32_t value, uint32_t maximum) {
    return value == 0 || value <= maximum;
}

constexpr bool valid_config(const Config &config) {
    return in_range(config.precision_digits, 10000) &&
           in_range(config.max_iterations, 10000000) &&
           in_range(config.deadline_ms, 3600000) &&
           in_range(config.max_input_bytes, 16777216) &&
           in_range(config.max_nesting_depth, 4096) &&
           in_range(config.max_ast_nodes, 5000000) &&
           config.symbolic_backend >= 0 && config.symbolic_backend <= 2 &&
           config.linear_algebra_backend >= 0 && config.linear_algebra_backend <= 2 &&
           config.integration_backend >= 0 && config.integration_backend <= 2 &&
           config.optimization_backend >= 0 && config.optimization_backend <= 2;
}

texsolve_string_view view_of(const std::string &value) {
    return {value.data(), value.size()};
}

std::unique_ptr<texsolve_result> make_node(int32_t kind, std::string name = {}, std::string exact = {});

}  // namespace

struct texsolve_result {
    texsolve_status status = TEXSOLVE_STATUS_OK;
    int32_t kind = TEXSOLVE_RESULT_NONE;
    std::string name;
    std::string exact;
    std::string approximation;
    std::string backend;
    std::vector<std::unique_ptr<texsolve_result>> children;
    std::unique_ptr<texsolve_result> metadata;
    std::vector<DiagnosticStorage> diagnostics;
};

struct texsolve_context {
    Config config;
    std::map<std::string, std::string> variables;
    std::map<std::string, Definition> functions;
};

namespace {

std::unique_ptr<texsolve_result> make_node(int32_t kind, std::string name, std::string exact) {
    auto node = std::make_unique<texsolve_result>();
    node->kind = kind;
    node->name = std::move(name);
    node->exact = std::move(exact);
    return node;
}

void append_integer(texsolve_result &parent, const char *name, uint32_t value) {
    parent.children.push_back(make_node(TEXSOLVE_RESULT_INTEGER, name, std::to_string(value)));
}

const char *backend_name(int32_t value, const char *first, const char *second) {
    return value == 0 ? "auto" : (value == 1 ? first : second);
}

std::unique_ptr<texsolve_result> make_snapshot(const texsolve_context &context) {
    auto root = make_node(TEXSOLVE_RESULT_MAPPING);
    auto variables = make_node(TEXSOLVE_RESULT_MAPPING, "variables");
    for (const auto &[name, value] : context.variables) {
        variables->children.push_back(make_node(TEXSOLVE_RESULT_SYMBOLIC, name, value));
    }
    root->children.push_back(std::move(variables));
    auto functions = make_node(TEXSOLVE_RESULT_MAPPING, "functions");
    for (const auto &[name, definition] : context.functions) {
        auto function = make_node(TEXSOLVE_RESULT_MAPPING, name);
        auto parameters = make_node(TEXSOLVE_RESULT_LIST, "parameters");
        for (const auto &parameter : definition.parameters) {
            parameters->children.push_back(make_node(TEXSOLVE_RESULT_SYMBOLIC, {}, parameter));
        }
        function->children.push_back(std::move(parameters));
        function->children.push_back(make_node(TEXSOLVE_RESULT_SYMBOLIC, "body", definition.body));
        functions->children.push_back(std::move(function));
    }
    root->children.push_back(std::move(functions));
    auto settings = make_node(TEXSOLVE_RESULT_MAPPING, "config");
    append_integer(*settings, "precision_digits", context.config.precision_digits);
    append_integer(*settings, "max_iterations", context.config.max_iterations);
    append_integer(*settings, "deadline_ms", context.config.deadline_ms);
    append_integer(*settings, "max_input_bytes", context.config.max_input_bytes);
    append_integer(*settings, "max_nesting_depth", context.config.max_nesting_depth);
    append_integer(*settings, "max_ast_nodes", context.config.max_ast_nodes);
    settings->children.push_back(make_node(TEXSOLVE_RESULT_SYMBOLIC, "symbolic_backend",
                                           backend_name(context.config.symbolic_backend, "symengine", "ginac")));
    settings->children.push_back(make_node(TEXSOLVE_RESULT_SYMBOLIC, "linear_algebra_backend",
                                           backend_name(context.config.linear_algebra_backend, "eigen", "armadillo")));
    settings->children.push_back(make_node(TEXSOLVE_RESULT_SYMBOLIC, "integration_backend",
                                           backend_name(context.config.integration_backend, "gsl", "boost_math")));
    settings->children.push_back(make_node(TEXSOLVE_RESULT_SYMBOLIC, "optimization_backend",
                                           backend_name(context.config.optimization_backend, "ceres", "nlopt")));
    root->children.push_back(std::move(settings));
    return root;
}

std::unique_ptr<texsolve_result> make_error(texsolve_status status, const texsolve::ParseOutput &parsed) {
    auto result = make_node(TEXSOLVE_RESULT_NONE);
    result->status = status;
    result->diagnostics.push_back({TEXSOLVE_SEVERITY_ERROR, parsed.diagnostic_code,
                                   parsed.error_begin, parsed.error_end, parsed.message});
    return result;
}

bool valid_view(texsolve_string_view view) {
    return view.data != nullptr || view.size == 0;
}

bool checked_add(std::size_t value, std::size_t &total) {
    if (value > std::numeric_limits<std::size_t>::max() - total) return false;
    total += value;
    return true;
}

void attach_metadata(texsolve_result &result, uint32_t precision) {
    result.metadata = make_node(TEXSOLVE_RESULT_METADATA);
    append_integer(*result.metadata, "precision_digits", precision);
}

std::unique_ptr<texsolve_result> from_solver(const texsolve::SolverNode &source) {
    auto result = make_node(source.kind, source.name, source.exact);
    result->approximation = source.approximation;
    result->backend = source.backend;
    for (const auto &child : source.children) result->children.push_back(from_solver(child));
    if (!source.metadata.empty()) {
        result->metadata = make_node(TEXSOLVE_RESULT_METADATA);
        for (const auto &item : source.metadata) result->metadata->children.push_back(from_solver(item));
    }
    return result;
}

std::string source_of(std::string_view input, const texsolve::Node &node) {
    if (node.end < node.begin || node.end > input.size()) return {};
    return std::string(input.substr(node.begin, node.end - node.begin));
}

}  // namespace

extern "C" {

uint32_t TEXSOLVE_CALL texsolve_abi_version(void) { return TEXSOLVE_ABI_VERSION; }

texsolve_status TEXSOLVE_CALL texsolve_context_create(texsolve_context **out) {
    if (out == nullptr) return TEXSOLVE_STATUS_INVALID_ARGUMENT;
    *out = nullptr;
    try {
        *out = new texsolve_context();
        return TEXSOLVE_STATUS_OK;
    } catch (const std::bad_alloc &) {
        return TEXSOLVE_STATUS_INTERNAL_ERROR;
    } catch (...) {
        return TEXSOLVE_STATUS_INTERNAL_ERROR;
    }
}

void TEXSOLVE_CALL texsolve_context_destroy(texsolve_context *ctx) { delete ctx; }

texsolve_status TEXSOLVE_CALL texsolve_context_configure(
    texsolve_context *ctx, const texsolve_context_options *options) {
    if (ctx == nullptr || options == nullptr) return TEXSOLVE_STATUS_INVALID_ARGUMENT;
    if (options->struct_size < TEXSOLVE_CONTEXT_OPTIONS_V1_SIZE ||
        options->abi_version != TEXSOLVE_ABI_VERSION) {
        return TEXSOLVE_STATUS_ABI_MISMATCH;
    }
    const Config defaults;
    Config next{
        options->precision_digits ? options->precision_digits : defaults.precision_digits,
        options->max_iterations ? options->max_iterations : defaults.max_iterations,
        options->deadline_ms ? options->deadline_ms : defaults.deadline_ms,
        options->max_input_bytes ? options->max_input_bytes : defaults.max_input_bytes,
        options->max_nesting_depth ? options->max_nesting_depth : defaults.max_nesting_depth,
        options->max_ast_nodes ? options->max_ast_nodes : defaults.max_ast_nodes,
        options->symbolic_backend,
        options->linear_algebra_backend,
        options->integration_backend,
        options->optimization_backend};
    if (!valid_config(next)) return TEXSOLVE_STATUS_RESOURCE_LIMIT;
    ctx->config = next;
    return TEXSOLVE_STATUS_OK;
}

texsolve_status TEXSOLVE_CALL texsolve_context_snapshot(
    const texsolve_context *ctx, texsolve_result **out) {
    if (out == nullptr) return TEXSOLVE_STATUS_INVALID_ARGUMENT;
    *out = nullptr;
    if (ctx == nullptr) return TEXSOLVE_STATUS_INVALID_ARGUMENT;
    try {
        *out = make_snapshot(*ctx).release();
        return TEXSOLVE_STATUS_OK;
    } catch (...) {
        return TEXSOLVE_STATUS_INTERNAL_ERROR;
    }
}

texsolve_status TEXSOLVE_CALL texsolve_context_reset(texsolve_context *ctx) {
    if (ctx == nullptr) return TEXSOLVE_STATUS_INVALID_ARGUMENT;
    ctx->variables.clear();
    ctx->functions.clear();
    return TEXSOLVE_STATUS_OK;
}

texsolve_status TEXSOLVE_CALL texsolve_execute(
    texsolve_context *ctx, const texsolve_request *request, texsolve_result **out) {
    if (out == nullptr) return TEXSOLVE_STATUS_INVALID_ARGUMENT;
    *out = nullptr;
    if (ctx == nullptr || request == nullptr) return TEXSOLVE_STATUS_INVALID_ARGUMENT;
    if (request->struct_size < TEXSOLVE_REQUEST_V1_SIZE || request->abi_version != TEXSOLVE_ABI_VERSION) {
        return TEXSOLVE_STATUS_ABI_MISMATCH;
    }
    if (!valid_view(request->latex)) return TEXSOLVE_STATUS_INVALID_ARGUMENT;
    if (request->operation < TEXSOLVE_OPERATION_AUTO || request->operation > TEXSOLVE_OPERATION_DEFINE) {
        return TEXSOLVE_STATUS_INVALID_ARGUMENT;
    }
    if (request->symbolic_backend < TEXSOLVE_SYMBOLIC_AUTO ||
        request->symbolic_backend > TEXSOLVE_SYMBOLIC_GINAC) {
        return TEXSOLVE_STATUS_INVALID_ARGUMENT;
    }
    if ((request->binding_count != 0 && request->bindings == nullptr) ||
        (request->binding_count != 0 && request->binding_stride < TEXSOLVE_BINDING_V1_SIZE) ||
        (request->binding_count != 0 && request->binding_stride >
             std::numeric_limits<std::size_t>::max() / request->binding_count)) {
        return TEXSOLVE_STATUS_INVALID_ARGUMENT;
    }
    const uint32_t input_limit = request->max_input_bytes ? request->max_input_bytes : ctx->config.max_input_bytes;
    std::size_t input_bytes = request->latex.size;
    if (request->latex.size > input_limit) {
        texsolve::ParseOutput error;
        error.message = "input byte limit exceeded";
        error.diagnostic_code = TEXSOLVE_DIAGNOSTIC_INPUT_LIMIT;
        *out = make_error(TEXSOLVE_STATUS_RESOURCE_LIMIT, error).release();
        return TEXSOLVE_STATUS_RESOURCE_LIMIT;
    }
    try {
        const std::string_view latex(request->latex.data, request->latex.size);
        const uint32_t nesting = request->max_nesting_depth ? request->max_nesting_depth : ctx->config.max_nesting_depth;
        const uint32_t nodes = request->max_ast_nodes ? request->max_ast_nodes : ctx->config.max_ast_nodes;
        auto parsed = texsolve::parse_for_debug(latex, nesting, nodes);
        if (!parsed.ok) {
            const texsolve_status status = parsed.diagnostic_code == TEXSOLVE_DIAGNOSTIC_INVALID_UTF8
                                               ? TEXSOLVE_STATUS_INVALID_UTF8
                                               : (parsed.diagnostic_code == TEXSOLVE_DIAGNOSTIC_DUPLICATE_NAME
                                                      ? TEXSOLVE_STATUS_SEMANTIC_ERROR
                                                      : TEXSOLVE_STATUS_PARSE_ERROR);
            *out = make_error(status, parsed).release();
            return status;
        }
        const bool definition = parsed.root.kind == texsolve::NodeKind::Definition;
        if ((request->operation == TEXSOLVE_OPERATION_DEFINE && !definition) ||
            (request->operation != TEXSOLVE_OPERATION_AUTO &&
             request->operation != TEXSOLVE_OPERATION_DEFINE && definition)) {
            parsed.message = "explicit operation conflicts with request syntax";
            parsed.diagnostic_code = TEXSOLVE_DIAGNOSTIC_OPERATION_CONFLICT;
            parsed.error_begin = 0;
            parsed.error_end = request->latex.size;
            *out = make_error(TEXSOLVE_STATUS_OPERATION_MISMATCH, parsed).release();
            return TEXSOLVE_STATUS_OPERATION_MISMATCH;
        }

        auto result = make_node(TEXSOLVE_RESULT_SYMBOLIC, {}, std::string(latex));
        if (definition) {
            const auto &target = parsed.root.children[0];
            const auto &body = parsed.root.children[1];
            if (target.kind == texsolve::NodeKind::Symbol) {
                ctx->variables[target.text] = source_of(latex, body);
                ctx->functions.erase(target.text);
                result->name = target.text;
            } else {
                Definition value;
                for (const auto &parameter : target.children) value.parameters.push_back(parameter.text);
                value.body = source_of(latex, body);
                ctx->functions[target.text] = std::move(value);
                ctx->variables.erase(target.text);
                result->name = target.text;
            }
            *out = result.release();
            return TEXSOLVE_STATUS_OK;
        }

        std::map<std::string, texsolve::Node> bindings;
        for (const auto &[name, value] : ctx->variables) {
            auto binding = texsolve::parse_for_debug(value, nesting, nodes);
            if (binding.ok) bindings.emplace(name, std::move(binding.root));
        }
        const auto *binding_bytes = reinterpret_cast<const unsigned char *>(request->bindings);
        for (std::size_t index = 0; index < request->binding_count; ++index) {
            const auto *binding = reinterpret_cast<const texsolve_binding *>(
                binding_bytes + index * request->binding_stride);
            if (binding->struct_size < TEXSOLVE_BINDING_V1_SIZE || !valid_view(binding->name) ||
                !valid_view(binding->value_latex) || !valid_view(binding->lower_latex) ||
                !valid_view(binding->upper_latex)) {
                return TEXSOLVE_STATUS_INVALID_ARGUMENT;
            }
            if (!checked_add(binding->name.size, input_bytes) ||
                !checked_add(binding->value_latex.size, input_bytes) ||
                !checked_add(binding->lower_latex.size, input_bytes) ||
                !checked_add(binding->upper_latex.size, input_bytes)) {
                return TEXSOLVE_STATUS_INVALID_ARGUMENT;
            }
            if (input_bytes > input_limit) {
                texsolve::ParseOutput error;
                error.message = "aggregate input byte limit exceeded";
                error.diagnostic_code = TEXSOLVE_DIAGNOSTIC_INPUT_LIMIT;
                *out = make_error(TEXSOLVE_STATUS_RESOURCE_LIMIT, error).release();
                return TEXSOLVE_STATUS_RESOURCE_LIMIT;
            }
            const std::string name(binding->name.data, binding->name.size);
            const std::string_view value(binding->value_latex.data, binding->value_latex.size);
            auto value_ast = texsolve::parse_for_debug(value, nesting, nodes);
            if (!value_ast.ok || value_ast.root.kind == texsolve::NodeKind::Definition ||
                !bindings.emplace(name, std::move(value_ast.root)).second) {
                texsolve::ParseOutput error;
                error.message = "binding names must be unique scalar symbols";
                error.diagnostic_code = TEXSOLVE_DIAGNOSTIC_DUPLICATE_NAME;
                *out = make_error(TEXSOLVE_STATUS_SEMANTIC_ERROR, error).release();
                return TEXSOLVE_STATUS_SEMANTIC_ERROR;
            }
        }

        int32_t operation = request->operation;
        if (operation == TEXSOLVE_OPERATION_AUTO) {
            if (parsed.root.kind == texsolve::NodeKind::Derivative) operation = TEXSOLVE_OPERATION_DIFFERENTIATE;
            else if (parsed.root.kind == texsolve::NodeKind::Integral) operation = TEXSOLVE_OPERATION_INTEGRATE;
            else if (parsed.root.kind == texsolve::NodeKind::Fold) {
                operation = parsed.root.text.starts_with("sum") ? TEXSOLVE_OPERATION_SUM : TEXSOLVE_OPERATION_PRODUCT;
            } else if (parsed.root.kind == texsolve::NodeKind::Matrix ||
                       (parsed.root.kind == texsolve::NodeKind::Call &&
                        (parsed.root.text == "det" || parsed.root.text == "rank" ||
                         parsed.root.text == "inv" || parsed.root.text == "eigenvalues" ||
                         parsed.root.text == "eigenvectors"))) {
                operation = TEXSOLVE_OPERATION_LINEAR_ALGEBRA;
            } else if (parsed.root.kind == texsolve::NodeKind::Relation) operation = TEXSOLVE_OPERATION_SOLVE;
            else if (parsed.root.kind == texsolve::NodeKind::Optimization) operation = TEXSOLVE_OPERATION_OPTIMIZE;
            else if (parsed.root.kind == texsolve::NodeKind::Ode) operation = TEXSOLVE_OPERATION_ODE_IVP;
            else operation = TEXSOLVE_OPERATION_EVALUATE;
        }
        const uint32_t precision = request->precision_digits ? request->precision_digits : ctx->config.precision_digits;
        if (precision > 10000) {
            texsolve::ParseOutput error;
            error.message = "precision limit exceeded";
            error.diagnostic_code = TEXSOLVE_DIAGNOSTIC_PRECISION_LIMIT;
            *out = make_error(TEXSOLVE_STATUS_RESOURCE_LIMIT, error).release();
            return TEXSOLVE_STATUS_RESOURCE_LIMIT;
        }
        if (operation == TEXSOLVE_OPERATION_LINEAR_ALGEBRA || operation == TEXSOLVE_OPERATION_SOLVE ||
            operation == TEXSOLVE_OPERATION_OPTIMIZE || operation == TEXSOLVE_OPERATION_ODE_IVP) {
            const uint32_t iterations = request->max_iterations ? request->max_iterations : ctx->config.max_iterations;
            const uint32_t deadline = request->deadline_ms ? request->deadline_ms : ctx->config.deadline_ms;
            auto solved = texsolve::solve_problem(parsed.root, operation, bindings, iterations, deadline);
            if (solved.status != TEXSOLVE_STATUS_OK) {
                texsolve::ParseOutput error;
                error.message = solved.message;
                error.diagnostic_code = solved.diagnostic_code;
                *out = make_error(solved.status, error).release();
                return solved.status;
            }
            *out = from_solver(solved.root).release();
            return TEXSOLVE_STATUS_OK;
        }
        auto evaluated = texsolve::evaluate(parsed.root, operation, request->symbolic_backend,
                                            bindings, precision);
        if (evaluated.status != TEXSOLVE_STATUS_OK) {
            texsolve::ParseOutput error;
            error.message = evaluated.message;
            error.diagnostic_code = evaluated.diagnostic_code;
            *out = make_error(evaluated.status, error).release();
            (*out)->backend = std::move(evaluated.backend);
            return evaluated.status;
        }
        result->kind = evaluated.kind;
        result->exact = std::move(evaluated.exact);
        result->approximation = std::move(evaluated.approximation);
        result->backend = std::move(evaluated.backend);
        attach_metadata(*result, precision);
        *out = result.release();
        return TEXSOLVE_STATUS_OK;
    } catch (...) {
        return TEXSOLVE_STATUS_INTERNAL_ERROR;
    }
}

void TEXSOLVE_CALL texsolve_result_destroy(texsolve_result *result) { delete result; }

texsolve_status TEXSOLVE_CALL texsolve_result_status(const texsolve_result *result) {
    return result == nullptr ? TEXSOLVE_STATUS_INVALID_ARGUMENT : result->status;
}

int32_t TEXSOLVE_CALL texsolve_result_kind(const texsolve_result *result) {
    return result == nullptr ? TEXSOLVE_RESULT_NONE : result->kind;
}

texsolve_string_view TEXSOLVE_CALL texsolve_result_name(const texsolve_result *result) {
    return result == nullptr ? texsolve_string_view{nullptr, 0} : view_of(result->name);
}

texsolve_string_view TEXSOLVE_CALL texsolve_result_exact_latex(const texsolve_result *result) {
    return result == nullptr ? texsolve_string_view{nullptr, 0} : view_of(result->exact);
}

texsolve_string_view TEXSOLVE_CALL texsolve_result_approximation(const texsolve_result *result) {
    return result == nullptr ? texsolve_string_view{nullptr, 0} : view_of(result->approximation);
}

texsolve_string_view TEXSOLVE_CALL texsolve_result_backend(const texsolve_result *result) {
    if (result == nullptr) return {nullptr, 0};
    const texsolve_result *node = result;
    while (node != nullptr && node->backend.empty()) node = node->metadata.get();
    return node == nullptr ? texsolve_string_view{nullptr, 0} : view_of(node->backend);
}

size_t TEXSOLVE_CALL texsolve_result_child_count(const texsolve_result *result) {
    return result == nullptr ? 0 : result->children.size();
}

const texsolve_result *TEXSOLVE_CALL texsolve_result_child(
    const texsolve_result *result, size_t index) {
    return result == nullptr || index >= result->children.size() ? nullptr : result->children[index].get();
}

size_t TEXSOLVE_CALL texsolve_result_diagnostic_count(const texsolve_result *result) {
    return result == nullptr ? 0 : result->diagnostics.size();
}

texsolve_status TEXSOLVE_CALL texsolve_result_diagnostic(
    const texsolve_result *result, size_t index, texsolve_diagnostic *out) {
    if (result == nullptr || out == nullptr || index >= result->diagnostics.size()) {
        return TEXSOLVE_STATUS_INVALID_ARGUMENT;
    }
    if (out->struct_size < TEXSOLVE_DIAGNOSTIC_V1_MIN_SIZE) return TEXSOLVE_STATUS_ABI_MISMATCH;
    const uint32_t caller_size = out->struct_size;
    const auto &source = result->diagnostics[index];
    texsolve_diagnostic value{sizeof(texsolve_diagnostic), source.severity, source.code,
                              source.begin_byte, source.end_byte, view_of(source.message)};
    std::memcpy(out, &value, std::min<size_t>(caller_size, sizeof(value)));
    return TEXSOLVE_STATUS_OK;
}

const texsolve_result *TEXSOLVE_CALL texsolve_result_metadata(const texsolve_result *result) {
    return result == nullptr ? nullptr : result->metadata.get();
}

}  // extern "C"
