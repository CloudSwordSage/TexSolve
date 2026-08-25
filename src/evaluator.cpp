#include "evaluator.hpp"

#include <texsolve/texsolve.h>

#include <boost/math/quadrature/gauss_kronrod.hpp>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_integration.h>
#include <ginac/factor.h>
#include <ginac/ginac.h>
#include <mpfr.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include <symengine/add.h>
#include <symengine/complex.h>
#include <symengine/eval_mpfr.h>
#include <symengine/functions.h>
#include <symengine/integer.h>
#include <symengine/infinity.h>
#include <symengine/ntheory.h>
#include <symengine/number.h>
#include <symengine/parser.h>
#include <symengine/pow.h>
#include <symengine/printers.h>
#include <symengine/rational.h>
#include <symengine/subs.h>
#include <symengine/symbol.h>
#pragma GCC diagnostic pop

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace texsolve {
namespace {

using SymEngine::Basic;
using SymEngine::RCP;

/** Convert a finite decimal literal to exact backend arithmetic. */
std::string exact_real_syntax(std::string_view value) {
    const auto exponent = value.find_first_of("eE");
    const auto mantissa = value.substr(0, exponent);
    const auto point = mantissa.find('.');
    std::string digits(mantissa);
    if (point != std::string_view::npos) digits.erase(point, 1);
    const auto first_nonzero = digits.find_first_not_of('0');
    digits = first_nonzero == std::string::npos ? "0" : digits.substr(first_nonzero);
    const std::size_t decimal_places = point == std::string_view::npos ? 0 : mantissa.size() - point - 1;
    std::string result = decimal_places == 0
                             ? digits
                             : "(" + digits + "/10^" + std::to_string(decimal_places) + ")";
    if (exponent != std::string_view::npos) {
        result = "(" + result + "*10^(" + std::string(value.substr(exponent + 1)) + "))";
    }
    return result;
}

/** Return whether two AST subtrees represent the same expression, ignoring source spans. */
bool same_expression(const Node &left, const Node &right) {
    if (left.kind != right.kind || left.text != right.text || left.children.size() != right.children.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.children.size(); ++index) {
        if (!same_expression(left.children[index], right.children[index])) return false;
    }
    return true;
}

/** Find the real symbol squared by a multiplication or power node. */
const Node *real_symbol_square_base(const Node &node) {
    if (node.kind != NodeKind::Binary || node.children.size() != 2) return nullptr;
    const bool product = node.text == "*" || node.text == "implicit" ||
                         node.text == "\\cdot" || node.text == "\\times";
    const Node *base = nullptr;
    if (product && same_expression(node.children[0], node.children[1])) base = &node.children[0];
    if (node.text == "^" && node.children[1].kind == NodeKind::Integer &&
        node.children[1].text == "2") base = &node.children[0];
    return base != nullptr && base->kind == NodeKind::Symbol && base->text != "i" ? base : nullptr;
}

std::string symbol_name(std::string value) {
    if (value == "\\pi") return "pi";
    if (value == "\\infty") return "oo";
    if (value == "e") return "E";
    if (value == "i") return "I";
    if (!value.empty() && value.front() == '\\') value.erase(value.begin());
    if (value.starts_with("operatorname{")) value = value.substr(13, value.size() - 14);
    return value;
}

std::string normalize_derivative_variable(std::string text) {
    if (text.starts_with("\\partial")) text.erase(0, 8);
    else if (!text.empty() && text.front() == 'd') text.erase(0, 1);
    text.erase(std::remove_if(text.begin(), text.end(), [](char ch) {
        return ch == ' ' || ch == '^' || std::isdigit(static_cast<unsigned char>(ch));
    }), text.end());
    return symbol_name(text);
}

std::vector<std::string> variables_from_derivative(std::string_view text) {
    std::vector<std::string> variables;
    for (std::size_t begin = 0; begin <= text.size();) {
        const auto end = text.find(',', begin);
        variables.push_back(normalize_derivative_variable(
            std::string(text.substr(begin, end == std::string_view::npos ? text.size() - begin : end - begin))));
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return variables;
}

const Node *odd_root_power_base(const Node &radicand, std::string_view degree) {
    if (degree.empty() || (degree.back() - '0') % 2 == 0 ||
        radicand.kind != NodeKind::Binary || radicand.text != "^" ||
        radicand.children.size() != 2 || radicand.children[1].kind != NodeKind::Integer ||
        radicand.children[1].text != degree) {
        return nullptr;
    }
    const Node &base = radicand.children[0];
    return base.kind == NodeKind::Symbol && base.text != "i" ? &base : nullptr;
}

bool is_provably_real(const Node &node) {
    if (node.kind == NodeKind::Integer || node.kind == NodeKind::Real) return true;
    if (node.kind == NodeKind::Symbol) return node.text == "\\pi" || node.text == "e";
    if (node.kind == NodeKind::Unary && node.children.size() == 1) {
        return is_provably_real(node.children.front());
    }
    if (node.kind == NodeKind::Binary && node.children.size() == 2) {
        if (node.text == "^") {
            return is_provably_real(node.children[0]) &&
                   node.children[1].kind == NodeKind::Integer;
        }
        return is_provably_real(node.children[0]) && is_provably_real(node.children[1]);
    }
    return false;
}

bool real_symbol(const Node &symbol, const std::map<std::string, Node> *bindings) {
    if (bindings == nullptr) return false;
    const auto binding = bindings->find(symbol.text);
    return binding == bindings->end() || is_provably_real(binding->second);
}

int classify_symengine(const Basic &expression) {
    if (SymEngine::is_a<SymEngine::Integer>(expression)) return TEXSOLVE_RESULT_INTEGER;
    if (SymEngine::is_a<SymEngine::Rational>(expression)) return TEXSOLVE_RESULT_RATIONAL;
    if (SymEngine::is_a_Complex(expression)) return TEXSOLVE_RESULT_COMPLEX;
    return SymEngine::is_a_Number(expression) ? TEXSOLVE_RESULT_REAL : TEXSOLVE_RESULT_SYMBOLIC;
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

void assign_symengine_scalar(Evaluation &result, const Basic &expression, uint32_t precision) {
    result.exact = SymEngine::latex(expression);
    result.kind = classify_symengine(expression);
    result.approximation = mpfr_approximation(expression, precision);
    if (result.kind != TEXSOLVE_RESULT_COMPLEX) return;
    const auto &complex = SymEngine::down_cast<const SymEngine::ComplexBase &>(expression);
    const auto real = complex.real_part();
    const auto imag = complex.imaginary_part();
    result.real_kind = classify_symengine(*real);
    result.imag_kind = classify_symengine(*imag);
    result.real = SymEngine::latex(*real);
    result.imag = SymEngine::latex(*imag);
}

Evaluation failure(int32_t status, int32_t code, std::string message, std::string backend = {});

RCP<const Basic> symengine_expression(const Node &node, const std::map<std::string, Node> &bindings) {
    auto expression = SymEngine::parse(to_backend_syntax(node, &bindings));
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
    if (node.kind == NodeKind::Binary &&
        (node.text == "implicit" || node.text == "\\cdot" || node.text == "\\times")) {
        auto contains = [&](const Node &candidate, const auto &self) -> bool {
            if (candidate.kind == NodeKind::Symbol && symbol_name(candidate.text) == variable) return true;
            return std::any_of(candidate.children.begin(), candidate.children.end(),
                               [&](const auto &child) { return self(child, self); });
        };
        const bool left_has_variable = contains(node.children[0], contains);
        const bool right_has_variable = contains(node.children[1], contains);
        if (left_has_variable != right_has_variable) {
            const Node &constant = left_has_variable ? node.children[1] : node.children[0];
            const Node &dependent = left_has_variable ? node.children[0] : node.children[1];
            if (auto integrated = integrate_simple(dependent, variable)) {
                return to_backend_syntax(constant) + "\\cdot(" + *integrated + ")";
            }
        }
    }
    if (node.kind == NodeKind::Binary && (node.text == "frac" || node.text == "/")) {
        auto contains = [&](const Node &candidate, const auto &self) -> bool {
            if (candidate.kind == NodeKind::Symbol && symbol_name(candidate.text) == variable) return true;
            return std::any_of(candidate.children.begin(), candidate.children.end(),
                               [&](const auto &child) { return self(child, self); });
        };
        if (!contains(node.children[1], contains)) {
            if (auto numerator = integrate_simple(node.children[0], variable)) {
                return "(" + *numerator + ")/" + to_backend_syntax(node.children[1]);
            }
        }
    }
    if (node.kind == NodeKind::Call && node.children.size() == 1 &&
        node.children[0].kind == NodeKind::Symbol && symbol_name(node.children[0].text) == variable) {
        if (node.text == "sin") return "-\\cos{" + variable + "}";
        if (node.text == "cos") return "\\sin{" + variable + "}";
        if (node.text == "exp") return "\\exp{" + variable + "}";
    }
    return std::nullopt;
}

enum class GrowthClass { Bounded, Logarithmic, Polynomial, Exponential, Unknown };

struct AsymptoticInfo {
    GrowthClass growth = GrowthClass::Unknown;
    int degree = 0;
    std::optional<RCP<const Basic>> limit;
    std::optional<RCP<const Basic>> leading;
};

AsymptoticInfo finite_asymptotic(const RCP<const Basic> &value) {
    return {GrowthClass::Bounded, 0, value, std::nullopt};
}

int growth_rank(const AsymptoticInfo &value) {
    switch (value.growth) {
        case GrowthClass::Bounded: return 0;
        case GrowthClass::Logarithmic: return 1;
        case GrowthClass::Polynomial: return 2;
        case GrowthClass::Exponential: return 3;
        case GrowthClass::Unknown: return -1;
    }
    return -1;
}

int compare_growth(const AsymptoticInfo &left, const AsymptoticInfo &right) {
    // Unknown growth is incomparable; callers must keep the result unknown.
    if (left.growth == GrowthClass::Unknown || right.growth == GrowthClass::Unknown) return 0;
    const int rank_difference = growth_rank(left) - growth_rank(right);
    if (rank_difference != 0) return rank_difference < 0 ? -1 : 1;
    if (left.growth == GrowthClass::Polynomial && left.degree != right.degree) {
        return left.degree < right.degree ? -1 : 1;
    }
    return 0;
}

int numeric_sign(const RCP<const Basic> &value) {
    try {
        const double number = SymEngine::eval_double(*value);
        return (number > 0.0) - (number < 0.0);
    } catch (...) {
        return 0;
    }
}

AsymptoticInfo negate_asymptotic(AsymptoticInfo value) {
    if (value.limit) value.limit = SymEngine::neg(*value.limit);
    if (value.leading) value.leading = SymEngine::neg(*value.leading);
    return value;
}

/**
 * Recursively infer finite limits and coarse growth at signed infinity.
 *
 * Args:
 *     node: Scalar AST to analyze.
 *     variable: Limit variable in backend spelling.
 *     infinity_sign: Positive or negative infinity direction.
 *     bindings: Request and context substitutions.
 * Returns:
 *     Known limit and growth information; Unknown when no safe rule applies.
 */
AsymptoticInfo asymptotic_info(const Node &node, std::string_view variable, int infinity_sign,
                               const std::map<std::string, Node> &bindings) {
    if (node.kind == NodeKind::Integer || node.kind == NodeKind::Real) {
        return finite_asymptotic(symengine_expression(node, bindings));
    }
    if (node.kind == NodeKind::Symbol) {
        if (symbol_name(node.text) == variable) {
            return {GrowthClass::Polynomial, 1, std::nullopt,
                    RCP<const Basic>(SymEngine::integer(infinity_sign))};
        }
        if (const auto binding = bindings.find(node.text); binding != bindings.end()) {
            return asymptotic_info(binding->second, variable, infinity_sign, bindings);
        }
        return finite_asymptotic(symengine_expression(node, bindings));
    }
    if (node.kind == NodeKind::Unary && node.children.size() == 1) {
        auto child = asymptotic_info(node.children.front(), variable, infinity_sign, bindings);
        return node.text == "-" ? negate_asymptotic(std::move(child)) : child;
    }
    if (node.kind == NodeKind::Call && node.children.size() == 1) {
        auto argument = asymptotic_info(node.children.front(), variable, infinity_sign, bindings);
        if (argument.limit) {
            try {
                const std::string function = node.text == "ln" ? "log" : node.text;
                return finite_asymptotic(SymEngine::parse(
                    function + "(" + SymEngine::str(**argument.limit) + ")"));
            } catch (...) {
                return {};
            }
        }
        if (node.text == "sin" || node.text == "cos" || node.text == "tanh") {
            return {GrowthClass::Bounded, 0, std::nullopt, std::nullopt};
        }
        if (node.text == "ln" || node.text == "log") {
            if (growth_rank(argument) > 0) {
                return {GrowthClass::Logarithmic, 0, std::nullopt,
                        RCP<const Basic>(SymEngine::integer(1))};
            }
            return {};
        }
        if (node.text == "exp" && argument.leading) {
            const int sign = numeric_sign(*argument.leading);
            if (sign < 0) return finite_asymptotic(RCP<const Basic>(SymEngine::integer(0)));
            if (sign > 0) {
                return {GrowthClass::Exponential, 0, std::nullopt,
                        RCP<const Basic>(SymEngine::integer(1))};
            }
        }
        return {};
    }
    if (node.kind != NodeKind::Binary || node.children.size() != 2) return {};

    auto left = asymptotic_info(node.children[0], variable, infinity_sign, bindings);
    auto right = asymptotic_info(node.children[1], variable, infinity_sign, bindings);
    if (node.text == "+" || node.text == "-") {
        if (left.limit && right.limit) {
            return finite_asymptotic(node.text == "+"
                ? SymEngine::add(*left.limit, *right.limit)
                : SymEngine::sub(*left.limit, *right.limit));
        }
        const int comparison = compare_growth(left, right);
        if (comparison > 0) return left;
        if (comparison < 0) return node.text == "-" ? negate_asymptotic(std::move(right)) : right;
        if (left.growth == GrowthClass::Polynomial && left.leading && right.leading) {
            auto leading = node.text == "+"
                ? SymEngine::add(*left.leading, *right.leading)
                : SymEngine::sub(*left.leading, *right.leading);
            if (!SymEngine::is_number_and_zero(*leading)) {
                return {GrowthClass::Polynomial, left.degree, std::nullopt, leading};
            }
        }
        return left.growth == GrowthClass::Bounded
            ? AsymptoticInfo{GrowthClass::Bounded, 0, std::nullopt, std::nullopt}
            : AsymptoticInfo{};
    }
    if (node.text == "^") {
        const Node *exponent_node = &node.children[1];
        int exponent_sign = 1;
        if (exponent_node->kind == NodeKind::Unary && exponent_node->text == "-" &&
            exponent_node->children.size() == 1) {
            exponent_sign = -1;
            exponent_node = &exponent_node->children.front();
        }
        if (exponent_node->kind != NodeKind::Integer) return {};
        int exponent = 0;
        const auto [end, error] = std::from_chars(exponent_node->text.data(),
            exponent_node->text.data() + exponent_node->text.size(), exponent);
        if (error != std::errc{} || end != exponent_node->text.data() + exponent_node->text.size()) return {};
        exponent *= exponent_sign;
        if (left.limit) {
            if (exponent < 0 && SymEngine::is_number_and_zero(**left.limit)) return {};
            return finite_asymptotic(SymEngine::pow(*left.limit, SymEngine::integer(exponent)));
        }
        if (exponent == 0) return finite_asymptotic(RCP<const Basic>(SymEngine::integer(1)));
        if (exponent < 0 && growth_rank(left) > 0) {
            return finite_asymptotic(RCP<const Basic>(SymEngine::integer(0)));
        }
        if (exponent > 0 && left.growth == GrowthClass::Polynomial) {
            return {GrowthClass::Polynomial, left.degree * exponent, std::nullopt,
                    left.leading ? std::optional<RCP<const Basic>>(
                        SymEngine::pow(*left.leading, SymEngine::integer(exponent))) : std::nullopt};
        }
        return {};
    }
    if (node.text == "frac" || node.text == "/") {
        if (right.limit && !SymEngine::is_number_and_zero(**right.limit)) {
            if (left.limit) return finite_asymptotic(SymEngine::div(*left.limit, *right.limit));
            if (left.leading) left.leading = SymEngine::div(*left.leading, *right.limit);
            return left;
        }
        const int comparison = compare_growth(left, right);
        if (comparison < 0) return finite_asymptotic(RCP<const Basic>(SymEngine::integer(0)));
        if (comparison == 0 && left.growth == GrowthClass::Polynomial &&
            left.leading && right.leading) {
            return finite_asymptotic(SymEngine::div(*left.leading, *right.leading));
        }
        if (comparison > 0 && left.growth == GrowthClass::Polynomial &&
            right.growth == GrowthClass::Polynomial && left.leading && right.leading) {
            return {GrowthClass::Polynomial, left.degree - right.degree, std::nullopt,
                    SymEngine::div(*left.leading, *right.leading)};
        }
        return {};
    }

    if (left.limit && right.limit) {
        return finite_asymptotic(SymEngine::mul(*left.limit, *right.limit));
    }
    if (left.limit && right.growth == GrowthClass::Bounded &&
        SymEngine::is_number_and_zero(**left.limit)) return left;
    if (right.limit && left.growth == GrowthClass::Bounded &&
        SymEngine::is_number_and_zero(**right.limit)) return right;
    if (left.limit) {
        if (right.leading) right.leading = SymEngine::mul(*left.limit, *right.leading);
        return right;
    }
    if (right.limit) {
        if (left.leading) left.leading = SymEngine::mul(*left.leading, *right.limit);
        return left;
    }
    if (left.growth == GrowthClass::Polynomial && right.growth == GrowthClass::Polynomial) {
        return {GrowthClass::Polynomial, left.degree + right.degree, std::nullopt,
                left.leading && right.leading
                    ? std::optional<RCP<const Basic>>(SymEngine::mul(*left.leading, *right.leading))
                    : std::nullopt};
    }
    return growth_rank(left) >= growth_rank(right) ? left : right;
}

bool finite_backend_value(const RCP<const Basic> &value) {
    const std::string rendered = SymEngine::str(*value);
    return rendered != "nan" && rendered != "NaN" &&
           rendered.find("ComplexInf") == std::string::npos &&
           rendered.find("oo") == std::string::npos;
}

/**
 * Evaluate a finite or infinite symbolic limit without leaking backend failures.
 *
 * Args:
 *     root: Limit AST.
 *     bindings: Request and context substitutions.
 *     max_iterations: Derivative budget for finite indeterminate forms.
 *     deadline: Absolute cooperative deadline.
 * Returns:
 *     Exact limit, or no value when the available rules cannot prove one.
 */
std::optional<RCP<const Basic>> analytic_limit(
    const Node &root, const std::map<std::string, Node> &bindings,
    uint32_t max_iterations, std::chrono::steady_clock::time_point deadline) {
    if (root.children.size() != 2) return std::nullopt;
    try {
        const auto direction_marker = root.text.find(':');
        const auto variable_name = root.text.substr(0, direction_marker);
        const auto direction = direction_marker == std::string::npos
                                   ? std::string{} : root.text.substr(direction_marker + 1);
        const auto variable = SymEngine::symbol(symbol_name(variable_name));
        const auto target = SymEngine::parse(to_backend_syntax(root.children[0]));
        const std::string target_text = SymEngine::str(*target);
        const Node &body = root.children[1];
        if (target_text.find("oo") != std::string::npos) {
            auto info = asymptotic_info(body, symbol_name(variable_name),
                                        target_text.starts_with("-") ? -1 : 1, bindings);
            if (info.limit) return info.limit;
            if (info.leading && growth_rank(info) > 0) {
                const int sign = numeric_sign(*info.leading);
                if (sign != 0) return RCP<const Basic>(SymEngine::infty(sign));
            }
            return std::nullopt;
        }

        auto expression = symengine_expression(body, bindings);
        auto direct = expression->subs({{variable, target}});
        if (finite_backend_value(direct)) return direct;
        if (body.kind != NodeKind::Binary || (body.text != "frac" && body.text != "/")) {
            return std::nullopt;
        }

        auto numerator = symengine_expression(body.children[0], bindings);
        auto denominator = symengine_expression(body.children[1], bindings);
        for (uint32_t order = 0; order < max_iterations; ++order) {
            if (std::chrono::steady_clock::now() >= deadline) return std::nullopt;
            const auto numerator_value = numerator->subs({{variable, target}});
            const auto denominator_value = denominator->subs({{variable, target}});
            const bool numerator_zero = SymEngine::is_number_and_zero(*numerator_value);
            const bool denominator_zero = SymEngine::is_number_and_zero(*denominator_value);
            const bool infinity_over_infinity =
                !finite_backend_value(numerator_value) && !finite_backend_value(denominator_value);
            if (!denominator_zero && !infinity_over_infinity) {
                const auto quotient = SymEngine::div(numerator_value, denominator_value);
                return finite_backend_value(quotient)
                           ? std::optional<RCP<const Basic>>(quotient) : std::nullopt;
            }
            if (denominator_zero && !numerator_zero) {
                auto local_denominator = denominator;
                for (uint32_t zero_order = 1; zero_order <= max_iterations - order; ++zero_order) {
                    local_denominator = local_denominator->diff(variable);
                    const auto coefficient = local_denominator->subs({{variable, target}});
                    if (SymEngine::is_number_and_zero(*coefficient)) continue;
                    if (direction.empty() && zero_order % 2 != 0) return std::nullopt;
                    const int side = direction == "-" && zero_order % 2 != 0 ? -1 : 1;
                    const int sign = numeric_sign(numerator_value) * numeric_sign(coefficient) * side;
                    return sign == 0 ? std::nullopt
                                     : std::optional<RCP<const Basic>>(SymEngine::infty(sign));
                }
                return std::nullopt;
            }
            if (!(numerator_zero && denominator_zero) && !infinity_over_infinity) {
                return std::nullopt;
            }
            numerator = numerator->diff(variable);
            denominator = denominator->diff(variable);
        }
    } catch (...) {
    }
    return std::nullopt;
}

struct NumericIntegrand {
    RCP<const Basic> expression;
    RCP<const SymEngine::Symbol> variable;
    bool failed = false;
};

double numeric_integrand(double value, void *raw) noexcept {
    auto &data = *static_cast<NumericIntegrand *>(raw);
    try {
        return SymEngine::eval_double(*data.expression->subs({{data.variable, SymEngine::real_double(value)}}));
    } catch (...) {
        data.failed = true;
        return std::numeric_limits<double>::quiet_NaN();
    }
}

Evaluation numeric_integral(const Node &root, const std::map<std::string, Node> &bindings,
                            int32_t backend) {
    if (root.children.size() != 3 || root.text.starts_with("iint")) {
        return failure(TEXSOLVE_STATUS_NO_ANALYTIC_SOLUTION, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                       "numeric integration requires one finite interval");
    }
    const auto separator = root.text.find(':');
    NumericIntegrand data{symengine_expression(root.children[2], bindings),
                          SymEngine::symbol(symbol_name(root.text.substr(separator + 1))), false};
    const double lower = SymEngine::eval_double(*symengine_expression(root.children[0], bindings));
    const double upper = SymEngine::eval_double(*symengine_expression(root.children[1], bindings));
    double value = 0.0;
    double error = 0.0;
    std::string backend_name;
    if (backend == TEXSOLVE_INTEGRATION_BOOST_MATH) {
        value = boost::math::quadrature::gauss_kronrod<double, 61>::integrate(
            [&](double x) { return numeric_integrand(x, &data); }, lower, upper, 15, 1e-12, &error);
        backend_name = "boost_math";
    } else {
        static std::once_flag gsl_error_handler;
        std::call_once(gsl_error_handler, [] { gsl_set_error_handler_off(); });
        std::unique_ptr<gsl_integration_workspace, decltype(&gsl_integration_workspace_free)> workspace(
            gsl_integration_workspace_alloc(1000), gsl_integration_workspace_free);
        if (!workspace) return failure(TEXSOLVE_STATUS_INTERNAL_ERROR, TEXSOLVE_DIAGNOSTIC_INTERNAL_EXCEPTION,
                                       "GSL workspace allocation failed", "gsl");
        gsl_function function{numeric_integrand, &data};
        const int status = gsl_integration_qag(&function, lower, upper, 1e-12, 1e-12, 1000,
                                               GSL_INTEG_GAUSS61, workspace.get(), &value, &error);
        if (data.failed) return failure(TEXSOLVE_STATUS_NOT_CONVERGED,
            TEXSOLVE_DIAGNOSTIC_NUMERICAL_FAILURE, "numeric integrand evaluation failed", "gsl");
        if (status != GSL_SUCCESS) return failure(TEXSOLVE_STATUS_NOT_CONVERGED,
            TEXSOLVE_DIAGNOSTIC_NUMERICAL_FAILURE, gsl_strerror(status), "gsl");
        backend_name = "gsl";
    }
    if (data.failed || !std::isfinite(value)) return failure(TEXSOLVE_STATUS_NOT_CONVERGED,
        TEXSOLVE_DIAGNOSTIC_NUMERICAL_FAILURE, "numeric integrand evaluation failed", backend_name);
    Evaluation result;
    result.kind = TEXSOLVE_RESULT_REAL;
    result.name = "value";
    result.precision_digits = 15;
    std::ostringstream rendered;
    rendered << std::setprecision(15) << value;
    result.approximation = rendered.str();
    result.backend = std::move(backend_name);
    std::ostringstream error_text;
    error_text << std::setprecision(15) << error;
    result.error_estimate = error_text.str();
    return result;
}

Evaluation failure(int32_t status, int32_t code, std::string message, std::string backend) {
    Evaluation result;
    result.status = status;
    result.diagnostic_code = code;
    result.message = std::move(message);
    result.backend = std::move(backend);
    return result;
}

/** Return whether a scalar AST contains a finite sum or product node. */
bool contains_fold(const Node &node) {
    if (node.kind == NodeKind::Fold) return true;
    return std::any_of(node.children.begin(), node.children.end(), contains_fold);
}

bool contains_factorial(const Node &node) {
    if (node.kind == NodeKind::Call && node.text == "factorial") return true;
    return std::any_of(node.children.begin(), node.children.end(), contains_factorial);
}

/**
 * Convert a scalar AST to SymEngine while evaluating bounded discrete operations.
 *
 * Args:
 *     node: Scalar AST node to convert.
 *     bindings: Request and context substitutions.
 *     locals: Lexically scoped fold indices.
 *     max_iterations: Shared finite-fold iteration budget.
 *     iterations: Iterations consumed by this expression.
 *     deadline: Absolute cooperative deadline.
 *     error: Failure result populated when conversion fails.
 * Returns:
 *     Converted scalar expression, or no value on failure.
 */
std::optional<RCP<const Basic>> scalar_expression(
    const Node &node, const std::map<std::string, Node> &bindings,
    const std::map<std::string, RCP<const Basic>> &locals, uint32_t max_iterations,
    uint64_t &iterations, std::chrono::steady_clock::time_point deadline,
    Evaluation &error) {
    if (std::chrono::steady_clock::now() >= deadline) {
        error = failure(TEXSOLVE_STATUS_DEADLINE_EXCEEDED, TEXSOLVE_DIAGNOSTIC_DEADLINE,
                        "finite fold deadline exceeded", "symengine");
        return std::nullopt;
    }
    if (node.kind == NodeKind::Integer) return SymEngine::parse(node.text);
    if (node.kind == NodeKind::Real) return SymEngine::parse(exact_real_syntax(node.text));
    if (node.kind == NodeKind::Symbol) {
        if (const auto local = locals.find(node.text); local != locals.end()) return local->second;
        if (const auto binding = bindings.find(node.text); binding != bindings.end()) {
            return SymEngine::parse(to_backend_syntax(binding->second));
        }
        return SymEngine::parse(symbol_name(node.text));
    }
    if (node.kind == NodeKind::Fold) {
        if (node.children.size() != 3) {
            error = failure(TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_INCOMPLETE_PROBLEM,
                            "finite fold is incomplete", "symengine");
            return std::nullopt;
        }
        auto lower_value = scalar_expression(node.children[0], bindings, locals, max_iterations,
                                             iterations, deadline, error);
        auto upper_value = scalar_expression(node.children[1], bindings, locals, max_iterations,
                                             iterations, deadline, error);
        if (!lower_value || !upper_value) return std::nullopt;
        const auto exact_integer = [](const RCP<const Basic> &value, int64_t &output) {
            if (!SymEngine::is_a<SymEngine::Integer>(*value)) return false;
            const std::string rendered = SymEngine::str(*value);
            const auto [end, error] = std::from_chars(
                rendered.data(), rendered.data() + rendered.size(), output);
            return error == std::errc{} && end == rendered.data() + rendered.size();
        };
        int64_t lower = 0;
        int64_t upper = 0;
        if (!exact_integer(*lower_value, lower) || !exact_integer(*upper_value, upper)) {
            error = failure(TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_DOMAIN_ERROR,
                            "finite fold bounds must evaluate to integers", "symengine");
            return std::nullopt;
        }
        const long double count = static_cast<long double>(upper) - static_cast<long double>(lower) + 1.0L;
        if (upper < lower || iterations >= max_iterations ||
            count > static_cast<long double>(max_iterations - iterations)) {
            error = failure(TEXSOLVE_STATUS_RESOURCE_LIMIT, TEXSOLVE_DIAGNOSTIC_ITERATION_LIMIT,
                            "finite fold iteration limit exceeded", "symengine");
            return std::nullopt;
        }
        const auto separator = node.text.find(':');
        const std::string variable = separator == std::string::npos ? std::string{} : node.text.substr(separator + 1);
        auto accumulator = node.text.starts_with("product") ? RCP<const Basic>(SymEngine::integer(1))
                                                            : RCP<const Basic>(SymEngine::integer(0));
        auto fold_locals = locals;
        for (int64_t value = lower;; ++value) {
            ++iterations;
            fold_locals[variable] = SymEngine::parse(std::to_string(value));
            auto term = scalar_expression(node.children[2], bindings, fold_locals, max_iterations,
                                          iterations, deadline, error);
            if (!term) return std::nullopt;
            accumulator = node.text.starts_with("product")
                              ? SymEngine::mul(accumulator, *term)
                              : SymEngine::add(accumulator, *term);
            if (value == upper) break;
        }
        return accumulator;
    }
    if (node.kind == NodeKind::Unary && node.children.size() == 1) {
        auto child = scalar_expression(node.children[0], bindings, locals, max_iterations,
                                       iterations, deadline, error);
        if (!child) return std::nullopt;
        return node.text == "-" ? SymEngine::neg(*child) : *child;
    }
    if (node.kind == NodeKind::Binary && node.children.size() == 2) {
        auto left = scalar_expression(node.children[0], bindings, locals, max_iterations,
                                      iterations, deadline, error);
        auto right = scalar_expression(node.children[1], bindings, locals, max_iterations,
                                       iterations, deadline, error);
        if (!left || !right) return std::nullopt;
        if (node.text == "+") return SymEngine::add(*left, *right);
        if (node.text == "-") return SymEngine::sub(*left, *right);
        if (node.text == "frac" || node.text == "/") return SymEngine::div(*left, *right);
        if (node.text == "^") return SymEngine::pow(*left, *right);
        return SymEngine::mul(*left, *right);
    }
    if (node.kind == NodeKind::Call) {
        if (node.text == "factorial" && node.children.size() == 1) {
            auto argument = scalar_expression(node.children.front(), bindings, locals, max_iterations,
                                              iterations, deadline, error);
            if (!argument) return std::nullopt;
            if (!SymEngine::is_a<SymEngine::Integer>(**argument)) {
                if (SymEngine::is_a_Number(**argument)) {
                    error = failure(TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_DOMAIN_ERROR,
                                    "factorial requires a nonnegative integer", "symengine");
                    return std::nullopt;
                }
                return SymEngine::parse("factorial(" + SymEngine::str(**argument) + ")");
            }
            const auto &integer = SymEngine::down_cast<const SymEngine::Integer &>(**argument);
            uint64_t value = 0;
            const std::string rendered = SymEngine::str(integer);
            const auto [end, conversion_error] = std::from_chars(
                rendered.data(), rendered.data() + rendered.size(), value);
            if (integer.is_negative()) {
                error = failure(TEXSOLVE_STATUS_SEMANTIC_ERROR, TEXSOLVE_DIAGNOSTIC_DOMAIN_ERROR,
                                "factorial requires a nonnegative integer", "symengine");
                return std::nullopt;
            }
            if (conversion_error != std::errc{} || end != rendered.data() + rendered.size() ||
                value > max_iterations || iterations > max_iterations - value) {
                error = failure(TEXSOLVE_STATUS_RESOURCE_LIMIT, TEXSOLVE_DIAGNOSTIC_ITERATION_LIMIT,
                                "factorial iteration limit exceeded", "symengine");
                return std::nullopt;
            }
            iterations += value;
            return SymEngine::factorial(static_cast<unsigned long>(value));
        }
        if (node.text.starts_with("sqrt:") && node.children.size() == 1) {
            const std::string_view degree(node.text.data() + 5, node.text.size() - 5);
            if (const Node *base = odd_root_power_base(node.children.front(), degree);
                base != nullptr && real_symbol(*base, &bindings)) {
                return scalar_expression(*base, bindings, locals, max_iterations, iterations, deadline, error);
            }
            const Node &radicand = node.children.front();
            if (radicand.kind == NodeKind::Call && radicand.text.starts_with("sqrt:") &&
                radicand.children.size() == 1 && radicand.children.front().kind == NodeKind::Symbol &&
                !bindings.contains(radicand.children.front().text)) {
                auto value = scalar_expression(radicand.children.front(), bindings, locals, max_iterations,
                                               iterations, deadline, error);
                if (!value) return std::nullopt;
                return SymEngine::pow(*value, SymEngine::parse(
                    "1/(" + std::string(degree) + "*" + radicand.text.substr(5) + ")"));
            }
        }
        if (node.text == "sqrt:2" && node.children.size() == 1) {
            const Node &radicand = node.children.front();
            const Node *base = real_symbol_square_base(radicand);
            const Node *numerator = nullptr;
            if (radicand.kind == NodeKind::Binary &&
                (radicand.text == "/" || radicand.text == "frac")) {
                base = real_symbol_square_base(radicand.children[1]);
                numerator = &radicand.children[0];
            }
            if (base != nullptr && !bindings.contains(base->text)) {
                const auto absolute = SymEngine::parse("abs(" + symbol_name(base->text) + ")");
                if (numerator == nullptr) return absolute;
                auto radicand_value = scalar_expression(*numerator, bindings, locals, max_iterations,
                                                        iterations, deadline, error);
                if (!radicand_value) return std::nullopt;
                return SymEngine::div(SymEngine::sqrt(*radicand_value), absolute);
            }
        }
        std::string expression = node.text.starts_with("sqrt:") ? "(" : node.text + "(";
        for (std::size_t index = 0; index < node.children.size(); ++index) {
            auto argument = scalar_expression(node.children[index], bindings, locals, max_iterations,
                                              iterations, deadline, error);
            if (!argument) return std::nullopt;
            if (index != 0) expression += ',';
            expression += SymEngine::str(**argument);
        }
        expression += node.text.starts_with("sqrt:") ? ")^(1/" + node.text.substr(5) + ")" : ")";
        return SymEngine::parse(expression);
    }
    error = failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                    "AST node is not a scalar expression", "symengine");
    return std::nullopt;
}

Evaluation evaluate_fold(const Node &root, bool product, const std::map<std::string, Node> &bindings,
                         uint32_t precision, uint32_t max_iterations,
                         std::chrono::steady_clock::time_point deadline, std::string backend) {
    (void)product;
    uint64_t iterations = 0;
    Evaluation error;
    auto accumulator = scalar_expression(root, bindings, {}, max_iterations, iterations, deadline, error);
    if (!accumulator) return error;
    Evaluation result;
    assign_symengine_scalar(result, **accumulator, precision);
    result.backend = std::move(backend);
    return result;
}

Evaluation evaluate_symengine(const Node &root, int32_t operation,
                              const std::map<std::string, Node> &bindings, uint32_t precision,
                              uint32_t max_iterations,
                              std::chrono::steady_clock::time_point deadline,
                              int32_t integration_backend) {
    const std::string backend = "symengine";
    if (contains_fold(root) || contains_factorial(root)) {
        return evaluate_fold(root, root.text.starts_with("product"), bindings, precision,
                             max_iterations, deadline, backend);
    }
    if (root.kind == NodeKind::Integral) {
        const auto separator = root.text.find(':');
        const std::string variables = separator == std::string::npos ? "x" : root.text.substr(separator + 1);
        const std::string variable = variables.substr(0, variables.find(','));
        const Node &body = root.children.back();
        auto antiderivative = integrate_simple(body, variable);
        if (!antiderivative && root.children.size() == 3) {
            return numeric_integral(root, bindings, integration_backend == TEXSOLVE_INTEGRATION_AUTO
                                                        ? TEXSOLVE_INTEGRATION_GSL : integration_backend);
        }
        if (!antiderivative) return failure(TEXSOLVE_STATUS_NO_ANALYTIC_SOLUTION,
            TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY, "no analytic antiderivative is available", backend);
        if (variables.find(',') != std::string::npos) {
            const auto second = variables.substr(variables.find(',') + 1);
            const auto lower = SymEngine::parse(to_backend_syntax(root.children[0]));
            const auto upper = SymEngine::parse(to_backend_syntax(root.children[1]));
            const auto first_variable = SymEngine::symbol(variable);
            auto primitive_ast = parse_for_debug(*antiderivative, 128, 50000);
            if (!primitive_ast.ok) return failure(TEXSOLVE_STATUS_NO_ANALYTIC_SOLUTION,
                TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY, "multiple integral normalization failed", backend);
            auto primitive = SymEngine::parse(to_backend_syntax(primitive_ast.root));
            const auto bounded = SymEngine::sub(primitive->subs({{first_variable, upper}}),
                                                primitive->subs({{first_variable, lower}}));
            const auto second_variable = SymEngine::symbol(second);
            const auto second_lower = SymEngine::parse(to_backend_syntax(root.children[0]));
            const auto second_upper = SymEngine::parse(to_backend_syntax(root.children[1]));
            auto bounded_latex = SymEngine::latex(*bounded);
            bounded_latex.erase(std::remove_if(bounded_latex.begin(), bounded_latex.end(), [](unsigned char ch) {
                return std::isspace(ch);
            }), bounded_latex.end());
            auto bounded_ast = parse_for_debug(bounded_latex, 128, 50000);
            if (!bounded_ast.ok) return failure(TEXSOLVE_STATUS_NO_ANALYTIC_SOLUTION,
                TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                "multiple integral normalization failed: " + bounded_latex, backend);
            auto second_integral = integrate_simple(bounded_ast.root, second);
            if (!second_integral) return failure(TEXSOLVE_STATUS_NO_ANALYTIC_SOLUTION,
                TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY, "no analytic multiple integral is available", backend);
            auto second_ast = parse_for_debug(*second_integral, 128, 50000);
            if (!second_ast.ok) return failure(TEXSOLVE_STATUS_NO_ANALYTIC_SOLUTION,
                TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY, "multiple integral normalization failed", backend);
            const auto second_primitive = SymEngine::parse(to_backend_syntax(second_ast.root));
            const auto value = SymEngine::sub(second_primitive->subs({{second_variable, second_upper}}),
                                              second_primitive->subs({{second_variable, second_lower}}));
            Evaluation result;
            assign_symengine_scalar(result, *value, precision);
            result.backend = backend;
            return result;
        }
        Evaluation result;
        result.kind = TEXSOLVE_RESULT_SYMBOLIC;
        result.exact = *antiderivative;
        result.backend = backend;
        if (root.children.size() == 3 && variables.find(',') == std::string::npos) {
            auto primitive = SymEngine::parse(to_backend_syntax(body));
            const auto symbol = SymEngine::symbol(variable);
            const auto upper = SymEngine::parse(to_backend_syntax(root.children[1]));
            const auto lower = SymEngine::parse(to_backend_syntax(root.children[0]));
            const auto integrated = SymEngine::parse(to_backend_syntax(parse_for_debug(*antiderivative, 128, 50000).root));
            primitive = SymEngine::sub(integrated->subs({{symbol, upper}}), integrated->subs({{symbol, lower}}));
            assign_symengine_scalar(result, *primitive, precision);
        }
        return result;
    }
    if (root.kind == NodeKind::Limit) {
        const auto limited = analytic_limit(root, bindings, max_iterations, deadline);
        if (!limited) return failure(TEXSOLVE_STATUS_NO_ANALYTIC_SOLUTION,
            TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY, "no analytic limit is available", backend);
        Evaluation result;
        assign_symengine_scalar(result, **limited, precision);
        result.backend = backend;
        return result;
    }

    const Node *expression_node = &root;
    std::vector<std::string> derivative_variables;
    if (root.kind == NodeKind::Derivative) {
        expression_node = &root.children.front();
        derivative_variables = variables_from_derivative(root.text);
    }
    RCP<const Basic> expression;
    if (contains_fold(*expression_node)) {
        uint64_t iterations = 0;
        Evaluation error;
        auto expanded = scalar_expression(*expression_node, bindings, {}, max_iterations,
                                          iterations, deadline, error);
        if (!expanded) return error;
        expression = *expanded;
    } else {
        expression = symengine_expression(*expression_node, bindings);
    }
    if (root.kind == NodeKind::Derivative || operation == TEXSOLVE_OPERATION_DIFFERENTIATE) {
        if (derivative_variables.empty()) derivative_variables.push_back("x");
        for (const auto &variable : derivative_variables) {
            expression = expression->diff(SymEngine::symbol(variable));
        }
    } else if (operation == TEXSOLVE_OPERATION_EXPAND) {
        expression = SymEngine::expand(expression);
    }
    Evaluation result;
    assign_symengine_scalar(result, *expression, precision);
    result.backend = backend;
    return result;
}

Evaluation evaluate_ginac(const Node &root, int32_t operation,
                          const std::map<std::string, Node> &bindings, uint32_t precision,
                          uint32_t max_iterations,
                          std::chrono::steady_clock::time_point deadline,
                          int32_t integration_backend) {
    // GiNaC::Digits is process-global, so the lock covers precision selection and evaluation.
    static std::mutex ginac_mutex;
    const std::lock_guard lock(ginac_mutex);
    if (root.kind == NodeKind::Limit) {
        return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED,
                       TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                       "GiNaC limit evaluation is unavailable", "ginac");
    }
    if (contains_fold(root) || root.kind == NodeKind::Integral) {
        auto result = evaluate_symengine(root, operation, bindings, precision, max_iterations,
                                         deadline, integration_backend);
        result.backend = "ginac";
        return result;
    }
    const Node *expression_node = &root;
    std::vector<std::string> derivative_variables;
    if (root.kind == NodeKind::Derivative) {
        expression_node = &root.children.front();
        derivative_variables = variables_from_derivative(root.text);
    }
    GiNaC::parser parser;
    GiNaC::ex expression = parser(to_backend_syntax(*expression_node, &bindings));
    for (const auto &[name, value] : bindings) {
        expression = expression.subs(parser(symbol_name(name)) == parser(to_backend_syntax(value)));
    }
    if (root.kind == NodeKind::Derivative || operation == TEXSOLVE_OPERATION_DIFFERENTIATE) {
        if (derivative_variables.empty()) derivative_variables.push_back("x");
        for (const auto &variable : derivative_variables) {
            const auto symbol_expression = parser(variable);
            if (!GiNaC::is_a<GiNaC::symbol>(symbol_expression)) {
                throw std::runtime_error("invalid derivative variable");
            }
            expression = expression.diff(GiNaC::ex_to<GiNaC::symbol>(symbol_expression));
        }
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
    result.kind = TEXSOLVE_RESULT_SYMBOLIC;
    if (GiNaC::is_a<GiNaC::numeric>(expression)) {
        const auto &number = GiNaC::ex_to<GiNaC::numeric>(expression);
        result.kind = number.is_integer() ? TEXSOLVE_RESULT_INTEGER
                    : number.is_rational() ? TEXSOLVE_RESULT_RATIONAL
                    : number.is_real() ? TEXSOLVE_RESULT_REAL : TEXSOLVE_RESULT_COMPLEX;
        if (result.kind == TEXSOLVE_RESULT_COMPLEX) {
            std::ostringstream real;
            std::ostringstream imag;
            real << GiNaC::latex << number.real();
            imag << GiNaC::latex << number.imag();
            result.real_kind = number.real().is_integer() ? TEXSOLVE_RESULT_INTEGER : TEXSOLVE_RESULT_REAL;
            result.imag_kind = number.imag().is_integer() ? TEXSOLVE_RESULT_INTEGER : TEXSOLVE_RESULT_REAL;
            result.real = real.str();
            result.imag = imag.str();
        }
    }
    result.backend = "ginac";
    std::ostringstream approximate;
    GiNaC::Digits = precision;
    approximate << expression.evalf();
    result.approximation = approximate.str();
    return result;
}

}  // namespace

std::string to_backend_syntax(const Node &node,
                              const std::map<std::string, Node> *real_bindings) {
    switch (node.kind) {
        case NodeKind::Integer:
            return node.text;
        case NodeKind::Real:
            return exact_real_syntax(node.text);
        case NodeKind::Symbol: return symbol_name(node.text);
        case NodeKind::Unary: return "(" + node.text + to_backend_syntax(node.children.front(), real_bindings) + ")";
        case NodeKind::Binary: {
            const std::string op = node.text == "frac" ? "/" :
                                   (node.text == "implicit" || node.text == "\\cdot" || node.text == "\\times") ? "*" : node.text;
            return "(" + to_backend_syntax(node.children[0], real_bindings) + op +
                   to_backend_syntax(node.children[1], real_bindings) + ")";
        }
        case NodeKind::Call: {
            if (node.text.starts_with("sqrt:")) {
                const std::string_view degree(node.text.data() + 5, node.text.size() - 5);
                if (const Node *base = odd_root_power_base(node.children.front(), degree);
                    base != nullptr && real_symbol(*base, real_bindings)) {
                    return to_backend_syntax(*base, real_bindings);
                }
                const Node &radicand = node.children.front();
                if (radicand.kind == NodeKind::Call && radicand.text.starts_with("sqrt:") &&
                    radicand.children.size() == 1 && radicand.children.front().kind == NodeKind::Symbol &&
                    real_bindings != nullptr && !real_bindings->contains(radicand.children.front().text)) {
                    return "(" + to_backend_syntax(radicand.children.front(), real_bindings) + ")^(1/(" +
                           std::string(degree) + "*" + radicand.text.substr(5) + "))";
                }
                if (node.text == "sqrt:2") {
                    const Node &radicand = node.children.front();
                    if (const Node *base = real_symbol_square_base(radicand);
                        base != nullptr && real_bindings != nullptr && !real_bindings->contains(base->text)) {
                        return "abs(" + to_backend_syntax(*base, real_bindings) + ")";
                    }
                    if (radicand.kind == NodeKind::Binary &&
                        (radicand.text == "/" || radicand.text == "frac")) {
                        if (const Node *base = real_symbol_square_base(radicand.children[1]);
                            base != nullptr && real_bindings != nullptr && !real_bindings->contains(base->text)) {
                            return "((" + to_backend_syntax(radicand.children[0], real_bindings) +
                                   ")^(1/2)/abs(" + to_backend_syntax(*base, real_bindings) + "))";
                        }
                    }
                }
                return "(" + to_backend_syntax(node.children.front(), real_bindings) + ")^(1/" +
                       node.text.substr(5) + ")";
            }
            std::string result = node.text + "(";
            for (std::size_t index = 0; index < node.children.size(); ++index) {
                if (index != 0) result += ',';
                result += to_backend_syntax(node.children[index], real_bindings);
            }
            return result + ')';
        }
        default: throw std::invalid_argument("AST node is not a scalar expression");
    }
}

Evaluation evaluate(const Node &root, int32_t operation, int32_t symbolic_backend,
                    const std::map<std::string, Node> &bindings, uint32_t precision_digits,
                    uint32_t max_iterations, uint32_t deadline_ms, int32_t integration_backend) {
    try {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(deadline_ms);
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
            return evaluate_symengine(root, operation, bindings, precision_digits, max_iterations,
                                      deadline, integration_backend);
        }
        if (selected == TEXSOLVE_SYMBOLIC_GINAC) {
            return evaluate_ginac(root, operation, bindings, precision_digits, max_iterations,
                                  deadline, integration_backend);
        }
        return failure(TEXSOLVE_STATUS_INVALID_ARGUMENT, TEXSOLVE_DIAGNOSTIC_BACKEND_MISSING,
                       "unknown symbolic backend");
    } catch (const std::exception &error) {
        return failure(TEXSOLVE_STATUS_BACKEND_UNSUPPORTED, TEXSOLVE_DIAGNOSTIC_BACKEND_CAPABILITY,
                       error.what(), symbolic_backend == TEXSOLVE_SYMBOLIC_GINAC ? "ginac" : "symengine");
    }
}

}  // namespace texsolve
