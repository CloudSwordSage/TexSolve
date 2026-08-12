#ifndef TEXSOLVE_INTERNAL_HPP
#define TEXSOLVE_INTERNAL_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace texsolve {

enum class NodeKind {
    Integer,
    Real,
    Symbol,
    Unary,
    Binary,
    Call,
    Definition,
    Relation,
    Matrix,
    Derivative,
    Integral,
    Limit,
    Fold,
    Optimization,
    Ode
};

struct Node {
    NodeKind kind = NodeKind::Symbol;
    std::string text;
    std::size_t begin = 0;
    std::size_t end = 0;
    std::vector<Node> children;
};

struct ParseOutput {
    bool ok = false;
    Node root;
    std::string ast;
    std::string message;
    std::size_t error_begin = 0;
    std::size_t error_end = 0;
    int32_t diagnostic_code = 3;
    uint32_t node_count = 0;
};

/**
 * Parse one TexSolve LaTeX request and render its deterministic debug tree.
 *
 * Args:
 *     input: UTF-8 LaTeX request without document delimiters.
 *     max_depth: Maximum nested grouping depth.
 *     max_nodes: Maximum AST node count.
 * Returns:
 *     ParseOutput containing either an immutable AST and debug text or a byte-span error.
 */
ParseOutput parse_for_debug(std::string_view input, uint32_t max_depth, uint32_t max_nodes);

/** Return whether a byte string is well-formed UTF-8. */
bool is_valid_utf8(std::string_view input);

}  // namespace texsolve

#endif
