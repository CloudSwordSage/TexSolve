#include "evaluator.hpp"

#include <texsolve/texsolve.h>

#include <ginac/factor.h>
#include <ginac/ginac.h>
#include <mpfr.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include <symengine/add.h>
#include <symengine/eval_mpfr.h>
#include <symengine/functions.h>
#include <symengine/integer.h>
#include <symengine/parser.h>
#include <symengine/pow.h>
#include <symengine/printers.h>
#include <symengine/rational.h>
#include <symengine/subs.h>
#include <symengine/symbol.h>
#pragma GCC diagnostic pop

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace texsolve {
namespace {

using SymEngine::Basic;
using SymEngine::RCP;

std::string symbol_name(std::string value) {
    if (value == "\\pi") return "pi";
    if (value == "e") return "E";
    if (value == "i") return "I";
    if (!value.empty() && value.front() == '\\') value.erase(value.begin());
    if (value.starts_with("operatorname{")) value = value.substr(13, value.size() - 14);
    return value;
}

std::string variable_from_derivative(std::string text) {
    if (text.starts_with("\\partial")) text.erase(0, 8);
    else if (!text.empty() && text.front() == 'd') text.erase(0, 1);
    text.erase(std::remove_if(text.begin(), text.end(), [](char ch) {
        return ch == ' ' || ch == '^' || std::isdigit(static_cast<unsigned char>(ch));
    }), text.end());
    return symbol_name(text);
}

int classify_latex(const std::string &latex) {
    if (latex.find('i') != std::string::npos || latex.find('I') != std::string::npos) {
        return TEXSOLVE_RESULT_COMPLEX;
    }
    if (latex.find("\\frac") != std::string::npos) return TEXSOLVE_RESULT_RATIONAL;
    const bool integer = !latex.empty() && std::all_of(latex.begin(), latex.end(), [](char ch) {
        return std::isdigit(static_cast<unsigned char>(ch)) || ch == '-';
    });
    if (integer) return TEXSOLVE_RESULT_INTEGER;
    const bool numeric = !latex.empty() && std::all_of(latex.begin(), latex.end(), [](char ch) {
        return std::isdigit(static_cast<unsigned char>(ch)) || ch == '-' || ch == '+' || ch == '.' ||
               ch == 'e' || ch == 'E';
    });
    return numeric ? TEXSOLVE_RESULT_REAL : TEXSOLVE_RESULT_SYMBOLIC;
}

std::string mpfr_approximation(const Basic &expression, uint32_t digits) {
    const auto bits = static_cast<mpfr_prec_t>(std::ceil(static_cast<double>(digits) * 3.3219280948873626)) + 8;
    mpfr_t value;
    mpfr_init2(value, bits);
    try {
        SymEngine::eval_mpfr(value, expression, MPFR_RNDN);
        std::string buffer(static_cast<std::size_t>(digits) + 32, '\0');
        const int written = mpfr_snprintf(buffer.data(), buffer.size(), "%.*Rg", static_cast<int>(digits), value);
        mpfr_clear(value);
        if (written < 0) return {};
        buffer.resize(static_cast<std::size_t>(written));
        return buffer;
    } catch (...) {
        mpfr_clear(value);
        return {};
    }
}

RCP<const Basic> symengine_expression(const Node &node, const std::map<std::string, Node> &bindings) {
    auto expression = SymEngine::parse(to_backend_syntax(node));
    SymEngine::map_basic_basic substitutions;
    for (const auto &[name, value] : bindings) {
        substitutions[SymEngine::symbol(symbol_name(name))] = SymEngine::parse(to_backend_syntax(value));
    }
    return substitutions.empty() ? expression : expression->subs(substitutions);
}

std::optional<std::string> integrate_simple(const Node &node, const std::string &variable) {
    if (node.kind == NodeKind::Integer || node.kind == NodeKind::Real) {
        return node.text + " " + variable;
    }
    if (node.kind == NodeKind::Symbol && symbol_name(node.text) == variable) {
        return "\\frac{" + variable + "^{2}}{2}";
    }
    if (node.kind == NodeKind::Binary && node.text == "^" && node.children.size() == 2 &&
        node.children[0].kind == NodeKind::Symbol && symbol_name(node.children[0].text) == variable &&
        node.children[1].kind == NodeKind::Integer) {
        const int exponent = std::stoi(node.children[1].text);
        if (exponent == -1) return "\\ln{" + variable + "}";
        return "\\frac{" + variable + "^{" + std::to_string(exponent + 1) + "}}{" +
               std::to_string(exponent + 1) + "}";
    }
    if (node.kind == NodeKind::Binary && (node.text == "+" || node.text == "-") && node.children.size() == 2) {
        auto left = integrate_simple(node.children[0], variable);
        auto right = integrate_simple(node.children[1], variable);
        if (left && right) return *left + node.text + *right;
    }
    if (node.kind == NodeKind::Call && node.children.size() == 1 &&
        node.children[0].kind == NodeKind::Symbol && symbol_name(node.children[0].text) == variable) {
        if (node.text == "sin") return "-\\cos{" + variable + "}";
        if (node.text == "cos") return "\\sin{" + variable + "}";
        if (node.text == "exp") return "\\exp{" + variable + "}";
    }
    return std::nullopt;
}

Evaluation failure(int32_t status, int32_t code, std::string message, std::string backend = {}) {
    Evaluation result;
    result.status = status;
    result.diagnostic_code = code;
    result.message = std::move(message);
    result.backend = std::move(backend);
    return result;
}

Evaluation evaluate_fold(const Node &root, bool product, const std::map<std::string, Node> &bindings,
                         uint32_t precision, std::string backend) {
    if (root.children.size() != 3) {
        return failure(TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM,
                       "finite fold is incomplete", std::move(backend));
    }
    const int lower = std::stoi(root.children[0].text);
    const int upper = std::stoi(root.children[1].text);
    if (upper < lower || upper - lower > 10000000) {
        return failure(TEXSOLVE_STATUS_RESOURCE_LIMIT, TEXSOLVE_DIAGNOSTIC_ITERATION_LIMIT,
                       "finite fold iteration limit exceeded", std::move(backend));
    }
    RCP<const Basic> accumulator = product ? SymEngine::integer(1) : SymEngine::integer(0);
    const auto variable = root.text.substr(root.text.find(':') + 1);
    auto expression = symengine_expression(root.children[2], bindings);
    const auto symbol = SymEngine::symbol(variable);
    for (int value = lower; value <= upper; ++value) {
        const auto term = expression->subs({{symbol, SymEngine::integer(value)}});
        accumulator = product ? SymEngine::mul(accumulator, term) : SymEngine::add(accumulator, term);
    }
    Evaluation result;
    result.kind = classify_latex(SymEngine::latex(*accumulator));
    result.exact = SymEngine::latex(*accumulator);
    result.approximation = mpfr_approximation(*accumulator, precision);
    result.backend = std::move(backend);
    return result;
}

Evaluation evaluate_symengine(const Node &root, int32_t operation,
                              const std::map<std::string, Node> &bindings, uint32_t precision) {
    const std::string backend = "symengine";
    if (root.kind == NodeKind::Fold) return evaluate_fold(root, root.text.starts_with("product"), bindings, precision, backend);
    if (root.kind == NodeKind::Integral) {
        const auto separator = root.text.find(':');
        const std::string variable = separator == std::string::npos ? "x" : root.text.substr(separator + 1);
        const Node &body = root.children.back();
        const auto antiderivative = integrate_simple(body, variable);
        if (!antiderivative) {
            return failure(TEXSOLVE_STATUS_NO_ANALYTIC_SOLUTION, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                           "no analytic antiderivative is available", backend);
        }
        Evaluation result;
        result.kind = TEXSOLVE_RESULT_SYMBOLIC;
        result.exact = *antiderivative;
        result.backend = backend;
        if (root.children.size() == 3) {
            auto primitive = SymEngine::parse(to_backend_syntax(body));
            const auto symbol = SymEngine::symbol(variable);
            const auto upper = SymEngine::parse(to_backend_syntax(root.children[1]));
            const auto lower = SymEngine::parse(to_backend_syntax(root.children[0]));
            const auto integrated = SymEngine::parse(to_backend_syntax(parse_for_debug(*antiderivative, 128, 50000).root));
            primitive = SymEngine::sub(integrated->subs({{symbol, upper}}), integrated->subs({{symbol, lower}}));
            result.exact = SymEngine::latex(*primitive);
            result.kind = classify_latex(result.exact);
            result.approximation = mpfr_approximation(*primitive, precision);
        }
        return result;
    }

    const Node *expression_node = &root;
    std::string derivative_variable;
    if (root.kind == NodeKind::Derivative) {
        expression_node = &root.children.front();
        derivative_variable = variable_from_derivative(root.text);
    }
    auto expression = symengine_expression(*expression_node, bindings);
    if (root.kind == NodeKind::Derivative || operation == TEXSOLVE_OPERATION_DIFFERENTIATE) {
        if (derivative_variable.empty()) derivative_variable = "x";
        expression = expression->diff(SymEngine::symbol(derivative_variable));
    } else if (operation == TEXSOLVE_OPERATION_EXPAND) {
        expression = SymEngine::expand(expression);
    }
    Evaluation result;
    result.exact = SymEngine::latex(*expression);
    result.kind = classify_latex(result.exact);
    result.approximation = mpfr_approximation(*expression, precision);
    result.backend = backend;
    return result;
}

Evaluation evaluate_ginac(const Node &root, int32_t operation,
                          const std::map<std::string, Node> &bindings, uint32_t precision) {
    if (root.kind == NodeKind::Fold || root.kind == NodeKind::Integral) {
        auto result = evaluate_symengine(root, operation, bindings, precision);
        result.backend = "ginac";
        return result;
    }
    const Node *expression_node = &root;
    std::string derivative_variable;
    if (root.kind == NodeKind::Derivative) {
        expression_node = &root.children.front();
        derivative_variable = variable_from_derivative(root.text);
    }
    GiNaC::parser parser;
    GiNaC::ex expression = parser(to_backend_syntax(*expression_node));
    for (const auto &[name, value] : bindings) {
        expression = expression.subs(parser(symbol_name(name)) == parser(to_backend_syntax(value)));
    }
    if (root.kind == NodeKind::Derivative || operation == TEXSOLVE_OPERATION_DIFFERENTIATE) {
        if (derivative_variable.empty()) derivative_variable = "x";
        const auto symbol_expression = parser(derivative_variable);
        if (!GiNaC::is_a<GiNaC::symbol>(symbol_expression)) throw std::runtime_error("invalid derivative variable");
        expression = expression.diff(GiNaC::ex_to<GiNaC::symbol>(symbol_expression));
    } else if (operation == TEXSOLVE_OPERATION_EXPAND) {
        expression = expression.expand();
    } else if (operation == TEXSOLVE_OPERATION_FACTOR) {
        expression = GiNaC::factor(expression);
    } else {
        expression = expression.normal();
    }
    std::ostringstream latex;
    latex << GiNaC::latex << expression;
    Evaluation result;
    result.exact = latex.str();
    result.kind = classify_latex(result.exact);
    result.backend = "ginac";
    std::ostringstream approximate;
    GiNaC::Digits = precision;
    approximate << expression.evalf();
    result.approximation = approximate.str();
    return result;
}

}  // namespace

std::string to_backend_syntax(const Node &node) {
    switch (node.kind) {
        case NodeKind::Integer:
        case NodeKind::Real: return node.text;
        case NodeKind::Symbol: return symbol_name(node.text);
        case NodeKind::Unary: return "(" + node.text + to_backend_syntax(node.children.front()) + ")";
        case NodeKind::Binary: {
            const std::string op = node.text == "frac" ? "/" :
                                   (node.text == "implicit" || node.text == "\\cdot" || node.text == "\\times") ? "*" : node.text;
            return "(" + to_backend_syntax(node.children[0]) + op + to_backend_syntax(node.children[1]) + ")";
        }
        case NodeKind::Call: {
            if (node.text.starts_with("sqrt:")) {
                return "(" + to_backend_syntax(node.children.front()) + ")^(1/" + node.text.substr(5) + ")";
            }
            std::string result = node.text + "(";
            for (std::size_t index = 0; index < node.children.size(); ++index) {
                if (index != 0) result += ',';
                result += to_backend_syntax(node.children[index]);
            }
            return result + ')';
        }
        default: throw std::invalid_argument("AST node is not a scalar expression");
    }
}

Evaluation evaluate(const Node &root, int32_t operation, int32_t symbolic_backend,
                    const std::map<std::string, Node> &bindings, uint32_t precision_digits) {
    try {
        const int32_t selected = symbolic_backend == TEXSOLVE_SYMBOLIC_AUTO
                                     ? (operation == TEXSOLVE_OPERATION_FACTOR
                                            ? TEXSOLVE_SYMBOLIC_GINAC
                                            : TEXSOLVE_SYMBOLIC_SYMENGINE)
                                     : symbolic_backend;
        if (selected == TEXSOLVE_SYMBOLIC_SYMENGINE) {
            if (operation == TEXSOLVE_OPERATION_FACTOR) {
                return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED,
                               TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                               "SymEngine does not provide polynomial factorization", "symengine");
            }
            return evaluate_symengine(root, operation, bindings, precision_digits);
        }
        if (selected == TEXSOLVE_SYMBOLIC_GINAC) {
            return evaluate_ginac(root, operation, bindings, precision_digits);
        }
        return failure(TEXSOLVE_STATUS_INVALID_ARGUMENT, TEXSOLVE_DIAGNOSTIC_BACKEND_MISSING,
                       "unknown symbolic backend");
    } catch (const std::exception &error) {
        return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                       error.what(), symbolic_backend == TEXSOLVE_SYMBOLIC_GINAC ? "ginac" : "symengine");
    }
}

}  // namespace texsolve
