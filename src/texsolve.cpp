#include <texsolve/texsolve.h>

#include "internal.hpp"
#include "evaluator.hpp"
#include "solver.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
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
    const texsolve_result *parent = nullptr;
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

void append_real(texsolve_result &parent, const char *name, std::string value) {
    parent.children.push_back(make_node(TEXSOLVE_RESULT_REAL, name, std::move(value)));
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

void link_tree(texsolve_result &node, const texsolve_result *parent = nullptr) {
    node.parent = parent;
    for (auto &child : node.children) link_tree(*child, &node);
    if (node.metadata) link_tree(*node.metadata, &node);
}

std::string source_of(std::string_view input, const texsolve::Node &node) {
    if (node.end < node.begin || node.end > input.size()) return {};
    return std::string(input.substr(node.begin, node.end - node.begin));
}

bool contains_call(const texsolve::Node &node, std::string_view name) {
    if (node.kind == texsolve::NodeKind::Call && node.text == name) return true;
    return std::any_of(node.children.begin(), node.children.end(), [&](const auto &child) {
        return contains_call(child, name);
    });
}

bool operation_matches(int32_t operation, const texsolve::Node &root) {
    using texsolve::NodeKind;
    if (operation == TEXSOLVE_OPERATION_AUTO) return true;
    if (operation == TEXSOLVE_OPERATION_DEFINE) return root.kind == NodeKind::Definition;
    if (operation == TEXSOLVE_OPERATION_INTEGRATE) return root.kind == NodeKind::Integral;
    if (operation == TEXSOLVE_OPERATION_LIMIT) return root.kind == NodeKind::Limit;
    if (operation == TEXSOLVE_OPERATION_SUM) return root.kind == NodeKind::Fold && root.text.starts_with("sum");
    if (operation == TEXSOLVE_OPERATION_PRODUCT) return root.kind == NodeKind::Fold && root.text.starts_with("product");
    if (operation == TEXSOLVE_OPERATION_SOLVE) return root.kind == NodeKind::Relation;
    if (operation == TEXSOLVE_OPERATION_OPTIMIZE) return root.kind == NodeKind::Optimization;
    if (operation == TEXSOLVE_OPERATION_ODE_IVP) return root.kind == NodeKind::Ode;
    if (operation == TEXSOLVE_OPERATION_LINEAR_ALGEBRA) {
        return root.kind == NodeKind::Matrix || root.kind == NodeKind::Binary ||
               (root.kind == NodeKind::Call && (root.text == "det" || root.text == "rank" ||
                root.text == "inv" || root.text == "eigenvalues" || root.text == "eigenvectors"));
    }
    return root.kind != NodeKind::Definition && root.kind != NodeKind::Relation &&
           root.kind != NodeKind::Optimization && root.kind != NodeKind::Ode;
}

texsolve::Node substitute(texsolve::Node node, const std::map<std::string, texsolve::Node> &arguments) {
    if (node.kind == texsolve::NodeKind::Symbol) {
        const auto found = arguments.find(node.text);
        if (found != arguments.end()) return found->second;
    }
    for (auto &child : node.children) child = substitute(std::move(child), arguments);
    return node;
}

uint64_t count_nodes(const texsolve::Node &node, uint64_t limit) {
    uint64_t count = 1;
    for (const auto &child : node.children) {
        const uint64_t child_count = count_nodes(child, limit);
        if (child_count > limit || count > limit - child_count) return limit + 1;
        count += child_count;
    }
    return count;
}

uint64_t substituted_node_count(const texsolve::Node &node,
                                const std::map<std::string, texsolve::Node> &arguments,
                                uint64_t limit) {
    if (node.kind == texsolve::NodeKind::Symbol) {
        const auto found = arguments.find(node.text);
        if (found != arguments.end()) return count_nodes(found->second, limit);
    }
    uint64_t count = 1;
    for (const auto &child : node.children) {
        const uint64_t child_count = substituted_node_count(child, arguments, limit);
        if (child_count > limit || count > limit - child_count) return limit + 1;
        count += child_count;
    }
    return count;
}

uint64_t substituted_depth(const texsolve::Node &node,
                           const std::map<std::string, texsolve::Node> &arguments,
                           uint64_t limit) {
    if (node.kind == texsolve::NodeKind::Symbol) {
        const auto found = arguments.find(node.text);
        if (found != arguments.end()) {
            uint64_t depth = 1;
            for (const auto &child : found->second.children) {
                depth = std::max(depth, 1 + substituted_depth(child, {}, limit));
                if (depth > limit) return limit + 1;
            }
            return depth;
        }
    }
    uint64_t depth = 1;
    for (const auto &child : node.children) {
        depth = std::max(depth, 1 + substituted_depth(child, arguments, limit));
        if (depth > limit) return limit + 1;
    }
    return depth;
}

bool expand_functions(texsolve::Node &node, const std::map<std::string, Definition> &definitions,
                      std::vector<std::string> &active, texsolve::ParseOutput &error,
                      uint32_t max_depth, uint32_t max_nodes, uint64_t &node_count,
                      std::chrono::steady_clock::time_point deadline) {
    if (std::chrono::steady_clock::now() >= deadline) {
        error.message = "request deadline exceeded during function expansion";
        error.diagnostic_code = TEXSOLVE_DIAGNOSTIC_DEADLINE;
        error.error_begin = node.begin;
        error.error_end = node.end;
        return false;
    }
    for (auto &child : node.children) {
        if (!expand_functions(child, definitions, active, error, max_depth, max_nodes,
                              node_count, deadline)) return false;
    }
    if (node.kind != texsolve::NodeKind::Call) return true;
    const auto found = definitions.find(node.text);
    if (found == definitions.end()) return true;
    if (found->second.parameters.size() != node.children.size()) {
        error.message = "user function arity mismatch";
        error.diagnostic_code = TEXSOLVE_DIAGNOSTIC_ARITY_MISMATCH;
        error.error_begin = node.begin;
        error.error_end = node.end;
        return false;
    }
    if (std::find(active.begin(), active.end(), node.text) != active.end()) {
        error.message = "recursive user functions are unsupported";
        error.diagnostic_code = TEXSOLVE_DIAGNOSTIC_ARITY_MISMATCH;
        error.error_begin = node.begin;
        error.error_end = node.end;
        return false;
    }
    if (active.size() >= max_depth) {
        error.message = "function expansion nesting limit exceeded";
        error.diagnostic_code = TEXSOLVE_DIAGNOSTIC_NESTING_LIMIT;
        error.error_begin = node.begin;
        error.error_end = node.end;
        return false;
    }
    auto body = texsolve::parse_for_debug(found->second.body,
                                          max_depth - static_cast<uint32_t>(active.size()), max_nodes);
    if (!body.ok) {
        error = std::move(body);
        return false;
    }
    std::map<std::string, texsolve::Node> arguments;
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        arguments.emplace(found->second.parameters[index], node.children[index]);
    }
    const uint64_t current_count = count_nodes(node, max_nodes);
    const uint64_t replacement_count = substituted_node_count(body.root, arguments, max_nodes);
    if (substituted_depth(body.root, arguments, max_depth + 1) > static_cast<uint64_t>(max_depth) + 1) {
        error.message = "function expansion nesting limit exceeded";
        error.diagnostic_code = TEXSOLVE_DIAGNOSTIC_NESTING_LIMIT;
        error.error_begin = node.begin;
        error.error_end = node.end;
        return false;
    }
    if (current_count > node_count || replacement_count > max_nodes ||
        node_count - current_count > max_nodes - replacement_count) {
        error.message = "AST node limit exceeded during function expansion";
        error.diagnostic_code = TEXSOLVE_DIAGNOSTIC_AST_NODE_LIMIT;
        error.error_begin = node.begin;
        error.error_end = node.end;
        return false;
    }
    node_count = node_count - current_count + replacement_count;
    node = substitute(std::move(body.root), arguments);
    active.push_back(found->first);
    const bool expanded = expand_functions(node, definitions, active, error, max_depth, max_nodes,
                                           node_count, deadline);
    active.pop_back();
    return expanded;
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
    if (options->symbolic_backend < TEXSOLVE_SYMBOLIC_AUTO ||
        options->symbolic_backend > TEXSOLVE_SYMBOLIC_GINAC ||
        options->linear_algebra_backend < TEXSOLVE_LINEAR_ALGEBRA_AUTO ||
        options->linear_algebra_backend > TEXSOLVE_LINEAR_ALGEBRA_ARMADILLO ||
        options->integration_backend < TEXSOLVE_INTEGRATION_AUTO ||
        options->integration_backend > TEXSOLVE_INTEGRATION_BOOST_MATH ||
        options->optimization_backend < TEXSOLVE_OPTIMIZATION_AUTO ||
        options->optimization_backend > TEXSOLVE_OPTIMIZATION_NLOPT) {
        return TEXSOLVE_STATUS_INVALID_ARGUMENT;
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
    if (request->linear_algebra_backend < TEXSOLVE_LINEAR_ALGEBRA_AUTO ||
        request->linear_algebra_backend > TEXSOLVE_LINEAR_ALGEBRA_ARMADILLO ||
        request->integration_backend < TEXSOLVE_INTEGRATION_AUTO ||
        request->integration_backend > TEXSOLVE_INTEGRATION_BOOST_MATH ||
        request->optimization_backend < TEXSOLVE_OPTIMIZATION_AUTO ||
        request->optimization_backend > TEXSOLVE_OPTIMIZATION_NLOPT ||
        request->optimization_kind < TEXSOLVE_OPTIMIZATION_KIND_AUTO ||
        request->optimization_kind > TEXSOLVE_OPTIMIZATION_KIND_LEAST_SQUARES) {
        return TEXSOLVE_STATUS_INVALID_ARGUMENT;
    }
    if ((request->binding_count != 0 && request->bindings == nullptr) ||
        (request->binding_count != 0 && request->binding_stride < TEXSOLVE_BINDING_V1_SIZE) ||
        (request->binding_count != 0 && request->binding_stride >
             std::numeric_limits<std::size_t>::max() / request->binding_count)) {
        return TEXSOLVE_STATUS_INVALID_ARGUMENT;
    }
    if ((request->residual_count != 0 && request->residuals == nullptr) ||
        request->residual_count > std::numeric_limits<std::size_t>::max() / sizeof(texsolve_string_view)) {
        return TEXSOLVE_STATUS_INVALID_ARGUMENT;
    }
    auto fail = [&](texsolve_status status, int32_t code, std::string message) noexcept -> texsolve_status {
        try {
            texsolve::ParseOutput error;
            error.message = std::move(message);
            error.diagnostic_code = code;
            *out = make_error(status, error).release();
            return status;
        } catch (...) {
            return TEXSOLVE_STATUS_INTERNAL_ERROR;
        }
    };
    if (request->precision_digits > 10000) {
        return fail(TEXSOLVE_STATUS_RESOURCE_LIMIT, TEXSOLVE_DIAGNOSTIC_PRECISION_LIMIT, "precision limit exceeded");
    }
    if (request->max_iterations > 10000000) {
        return fail(TEXSOLVE_STATUS_RESOURCE_LIMIT, TEXSOLVE_DIAGNOSTIC_ITERATION_LIMIT, "iteration limit exceeded");
    }
    if (request->deadline_ms > 3600000) {
        return fail(TEXSOLVE_STATUS_RESOURCE_LIMIT, TEXSOLVE_DIAGNOSTIC_DEADLINE, "deadline limit exceeded");
    }
    if (request->max_input_bytes > 16777216) {
        return fail(TEXSOLVE_STATUS_RESOURCE_LIMIT, TEXSOLVE_DIAGNOSTIC_INPUT_LIMIT, "input byte limit exceeded");
    }
    if (request->max_nesting_depth > 4096) {
        return fail(TEXSOLVE_STATUS_RESOURCE_LIMIT, TEXSOLVE_DIAGNOSTIC_NESTING_LIMIT, "nesting limit exceeded");
    }
    if (request->max_ast_nodes > 5000000) {
        return fail(TEXSOLVE_STATUS_RESOURCE_LIMIT, TEXSOLVE_DIAGNOSTIC_AST_NODE_LIMIT, "AST node limit exceeded");
    }
    const Config effective{
        request->precision_digits ? request->precision_digits : ctx->config.precision_digits,
        request->max_iterations ? request->max_iterations : ctx->config.max_iterations,
        request->deadline_ms ? request->deadline_ms : ctx->config.deadline_ms,
        request->max_input_bytes ? request->max_input_bytes : ctx->config.max_input_bytes,
        request->max_nesting_depth ? request->max_nesting_depth : ctx->config.max_nesting_depth,
        request->max_ast_nodes ? request->max_ast_nodes : ctx->config.max_ast_nodes,
        request->symbolic_backend ? request->symbolic_backend : ctx->config.symbolic_backend,
        request->linear_algebra_backend ? request->linear_algebra_backend : ctx->config.linear_algebra_backend,
        request->integration_backend ? request->integration_backend : ctx->config.integration_backend,
        request->optimization_backend ? request->optimization_backend : ctx->config.optimization_backend};
    const auto started = std::chrono::steady_clock::now();
    auto deadline_exceeded = [&] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - started).count() >= effective.deadline_ms;
    };
    auto remaining_deadline = [&] {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        return static_cast<uint32_t>(effective.deadline_ms - std::min<int64_t>(effective.deadline_ms, elapsed));
    };
    const uint32_t input_limit = effective.max_input_bytes;
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
        const bool residual_only = latex.empty() && request->residual_count != 0;
        texsolve::ParseOutput parsed;
        if (residual_only) {
            parsed.ok = true;
            parsed.root.kind = texsolve::NodeKind::Optimization;
            parsed.root.text = "min";
        } else {
            parsed = texsolve::parse_for_debug(latex, nesting, nodes);
        }
        if (!parsed.ok) {
            const texsolve_status status = parsed.diagnostic_code == TEXSOLVE_DIAGNOSTIC_INVALID_UTF8
                                               ? TEXSOLVE_STATUS_INVALID_UTF8
                                               : (parsed.diagnostic_code == TEXSOLVE_DIAGNOSTIC_NESTING_LIMIT ||
                                                  parsed.diagnostic_code == TEXSOLVE_DIAGNOSTIC_AST_NODE_LIMIT)
                                                     ? TEXSOLVE_STATUS_RESOURCE_LIMIT
                                               : (parsed.diagnostic_code == TEXSOLVE_DIAGNOSTIC_DUPLICATE_NAME
                                                      ? TEXSOLVE_STATUS_SEMANTIC_ERROR
                                                      : TEXSOLVE_STATUS_PARSE_ERROR);
            *out = make_error(status, parsed).release();
            return status;
        }
        if (deadline_exceeded()) {
            return fail(TEXSOLVE_STATUS_DEADLINE_EXCEEDED, TEXSOLVE_DIAGNOSTIC_DEADLINE, "request deadline exceeded");
        }
        const bool definition = parsed.root.kind == texsolve::NodeKind::Definition;
        if (!operation_matches(request->operation, parsed.root)) {
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
                if (contains_call(body, target.text)) {
                    parsed.message = "recursive user functions are unsupported";
                    parsed.diagnostic_code = TEXSOLVE_DIAGNOSTIC_ARITY_MISMATCH;
                    parsed.error_begin = body.begin;
                    parsed.error_end = body.end;
                    *out = make_error(TEXSOLVE_STATUS_SEMANTIC_ERROR, parsed).release();
                    return TEXSOLVE_STATUS_SEMANTIC_ERROR;
                }
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

        texsolve::ParseOutput expansion_error;
        std::vector<std::string> active_functions;
        uint64_t expansion_nodes = parsed.node_count ? parsed.node_count : count_nodes(parsed.root, nodes);
        const auto expansion_deadline = started + std::chrono::milliseconds(effective.deadline_ms);
        if (!expand_functions(parsed.root, ctx->functions, active_functions, expansion_error,
                              nesting, nodes, expansion_nodes, expansion_deadline)) {
            const texsolve_status status = expansion_error.diagnostic_code == TEXSOLVE_DIAGNOSTIC_DEADLINE
                                               ? TEXSOLVE_STATUS_DEADLINE_EXCEEDED
                                           : (expansion_error.diagnostic_code == TEXSOLVE_DIAGNOSTIC_NESTING_LIMIT ||
                                              expansion_error.diagnostic_code == TEXSOLVE_DIAGNOSTIC_AST_NODE_LIMIT)
                                               ? TEXSOLVE_STATUS_RESOURCE_LIMIT
                                               : TEXSOLVE_STATUS_SEMANTIC_ERROR;
            *out = make_error(status, expansion_error).release();
            return status;
        }

        std::map<std::string, texsolve::Node> bindings;
        std::map<std::string, texsolve::Node> lower_bounds;
        std::map<std::string, texsolve::Node> upper_bounds;
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
            const std::string_view name_view(binding->name.data, binding->name.size);
            if (!texsolve::is_valid_utf8(name_view)) {
                return fail(TEXSOLVE_STATUS_INVALID_UTF8, TEXSOLVE_DIAGNOSTIC_INVALID_UTF8,
                            "binding name is not valid UTF-8");
            }
            const auto valid_binding_utf8 = [](texsolve_string_view view) {
                return texsolve::is_valid_utf8(std::string_view(view.data == nullptr ? "" : view.data, view.size));
            };
            if (!valid_binding_utf8(binding->value_latex) ||
                !valid_binding_utf8(binding->lower_latex) ||
                !valid_binding_utf8(binding->upper_latex)) {
                return fail(TEXSOLVE_STATUS_INVALID_UTF8, TEXSOLVE_DIAGNOSTIC_INVALID_UTF8,
                            "binding expression is not valid UTF-8");
            }
            const auto name_ast = texsolve::parse_for_debug(name_view, nesting, nodes);
            const bool plain_symbol = name_view.size() == 1 &&
                std::isalpha(static_cast<unsigned char>(name_view.front()));
            const bool command_symbol = name_view.starts_with("\\") && name_ast.ok &&
                name_ast.root.text == name_view && name_view != "\\pi" && name_view != "\\infty";
            if (!name_ast.ok || name_ast.root.kind != texsolve::NodeKind::Symbol ||
                (!plain_symbol && !command_symbol)) {
                return fail(TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_DUPLICATE_NAME,
                            "binding name must be one scalar symbol");
            }
            const std::string name(name_view);
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
            auto add_bound = [&](texsolve_string_view view, auto &bounds) {
                if (view.size == 0) return true;
                auto parsed_bound = texsolve::parse_for_debug(
                    std::string_view(view.data, view.size), nesting, nodes);
                return parsed_bound.ok &&
                       bounds.emplace(name, std::move(parsed_bound.root)).second;
            };
            if (!add_bound(binding->lower_latex, lower_bounds) ||
                !add_bound(binding->upper_latex, upper_bounds)) {
                return fail(TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_DOMAIN_ERROR,
                            "binding bounds must be valid scalar expressions");
            }
            if (deadline_exceeded()) {
                return fail(TEXSOLVE_STATUS_DEADLINE_EXCEEDED, TEXSOLVE_DIAGNOSTIC_DEADLINE, "request deadline exceeded");
            }
        }
        std::vector<texsolve::Node> residual_nodes;
        for (std::size_t index = 0; index < request->residual_count; ++index) {
            const auto residual = request->residuals[index];
            if (!valid_view(residual) || !checked_add(residual.size, input_bytes)) {
                return TEXSOLVE_STATUS_INVALID_ARGUMENT;
            }
            if (input_bytes > input_limit) {
                return fail(TEXSOLVE_STATUS_RESOURCE_LIMIT, TEXSOLVE_DIAGNOSTIC_INPUT_LIMIT,
                            "aggregate input byte limit exceeded");
            }
            const auto parsed_residual = texsolve::parse_for_debug(
                std::string_view(residual.data, residual.size), nesting, nodes);
            if (!parsed_residual.ok) {
                const auto status = parsed_residual.diagnostic_code == TEXSOLVE_DIAGNOSTIC_INVALID_UTF8
                                        ? TEXSOLVE_STATUS_INVALID_UTF8 : TEXSOLVE_STATUS_PARSE_ERROR;
                *out = make_error(status, parsed_residual).release();
                return status;
            }
            residual_nodes.push_back(parsed_residual.root);
            if (deadline_exceeded()) {
                return fail(TEXSOLVE_STATUS_DEADLINE_EXCEEDED, TEXSOLVE_DIAGNOSTIC_DEADLINE, "request deadline exceeded");
            }
        }
        const int32_t optimization_kind = request->optimization_kind == TEXSOLVE_OPTIMIZATION_KIND_AUTO
                                              ? (residual_nodes.empty() ? TEXSOLVE_OPTIMIZATION_KIND_GENERAL
                                                                        : TEXSOLVE_OPTIMIZATION_KIND_LEAST_SQUARES)
                                              : request->optimization_kind;
        if ((optimization_kind == TEXSOLVE_OPTIMIZATION_KIND_GENERAL && !residual_nodes.empty()) ||
            (optimization_kind == TEXSOLVE_OPTIMIZATION_KIND_LEAST_SQUARES && residual_nodes.empty())) {
            return fail(TEXSOLVE_STATUS_INVALID_ARGUMENT, TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM,
                        "optimization kind and residual array disagree");
        }
        if (residual_only) {
            for (const auto &[name, value] : bindings) {
                (void)value;
                texsolve::Node variable;
                variable.kind = texsolve::NodeKind::Symbol;
                variable.text = name;
                parsed.root.children.push_back(std::move(variable));
            }
            texsolve::Node objective;
            objective.kind = texsolve::NodeKind::Integer;
            objective.text = "0";
            parsed.root.children.push_back(std::move(objective));
        }

        int32_t operation = request->operation;
        if (operation == TEXSOLVE_OPERATION_AUTO) {
            if (parsed.root.kind == texsolve::NodeKind::Derivative) operation = TEXSOLVE_OPERATION_DIFFERENTIATE;
            else if (parsed.root.kind == texsolve::NodeKind::Integral) operation = TEXSOLVE_OPERATION_INTEGRATE;
            else if (parsed.root.kind == texsolve::NodeKind::Fold) {
                operation = parsed.root.text.starts_with("sum") ? TEXSOLVE_OPERATION_SUM : TEXSOLVE_OPERATION_PRODUCT;
            } else if (parsed.root.kind == texsolve::NodeKind::Matrix ||
                       (parsed.root.kind == texsolve::NodeKind::Binary &&
                        (parsed.root.children[0].kind == texsolve::NodeKind::Matrix ||
                         parsed.root.children[1].kind == texsolve::NodeKind::Matrix)) ||
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
        const uint32_t precision = effective.precision_digits;
        if (operation == TEXSOLVE_OPERATION_LINEAR_ALGEBRA || operation == TEXSOLVE_OPERATION_SOLVE ||
            operation == TEXSOLVE_OPERATION_OPTIMIZE || operation == TEXSOLVE_OPERATION_ODE_IVP) {
            auto solved = texsolve::solve_problem(parsed.root, operation, bindings,
                                                   effective.max_iterations, remaining_deadline(),
                                                   effective.linear_algebra_backend,
                                                   effective.optimization_backend,
                                                   optimization_kind, residual_nodes,
                                                   lower_bounds, upper_bounds);
            if (deadline_exceeded()) {
                return fail(TEXSOLVE_STATUS_DEADLINE_EXCEEDED, TEXSOLVE_DIAGNOSTIC_DEADLINE,
                            "request deadline exceeded");
            }
            if (solved.status != TEXSOLVE_STATUS_OK) {
                texsolve::ParseOutput error;
                error.message = solved.message;
                error.diagnostic_code = solved.diagnostic_code;
                *out = make_error(solved.status, error).release();
                return solved.status;
            }
            auto converted = from_solver(solved.root);
            link_tree(*converted);
            *out = converted.release();
            return TEXSOLVE_STATUS_OK;
        }
        auto evaluated = texsolve::evaluate(parsed.root, operation, effective.symbolic_backend,
                                            bindings, precision, effective.max_iterations,
                                            remaining_deadline(), effective.integration_backend);
        if (deadline_exceeded()) {
            return fail(TEXSOLVE_STATUS_DEADLINE_EXCEEDED, TEXSOLVE_DIAGNOSTIC_DEADLINE,
                        "request deadline exceeded");
        }
        if (evaluated.status != TEXSOLVE_STATUS_OK) {
            texsolve::ParseOutput error;
            error.message = evaluated.message;
            error.diagnostic_code = evaluated.diagnostic_code;
            *out = make_error(evaluated.status, error).release();
            (*out)->backend = std::move(evaluated.backend);
            return evaluated.status;
        }
        result->kind = evaluated.kind;
        result->name = std::move(evaluated.name);
        result->exact = std::move(evaluated.exact);
        result->approximation = std::move(evaluated.approximation);
        result->backend = std::move(evaluated.backend);
        if (result->kind == TEXSOLVE_RESULT_COMPLEX) {
            result->children.push_back(make_node(evaluated.real_kind, "real", std::move(evaluated.real)));
            result->children.push_back(make_node(evaluated.imag_kind, "imag", std::move(evaluated.imag)));
        }
        attach_metadata(*result, evaluated.precision_digits ? evaluated.precision_digits : precision);
        if (!evaluated.error_estimate.empty()) {
            append_real(*result->metadata, "error_estimate", std::move(evaluated.error_estimate));
        }
        link_tree(*result);
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
    while (node != nullptr && node->backend.empty()) node = node->parent;
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
    while (result != nullptr && !result->metadata) result = result->parent;
    return result == nullptr ? nullptr : result->metadata.get();
}

}  // extern "C"
