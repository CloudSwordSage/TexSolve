#include "solver.hpp"

#include "evaluator.hpp"

#include <texsolve/texsolve.h>

#include <Eigen/Dense>
#include <armadillo>
#if defined(_WIN32)
#define j0 _j0
#define j1 _j1
#define jn _jn
#endif
#include <ceres/ceres.h>
#if defined(_WIN32)
#undef j0
#undef j1
#undef jn
#endif
#include <cvode/cvode.h>
#include <nlopt.h>
#include <nvector/nvector_serial.h>
#include <sundials/sundials_context.h>
#include <sunnonlinsol/sunnonlinsol_fixedpoint.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include <symengine/add.h>
#include <symengine/eval_double.h>
#include <symengine/complex.h>
#include <symengine/integer.h>
#include <symengine/matrix.h>
#include <symengine/mul.h>
#include <symengine/ntheory.h>
#include <symengine/number.h>
#include <symengine/parser.h>
#include <symengine/printers.h>
#include <symengine/sets.h>
#include <symengine/simplify.h>
#include <symengine/solve.h>
#include <symengine/subs.h>
#include <symengine/symbol.h>
#pragma GCC diagnostic pop

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace texsolve {
namespace {

using SymEngine::Basic;
using SymEngine::RCP;

std::string number(double value) {
    std::ostringstream stream;
    stream << std::setprecision(15) << value;
    return stream.str();
}

SolverNode scalar(std::string name, double value) {
    SolverNode result;
    result.kind = std::abs(value - std::round(value)) < 1e-12 ? TEXSOLVE_RESULT_INTEGER : TEXSOLVE_RESULT_REAL;
    result.name = std::move(name);
    result.exact = number(value);
    result.approximation = result.exact;
    return result;
}

SolverNode complex_scalar(std::string name, double real, double imag) {
    if (std::abs(imag) < 1e-12) return scalar(std::move(name), real);
    SolverNode result;
    result.kind = TEXSOLVE_RESULT_COMPLEX;
    result.name = std::move(name);
    result.exact = number(real) + (imag < 0 ? "" : "+") + number(imag) + "i";
    result.children.push_back(scalar("real", real));
    result.children.push_back(scalar("imag", imag));
    return result;
}

SolverNode basic_scalar(std::string name, const Basic &value) {
    SolverNode result;
    result.name = std::move(name);
    result.exact = SymEngine::latex(value);
    if (SymEngine::is_a<SymEngine::Integer>(value)) result.kind = TEXSOLVE_RESULT_INTEGER;
    else if (SymEngine::is_a<SymEngine::Rational>(value)) result.kind = TEXSOLVE_RESULT_RATIONAL;
    else if (SymEngine::is_a_Complex(value)) {
        result.kind = TEXSOLVE_RESULT_COMPLEX;
        const auto &complex = SymEngine::down_cast<const SymEngine::ComplexBase &>(value);
        result.children.push_back(basic_scalar("real", *complex.real_part()));
        result.children.push_back(basic_scalar("imag", *complex.imaginary_part()));
    } else result.kind = SymEngine::is_a_Number(value) ? TEXSOLVE_RESULT_REAL : TEXSOLVE_RESULT_SYMBOLIC;
    try {
        result.approximation = number(SymEngine::eval_double(value));
    } catch (...) {
    }
    return result;
}

/** Extract square factors from exact integer radicals throughout an expression. */
RCP<const Basic> normalize_integer_radicals(const RCP<const Basic> &value) {
    if (SymEngine::is_a<SymEngine::Pow>(*value)) {
        const auto &power = SymEngine::down_cast<const SymEngine::Pow &>(*value);
        if (SymEngine::is_a<SymEngine::Integer>(*power.get_base()) &&
            SymEngine::str(*power.get_exp()) == "1/2") {
            const auto &radicand = SymEngine::down_cast<const SymEngine::Integer &>(*power.get_base());
            // ponytail: avoid unbounded factorization; large radicals remain exact but less compact.
            if (radicand.is_positive() && SymEngine::str(radicand).size() <= 12) {
                SymEngine::map_integer_uint factors;
                SymEngine::prime_factor_multiplicities(factors, radicand);
                RCP<const Basic> outside = SymEngine::integer(1);
                RCP<const Basic> inside = SymEngine::integer(1);
                for (const auto &[factor, multiplicity] : factors) {
                    if (multiplicity / 2 != 0) {
                        outside = SymEngine::mul(outside,
                            SymEngine::pow(factor, SymEngine::integer(multiplicity / 2)));
                    }
                    if (multiplicity % 2 != 0) inside = SymEngine::mul(inside, factor);
                }
                return SymEngine::mul(outside, SymEngine::sqrt(inside));
            }
        }
        return value;
    }
    if (SymEngine::is_a<SymEngine::Add>(*value)) {
        SymEngine::vec_basic terms;
        for (const auto &term : value->get_args()) terms.push_back(normalize_integer_radicals(term));
        return SymEngine::add(terms);
    }
    if (SymEngine::is_a<SymEngine::Mul>(*value)) {
        SymEngine::vec_basic factors;
        for (const auto &factor : value->get_args()) factors.push_back(normalize_integer_radicals(factor));
        return SymEngine::mul(factors);
    }
    return value;
}

SolverNode exact_scalar(std::string name, std::string exact) {
    SolverNode result;
    result.kind = TEXSOLVE_RESULT_SYMBOLIC;
    result.name = std::move(name);
    result.exact = std::move(exact);
    return result;
}

/** Normalize SymEngine set notation to compilable LaTeX and integer parameters. */
std::string normalized_set_latex(const Basic &value) {
    std::string result = SymEngine::latex(value);
    const auto replace_all = [&](std::string_view from, std::string_view to) {
        for (std::size_t position = 0; (position = result.find(from, position)) != std::string::npos;) {
            result.replace(position, from.size(), to);
            position += to.size();
        }
    };
    replace_all(R"(n \in \left(-oo, oo\right))", R"(n \in \mathbb{Z})");
    replace_all("-oo", R"(-\infty)");
    replace_all("oo", R"(\infty)");
    return result;
}

SolverNode integer_node(std::string name, long value) {
    SolverNode result;
    result.kind = TEXSOLVE_RESULT_INTEGER;
    result.name = std::move(name);
    result.exact = std::to_string(value);
    return result;
}

SolverNode boolean_node(std::string name, bool value) {
    SolverNode result;
    result.kind = TEXSOLVE_RESULT_BOOLEAN;
    result.name = std::move(name);
    result.exact = value ? "true" : "false";
    return result;
}

SolverOutput failure(int status, int code, std::string message) {
    SolverOutput output;
    output.status = status;
    output.diagnostic_code = code;
    output.message = std::move(message);
    return output;
}

double scalar_value(const Node &node) {
    const auto expression = SymEngine::parse(to_backend_syntax(node));
    return SymEngine::eval_double(*expression);
}

Eigen::MatrixXd matrix_value(const Node &node) {
    if (node.kind == NodeKind::Binary && node.children.size() == 2) {
        if (node.text == "^" && node.children[1].kind == NodeKind::Symbol && node.children[1].text == "T") {
            return matrix_value(node.children[0]).transpose();
        }
        if (node.text == "+" || node.text == "-") {
            const auto left = matrix_value(node.children[0]);
            const auto right = matrix_value(node.children[1]);
            if (left.rows() != right.rows() || left.cols() != right.cols()) {
                throw std::invalid_argument("matrix dimensions do not match");
            }
            return node.text == "+" ? Eigen::MatrixXd(left + right) : Eigen::MatrixXd(left - right);
        }
        if (node.text == "implicit" || node.text == "\\cdot" || node.text == "\\times") {
            if (node.children[0].kind == NodeKind::Matrix || node.children[0].kind == NodeKind::Binary) {
                const auto left = matrix_value(node.children[0]);
                if (node.children[1].kind == NodeKind::Matrix || node.children[1].kind == NodeKind::Binary) {
                    const auto right = matrix_value(node.children[1]);
                    if (left.cols() != right.rows()) throw std::invalid_argument("matrix dimensions do not match");
                    return Eigen::MatrixXd(left * right);
                }
                return Eigen::MatrixXd(left * scalar_value(node.children[1]));
            }
            return Eigen::MatrixXd(scalar_value(node.children[0]) * matrix_value(node.children[1]));
        }
    }
    if (node.kind != NodeKind::Matrix || node.children.empty()) throw std::invalid_argument("matrix required");
    const Eigen::Index rows = static_cast<Eigen::Index>(node.children.size());
    const Eigen::Index cols = static_cast<Eigen::Index>(node.children.front().children.size());
    Eigen::MatrixXd matrix(rows, cols);
    for (Eigen::Index row = 0; row < rows; ++row) {
        if (static_cast<Eigen::Index>(node.children[static_cast<std::size_t>(row)].children.size()) != cols) {
            throw std::invalid_argument("matrix rows have inconsistent dimensions");
        }
        for (Eigen::Index col = 0; col < cols; ++col) {
            matrix(row, col) = scalar_value(node.children[static_cast<std::size_t>(row)].children[static_cast<std::size_t>(col)]);
        }
    }
    return matrix;
}

bool is_matrix_expression(const Node &node) {
    if (node.kind == NodeKind::Matrix) return true;
    if (node.kind != NodeKind::Binary || node.children.size() != 2) return false;
    if (node.text == "^" && node.children[1].kind == NodeKind::Symbol && node.children[1].text == "T") {
        return is_matrix_expression(node.children[0]);
    }
    if (node.text == "+" || node.text == "-") {
        return is_matrix_expression(node.children[0]) && is_matrix_expression(node.children[1]);
    }
    return (node.text == "implicit" || node.text == "\\cdot" || node.text == "\\times") &&
           (is_matrix_expression(node.children[0]) || is_matrix_expression(node.children[1]));
}

SymEngine::DenseMatrix symbolic_matrix(const Node &node) {
    if (node.kind == NodeKind::Matrix && !node.children.empty()) {
        const unsigned rows = static_cast<unsigned>(node.children.size());
        const unsigned cols = static_cast<unsigned>(node.children.front().children.size());
        SymEngine::vec_basic values;
        values.reserve(static_cast<std::size_t>(rows) * cols);
        for (const auto &row : node.children) {
            if (row.children.size() != cols) throw std::invalid_argument("matrix rows have inconsistent dimensions");
            for (const auto &cell : row.children) values.push_back(SymEngine::parse(to_backend_syntax(cell)));
        }
        return SymEngine::DenseMatrix(rows, cols, values);
    }
    if (node.kind != NodeKind::Binary || node.children.size() != 2) throw std::invalid_argument("matrix required");
    if (node.text == "^" && node.children[1].kind == NodeKind::Symbol && node.children[1].text == "T") {
        const auto input = symbolic_matrix(node.children[0]);
        SymEngine::DenseMatrix result(input.ncols(), input.nrows());
        input.transpose(result);
        return result;
    }
    if (node.text == "+" || node.text == "-") {
        const auto left = symbolic_matrix(node.children[0]);
        auto right = symbolic_matrix(node.children[1]);
        SymEngine::DenseMatrix result(left.nrows(), left.ncols());
        if (node.text == "-") {
            SymEngine::DenseMatrix negative(right.nrows(), right.ncols());
            right.mul_scalar(SymEngine::integer(-1), negative);
            left.add_matrix(negative, result);
        } else left.add_matrix(right, result);
        return result;
    }
    if (node.text == "implicit" || node.text == "\\cdot" || node.text == "\\times") {
        const bool left_matrix = is_matrix_expression(node.children[0]);
        const bool right_matrix = is_matrix_expression(node.children[1]);
        if (left_matrix && right_matrix) {
            const auto left = symbolic_matrix(node.children[0]);
            const auto right = symbolic_matrix(node.children[1]);
            SymEngine::DenseMatrix result(left.nrows(), right.ncols());
            left.mul_matrix(right, result);
            return result;
        }
        const Node &matrix_node = left_matrix ? node.children[0] : node.children[1];
        const Node &scalar_node = left_matrix ? node.children[1] : node.children[0];
        const auto matrix = symbolic_matrix(matrix_node);
        SymEngine::DenseMatrix result(matrix.nrows(), matrix.ncols());
        matrix.mul_scalar(SymEngine::parse(to_backend_syntax(scalar_node)), result);
        return result;
    }
    throw std::invalid_argument("unsupported matrix expression");
}

SolverNode matrix_node(const SymEngine::DenseMatrix &matrix) {
    SolverNode result;
    result.kind = TEXSOLVE_RESULT_MATRIX;
    result.backend = "eigen";
    for (unsigned row = 0; row < matrix.nrows(); ++row) {
        SolverNode row_node;
        row_node.kind = TEXSOLVE_RESULT_LIST;
        for (unsigned col = 0; col < matrix.ncols(); ++col) {
            row_node.children.push_back(basic_scalar({}, *matrix.get(row, col)));
        }
        result.children.push_back(std::move(row_node));
    }
    result.metadata.push_back(integer_node("precision_digits", 15));
    return result;
}

SolverNode matrix_node(const Eigen::MatrixXd &matrix) {
    SolverNode result;
    result.kind = TEXSOLVE_RESULT_MATRIX;
    result.backend = "eigen";
    for (Eigen::Index row = 0; row < matrix.rows(); ++row) {
        SolverNode row_node;
        row_node.kind = TEXSOLVE_RESULT_LIST;
        for (Eigen::Index col = 0; col < matrix.cols(); ++col) row_node.children.push_back(scalar({}, matrix(row, col)));
        result.children.push_back(std::move(row_node));
    }
    result.metadata.push_back(integer_node("precision_digits", 15));
    return result;
}

SolverOutput solve_linear(const Node &root) {
    try {
        if (root.kind == NodeKind::Matrix || root.kind == NodeKind::Binary) {
            SolverOutput output;
            output.root = matrix_node(symbolic_matrix(root));
            return output;
        }
        if (root.kind != NodeKind::Call || root.children.size() != 1) {
            return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                           "unsupported linear algebra operation");
        }
        SolverOutput output;
        if (root.text == "det") {
            const auto matrix = symbolic_matrix(root.children.front());
            if (!matrix.is_square()) return failure(TEXSOLVE_STATUS_SEMANTIC_ERROR,
                TEXSOLVE_DIAGNOSTIC_DIMENSION_MISMATCH, "determinant requires a square matrix");
            output.root = basic_scalar({}, *matrix.det());
            output.root.backend = "eigen";
        } else if (root.text == "rank") {
            const auto matrix = symbolic_matrix(root.children.front());
            output.root = integer_node({}, matrix.rank());
            output.root.backend = "eigen";
        } else if (root.text == "inv") {
            const auto matrix = symbolic_matrix(root.children.front());
            if (!matrix.is_square() || SymEngine::is_number_and_zero(*matrix.det())) {
                return failure(TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_SINGULAR_MATRIX,
                               "matrix inverse requires a nonsingular square matrix");
            }
            SymEngine::DenseMatrix inverse(matrix.nrows(), matrix.ncols());
            matrix.inv(inverse);
            output.root = matrix_node(inverse);
        } else if (root.text == "eigenvalues") {
            const Eigen::MatrixXd matrix = matrix_value(root.children.front());
            if (matrix.rows() != matrix.cols()) return failure(TEXSOLVE_STATUS_SEMANTIC_ERROR,
                TEXSOLVE_DIAGNOSTIC_DIMENSION_MISMATCH, "eigenvalues require a square matrix");
            Eigen::EigenSolver<Eigen::MatrixXd> eigen(matrix, false);
            output.root.kind = TEXSOLVE_RESULT_LIST;
            output.root.backend = "eigen";
            for (Eigen::Index index = 0; index < eigen.eigenvalues().size(); ++index) {
                const auto value = eigen.eigenvalues()[index];
                output.root.children.push_back(complex_scalar({}, value.real(), value.imag()));
            }
        } else if (root.text == "eigenvectors") {
            const Eigen::MatrixXd matrix = matrix_value(root.children.front());
            if (matrix.rows() != matrix.cols()) return failure(TEXSOLVE_STATUS_SEMANTIC_ERROR,
                TEXSOLVE_DIAGNOSTIC_DIMENSION_MISMATCH, "eigenvectors require a square matrix");
            Eigen::EigenSolver<Eigen::MatrixXd> eigen(matrix, true);
            const auto vectors = eigen.eigenvectors();
            SolverNode complex_matrix;
            complex_matrix.kind = TEXSOLVE_RESULT_MATRIX;
            complex_matrix.backend = "eigen";
            for (Eigen::Index row = 0; row < vectors.rows(); ++row) {
                SolverNode row_node;
                row_node.kind = TEXSOLVE_RESULT_LIST;
                for (Eigen::Index col = 0; col < vectors.cols(); ++col) {
                    row_node.children.push_back(complex_scalar({}, vectors(row, col).real(), vectors(row, col).imag()));
                }
                complex_matrix.children.push_back(std::move(row_node));
            }
            output.root = std::move(complex_matrix);
        } else {
            return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                           "unsupported linear algebra operation");
        }
        if (output.root.metadata.empty()) output.root.metadata.push_back(integer_node("precision_digits", 15));
        return output;
    } catch (const std::exception &error) {
        return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY, error.what());
    }
}

