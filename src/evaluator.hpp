#ifndef TEXSOLVE_EVALUATOR_HPP
#define TEXSOLVE_EVALUATOR_HPP

#include "internal.hpp"

#include <cstdint>
#include <map>
#include <string>

namespace texsolve {

struct Evaluation {
    int32_t status = 0;
    int32_t kind = 0;
    int32_t diagnostic_code = 0;
    std::string exact;
    std::string approximation;
    std::string backend;
    std::string message;
    std::string error_estimate;
    std::string name;
    uint32_t precision_digits = 0;
    int32_t real_kind = 0;
    int32_t imag_kind = 0;
    std::string real;
    std::string imag;
};

/**
 * Evaluate a validated AST through the selected symbolic backend.
 *
 * Args:
 *     root: Validated immutable request AST.
 *     operation: Public operation constant.
 *     symbolic_backend: AUTO, SymEngine, or GiNaC.
 *     bindings: Validated binding expressions keyed by symbol name.
 *     precision_digits: Requested decimal precision.
 *     max_iterations: Maximum finite-fold term count.
 *     deadline_ms: Remaining cooperative deadline budget.
 *     integration_backend: AUTO, GSL, or Boost.Math for numeric definite integrals.
 * Returns:
 *     Backend-independent exact/approximate result data or a stable failure.
 */
Evaluation evaluate(const Node &root, int32_t operation, int32_t symbolic_backend,
                    const std::map<std::string, Node> &bindings, uint32_t precision_digits,
                    uint32_t max_iterations, uint32_t deadline_ms, int32_t integration_backend);

/**
 * Render a scalar AST into the common backend expression syntax.
 *
 * Args:
 *     node: Scalar AST to render.
 *     real_bindings: When non-null, unbound ordinary symbols use real-domain
 *         root identities; bound symbols retain the value's actual domain.
 * Returns:
 *     Backend expression syntax.
 */
std::string to_backend_syntax(const Node &node,
                              const std::map<std::string, Node> *real_bindings = nullptr);

}  // namespace texsolve

#endif
