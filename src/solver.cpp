#include "solver.hpp"

#include "evaluator.hpp"

#include <texsolve/texsolve.h>

#include <Eigen/Dense>
#include <cvode/cvode.h>
#include <nlopt.h>
#include <nvector/nvector_serial.h>
#include <sundials/sundials_context.h>
#include <sunnonlinsol/sunnonlinsol_fixedpoint.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include <symengine/eval_double.h>
#include <symengine/parser.h>
#include <symengine/printers.h>
#include <symengine/sets.h>
#include <symengine/solve.h>
#include <symengine/subs.h>
#include <symengine/symbol.h>
#pragma GCC diagnostic pop

#include <algorithm>
#include <chrono>
#include <cmath>
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

SolverNode exact_scalar(std::string name, std::string exact) {
    SolverNode result;
    result.kind = TEXSOLVE_RESULT_SYMBOLIC;
    result.name = std::move(name);
    result.exact = std::move(exact);
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
        if (root.kind == NodeKind::Matrix) {
            SolverOutput output;
            output.root = matrix_node(matrix_value(root));
            return output;
        }
        if (root.kind != NodeKind::Call || root.children.size() != 1) {
            return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                           "unsupported linear algebra operation");
        }
        const Eigen::MatrixXd matrix = matrix_value(root.children.front());
        SolverOutput output;
        if (root.text == "det") {
            if (matrix.rows() != matrix.cols()) return failure(TEXSOLVE_STATUS_SEMANTIC_ERROR,
                TEXSOLVE_DIAGNOSTIC_DIMENSION_MISMATCH, "determinant requires a square matrix");
            output.root = scalar({}, matrix.determinant());
            output.root.backend = "eigen";
        } else if (root.text == "rank") {
            output.root = integer_node({}, matrix.fullPivLu().rank());
            output.root.backend = "eigen";
        } else if (root.text == "inv") {
            if (matrix.rows() != matrix.cols() || !matrix.fullPivLu().isInvertible()) {
                return failure(TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_SINGULAR_MATRIX,
                               "matrix inverse requires a nonsingular square matrix");
            }
            output.root = matrix_node(matrix.inverse());
        } else if (root.text == "eigenvalues") {
            if (matrix.rows() != matrix.cols()) return failure(TEXSOLVE_STATUS_SEMANTIC_ERROR,
                TEXSOLVE_DIAGNOSTIC_DIMENSION_MISMATCH, "eigenvalues require a square matrix");
            Eigen::EigenSolver<Eigen::MatrixXd> eigen(matrix, false);
            output.root.kind = TEXSOLVE_RESULT_LIST;
            output.root.backend = "eigen";
            for (Eigen::Index index = 0; index < eigen.eigenvalues().size(); ++index) {
                const auto value = eigen.eigenvalues()[index];
                output.root.children.push_back(exact_scalar({}, number(value.real()) +
                    (value.imag() < 0 ? "" : "+") + number(value.imag()) + "i"));
            }
        } else {
            return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                           "unsupported linear algebra operation");
        }
        output.root.metadata.push_back(integer_node("precision_digits", 15));
        return output;
    } catch (const std::exception &error) {
        return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY, error.what());
    }
}

void collect_symbols(const Node &node, std::vector<std::string> &symbols) {
    if (node.kind == NodeKind::Symbol && node.text != "\\pi" && node.text != "e" && node.text != "i" &&
        std::find(symbols.begin(), symbols.end(), node.text) == symbols.end()) symbols.push_back(node.text);
    for (const auto &child : node.children) collect_symbols(child, symbols);
}