SolverOutput solve_linear_armadillo(const Node &root) {
    try {
        const Node *input = &root;
        if (root.kind == NodeKind::Call && root.children.size() == 1) input = &root.children.front();
        const auto eigen = matrix_value(*input);
        arma::mat matrix(eigen.data(), static_cast<arma::uword>(eigen.rows()),
                         static_cast<arma::uword>(eigen.cols()));
        SolverOutput output;
        if (root.kind == NodeKind::Matrix || root.kind == NodeKind::Binary) {
            output.root = matrix_node(eigen);
        } else if (root.text == "det") {
            output.root = scalar({}, arma::det(matrix));
        } else if (root.text == "rank") {
            output.root = integer_node({}, static_cast<long>(arma::rank(matrix)));
        } else if (root.text == "inv") {
            arma::mat inverse;
            if (!arma::inv(inverse, matrix)) return failure(TEXSOLVE_STATUS_SEMANTIC_ERROR,
                TEXSOLVE_DIAGNOSTIC_SINGULAR_MATRIX, "matrix inverse requires a nonsingular square matrix");
            Eigen::MatrixXd converted(static_cast<Eigen::Index>(inverse.n_rows), static_cast<Eigen::Index>(inverse.n_cols));
            for (arma::uword row = 0; row < inverse.n_rows; ++row) {
                for (arma::uword col = 0; col < inverse.n_cols; ++col) converted(row, col) = inverse(row, col);
            }
            output.root = matrix_node(converted);
        } else if (root.text == "eigenvalues" || root.text == "eigenvectors") {
            arma::cx_vec values;
            arma::cx_mat vectors;
            if (!arma::eig_gen(values, vectors, matrix)) return failure(TEXSOLVE_STATUS_NOT_CONVERGED,
                TEXSOLVE_DIAGNOSTIC_NUMERICAL_FAILURE, "Armadillo eigen solve failed");
            if (root.text == "eigenvalues") {
                output.root.kind = TEXSOLVE_RESULT_LIST;
                for (const auto &value : values) {
                    output.root.children.push_back(complex_scalar({}, value.real(), value.imag()));
                }
            } else {
                output.root.kind = TEXSOLVE_RESULT_MATRIX;
                for (arma::uword row = 0; row < vectors.n_rows; ++row) {
                    SolverNode row_node;
                    row_node.kind = TEXSOLVE_RESULT_LIST;
                    for (arma::uword col = 0; col < vectors.n_cols; ++col) {
                        row_node.children.push_back(complex_scalar({}, vectors(row, col).real(), vectors(row, col).imag()));
                    }
                    output.root.children.push_back(std::move(row_node));
                }
            }
        } else {
            return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                           "unsupported Armadillo linear algebra operation");
        }
        output.root.backend = "armadillo";
        if (output.root.metadata.empty()) output.root.metadata.push_back(integer_node("precision_digits", 15));
        return output;
    } catch (const std::exception &error) {
        return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY, error.what());
    }
}

