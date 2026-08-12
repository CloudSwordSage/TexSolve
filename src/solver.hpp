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
 * Returns:
 *     Backend-neutral structured result or stable failure information.
 */
SolverOutput solve_problem(const Node &root, int32_t operation,
                           const std::map<std::string, Node> &bindings,
                           uint32_t max_iterations, uint32_t deadline_ms);

}  // namespace texsolve

#endif