SolverOutput solve_equation(const Node &root) {
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
        const auto expression = SymEngine::sub(SymEngine::parse(to_backend_syntax(root.children[0])),
                                               SymEngine::parse(to_backend_syntax(root.children[1])));
        const auto solutions = SymEngine::solve_poly(expression, variable);
        if (!SymEngine::is_a<SymEngine::FiniteSet>(*solutions)) {
            return failure(TEXSOLVE_STATUS_NO_ANALYTIC_SOLUTION, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                           "solver did not return a finite root set");
        }
        std::vector<RCP<const Basic>> values = solutions->get_args();
        std::sort(values.begin(), values.end(), [](const auto &left, const auto &right) {
            const auto left_complex = SymEngine::str(*left).find('I') != std::string::npos;
            const auto right_complex = SymEngine::str(*right).find('I') != std::string::npos;
            if (left_complex != right_complex) return !left_complex;
            return SymEngine::str(*left) < SymEngine::str(*right);
        });
        SolverOutput output;
        output.root.kind = TEXSOLVE_RESULT_ROOT_SET;
        output.root.backend = "symengine";
        for (const auto &value : values) {
            SolverNode root_node;
            root_node.kind = TEXSOLVE_RESULT_ROOT;
            root_node.children.push_back(exact_scalar("value", SymEngine::latex(*value)));
            root_node.children.push_back(integer_node("multiplicity", 1));
            root_node.children.push_back(exact_scalar("search_kind", "analytic"));
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
};

double objective_callback(unsigned n, const double *values, double *, void *raw) {
    auto &data = *static_cast<ObjectiveData *>(raw);
    SymEngine::map_basic_basic substitutions;
    for (unsigned index = 0; index < n; ++index) substitutions[data.symbols[index]] = SymEngine::real_double(values[index]);
    return SymEngine::eval_double(*data.expression->subs(substitutions));
}

SolverOutput solve_optimization(const Node &root, const std::map<std::string, Node> &bindings,
                                uint32_t max_iterations, uint32_t deadline_ms) {
    if (root.kind != NodeKind::Optimization || root.children.size() < 2) return failure(
        TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM, "optimization problem is incomplete");
    const std::size_t variable_count = root.children.size() - 1;
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
    data.expression = SymEngine::parse(to_backend_syntax(root.children.back()));
    nlopt_opt optimizer = nlopt_create(NLOPT_LN_COBYLA, static_cast<unsigned>(variable_count));
    if (optimizer == nullptr) return failure(TEXSOLVE_STATUS_INTERNAL_ERROR, TEXSOLVE_DIAGNOSTIC_INTERNAL_EXCEPTION,
                                             "failed to create NLopt optimizer");
    nlopt_set_min_objective(optimizer, objective_callback, &data);
    nlopt_set_maxeval(optimizer, static_cast<int>(std::min<uint32_t>(max_iterations, 1000000)));
    nlopt_set_maxtime(optimizer, static_cast<double>(deadline_ms) / 1000.0);
    nlopt_set_xtol_rel(optimizer, 1e-10);
    double optimum_value = 0.0;
    const nlopt_result status = nlopt_optimize(optimizer, values.data(), &optimum_value);
    nlopt_destroy(optimizer);
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
    output.root.metadata.push_back(integer_node("iterations", 0));
    output.root.metadata.push_back(boolean_node("converged", true));
    output.root.metadata.push_back(exact_scalar("termination_reason", "converged"));
    output.root.metadata.push_back(integer_node("precision_digits", 15));
    return output;
}

struct OdeData {
    RCP<const Basic> expression;
    RCP<const SymEngine::Symbol> state;
    RCP<const SymEngine::Symbol> time;
};

int ode_rhs(sunrealtype t, N_Vector y, N_Vector derivative, void *raw) {
    auto &data = *static_cast<OdeData *>(raw);
    const auto value = data.expression->subs({{data.state, SymEngine::real_double(NV_Ith_S(y, 0))},
                                               {data.time, SymEngine::real_double(t)}});
    NV_Ith_S(derivative, 0) = SymEngine::eval_double(*value);
    return 0;
}

SolverOutput solve_ode(const Node &root) {
    if (root.kind != NodeKind::Ode || root.children.size() < 4) return failure(
        TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM, "ODE IVP is incomplete");
    const Node &differential = root.children.front();
    if (differential.kind != NodeKind::Relation || differential.children.front().kind != NodeKind::Derivative) {
        return failure(TEXSOLVE_STATUS_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY, "DAE and BVP are unsupported");
    }
    const auto descriptor = differential.children.front().text;
    const auto separator = descriptor.find('|');
    if (separator == std::string::npos) return failure(TEXSOLVE_STATUS_SEMANTIC_ERROR,
        TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM, "ODE derivative state is missing");
    std::string state = descriptor.substr(0, separator);
    std::string time = descriptor.substr(separator + 1);
    if (!state.empty() && state.front() == 'd') state.erase(state.begin());
    if (!time.empty() && time.front() == 'd') time.erase(time.begin());
    const Node &initial = root.children[1];
    if (initial.kind != NodeKind::Relation || initial.children.size() != 2) return failure(
        TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM, "ODE initial value is missing");
    const double y0 = scalar_value(initial.children[1]);
    const double t0 = scalar_value(root.children[root.children.size() - 2]);
    const double t1 = scalar_value(root.children.back());
    if (!(t1 > t0)) return failure(TEXSOLVE_STATUS_SEMANTIC_ERROR,
        TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM, "ODE interval must increase");

    SUNContext sun_context = nullptr;
    if (SUNContext_Create(SUN_COMM_NULL, &sun_context) != SUN_SUCCESS) return failure(
        TEXSOLVE_STATUS_INTERNAL_ERROR, TEXSOLVE_DIAGNOSTIC_INTERNAL_EXCEPTION, "SUNDIALS context creation failed");
    N_Vector vector = N_VNew_Serial(1, sun_context);
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
    NV_Ith_S(vector, 0) = y0;
    OdeData data{SymEngine::parse(to_backend_syntax(differential.children[1])),
                 SymEngine::symbol(state), SymEngine::symbol(time)};
    int flag = CVodeInit(memory, ode_rhs, t0, vector);
    flag = flag < 0 ? flag : CVodeSStolerances(memory, 1e-9, 1e-11);
    flag = flag < 0 ? flag : CVodeSetUserData(memory, &data);
    flag = flag < 0 ? flag : CVodeSetNonlinearSolver(memory, nonlinear_solver);
    SolverOutput output;
    output.root.kind = TEXSOLVE_RESULT_TRAJECTORY;
    output.root.backend = "sundials";
    auto append_sample = [&](double t, double y) {
        SolverNode sample;
        sample.kind = TEXSOLVE_RESULT_SAMPLE;
        sample.children.push_back(scalar("t", t));
        sample.children.push_back(scalar(state, y));
        output.root.children.push_back(std::move(sample));
    };
    append_sample(t0, y0);
    double reached = t0;
    for (int index = 1; flag >= 0 && index <= 10; ++index) {
        const double target = t0 + (t1 - t0) * static_cast<double>(index) / 10.0;
        flag = CVode(memory, target, vector, &reached, CV_NORMAL);
        if (flag >= 0) append_sample(reached, NV_Ith_S(vector, 0));
    }
    long steps = 0;
    CVodeGetNumSteps(memory, &steps);
    CVodeFree(&memory);
    SUNNonlinSolFree(nonlinear_solver);
    N_VDestroy(vector);
    SUNContext_Free(&sun_context);
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
                           uint32_t max_iterations, uint32_t deadline_ms) {
    if (operation == TEXSOLVE_OPERATION_LINEAR_ALGEBRA) return solve_linear(root);
    if (operation == TEXSOLVE_OPERATION_SOLVE) return solve_equation(root);
    if (operation == TEXSOLVE_OPERATION_OPTIMIZE) return solve_optimization(root, bindings, max_iterations, deadline_ms);
    if (operation == TEXSOLVE_OPERATION_ODE_IVP) return solve_ode(root);
    return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                   "unsupported solver operation");
}

}  // namespace texsolve