/** Collect free scalar symbols while respecting finite-fold index scope. */
void collect_symbols(const Node &node, std::vector<std::string> &symbols,
                     const std::vector<std::string> &bound = {}) {
    if (node.kind == NodeKind::Symbol && node.text != "\\pi" && node.text != "e" && node.text != "i" &&
        std::find(bound.begin(), bound.end(), node.text) == bound.end() &&
        std::find(symbols.begin(), symbols.end(), node.text) == symbols.end()) {
        symbols.push_back(node.text);
    }
    if (node.kind == NodeKind::Fold && node.children.size() == 3) {
        collect_symbols(node.children[0], symbols, bound);
        collect_symbols(node.children[1], symbols, bound);
        auto fold_bound = bound;
        fold_bound.push_back(node.text.substr(node.text.find(':') + 1));
        collect_symbols(node.children[2], symbols, fold_bound);
        return;
    }
    for (const auto &child : node.children) collect_symbols(child, symbols, bound);
}

/** Return whether an equation side is the literal zero. */
bool is_zero(const Node &node) {
    return (node.kind == NodeKind::Integer || node.kind == NodeKind::Real) &&
           SymEngine::is_number_and_zero(*SymEngine::parse(to_backend_syntax(node)));
}

/** Return whether a node is sin(variable). */
bool is_sine_of(const Node &node, std::string_view variable) {
    return node.kind == NodeKind::Call && node.text == "sin" && node.children.size() == 1 &&
           node.children[0].kind == NodeKind::Symbol && node.children[0].text == variable;
}

