#ifndef TEXSOLVE_SOLVER_HPP
#define TEXSOLVE_SOLVER_HPP

#include "internal.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace texsolve {

struct SolverNode {
    int32_t kind = 0;
    std::string name;
    std::string exact;
    std::string approximation;
    std::string backend;
    std::vector<SolverNode> children;
    std::vector<SolverNode> metadata;
};

struct SolverOutput {
    int32_t status = 0;
    int32_t diagnostic_code = 0;
    std::string message;
    SolverNode root;
};

/**
 * Execute linear algebra, equation, optimization, or ODE requests.
 *
 * Args:
 *     root: Validated problem AST.
 *     operation: Explicit resolved public operation.
 *     bindings: Initial values keyed by variable name.
 *     max_iterations: Iteration budget.
 *     deadline_ms: Cooperative deadline budget.
 *     linear_backend: AUTO, Eigen, or Armadillo.
 *     optimization_backend: AUTO, Ceres, or NLopt.
 *     optimization_kind: GENERAL or LEAST_SQUARES.
 *     residuals: Validated least-squares residual expressions.
 *     lower_bounds: Optional lower bound per variable.
 *     upper_bounds: Optional upper bound per variable.
 * Returns:
 *     Backend-neutral structured result or stable failure information.
 */
SolverOutput solve_problem(const Node &root, int32_t operation,
                           const std::map<std::string, Node> &bindings,
                           uint32_t max_iterations, uint32_t deadline_ms,
                           int32_t linear_backend, int32_t optimization_backend,
                           int32_t optimization_kind, const std::vector<Node> &residuals,
                           const std::map<std::string, Node> &lower_bounds,
                           const std::map<std::string, Node> &upper_bounds);

}  // namespace texsolve

#endif
