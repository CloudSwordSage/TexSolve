#ifndef TEXSOLVE_TEXSOLVE_H
#define TEXSOLVE_TEXSOLVE_H

#include <stddef.h>
#include <stdint.h>

#define TEXSOLVE_VERSION_MAJOR 0
#define TEXSOLVE_VERSION_MINOR 1
#define TEXSOLVE_VERSION_PATCH 0
#define TEXSOLVE_ABI_VERSION 1u

#if defined(_WIN32) && !defined(TEXSOLVE_STATIC)
#  if defined(TEXSOLVE_BUILDING_DLL)
#    define TEXSOLVE_API __declspec(dllexport)
#  else
#    define TEXSOLVE_API __declspec(dllimport)
#  endif
#else
#  define TEXSOLVE_API
#endif
#define TEXSOLVE_CALL

#ifdef __cplusplus
extern "C" {
#endif

typedef struct texsolve_context texsolve_context;
typedef struct texsolve_result texsolve_result;

typedef int32_t texsolve_status;
enum {
    TEXSOLVE_STATUS_OK = 0,
    TEXSOLVE_STATUS_INVALID_ARGUMENT = 1,
    TEXSOLVE_STATUS_ABI_MISMATCH = 2,
    TEXSOLVE_STATUS_INVALID_UTF8 = 3,
    TEXSOLVE_STATUS_PARSE_ERROR = 4,
    TEXSOLVE_STATUS_SEMANTIC_ERROR = 5,
    TEXSOLVE_STATUS_OPERATION_MISMATCH = 6,
    TEXSOLVE_STATUS_UNSUPPORTED = 7,
    TEXSOLVE_STATUS_NO_ANALYTIC_SOLUTION = 8,
    TEXSOLVE_STATUS_BACKEND_UNAVAILABLE = 9,
    TEXSOLVE_STATUS_BACKEND_UNSUPPORTED = 10,
    TEXSOLVE_STATUS_NOT_CONVERGED = 11,
    TEXSOLVE_STATUS_RESOURCE_LIMIT = 12,
    TEXSOLVE_STATUS_DEADLINE_EXCEEDED = 13,
    TEXSOLVE_STATUS_INTERNAL_ERROR = 14
};

typedef int32_t texsolve_operation;
enum {
    TEXSOLVE_OPERATION_AUTO = 0,
    TEXSOLVE_OPERATION_EVALUATE = 1,
    TEXSOLVE_OPERATION_SIMPLIFY = 2,
    TEXSOLVE_OPERATION_EXPAND = 3,
    TEXSOLVE_OPERATION_FACTOR = 4,
    TEXSOLVE_OPERATION_DIFFERENTIATE = 5,
    TEXSOLVE_OPERATION_INTEGRATE = 6,
    TEXSOLVE_OPERATION_LIMIT = 7,
    TEXSOLVE_OPERATION_SUM = 8,
    TEXSOLVE_OPERATION_PRODUCT = 9,
    TEXSOLVE_OPERATION_SOLVE = 10,
    TEXSOLVE_OPERATION_LINEAR_ALGEBRA = 11,
    TEXSOLVE_OPERATION_OPTIMIZE = 12,
    TEXSOLVE_OPERATION_ODE_IVP = 13,
    TEXSOLVE_OPERATION_DEFINE = 14
};

typedef int32_t texsolve_result_kind_value;
enum {
    TEXSOLVE_RESULT_NONE = 0,
    TEXSOLVE_RESULT_INTEGER = 1,
    TEXSOLVE_RESULT_RATIONAL = 2,
    TEXSOLVE_RESULT_REAL = 3,
    TEXSOLVE_RESULT_COMPLEX = 4,
    TEXSOLVE_RESULT_SYMBOLIC = 5,
    TEXSOLVE_RESULT_BOOLEAN = 6,
    TEXSOLVE_RESULT_LIST = 7,
    TEXSOLVE_RESULT_MAPPING = 8,
    TEXSOLVE_RESULT_MATRIX = 9,
    TEXSOLVE_RESULT_ROOT_SET = 10,
    TEXSOLVE_RESULT_ROOT = 11,
    TEXSOLVE_RESULT_OPTIMUM = 12,
    TEXSOLVE_RESULT_TRAJECTORY = 13,
    TEXSOLVE_RESULT_SAMPLE = 14,
    TEXSOLVE_RESULT_METADATA = 15
};

typedef int32_t texsolve_severity;
enum {
    TEXSOLVE_SEVERITY_INFO = 0,
    TEXSOLVE_SEVERITY_WARNING = 1,
    TEXSOLVE_SEVERITY_ERROR = 2
};

typedef int32_t texsolve_diagnostic_code;
enum {
    TEXSOLVE_DIAGNOSTIC_NONE = 0,
    TEXSOLVE_DIAGNOSTIC_INVALID_UTF8 = 1,
    TEXSOLVE_DIAGNOSTIC_UNKNOWN_COMMAND = 2,
    TEXSOLVE_DIAGNOSTIC_UNEXPECTED_TOKEN = 3,
    TEXSOLVE_DIAGNOSTIC_TRAILING_INPUT = 4,
    TEXSOLVE_DIAGNOSTIC_INVALID_SPAN = 5,
    TEXSOLVE_DIAGNOSTIC_UNKNOWN_NAME = 6,
    TEXSOLVE_DIAGNOSTIC_DUPLICATE_NAME = 7,
    TEXSOLVE_DIAGNOSTIC_ARITY_MISMATCH = 8,
    TEXSOLVE_DIAGNOSTIC_DOMAIN_ERROR = 9,
    TEXSOLVE_DIAGNOSTIC_DIMENSION_MISMATCH = 10,
    TEXSOLVE_DIAGNOSTIC_SINGULAR_MATRIX = 11,
    TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM = 12,
    TEXSOLVE_DIAGNOSTIC_OPERATION_CONFLICT = 13,
    TEXSOLVE_DIAGNOSTIC_INPUT_LIMIT = 14,
    TEXSOLVE_DIAGNOSTIC_NESTING_LIMIT = 15,
    TEXSOLVE_DIAGNOSTIC_AST_NODE_LIMIT = 16,
    TEXSOLVE_DIAGNOSTIC_ITERATION_LIMIT = 17,
    TEXSOLVE_DIAGNOSTIC_PRECISION_LIMIT = 18,
    TEXSOLVE_DIAGNOSTIC_DEADLINE = 19,
    TEXSOLVE_DIAGNOSTIC_BACKEND_MISSING = 20,
    TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY = 21,
    TEXSOLVE_DIAGNOSTIC_NOT_CONVERGED = 22,
    TEXSOLVE_DIAGNOSTIC_NUMERICAL_FAILURE = 23,
    TEXSOLVE_DIAGNOSTIC_INTERNAL_EXCEPTION = 24
};

typedef int32_t texsolve_symbolic_backend;
enum {
    TEXSOLVE_SYMBOLIC_AUTO = 0,
    TEXSOLVE_SYMBOLIC_SYMENGINE = 1,
    TEXSOLVE_SYMBOLIC_GINAC = 2
};

typedef int32_t texsolve_linear_algebra_backend;
enum {
    TEXSOLVE_LINEAR_ALGEBRA_AUTO = 0,
    TEXSOLVE_LINEAR_ALGEBRA_EIGEN = 1,
    TEXSOLVE_LINEAR_ALGEBRA_ARMADILLO = 2
};

typedef int32_t texsolve_integration_backend;
enum {
    TEXSOLVE_INTEGRATION_AUTO = 0,
    TEXSOLVE_INTEGRATION_GSL = 1,
    TEXSOLVE_INTEGRATION_BOOST_MATH = 2
};

typedef int32_t texsolve_optimization_backend;
enum {
    TEXSOLVE_OPTIMIZATION_AUTO = 0,
    TEXSOLVE_OPTIMIZATION_CERES = 1,
    TEXSOLVE_OPTIMIZATION_NLOPT = 2
};

typedef int32_t texsolve_optimization_kind;
enum {
    TEXSOLVE_OPTIMIZATION_KIND_AUTO = 0,
    TEXSOLVE_OPTIMIZATION_KIND_GENERAL = 1,
    TEXSOLVE_OPTIMIZATION_KIND_LEAST_SQUARES = 2
};

typedef struct texsolve_string_view {
    const char *data;
    size_t size;
} texsolve_string_view;

typedef struct texsolve_binding {
    uint32_t struct_size;
    texsolve_string_view name;
    texsolve_string_view value_latex;
    texsolve_string_view lower_latex;
    texsolve_string_view upper_latex;
} texsolve_binding;

#define TEXSOLVE_BINDING_V1_SIZE ((uint32_t)sizeof(texsolve_binding))

typedef struct texsolve_request {
    uint32_t struct_size;
    uint32_t abi_version;
    int32_t operation;
    uint32_t precision_digits;
    uint32_t max_iterations;
    uint32_t deadline_ms;
    uint32_t max_input_bytes;
    uint32_t max_nesting_depth;
    uint32_t max_ast_nodes;
    int32_t symbolic_backend;
    int32_t linear_algebra_backend;
    int32_t integration_backend;
    int32_t optimization_backend;
    int32_t optimization_kind;
    texsolve_string_view latex;
    const texsolve_binding *bindings;
    size_t binding_count;
    size_t binding_stride;
    const texsolve_string_view *residuals;
    size_t residual_count;
} texsolve_request;

#define TEXSOLVE_REQUEST_V1_SIZE ((uint32_t)sizeof(texsolve_request))

typedef struct texsolve_context_options {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t precision_digits;
    uint32_t max_iterations;
    uint32_t deadline_ms;
    uint32_t max_input_bytes;
    uint32_t max_nesting_depth;
    uint32_t max_ast_nodes;
    int32_t symbolic_backend;
    int32_t linear_algebra_backend;
    int32_t integration_backend;
    int32_t optimization_backend;
} texsolve_context_options;

#define TEXSOLVE_CONTEXT_OPTIONS_V1_SIZE ((uint32_t)sizeof(texsolve_context_options))

typedef struct texsolve_diagnostic {
    uint32_t struct_size;
    int32_t severity;
    int32_t code;
    size_t begin_byte;
    size_t end_byte;
    texsolve_string_view message;
} texsolve_diagnostic;

#define TEXSOLVE_DIAGNOSTIC_V1_MIN_SIZE \
    ((uint32_t)(offsetof(texsolve_diagnostic, message) + sizeof(texsolve_string_view)))

/** Return the stable C ABI version. Returns: ABI version number. */
TEXSOLVE_API uint32_t TEXSOLVE_CALL texsolve_abi_version(void);

/**
 * Create an independent calculation context.
 * Args: out: Receives the allocated context.
 * Returns: TEXSOLVE_STATUS_OK or an error status.
 */
TEXSOLVE_API texsolve_status TEXSOLVE_CALL texsolve_context_create(texsolve_context **out);

/** Destroy a context. Args: ctx: Context or NULL. */
TEXSOLVE_API void TEXSOLVE_CALL texsolve_context_destroy(texsolve_context *ctx);

/**
 * Replace context defaults atomically.
 * Args: ctx: Context. options: ABI-versioned options.
 * Returns: TEXSOLVE_STATUS_OK or validation error.
 */
TEXSOLVE_API texsolve_status TEXSOLVE_CALL texsolve_context_configure(
    texsolve_context *ctx, const texsolve_context_options *options);

/**
 * Copy definitions and configuration into a result tree.
 * Args: ctx: Context. out: Receives owned result.
 * Returns: TEXSOLVE_STATUS_OK or error.
 */
TEXSOLVE_API texsolve_status TEXSOLVE_CALL texsolve_context_snapshot(
    const texsolve_context *ctx, texsolve_result **out);

/** Clear definitions while preserving configuration. Returns: Status. */
TEXSOLVE_API texsolve_status TEXSOLVE_CALL texsolve_context_reset(texsolve_context *ctx);

/** Execute one request. Args: ctx: Context. request: Request. out: Result. Returns: Status. */
TEXSOLVE_API texsolve_status TEXSOLVE_CALL texsolve_execute(
    texsolve_context *ctx, const texsolve_request *request, texsolve_result **out);

/** Destroy a result tree. Args: result: Result or NULL. */
TEXSOLVE_API void TEXSOLVE_CALL texsolve_result_destroy(texsolve_result *result);

/** Return result status. Returns: Status or INVALID_ARGUMENT for NULL. */
TEXSOLVE_API texsolve_status TEXSOLVE_CALL texsolve_result_status(const texsolve_result *result);
/** Return result kind. Returns: Stable kind or NONE for NULL. */
TEXSOLVE_API int32_t TEXSOLVE_CALL texsolve_result_kind(const texsolve_result *result);
/** Return borrowed result name. Returns: View valid for the result lifetime. */
TEXSOLVE_API texsolve_string_view TEXSOLVE_CALL texsolve_result_name(const texsolve_result *result);
/** Return borrowed exact LaTeX. Returns: View valid for the result lifetime. */
TEXSOLVE_API texsolve_string_view TEXSOLVE_CALL texsolve_result_exact_latex(const texsolve_result *result);
/** Return borrowed approximation. Returns: View valid for the result lifetime. */
TEXSOLVE_API texsolve_string_view TEXSOLVE_CALL texsolve_result_approximation(const texsolve_result *result);
/** Return borrowed backend name. Returns: View valid for the result lifetime. */
TEXSOLVE_API texsolve_string_view TEXSOLVE_CALL texsolve_result_backend(const texsolve_result *result);
/** Return direct mathematical child count. Returns: Zero for NULL. */
TEXSOLVE_API size_t TEXSOLVE_CALL texsolve_result_child_count(const texsolve_result *result);
/** Return borrowed child. Args: result: Parent. index: Child index. Returns: Child or NULL. */
TEXSOLVE_API const texsolve_result *TEXSOLVE_CALL texsolve_result_child(
    const texsolve_result *result, size_t index);
/** Return diagnostic count. Returns: Zero for NULL. */
TEXSOLVE_API size_t TEXSOLVE_CALL texsolve_result_diagnostic_count(const texsolve_result *result);
/** Copy one diagnostic. Returns: Status. */
TEXSOLVE_API texsolve_status TEXSOLVE_CALL texsolve_result_diagnostic(
    const texsolve_result *result, size_t index, texsolve_diagnostic *out);
/** Return borrowed metadata. Returns: Metadata node or NULL. */
TEXSOLVE_API const texsolve_result *TEXSOLVE_CALL texsolve_result_metadata(
    const texsolve_result *result);

#ifdef __cplusplus
}
#endif

#endif