SolverOutput solve_equation(const Node &root, const std::map<std::string, Node> &bindings,
                            uint32_t max_iterations, uint32_t deadline_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(deadline_ms);
    if (root.kind == NodeKind::Relation && root.text == "cases") {
        std::vector<std::string> symbols;
        for (const auto &equation : root.children) collect_symbols(equation, symbols);
        std::sort(symbols.begin(), symbols.end());
        if (symbols.empty() || root.children.size() != symbols.size()) return failure(
            TEXSOLVE_STATUS_INVALID_ARGUMENT, TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM,
            "linear system must have one equation per unknown");
        Eigen::MatrixXd coefficients(root.children.size(), symbols.size());
        Eigen::VectorXd constants(root.children.size());
        try {
            SymEngine::map_basic_basic zeros;
            for (const auto &symbol : symbols) zeros[SymEngine::symbol(symbol)] = SymEngine::integer(0);
            for (std::size_t row = 0; row < root.children.size(); ++row) {
                const auto &equation = root.children[row];
                auto expression = SymEngine::sub(SymEngine::parse(to_backend_syntax(equation.children[0])),
                                                  SymEngine::parse(to_backend_syntax(equation.children[1])));
                constants(static_cast<Eigen::Index>(row)) = -SymEngine::eval_double(*expression->subs(zeros));
                for (std::size_t col = 0; col < symbols.size(); ++col) {
                    coefficients(static_cast<Eigen::Index>(row), static_cast<Eigen::Index>(col)) =
                        SymEngine::eval_double(*expression->diff(SymEngine::symbol(symbols[col]))->subs(zeros));
                }
            }
            const auto decomposition = coefficients.fullPivLu();
            if (decomposition.rank() != coefficients.cols()) return failure(
                TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_SINGULAR_MATRIX,
                "linear system is singular or underdetermined");
            const Eigen::VectorXd solution = decomposition.solve(constants);
            SolverOutput output;
            output.root.kind = TEXSOLVE_RESULT_MAPPING;
            output.root.backend = "eigen";
            for (std::size_t index = 0; index < symbols.size(); ++index) {
                output.root.children.push_back(scalar(symbols[index], solution(static_cast<Eigen::Index>(index))));
            }
            output.root.metadata.push_back(integer_node("precision_digits", 15));
            return output;
        } catch (const std::exception &error) {
            return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY, error.what());
        }
    }
    if (root.kind != NodeKind::Relation || root.text != "=" || root.children.size() != 2) {
        return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                       "solve requires an equation");
    }
    std::vector<std::string> symbols;
    collect_symbols(root, symbols);
    if (symbols.size() != 1) return failure(TEXSOLVE_STATUS_INVALID_ARGUMENT,
        TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM, "one-variable polynomial solve requires exactly one unknown");
    try {
        const auto variable = SymEngine::symbol(symbols.front());
        const Node *fold = root.children[0].kind == NodeKind::Fold ? &root.children[0]
                         : root.children[1].kind == NodeKind::Fold ? &root.children[1] : nullptr;
        const Node *target = fold == &root.children[0] ? &root.children[1]
                           : fold == &root.children[1] ? &root.children[0] : nullptr;
        if (fold != nullptr && target != nullptr && fold->text.starts_with("product:") &&
            fold->children.size() == 3 && fold->children[0].kind == NodeKind::Integer &&
            fold->children[0].text == "1" && fold->children[1].kind == NodeKind::Symbol &&
            fold->children[1].text == symbols.front() && fold->children[2].kind == NodeKind::Symbol &&
            fold->children[2].text == fold->text.substr(fold->text.find(':') + 1)) {
            const auto product_result = [&](std::optional<uint32_t> value) {
                SolverOutput output;
                output.root.kind = TEXSOLVE_RESULT_ROOT_SET;
                output.root.backend = "symengine";
                if (value) {
                    SolverNode root_node;
                    root_node.kind = TEXSOLVE_RESULT_ROOT;
                    root_node.children.push_back(integer_node("value", *value));
                    root_node.children.push_back(integer_node("multiplicity", 1));
                    root_node.children.push_back(exact_scalar("search_kind", "analytic"));
                    output.root.children.push_back(std::move(root_node));
                }
                output.root.metadata.push_back(integer_node("precision_digits", 15));
                output.root.metadata.push_back(exact_scalar(
                    "domain", symbols.front() + " \\in \\mathbb{Z}_{>0}"));
                return output;
            };
            const auto requested = SymEngine::parse(to_backend_syntax(*target));
            if (!SymEngine::is_a<SymEngine::Integer>(*requested) ||
                !SymEngine::down_cast<const SymEngine::Integer &>(*requested).is_positive()) {
                return product_result(std::nullopt);
            }
            const auto &requested_integer = SymEngine::down_cast<const SymEngine::Integer &>(*requested);
            RCP<const Basic> product = SymEngine::integer(1);
            for (uint32_t value = 1; value <= max_iterations; ++value) {
                if (std::chrono::steady_clock::now() >= deadline) return failure(
                    TEXSOLVE_STATUS_DEADLINE_EXCEEDED, TEXSOLVE_DIAGNOSTIC_DEADLINE,
                    "equation solve deadline exceeded");
                product = SymEngine::mul(product, SymEngine::integer(value));
                const auto &product_integer = SymEngine::down_cast<const SymEngine::Integer &>(*product);
                if (SymEngine::eq(product_integer, requested_integer)) return product_result(value);
                if (product_integer.as_integer_class() > requested_integer.as_integer_class()) {
                    return product_result(std::nullopt);
                }
            }
            return failure(TEXSOLVE_STATUS_RESOURCE_LIMIT, TEXSOLVE_DIAGNOSTIC_ITERATION_LIMIT,
                           "finite product equation iteration limit exceeded");
        }
        if (!bindings.contains(symbols.front()) &&
            ((is_sine_of(root.children[0], symbols.front()) && is_zero(root.children[1])) ||
             (is_sine_of(root.children[1], symbols.front()) && is_zero(root.children[0])))) {
            const std::string parameter = symbols.front() == "n" ? "k" : "n";
            SolverOutput output;
            output.root.kind = TEXSOLVE_RESULT_ROOT_SET;
            output.root.backend = "symengine";
            SolverNode root_node;
            root_node.kind = TEXSOLVE_RESULT_ROOT;
            root_node.children.push_back(exact_scalar("value", parameter + " \\pi"));
            root_node.children.push_back(integer_node("multiplicity", 1));
            root_node.children.push_back(exact_scalar("search_kind", "analytic"));
            output.root.children.push_back(std::move(root_node));
            output.root.metadata.push_back(integer_node("precision_digits", 15));
            output.root.metadata.push_back(exact_scalar(
                "domain", parameter + " \\in \\mathbb{Z}"));
            return output;
        }
        const auto expression = SymEngine::sub(SymEngine::parse(to_backend_syntax(root.children[0])),
                                               SymEngine::parse(to_backend_syntax(root.children[1])));
        RCP<const SymEngine::Set> solutions;
        try {
            if (std::chrono::steady_clock::now() >= deadline) return failure(
                TEXSOLVE_STATUS_DEADLINE_EXCEEDED, TEXSOLVE_DIAGNOSTIC_DEADLINE,
                "equation solve deadline exceeded");
            if (bindings.contains(symbols.front())) solutions = SymEngine::emptyset();
            else solutions = SymEngine::solve(expression, variable);
            if (std::chrono::steady_clock::now() >= deadline) return failure(
                TEXSOLVE_STATUS_DEADLINE_EXCEEDED, TEXSOLVE_DIAGNOSTIC_DEADLINE,
                "equation solve deadline exceeded");
        } catch (...) {
            solutions = SymEngine::emptyset();
        }
        if (std::chrono::steady_clock::now() >= deadline) return failure(
            TEXSOLVE_STATUS_DEADLINE_EXCEEDED, TEXSOLVE_DIAGNOSTIC_DEADLINE,
            "equation solve deadline exceeded");
        std::vector<RCP<const Basic>> values;
        if (SymEngine::is_a<SymEngine::FiniteSet>(*solutions)) values = solutions->get_args();
        const auto root_degree = [](const Node &node) -> std::optional<unsigned long> {
            if (node.kind != NodeKind::Call || !node.text.starts_with("sqrt:") ||
                node.children.size() != 1) return std::nullopt;
            try {
                return std::stoul(node.text.substr(5));
            } catch (...) {
                return std::nullopt;
            }
        };
        const auto left_degree = root_degree(root.children[0]);
        const auto right_degree = root_degree(root.children[1]);
        bool radical_solution_set_is_finite = false;
        if (values.empty() && (left_degree || right_degree)) {
            try {
                RCP<const Basic> transformed;
                if (left_degree && right_degree && left_degree == right_degree) {
                    transformed = SymEngine::sub(
                        SymEngine::parse(to_backend_syntax(root.children[0].children[0])),
                        SymEngine::parse(to_backend_syntax(root.children[1].children[0])));
                } else if (left_degree) {
                    transformed = SymEngine::sub(
                        SymEngine::parse(to_backend_syntax(root.children[0].children[0])),
                        SymEngine::pow(SymEngine::parse(to_backend_syntax(root.children[1])),
                                       SymEngine::integer(*left_degree)));
                } else {
                    transformed = SymEngine::sub(
                        SymEngine::pow(SymEngine::parse(to_backend_syntax(root.children[0])),
                                       SymEngine::integer(*right_degree)),
                        SymEngine::parse(to_backend_syntax(root.children[1].children[0])));
                }
                if (std::chrono::steady_clock::now() >= deadline) return failure(
                    TEXSOLVE_STATUS_DEADLINE_EXCEEDED, TEXSOLVE_DIAGNOSTIC_DEADLINE,
                    "equation solve deadline exceeded");
                const auto radical_solutions = SymEngine::solve(transformed, variable);
                if (std::chrono::steady_clock::now() >= deadline) return failure(
                    TEXSOLVE_STATUS_DEADLINE_EXCEEDED, TEXSOLVE_DIAGNOSTIC_DEADLINE,
                    "equation solve deadline exceeded");
                if (SymEngine::is_a<SymEngine::EmptySet>(*radical_solutions)) {
                    radical_solution_set_is_finite = true;
                } else if (SymEngine::is_a<SymEngine::FiniteSet>(*radical_solutions)) {
                    radical_solution_set_is_finite = true;
                    for (const auto &candidate : radical_solutions->get_args()) {
                        const auto left = expression->subs({{variable, candidate}});
                        if (SymEngine::is_number_and_zero(*left)) values.push_back(candidate);
                    }
                }
            } catch (...) {
            }
        }
        if (values.empty() && (SymEngine::is_a<SymEngine::ImageSet>(*solutions) ||
                               SymEngine::is_a<SymEngine::Union>(*solutions))) {
            SolverOutput output;
            output.root.kind = TEXSOLVE_RESULT_ROOT_SET;
            output.root.backend = "symengine";
            SolverNode root_node;
            root_node.kind = TEXSOLVE_RESULT_ROOT;
            auto family = basic_scalar("value", *solutions);
            family.exact = normalized_set_latex(*solutions);
            root_node.children.push_back(std::move(family));
            root_node.children.push_back(integer_node("multiplicity", 1));
            root_node.children.push_back(exact_scalar("search_kind", "analytic"));
            output.root.children.push_back(std::move(root_node));
            output.root.metadata.push_back(integer_node("precision_digits", 15));
            output.root.metadata.push_back(exact_scalar("domain", R"(n \in \mathbb{Z})"));
            return output;
        }
        if (values.empty() && radical_solution_set_is_finite) {
            SolverOutput output;
            output.root.kind = TEXSOLVE_RESULT_ROOT_SET;
            output.root.backend = "symengine";
            output.root.metadata.push_back(integer_node("precision_digits", 15));
            return output;
        }
        if (values.empty()) {
            const auto initial = bindings.find(symbols.front());
            if (initial == bindings.end()) return failure(TEXSOLVE_STATUS_INVALID_ARGUMENT,
                TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM, "numeric solve requires an initial value or interval");
            double value = scalar_value(initial->second);
            const auto derivative = expression->diff(variable);
            bool converged = false;
            for (uint32_t iteration = 0; iteration < std::min<uint32_t>(max_iterations, 1000); ++iteration) {
                if (std::chrono::steady_clock::now() >= deadline) return failure(
                    TEXSOLVE_STATUS_DEADLINE_EXCEEDED, TEXSOLVE_DIAGNOSTIC_DEADLINE,
                    "equation solve deadline exceeded");
                const auto substitution = SymEngine::map_basic_basic{{variable, SymEngine::real_double(value)}};
                const double residual = SymEngine::eval_double(*expression->subs(substitution));
                const double slope = SymEngine::eval_double(*derivative->subs(substitution));
                if (std::abs(residual) < 1e-12) { converged = true; break; }
                if (std::abs(slope) < 1e-14) break;
                value -= residual / slope;
            }
            if (!converged) return failure(TEXSOLVE_STATUS_NOT_CONVERGED,
                TEXSOLVE_DIAGNOSTIC_NOT_CONVERGED, "numeric root search did not converge");
            values.push_back(SymEngine::real_double(value));
        }
        struct OrderedRoot {
            RCP<const Basic> value;
            std::complex<double> approximate;
        };
        for (auto &value : values) value = normalize_integer_radicals(SymEngine::simplify(value));
        std::vector<OrderedRoot> ordered;
        ordered.reserve(values.size());
        for (const auto &value : values) ordered.push_back({value, SymEngine::eval_complex_double(*value)});
        std::sort(ordered.begin(), ordered.end(), [](const auto &left, const auto &right) {
            const bool left_real = std::abs(left.approximate.imag()) < 1e-12;
            const bool right_real = std::abs(right.approximate.imag()) < 1e-12;
            if (left_real != right_real) return left_real;
            if (left.approximate.real() != right.approximate.real()) {
                return left.approximate.real() < right.approximate.real();
            }
            if (left.approximate.imag() != right.approximate.imag()) {
                return left.approximate.imag() < right.approximate.imag();
            }
            return SymEngine::str(*left.value) < SymEngine::str(*right.value);
        });
        SolverOutput output;
        output.root.kind = TEXSOLVE_RESULT_ROOT_SET;
        output.root.backend = "symengine";
        for (const auto &ordered_root : ordered) {
            const auto &value = ordered_root.value;
            SolverNode root_node;
            root_node.kind = TEXSOLVE_RESULT_ROOT;
            const bool local = SymEngine::is_a<SymEngine::RealDouble>(*value);
            long multiplicity = 1;
            if (!local) {
                multiplicity = 0;
                auto derivative = expression;
                while (!SymEngine::is_number_and_zero(*derivative)) {
                    if (std::chrono::steady_clock::now() >= deadline) return failure(
                        TEXSOLVE_STATUS_DEADLINE_EXCEEDED, TEXSOLVE_DIAGNOSTIC_DEADLINE,
                        "root multiplicity deadline exceeded");
                    const auto at_root = derivative->subs({{variable, value}});
                    if (!SymEngine::is_number_and_zero(*at_root)) break;
                    ++multiplicity;
                    derivative = derivative->diff(variable);
                }
                multiplicity = std::max<long>(1, multiplicity);
            }
            auto value_node = basic_scalar("value", *value);
            root_node.children.push_back(std::move(value_node));
            root_node.children.push_back(integer_node("multiplicity", multiplicity));
            root_node.children.push_back(exact_scalar("search_kind", local ? "local" : "analytic"));
            output.root.children.push_back(std::move(root_node));
        }
        output.root.metadata.push_back(integer_node("precision_digits", 15));
        return output;
    } catch (const std::exception &error) {
        return failure(TEXSOLVE_STATUS_NO_ANALYTIC_SOLUTION, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY, error.what());
    }
}

