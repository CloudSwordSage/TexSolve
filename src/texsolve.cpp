#include <texsolve/texsolve.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <string>
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

std::unique_ptr<texsolve_result> make_snapshot(const Config &config) {
    auto root = make_node(TEXSOLVE_RESULT_MAPPING);
    root->children.push_back(make_node(TEXSOLVE_RESULT_MAPPING, "variables"));
    root->children.push_back(make_node(TEXSOLVE_RESULT_MAPPING, "functions"));
    auto settings = make_node(TEXSOLVE_RESULT_MAPPING, "config");
    append_integer(*settings, "precision_digits", config.precision_digits);
    append_integer(*settings, "max_iterations", config.max_iterations);
    append_integer(*settings, "deadline_ms", config.deadline_ms);
    append_integer(*settings, "max_input_bytes", config.max_input_bytes);
    append_integer(*settings, "max_nesting_depth", config.max_nesting_depth);
    append_integer(*settings, "max_ast_nodes", config.max_ast_nodes);
    settings->children.push_back(make_node(TEXSOLVE_RESULT_SYMBOLIC, "symbolic_backend",
                                           backend_name(config.symbolic_backend, "symengine", "ginac")));
    settings->children.push_back(make_node(TEXSOLVE_RESULT_SYMBOLIC, "linear_algebra_backend",
                                           backend_name(config.linear_algebra_backend, "eigen", "armadillo")));
    settings->children.push_back(make_node(TEXSOLVE_RESULT_SYMBOLIC, "integration_backend",
                                           backend_name(config.integration_backend, "gsl", "boost_math")));
    settings->children.push_back(make_node(TEXSOLVE_RESULT_SYMBOLIC, "optimization_backend",
                                           backend_name(config.optimization_backend, "ceres", "nlopt")));
    root->children.push_back(std::move(settings));
    return root;
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
        *out = make_snapshot(ctx->config).release();
        return TEXSOLVE_STATUS_OK;
    } catch (...) {
        return TEXSOLVE_STATUS_INTERNAL_ERROR;
    }
}

texsolve_status TEXSOLVE_CALL texsolve_context_reset(texsolve_context *ctx) {
    return ctx == nullptr ? TEXSOLVE_STATUS_INVALID_ARGUMENT : TEXSOLVE_STATUS_OK;
}

texsolve_status TEXSOLVE_CALL texsolve_execute(
    texsolve_context *, const texsolve_request *, texsolve_result **out) {
    if (out != nullptr) *out = nullptr;
    return TEXSOLVE_STATUS_UNSUPPORTED;
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