struct ObjectiveData {
    RCP<const Basic> expression;
    std::vector<RCP<const SymEngine::Symbol>> symbols;
    long evaluations = 0;
    nlopt_opt optimizer = nullptr;
    bool failed = false;
};

double objective_callback(unsigned n, const double *values, double *, void *raw) noexcept {
    auto &data = *static_cast<ObjectiveData *>(raw);
    try {
        ++data.evaluations;
        SymEngine::map_basic_basic substitutions;
        for (unsigned index = 0; index < n; ++index) {
            substitutions[data.symbols[index]] = SymEngine::real_double(values[index]);
        }
        return SymEngine::eval_double(*data.expression->subs(substitutions));
    } catch (...) {
        data.failed = true;
        if (data.optimizer != nullptr) nlopt_force_stop(data.optimizer);
        return HUGE_VAL;
    }
}

SolverOutput solve_optimization(const Node &root, const std::map<std::string, Node> &bindings,
                                const std::map<std::string, Node> &lower_bounds,
                                const std::map<std::string, Node> &upper_bounds,
                                uint32_t max_iterations, uint32_t deadline_ms) {
    if (root.kind != NodeKind::Optimization || root.children.size() < 2) return failure(
        TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM, "optimization problem is incomplete");
    const auto objective_position = std::find_if(root.children.begin(), root.children.end(), [](const Node &node) {
        return node.kind != NodeKind::Symbol;
    });
    const std::size_t variable_count = static_cast<std::size_t>(objective_position - root.children.begin());
    if (variable_count == 0 || objective_position == root.children.end()) return failure(
        TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM, "optimization objective is missing");
    ObjectiveData data;
    std::vector<double> values(variable_count);
    for (std::size_t index = 0; index < variable_count; ++index) {
        const auto &name = root.children[index].text;
        const auto binding = bindings.find(name);
        if (binding == bindings.end()) return failure(TEXSOLVE_STATUS_INVALID_ARGUMENT,
            TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM, "optimization requires an initial value for every variable");
        data.symbols.push_back(SymEngine::symbol(name));
        values[index] = scalar_value(binding->second);
    }
    data.expression = SymEngine::parse(to_backend_syntax(*objective_position));
    nlopt_opt optimizer = nlopt_create(NLOPT_LN_COBYLA, static_cast<unsigned>(variable_count));
    if (optimizer == nullptr) return failure(TEXSOLVE_STATUS_INTERNAL_ERROR, TEXSOLVE_DIAGNOSTIC_INTERNAL_EXCEPTION,
                                             "failed to create NLopt optimizer");
    data.optimizer = optimizer;
    if (root.text == "max") nlopt_set_max_objective(optimizer, objective_callback, &data);
    else nlopt_set_min_objective(optimizer, objective_callback, &data);
    std::vector<double> lower(variable_count, -HUGE_VAL);
    std::vector<double> upper(variable_count, HUGE_VAL);
    for (std::size_t index = 0; index < variable_count; ++index) {
        const auto &name = root.children[index].text;
        if (const auto found = lower_bounds.find(name); found != lower_bounds.end()) lower[index] = scalar_value(found->second);
        if (const auto found = upper_bounds.find(name); found != upper_bounds.end()) upper[index] = scalar_value(found->second);
        if (lower[index] > upper[index]) {
            nlopt_destroy(optimizer);
            return failure(TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_DOMAIN_ERROR,
                           "optimization lower bound exceeds upper bound");
        }
        values[index] = std::clamp(values[index], lower[index], upper[index]);
    }
    nlopt_set_lower_bounds(optimizer, lower.data());
    nlopt_set_upper_bounds(optimizer, upper.data());
    std::vector<ObjectiveData> constraints;
    constraints.reserve(static_cast<std::size_t>(root.children.end() - objective_position - 1));
    for (auto constraint = objective_position + 1; constraint != root.children.end(); ++constraint) {
        ObjectiveData constraint_data;
        constraint_data.symbols = data.symbols;
        constraint_data.optimizer = optimizer;
        auto left = SymEngine::parse(to_backend_syntax(constraint->children[0]));
        auto right = SymEngine::parse(to_backend_syntax(constraint->children[1]));
        constraint_data.expression = constraint->text == "\\ge" || constraint->text == ">"
                                         ? SymEngine::sub(right, left) : SymEngine::sub(left, right);
        constraints.push_back(std::move(constraint_data));
        if (constraint->text == "=") nlopt_add_equality_constraint(
            optimizer, objective_callback, &constraints.back(), 1e-10);
        else nlopt_add_inequality_constraint(optimizer, objective_callback, &constraints.back(), 1e-10);
    }
    nlopt_set_maxeval(optimizer, static_cast<int>(std::min<uint32_t>(max_iterations, 1000000)));
    nlopt_set_maxtime(optimizer, static_cast<double>(deadline_ms) / 1000.0);
    nlopt_set_xtol_rel(optimizer, 1e-10);
    double optimum_value = 0.0;
    const nlopt_result status = nlopt_optimize(optimizer, values.data(), &optimum_value);
    const bool callback_failed = data.failed || std::any_of(constraints.begin(), constraints.end(), [](const auto &item) {
        return item.failed;
    });
    nlopt_destroy(optimizer);
    if (callback_failed) return failure(TEXSOLVE_STATUS_NOT_CONVERGED,
        TEXSOLVE_DIAGNOSTIC_NUMERICAL_FAILURE, "NLopt callback evaluation failed");
    if (status == NLOPT_MAXTIME_REACHED) return failure(TEXSOLVE_STATUS_DEADLINE_EXCEEDED,
        TEXSOLVE_DIAGNOSTIC_DEADLINE, "NLopt deadline exceeded");
    if (status == NLOPT_MAXEVAL_REACHED) return failure(TEXSOLVE_STATUS_RESOURCE_LIMIT,
        TEXSOLVE_DIAGNOSTIC_ITERATION_LIMIT, "NLopt iteration limit exceeded");
    if (status < 0) return failure(TEXSOLVE_STATUS_NOT_CONVERGED, TEXSOLVE_DIAGNOSTIC_NOT_CONVERGED,
                                   "NLopt did not converge");
    SolverOutput output;
    output.root.kind = TEXSOLVE_RESULT_OPTIMUM;
    output.root.backend = "nlopt";
    SolverNode variables;
    variables.kind = TEXSOLVE_RESULT_MAPPING;
    variables.name = "variables";
    for (std::size_t index = 0; index < variable_count; ++index) variables.children.push_back(scalar(root.children[index].text, values[index]));
    output.root.children.push_back(std::move(variables));
    output.root.children.push_back(scalar("objective", optimum_value));
    output.root.metadata.push_back(integer_node("iterations", data.evaluations));
    output.root.metadata.push_back(boolean_node("converged", true));
    output.root.metadata.push_back(exact_scalar("termination_reason", "converged"));
    output.root.metadata.push_back(integer_node("precision_digits", 15));
    return output;
}

struct CeresResidualData final : ceres::FirstOrderFunction {
    std::vector<RCP<const Basic>> residuals;
    std::vector<RCP<const SymEngine::Symbol>> symbols;

    double cost_at(const double *parameters) const {
        SymEngine::map_basic_basic substitutions;
        for (std::size_t index = 0; index < symbols.size(); ++index) {
            substitutions[symbols[index]] = SymEngine::real_double(parameters[index]);
        }
        double cost = 0.0;
        for (std::size_t index = 0; index < residuals.size(); ++index) {
            const double value = SymEngine::eval_double(*residuals[index]->subs(substitutions));
            cost += value * value;
        }
        return cost;
    }

    bool Evaluate(const double *parameters, double *cost, double *gradient) const override {
        *cost = cost_at(parameters);
        if (gradient == nullptr) return true;
        std::vector<double> perturbed(parameters, parameters + symbols.size());
        for (std::size_t index = 0; index < symbols.size(); ++index) {
            const double step = 1e-6 * std::max(1.0, std::abs(parameters[index]));
            perturbed[index] = parameters[index] + step;
            const double upper = cost_at(perturbed.data());
            perturbed[index] = parameters[index] - step;
            const double lower = cost_at(perturbed.data());
            perturbed[index] = parameters[index];
            gradient[index] = (upper - lower) / (2.0 * step);
        }
        return true;
    }

    int NumParameters() const override { return static_cast<int>(symbols.size()); }
};

SolverOutput solve_least_squares(const Node &root, const std::map<std::string, Node> &bindings,
                                 const std::vector<Node> &residuals, uint32_t max_iterations,
                                 uint32_t deadline_ms) {
    const auto objective_position = std::find_if(root.children.begin(), root.children.end(), [](const Node &node) {
        return node.kind != NodeKind::Symbol;
    });
    const std::size_t variable_count = static_cast<std::size_t>(objective_position - root.children.begin());
    if (variable_count == 0 || residuals.empty()) return failure(TEXSOLVE_STATUS_INVALID_ARGUMENT,
        TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM, "least squares requires variables and residuals");
    std::vector<double> values(variable_count);
    auto *data = new CeresResidualData;
    for (std::size_t index = 0; index < variable_count; ++index) {
        const auto &name = root.children[index].text;
        const auto initial = bindings.find(name);
        if (initial == bindings.end()) {
            delete data;
            return failure(TEXSOLVE_STATUS_INVALID_ARGUMENT, TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM,
                           "least squares requires an initial value for every variable");
        }
        data->symbols.push_back(SymEngine::symbol(name));
        values[index] = scalar_value(initial->second);
    }
    for (const auto &residual : residuals) data->residuals.push_back(SymEngine::parse(to_backend_syntax(residual)));
    ceres::GradientProblem problem(data);
    ceres::GradientProblemSolver::Options options;
    options.max_num_iterations = static_cast<int>(std::min<uint32_t>(max_iterations, 1000000));
    options.max_solver_time_in_seconds = static_cast<double>(deadline_ms) / 1000.0;
    options.logging_type = ceres::SILENT;
    ceres::GradientProblemSolver::Summary summary;
    ceres::Solve(options, problem, values.data(), &summary);
    if (!summary.IsSolutionUsable() || summary.termination_type != ceres::CONVERGENCE) {
        return failure(TEXSOLVE_STATUS_NOT_CONVERGED,
        TEXSOLVE_DIAGNOSTIC_NOT_CONVERGED, "Ceres least-squares solve did not converge");
    }
    SolverOutput output;
    output.root.kind = TEXSOLVE_RESULT_OPTIMUM;
    output.root.backend = "ceres";
    SolverNode variables;
    variables.kind = TEXSOLVE_RESULT_MAPPING;
    variables.name = "variables";
    for (std::size_t index = 0; index < variable_count; ++index) {
        variables.children.push_back(scalar(root.children[index].text, values[index]));
    }
    output.root.children.push_back(std::move(variables));
    output.root.children.push_back(scalar("objective", summary.final_cost));
    output.root.metadata.push_back(integer_node("iterations", static_cast<long>(summary.iterations.size())));
    output.root.metadata.push_back(boolean_node("converged", true));
    output.root.metadata.push_back(exact_scalar("termination_reason", "converged"));
    output.root.metadata.push_back(integer_node("precision_digits", 15));
    return output;
}

struct OdeData {
    std::vector<RCP<const Basic>> expressions;
    std::vector<RCP<const SymEngine::Symbol>> states;
    RCP<const SymEngine::Symbol> time;
    bool failed = false;
};

int ode_rhs(sunrealtype t, N_Vector y, N_Vector derivative, void *raw) noexcept {
    auto &data = *static_cast<OdeData *>(raw);
    try {
        SymEngine::map_basic_basic substitutions{{data.time, SymEngine::real_double(t)}};
        for (std::size_t index = 0; index < data.states.size(); ++index) {
            substitutions[data.states[index]] = SymEngine::real_double(NV_Ith_S(y, index));
        }
        for (std::size_t index = 0; index < data.expressions.size(); ++index) {
            NV_Ith_S(derivative, index) = SymEngine::eval_double(*data.expressions[index]->subs(substitutions));
        }
        return 0;
    } catch (...) {
        data.failed = true;
        return -1;
    }
}

SolverOutput solve_ode(const Node &root, uint32_t max_iterations, uint32_t deadline_ms) {
    if (root.kind != NodeKind::Ode || root.children.size() < 4) return failure(
        TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM, "ODE IVP is incomplete");
    std::vector<std::string> state_names;
    std::vector<RCP<const Basic>> expressions;
    std::map<std::string, double> initial_values;
    std::optional<double> initial_time;
    std::string time;
    for (std::size_t index = 0; index + 2 < root.children.size(); ++index) {
        const Node &clause = root.children[index];
        if (clause.kind != NodeKind::Relation || clause.children.size() != 2) return failure(
            TEXSOLVE_STATUS_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY, "DAE and BVP are unsupported");
        const Node &left = clause.children.front();
        if (left.kind == NodeKind::Call && left.children.size() == 1) {
            std::string name = left.text;
            const auto primes = static_cast<unsigned>(std::count(name.begin(), name.end(), '\''));
            name.erase(std::remove(name.begin(), name.end(), '\''), name.end());
            if (primes != 0) name += "_d" + std::to_string(primes);
            const double at = scalar_value(left.children.front());
            if (initial_time && std::abs(*initial_time - at) > 1e-12) return failure(
                TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM,
                "ODE initial values must use one initial time");
            initial_time = at;
            initial_values[name] = scalar_value(clause.children[1]);
            continue;
        }
        if (left.kind != NodeKind::Derivative) return failure(
            TEXSOLVE_STATUS_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY, "DAE and BVP are unsupported");
        const auto separator = left.text.find('|');
        if (separator == std::string::npos) return failure(TEXSOLVE_STATUS_SEMANTIC_ERROR,
            TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM, "ODE derivative state is missing");
        const auto numerator = left.text.substr(0, separator);
        const auto denominator = left.text.substr(separator + 1);
        unsigned order = 1;
        std::size_t state_begin = 1;
        if (numerator.starts_with("d^")) {
            const auto position = numerator.find_first_not_of("0123456789", 2);
            order = static_cast<unsigned>(std::stoul(numerator.substr(2, position - 2)));
            state_begin = position;
        }
        const std::string state = numerator.substr(state_begin);
        const auto time_position = denominator.find_first_not_of("d^0123456789");
        if (time_position == std::string::npos) return failure(TEXSOLVE_STATUS_SEMANTIC_ERROR,
            TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM, "ODE independent variable is missing");
        const std::string current_time(1, denominator[time_position]);
        if (!time.empty() && time != current_time) return failure(TEXSOLVE_STATUS_SEMANTIC_ERROR,
            TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM, "ODE system must use one independent variable");
        time = current_time;
        for (unsigned derivative = 0; derivative < order; ++derivative) {
            const std::string name = derivative == 0 ? state : state + "_d" + std::to_string(derivative);
            state_names.push_back(name);
            expressions.push_back(derivative + 1 < order
                ? RCP<const Basic>(SymEngine::symbol(state + "_d" + std::to_string(derivative + 1)))
                : SymEngine::parse(to_backend_syntax(clause.children[1])));
        }
    }
    if (state_names.empty() || !initial_time || initial_values.size() != state_names.size()) return failure(
        TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM,
        "ODE requires a complete initial value for every normalized state");
    const double t0 = scalar_value(root.children[root.children.size() - 2]);
    const double t1 = scalar_value(root.children.back());
    if (!(t1 > t0) || std::abs(t0 - *initial_time) > 1e-12) return failure(TEXSOLVE_STATUS_SEMANTIC_ERROR,
        TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM, "ODE interval must increase");

    SUNContext sun_context = nullptr;
    if (SUNContext_Create(SUN_COMM_NULL, &sun_context) != SUN_SUCCESS) return failure(
        TEXSOLVE_STATUS_INTERNAL_ERROR, TEXSOLVE_DIAGNOSTIC_INTERNAL_EXCEPTION, "SUNDIALS context creation failed");
    N_Vector vector = N_VNew_Serial(static_cast<sunindextype>(state_names.size()), sun_context);
    void *memory = CVodeCreate(CV_ADAMS, sun_context);
    SUNNonlinearSolver nonlinear_solver = vector == nullptr ? nullptr : SUNNonlinSol_FixedPoint(vector, 3, sun_context);
    if (vector == nullptr || memory == nullptr || nonlinear_solver == nullptr) {
        if (vector != nullptr) N_VDestroy(vector);
        if (memory != nullptr) CVodeFree(&memory);
        if (nonlinear_solver != nullptr) SUNNonlinSolFree(nonlinear_solver);
        SUNContext_Free(&sun_context);
        return failure(TEXSOLVE_STATUS_INTERNAL_ERROR, TEXSOLVE_DIAGNOSTIC_INTERNAL_EXCEPTION,
                       "SUNDIALS allocation failed");
    }
    OdeData data;
    data.expressions = std::move(expressions);
    data.time = SymEngine::symbol(time);
    for (std::size_t index = 0; index < state_names.size(); ++index) {
        data.states.push_back(SymEngine::symbol(state_names[index]));
        NV_Ith_S(vector, index) = initial_values[state_names[index]];
    }
    int flag = CVodeInit(memory, ode_rhs, t0, vector);
    flag = flag < 0 ? flag : CVodeSStolerances(memory, 1e-9, 1e-11);
    flag = flag < 0 ? flag : CVodeSetUserData(memory, &data);
    flag = flag < 0 ? flag : CVodeSetNonlinearSolver(memory, nonlinear_solver);
    flag = flag < 0 ? flag : CVodeSetMaxNumSteps(memory, static_cast<long>(max_iterations));
    SolverOutput output;
    output.root.kind = TEXSOLVE_RESULT_TRAJECTORY;
    output.root.backend = "sundials";
    auto append_sample = [&](double t) {
        SolverNode sample;
        sample.kind = TEXSOLVE_RESULT_SAMPLE;
        sample.children.push_back(scalar("t", t));
        for (std::size_t index = 0; index < state_names.size(); ++index) {
            sample.children.push_back(scalar(state_names[index], NV_Ith_S(vector, index)));
        }
        output.root.children.push_back(std::move(sample));
    };
    append_sample(t0);
    double reached = t0;
    const auto started = std::chrono::steady_clock::now();
    bool deadline_hit = false;
    for (int index = 1; flag >= 0 && index <= 10; ++index) {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count() >= deadline_ms) {
            deadline_hit = true;
            flag = -1;
            break;
        }
        const double target = t0 + (t1 - t0) * static_cast<double>(index) / 10.0;
        flag = CVode(memory, target, vector, &reached, CV_NORMAL);
        if (flag >= 0) append_sample(reached);
    }
    long steps = 0;
    CVodeGetNumSteps(memory, &steps);
    const bool callback_failed = data.failed;
    CVodeFree(&memory);
    SUNNonlinSolFree(nonlinear_solver);
    N_VDestroy(vector);
    SUNContext_Free(&sun_context);
    if (deadline_hit) return failure(TEXSOLVE_STATUS_DEADLINE_EXCEEDED, TEXSOLVE_DIAGNOSTIC_DEADLINE,
                                     "SUNDIALS request deadline exceeded");
    if (callback_failed) return failure(TEXSOLVE_STATUS_NOT_CONVERGED,
        TEXSOLVE_DIAGNOSTIC_NUMERICAL_FAILURE, "SUNDIALS RHS evaluation failed");
    if (flag < 0) return failure(TEXSOLVE_STATUS_NOT_CONVERGED, TEXSOLVE_DIAGNOSTIC_NUMERICAL_FAILURE,
                                 "SUNDIALS integration failed");
    output.root.metadata.push_back(integer_node("steps", steps));
    output.root.metadata.push_back(exact_scalar("termination_reason", "converged"));
    output.root.metadata.push_back(integer_node("precision_digits", 15));
    return output;
}

}  // namespace

SolverOutput solve_problem(const Node &root, int32_t operation,
                           const std::map<std::string, Node> &bindings,
                           uint32_t max_iterations, uint32_t deadline_ms,
                           int32_t linear_backend, int32_t optimization_backend,
                           int32_t optimization_kind, const std::vector<Node> &residuals,
                           const std::map<std::string, Node> &lower_bounds,
                           const std::map<std::string, Node> &upper_bounds) {
    if (operation == TEXSOLVE_OPERATION_LINEAR_ALGEBRA) {
        return linear_backend == TEXSOLVE_LINEAR_ALGEBRA_ARMADILLO
                   ? solve_linear_armadillo(root) : solve_linear(root);
    }
    if (operation == TEXSOLVE_OPERATION_SOLVE) {
        return solve_equation(root, bindings, max_iterations, deadline_ms);
    }
    if (operation == TEXSOLVE_OPERATION_OPTIMIZE) {
        if (optimization_backend == TEXSOLVE_OPTIMIZATION_CERES &&
            optimization_kind != TEXSOLVE_OPTIMIZATION_KIND_LEAST_SQUARES) {
            return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                           "Ceres requires a least-squares residual request");
        }
        const bool has_bounds = !lower_bounds.empty() || !upper_bounds.empty();
        if (optimization_kind == TEXSOLVE_OPTIMIZATION_KIND_LEAST_SQUARES &&
            optimization_backend != TEXSOLVE_OPTIMIZATION_NLOPT &&
            !(optimization_backend == TEXSOLVE_OPTIMIZATION_AUTO && has_bounds)) {
            if (has_bounds) return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED,
                TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY, "Ceres least squares does not accept variable bounds");
            const auto objective = std::find_if(root.children.begin(), root.children.end(), [](const Node &node) {
                return node.kind != NodeKind::Symbol;
            });
            if (objective != root.children.end() && objective + 1 != root.children.end()) return failure(
                TEXSOLVE_STATUS_BACKEND_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                "Ceres least squares does not accept general relation constraints");
            return solve_least_squares(root, bindings, residuals, max_iterations, deadline_ms);
        }
        if (optimization_kind == TEXSOLVE_OPTIMIZATION_KIND_LEAST_SQUARES) {
            Node nlopt_root = root;
            auto objective = std::find_if(nlopt_root.children.begin(), nlopt_root.children.end(), [](const Node &node) {
                return node.kind != NodeKind::Symbol;
            });
            Node sum;
            for (const auto &residual : residuals) {
                Node exponent;
                exponent.kind = NodeKind::Integer;
                exponent.text = "2";
                Node square;
                square.kind = NodeKind::Binary;
                square.text = "^";
                square.children = {residual, std::move(exponent)};
                if (sum.children.empty()) sum = std::move(square);
                else {
                    Node addition;
                    addition.kind = NodeKind::Binary;
                    addition.text = "+";
                    addition.children = {std::move(sum), std::move(square)};
                    sum = std::move(addition);
                }
            }
            *objective = std::move(sum);
            return solve_optimization(nlopt_root, bindings, lower_bounds, upper_bounds,
                                      max_iterations, deadline_ms);
        }
        return solve_optimization(root, bindings, lower_bounds, upper_bounds, max_iterations, deadline_ms);
    }
    if (operation == TEXSOLVE_OPERATION_ODE_IVP) return solve_ode(root, max_iterations, deadline_ms);
    return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                   "unsupported solver operation");
}

}  // namespace texsolve
