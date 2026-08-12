#include "internal.hpp"

#include <boost/spirit/home/x3.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace texsolve {
namespace {

namespace x3 = boost::spirit::x3;

constexpr std::string_view kGreek[] = {
    "alpha", "beta", "gamma", "delta", "epsilon", "theta", "lambda", "mu",
    "rho", "sigma", "tau", "phi", "chi", "psi", "omega", "Gamma", "Delta",
    "Theta", "Lambda", "Sigma", "Phi", "Psi", "Omega"};

constexpr std::string_view kSimpleFunctions[] = {
    "sin", "cos", "tan", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh",
    "exp", "ln", "abs", "floor", "ceil"};

const char *kind_name(NodeKind kind) {
    switch (kind) {
        case NodeKind::Integer: return "Integer";
        case NodeKind::Real: return "Real";
        case NodeKind::Symbol: return "Symbol";
        case NodeKind::Unary: return "Unary";
        case NodeKind::Binary: return "Binary";
        case NodeKind::Call: return "Call";
        case NodeKind::Definition: return "Definition";
        case NodeKind::Relation: return "Relation";
        case NodeKind::Matrix: return "Matrix";
        case NodeKind::Derivative: return "Derivative";
        case NodeKind::Integral: return "Integral";
        case NodeKind::Limit: return "Limit";
        case NodeKind::Fold: return "Fold";
        case NodeKind::Optimization: return "Optimization";
        case NodeKind::Ode: return "Ode";
    }
    return "Unknown";
}

std::string escape_payload(std::string_view value) {
    std::string result;
    for (const char ch : value) {
        if (ch == '\\' || ch == '"') result.push_back('\\');
        result.push_back(ch);
    }
    return result;
}

void render_node(const Node &node, int indent, std::string &out) {
    out.append(static_cast<std::size_t>(indent) * 2, ' ');
    out += kind_name(node.kind);
    out += " [" + std::to_string(node.begin) + ',' + std::to_string(node.end) + ")";
    if (!node.text.empty()) out += " " + escape_payload(node.text);
    out.push_back('\n');
    for (const auto &child : node.children) render_node(child, indent + 1, out);
}

class Parser {
public:
    Parser(std::string_view input, uint32_t max_depth, uint32_t max_nodes, std::size_t offset = 0,
           bool allow_ode_clause = false)
        : input_(input), max_depth_(max_depth), max_nodes_(max_nodes), offset_(offset),
          allow_ode_clause_(allow_ode_clause) {}

    ParseOutput run() {
        ParseOutput output;
        if (!is_valid_utf8(input_)) {
            output.message = "invalid UTF-8";
            output.diagnostic_code = 1;
            return output;
        }
        if (input_.empty()) {
            fail("empty input", 0);
        } else if (input_.find("\\newcommand") != std::string_view::npos ||
                   input_.find("\\def") != std::string_view::npos ||
                   input_.find("\\input") != std::string_view::npos ||
                   input_.find("\\include") != std::string_view::npos ||
                   input_.find("\\usepackage") != std::string_view::npos) {
            fail("forbidden TeX command", input_.find('\\'), 2);
        }
        if (!error_) {
            skip_space();
            auto root = parse_request();
            skip_space();
            if (root && pos_ != input_.size()) fail("trailing input", pos_, 4);
            if (root && !error_) {
                output.ok = true;
                output.root = std::move(*root);
                render_node(output.root, 0, output.ast);
                output.node_count = node_count_;
                return output;
            }
        }
        output.message = message_;
        output.error_begin = offset_ + error_pos_;
        output.error_end = offset_ + std::min(input_.size(), error_pos_ + 1);
        output.diagnostic_code = diagnostic_code_;
        output.node_count = node_count_;
        return output;
    }

private:
    std::optional<Node> parse_request() {
        if (starts("\\begin{cases}")) return parse_cases();
        if (starts("\\min_") || starts("\\max_")) return parse_optimization();

        const std::size_t start = pos_;
        auto left = parse_expression();
        if (!left) return std::nullopt;
        skip_space();
        if (consume(":")) {
            if (!consume("=")) return fail_node("expected '=' after ':'", pos_);
            if (left->kind != NodeKind::Symbol && left->kind != NodeKind::Call) {
                return fail_node("invalid definition target", start);
            }
            if (left->kind == NodeKind::Call) {
                std::vector<std::string> names;
                for (const auto &argument : left->children) {
                    if (argument.kind != NodeKind::Symbol ||
                        std::find(names.begin(), names.end(), argument.text) != names.end()) {
                        return fail_node("duplicate or invalid function parameter", argument.begin, 7);
                    }
                    names.push_back(argument.text);
                }
            }
            auto value = parse_expression();
            if (!value) return std::nullopt;
            return make(NodeKind::Definition, ":=", start, value->end,
                        {std::move(*left), std::move(*value)});
        }

        auto relation = parse_relation_tail(std::move(*left), start);
        if (!relation) return std::nullopt;
        if (relation->kind == NodeKind::Relation && relation->children.front().kind == NodeKind::Derivative) {
            if (allow_ode_clause_) return relation;
            std::vector<Node> clauses;
            clauses.push_back(std::move(*relation));
            bool has_interval = false;
            while (consume(",")) {
                consume_spacing();
                const std::size_t clause_start = pos_;
                const auto variable = parse_symbol_text();
                if (!variable) return fail_node("invalid ODE clause", pos_);
                if (consume("\\in[")) {
                    auto lower = parse_expression();
                    if (!lower || !consume(",")) return fail_node("invalid ODE interval", pos_);
                    auto upper = parse_expression();
                    if (!upper || !consume("]")) return fail_node("invalid ODE interval", pos_);
                    clauses.push_back(std::move(*lower));
                    clauses.push_back(std::move(*upper));
                    has_interval = true;
                    continue;
                }
                pos_ = clause_start;
                auto clause_left = parse_expression();
                if (!clause_left) return std::nullopt;
                const std::size_t relation_start = clause_left->begin;
                auto clause = parse_relation_tail(std::move(*clause_left), relation_start);
                if (!clause || clause->kind != NodeKind::Relation) {
                    return fail_node("ODE initial value must be a relation", clause_start, 12);
                }
                clauses.push_back(std::move(*clause));
            }
            if (!has_interval || clauses.size() < 4) {
                return fail_node("ODE requires an interval and initial values", start, 12);
            }
            if (!complete_ode_initials(clauses)) {
                return fail_node("ODE requires every initial derivative value", start, 12);
            }
            return make(NodeKind::Ode, "ivp", start, pos_, std::move(clauses));
        }
        return relation;
    }

    std::optional<Node> parse_relation_tail(Node left, std::size_t start) {
        skip_space();
        const auto op = relation_operator();
        if (!op) return left;
        if (*op == "=" && starts("=")) return fail_node("duplicate relation operator", pos_);
        auto right = parse_expression();
        if (!right) return std::nullopt;
        return make(NodeKind::Relation, *op, start, right->end,
                    {std::move(left), std::move(*right)});
    }

    std::optional<Node> parse_expression() { return parse_sum(); }

    std::optional<Node> parse_sum() {
        auto left = parse_product();
        if (!left) return std::nullopt;
        while (true) {
            skip_space();
            const std::size_t start = left->begin;
            std::string op;
            if (consume("+")) op = "+";
            else if (consume("-")) op = "-";
            else break;
            auto right = parse_product();
            if (!right) return fail_node("missing operand", pos_);
            left = make(NodeKind::Binary, op, start, right->end,
                        {std::move(*left), std::move(*right)});
        }
        return left;
    }

    std::optional<Node> parse_product() {
        auto left = parse_prefix();
        if (!left) return std::nullopt;
        while (true) {
            const bool had_space = offset_ + pos_ > left->end;
            skip_space();
            std::string op;
            if (consume("\\cdot")) op = "\\cdot";
            else if (consume("\\times")) op = "\\times";
            else if (consume("/")) op = "/";
            else if (starts_primary() && (!had_space || starts("\\") || starts("("))) op = "implicit";
            else break;
            auto right = parse_prefix();
            if (!right) return fail_node("missing factor", pos_);
            left = make(NodeKind::Binary, op, left->begin, right->end,
                        {std::move(*left), std::move(*right)});
        }
        return left;
    }

    std::optional<Node> parse_prefix() {
        skip_space();
        const std::size_t start = pos_;
        if (consume("+") || consume("-")) {
            const std::string op(1, input_[start]);
            if (++depth_ > max_depth_) {
                --depth_;
                return fail_node("nesting limit exceeded", start, 15);
            }
            auto child = parse_prefix();
            --depth_;
            if (!child) return fail_node("missing unary operand", pos_);
            return make(NodeKind::Unary, op, start, child->end, {std::move(*child)});
        }
        return parse_power();
    }

    std::optional<Node> parse_power() {
        auto base = parse_postfix();
        if (!base) return std::nullopt;
        skip_space();
        if (limit_target_ && (starts("^+") || starts("^-"))) return base;
        if (!consume("^")) return base;
        if (++depth_ > max_depth_) {
            --depth_;
            return fail_node("nesting limit exceeded", pos_, 15);
        }
        auto exponent = parse_prefix_or_group();
        --depth_;
        if (!exponent) return fail_node("missing exponent", pos_);
        if (exponent->kind == NodeKind::Symbol && exponent->text == "T" &&
            (base->kind == NodeKind::Integer || base->kind == NodeKind::Real)) {
            return fail_node("transpose requires a matrix expression", base->begin, 9);
        }
        return make(NodeKind::Binary, "^", base->begin, exponent->end,
                    {std::move(*base), std::move(*exponent)});
    }

    std::optional<Node> parse_prefix_or_group() {
        skip_space();
        if (starts("{")) return parse_group('{', '}');
        return parse_prefix();
    }

    std::optional<Node> parse_postfix() {
        auto value = parse_primary();
        if (!value) return std::nullopt;
        while (consume("!")) {
            if (value->kind == NodeKind::Real ||
                (value->kind == NodeKind::Unary && value->text == "-")) {
                return fail_node("factorial requires a non-negative integer", value->begin, 9);
            }
            value = make(NodeKind::Call, "factorial", value->begin, pos_, {std::move(*value)});
        }
        return value;
    }

    std::optional<Node> parse_primary() {
        skip_space();
        const std::size_t start = pos_;
        if (pos_ >= input_.size()) return fail_node("expected expression", pos_);
        if (starts("\\left|")) return parse_absolute();
        if (starts("\\left(")) return parse_left_group();
        if (input_[pos_] == '{') return parse_group('{', '}');
        if (input_[pos_] == '(') return parse_group('(', ')');
        if (input_[pos_] == '\\') return parse_command();
        if (std::isdigit(static_cast<unsigned char>(input_[pos_]))) return parse_number();
        if (std::isalpha(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
            auto symbol = make(NodeKind::Symbol, std::string(input_.substr(start, 1)), start, pos_);
            if (!symbol) return std::nullopt;
            while (consume("'")) symbol->text.push_back('\'');
            if (starts("(")) return parse_call(std::move(*symbol));
            return symbol;
        }
        return fail_node("unexpected token", pos_);
    }

    std::optional<Node> parse_number() {
        const std::size_t start = pos_;
        auto first = input_.begin() + static_cast<std::ptrdiff_t>(pos_);
        const auto last = input_.end();
        boost::iterator_range<std::string_view::const_iterator> raw;
        const auto grammar = x3::raw[+x3::digit >> -(x3::lit('.') >> +x3::digit) >>
                                     -(x3::char_("eE") >> -x3::char_("+-") >> +x3::digit)];
        if (!x3::parse(first, last, grammar, raw)) return fail_node("invalid number", start);
        pos_ += static_cast<std::size_t>(std::distance(raw.begin(), raw.end()));
        if (pos_ < input_.size() && (input_[pos_] == '.' || input_[pos_] == 'e' || input_[pos_] == 'E')) {
            return fail_node("invalid number", pos_);
        }
        const std::string text(input_.substr(start, pos_ - start));
        return make(text.find_first_of(".eE") == std::string::npos ? NodeKind::Integer : NodeKind::Real,
                    text, start, pos_);
    }

    std::optional<Node> parse_group(char open, char close) {
        const std::size_t start = pos_;
        if (input_[pos_] != open) return fail_node("missing group opener", pos_);
        if (++depth_ > max_depth_) return fail_node("nesting limit exceeded", pos_, 15);
        ++pos_;
        auto value = parse_expression();
        skip_space();
        if (!value || pos_ >= input_.size() || input_[pos_] != close) {
            --depth_;
            return fail_node("unterminated group", pos_);
        }
        ++pos_;
        --depth_;
        value->begin = start;
        value->end = pos_;
        return value;
    }

    std::optional<Node> parse_left_group() {
        const std::size_t start = pos_;
        consume("\\left(");
        if (++depth_ > max_depth_) return fail_node("nesting limit exceeded", start, 15);
        auto value = parse_expression();
        skip_space();
        if (!value || !consume("\\right)")) {
            --depth_;
            return fail_node("unterminated left/right group", pos_);
        }
        --depth_;
        value->begin = start;
        value->end = pos_;
        return value;
    }

    std::optional<Node> parse_absolute() {
        const std::size_t start = pos_;
        consume("\\left|");
        auto value = parse_expression();
        if (!value || !consume("\\right|")) return fail_node("unterminated absolute value", pos_);
        return make(NodeKind::Call, "abs", start, pos_, {std::move(*value)});
    }

    std::optional<Node> parse_command() {
        const std::size_t start = pos_;
        if (starts("\\frac")) return parse_fraction_or_derivative();
        if (starts("\\sqrt")) return parse_sqrt();
        if (starts("\\begin{")) return parse_matrix();
        if (starts("\\int") || starts("\\iint")) return parse_integral();
        if (starts("\\lim")) return parse_limit();
        if (starts("\\sum") || starts("\\prod")) return parse_fold();
        if (starts("\\operatorname{")) return parse_operator_name();
        if (starts("\\det")) return parse_matrix_builtin("det");
        if (starts("\\log")) return parse_log();
        if (starts("\\min") || starts("\\max")) return parse_variadic();

        ++pos_;
        const std::size_t name_start = pos_;
        while (pos_ < input_.size() && std::isalpha(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        const std::string_view name = input_.substr(name_start, pos_ - name_start);
        if (name == "pi" || name == "infty") return make(NodeKind::Symbol, "\\" + std::string(name), start, pos_);
        if (std::find(std::begin(kGreek), std::end(kGreek), name) != std::end(kGreek)) {
            return make(NodeKind::Symbol, "\\" + std::string(name), start, pos_);
        }
        if (std::find(std::begin(kSimpleFunctions), std::end(kSimpleFunctions), name) !=
            std::end(kSimpleFunctions)) {
            auto argument = starts("{") ? parse_group('{', '}') : std::nullopt;
            if (!argument) return fail_node("function requires a braced argument", pos_);
            return make(NodeKind::Call, std::string(name), start, argument->end, {std::move(*argument)});
        }
        return fail_node("unknown command", start, 2);
    }

    std::optional<Node> parse_fraction_or_derivative() {
        const std::size_t start = pos_;
        consume("\\frac");
        const std::size_t saved = pos_;
        const auto numerator_text = extract_raw_group();
        const auto denominator_text = numerator_text ? extract_raw_group() : std::nullopt;
        if (!numerator_text || !denominator_text) return fail_node("fraction requires two groups", pos_);
        const bool derivative = numerator_text->starts_with("d") || numerator_text->starts_with("\\partial");
        if (derivative) {
            const auto numerator_order = derivative_order(*numerator_text);
            const auto denominator_order = derivative_denominator_order(*denominator_text);
            const bool partial = numerator_text->starts_with("\\partial");
            if (partial != denominator_text->starts_with("\\partial") || !numerator_order ||
                !denominator_order || *numerator_order != *denominator_order) {
                return fail_node("derivative orders do not match", start);
            }
            const bool ode_lhs = !partial && *numerator_text != "d" &&
                                 !numerator_text->ends_with(std::to_string(*numerator_order));
            if (ode_lhs) return make(NodeKind::Derivative, *numerator_text + "|" + *denominator_text,
                                     start, pos_);
            auto body = starts("{") ? parse_group('{', '}') : std::nullopt;
            if (!body) return fail_node("derivative requires an expression", pos_);
            return make(NodeKind::Derivative, *denominator_text, start, body->end, {std::move(*body)});
        }
        pos_ = saved;
        auto numerator = parse_group('{', '}');
        auto denominator = numerator ? parse_group('{', '}') : std::nullopt;
        if (!numerator || !denominator) return fail_node("fraction requires two groups", pos_);
        return make(NodeKind::Binary, "frac", start, denominator->end,
                    {std::move(*numerator), std::move(*denominator)});
    }

    std::optional<unsigned> derivative_order(std::string_view text) const {
        if (!(text.starts_with("d") || text.starts_with("\\partial"))) return std::nullopt;
        const auto marker = text.find('^');
        if (marker == std::string_view::npos) return 1;
        const auto digits = text.substr(marker + 1, text.find_first_not_of("0123456789", marker + 1) - marker - 1);
        return parse_positive_integer(digits);
    }

    std::optional<unsigned> derivative_denominator_order(std::string_view text) const {
        if (!(text.starts_with("d") || text.starts_with("\\partial"))) return std::nullopt;
        const auto marker = text.find('^');
        return marker == std::string_view::npos ? std::optional<unsigned>(1)
                                                : parse_positive_integer(text.substr(marker + 1));
    }

    std::optional<Node> parse_sqrt() {
        const std::size_t start = pos_;
        consume("\\sqrt");
        std::string degree = "2";
        if (consume("[")) {
            const std::size_t degree_start = pos_;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            degree = std::string(input_.substr(degree_start, pos_ - degree_start));
            if (!consume("]") || degree.empty() || degree == "0") return fail_node("invalid root degree", degree_start);
        }
        auto radicand = starts("{") ? parse_group('{', '}') : std::nullopt;
        if (!radicand) return fail_node("root requires a braced expression", pos_);
        return make(NodeKind::Call, "sqrt:" + degree, start, radicand->end, {std::move(*radicand)});
    }

    std::optional<Node> parse_log() {
        const std::size_t start = pos_;
        consume("\\log");
        std::vector<Node> arguments;
        if (consume("_")) {
            auto base = starts("{") ? parse_group('{', '}') : std::nullopt;
            if (!base) return fail_node("log base requires a group", pos_);
            if (base->text == "1" || (base->kind == NodeKind::Integer && base->text == "1")) {
                return fail_node("log base cannot be one", base->begin, 9);
            }
            arguments.push_back(std::move(*base));
        }
        auto value = starts("{") ? parse_group('{', '}') : std::nullopt;
        if (!value) return fail_node("log requires a braced expression", pos_);
        arguments.push_back(std::move(*value));
        const std::size_t end = arguments.back().end;
        return make(NodeKind::Call, "log", start, end, std::move(arguments));
    }

    std::optional<Node> parse_variadic() {
        const std::size_t start = pos_;
        const std::string name = starts("\\min") ? "min" : "max";
        consume(name == "min" ? "\\min" : "\\max");
        if (!consume("(")) return fail_node("variadic function requires parentheses", pos_);
        std::vector<Node> arguments;
        if (consume(")")) return fail_node("variadic function requires arguments", pos_);
        while (true) {
            auto argument = parse_expression();
            if (!argument) return std::nullopt;
            arguments.push_back(std::move(*argument));
            skip_space();
            if (consume(")")) break;
            if (!consume(",")) return fail_node("expected ',' or ')'", pos_);
        }
        return make(NodeKind::Call, name, start, pos_, std::move(arguments));
    }

    std::optional<Node> parse_call(Node callee) {
        const std::size_t start = callee.begin;
        consume("(");
        std::vector<Node> arguments;
        if (consume(")")) return fail_node("empty calls are not allowed", pos_);
        while (true) {
            auto argument = parse_expression();
            if (!argument) return std::nullopt;
            arguments.push_back(std::move(*argument));
            skip_space();
            if (consume(")")) break;
            if (!consume(",")) return fail_node("expected ',' or ')'", pos_);
        }
        return make(NodeKind::Call, callee.text, start, pos_, std::move(arguments));
    }

    std::optional<Node> parse_operator_name() {
        const std::size_t start = pos_;
        consume("\\operatorname{");
        const std::size_t name_start = pos_;
        if (pos_ >= input_.size() ||
            !(std::isalpha(static_cast<unsigned char>(input_[pos_])) || input_[pos_] == '_')) {
            return fail_node("invalid operator name", pos_);
        }
        ++pos_;
        while (pos_ < input_.size() &&
               (std::isalnum(static_cast<unsigned char>(input_[pos_])) || input_[pos_] == '_')) ++pos_;
        const std::string name(input_.substr(name_start, pos_ - name_start));
        if (!consume("}")) return fail_node("unterminated operator name", pos_);
        if (name == "rank" || name == "inv" || name == "eigenvalues" || name == "eigenvectors") {
            return parse_matrix_builtin(name, start);
        }
        auto symbol = make(NodeKind::Symbol, "\\operatorname{" + name + "}", start, pos_);
        if (symbol && starts("(")) return parse_call(std::move(*symbol));
        return symbol;
    }

    std::optional<Node> parse_matrix_builtin(std::string name, std::optional<std::size_t> existing_start = std::nullopt) {
        const std::size_t start = existing_start.value_or(pos_);
        if (!existing_start) consume("\\det");
        std::optional<Node> argument;
        if (starts("\\begin{")) argument = parse_matrix();
        else if (starts("(")) argument = parse_group('(', ')');
        else if (starts("{")) argument = parse_group('{', '}');
        if (!argument || argument->kind != NodeKind::Matrix) {
            return fail_node("matrix operation requires a matrix", pos_, 9);
        }
        return make(NodeKind::Call, std::move(name), start, argument->end, {std::move(*argument)});
    }

    std::optional<Node> parse_matrix() {
        const std::size_t start = pos_;
        consume("\\begin{");
        const std::size_t env_start = pos_;
        while (pos_ < input_.size() && std::isalpha(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        const std::string env(input_.substr(env_start, pos_ - env_start));
        if ((env != "matrix" && env != "pmatrix" && env != "bmatrix") || !consume("}")) {
            return fail_node("unsupported matrix environment", start, 2);
        }
        std::vector<Node> rows;
        std::size_t width = 0;
        while (true) {
            std::vector<Node> cells;
            while (true) {
                auto cell = parse_expression();
                if (!cell) return std::nullopt;
                cells.push_back(std::move(*cell));
                if (!consume("&")) break;
            }
            if (width == 0) width = cells.size();
            if (cells.size() != width) return fail_node("matrix rows must have equal width", pos_, 10);
            const std::size_t row_begin = cells.front().begin;
            const std::size_t row_end = cells.back().end;
            auto row = make(NodeKind::Call, "row", row_begin, row_end, std::move(cells));
            if (!row) return std::nullopt;
            rows.push_back(std::move(*row));
            if (consume("\\\\")) continue;
            break;
        }
        if (!consume("\\end{" + env + "}")) return fail_node("mismatched matrix environment", pos_);
        return make(NodeKind::Matrix, env, start, pos_, std::move(rows));
    }

    std::optional<Node> parse_integral() {
        const std::size_t start = pos_;
        const bool twice = starts("\\iint");
        consume(twice ? "\\iint" : "\\int");
        std::vector<Node> children;
        if (consume("_")) {
            auto lower = starts("{") ? parse_group('{', '}') : std::nullopt;
            if (!lower || !consume("^")) return fail_node("integral bounds are incomplete", pos_);
            auto upper = starts("{") ? parse_group('{', '}') : std::nullopt;
            if (!upper) return fail_node("integral bounds are incomplete", pos_);
            children.push_back(std::move(*lower));
            children.push_back(std::move(*upper));
        }
        const auto differential = input_.find("\\,d", pos_);
        if (differential == std::string_view::npos) return fail_node("integral requires a differential", pos_);
        if (node_count_ >= max_nodes_) return fail_node("AST node limit exceeded", pos_, 16);
        Parser body_parser(input_.substr(pos_, differential - pos_), max_depth_ - depth_,
                           max_nodes_ - node_count_, offset_ + pos_);
        auto body_output = body_parser.run();
        node_count_ += body_output.node_count;
        if (!body_output.ok) {
            return fail_node(body_output.message, body_output.error_begin - offset_, body_output.diagnostic_code);
        }
        children.push_back(std::move(body_output.root));
        pos_ = differential + 3;
        const auto first_variable = parse_symbol_text();
        if (!first_variable) return fail_node("invalid differential variable", pos_);
        std::string variables = *first_variable;
        if (twice) {
            if (!consume("\\,d")) return fail_node("double integral requires two differentials", pos_);
            const auto second_variable = parse_symbol_text();
            if (!second_variable || *second_variable == *first_variable) {
                return fail_node("double integral requires distinct differentials", pos_);
            }
            variables += "," + *second_variable;
        }
        return make(NodeKind::Integral, (twice ? "iint:" : "int:") + variables,
                    start, pos_, std::move(children));
    }

    std::optional<Node> parse_limit() {
        const std::size_t start = pos_;
        consume("\\lim");
        if (!consume("_{")) return fail_node("limit requires a subscript", pos_);
        const auto variable = parse_symbol_text();
        if (!variable || !consume("\\to")) return fail_node("limit requires \\to", pos_);
        limit_target_ = true;
        auto target = parse_expression();
        limit_target_ = false;
        if (!target) return std::nullopt;
        std::string direction;
        if (consume("^+")) direction = "+";
        else if (consume("^-")) direction = "-";
        if (!consume("}")) return fail_node("unterminated limit subscript", pos_);
        auto body = parse_expression();
        if (!body) return std::nullopt;
        return make(NodeKind::Limit, *variable + (direction.empty() ? "" : ":" + direction), start, body->end,
                    {std::move(*target), std::move(*body)});
    }

    std::optional<Node> parse_fold() {
        const std::size_t start = pos_;
        const std::string name = starts("\\sum") ? "sum" : "product";
        consume(name == "sum" ? "\\sum" : "\\prod");
        if (!consume("_{")) return fail_node("fold requires indexed lower bound", pos_);
        const auto variable = parse_symbol_text();
        if (!variable || !consume("=")) return fail_node("fold lower bound requires assignment", pos_);
        auto lower = parse_expression();
        if (!lower || !consume("}" ) || !consume("^{")) return fail_node("invalid fold bounds", pos_);
        auto upper = parse_expression();
        if (!upper || !consume("}")) return fail_node("invalid fold upper bound", pos_);
        if (upper->text == "\\infty") return fail_node("infinite folds are unsupported", upper->begin, 12);
        auto body = parse_expression();
        if (!body) return std::nullopt;
        return make(NodeKind::Fold, name + ":" + *variable, start, body->end,
                    {std::move(*lower), std::move(*upper), std::move(*body)});
    }

    std::optional<Node> parse_optimization() {
        const std::size_t start = pos_;
        const std::string name = starts("\\min_") ? "min" : "max";
        consume(name == "min" ? "\\min" : "\\max");
        if (!consume("_{")) return fail_node("optimization requires variables", pos_);
        std::vector<Node> children;
        while (true) {
            const auto variable = parse_symbol_text();
            if (!variable) return fail_node("invalid optimization variable", pos_);
            auto node = make(NodeKind::Symbol, *variable, pos_ - variable->size(), pos_);
            if (!node) return std::nullopt;
            children.push_back(std::move(*node));
            if (!consume(",")) break;
        }
        if (!consume("}")) return fail_node("unterminated optimization variables", pos_);
        auto objective = starts("{") ? parse_group('{', '}') : std::nullopt;
        if (!objective) return fail_node("optimization requires an objective", pos_);
        children.push_back(std::move(*objective));
        while (consume(",")) {
            consume_spacing();
            auto left = parse_expression();
            if (!left) return std::nullopt;
            const std::size_t relation_start = left->begin;
            auto constraint = parse_relation_tail(std::move(*left), relation_start);
            if (!constraint || constraint->kind != NodeKind::Relation) {
                return fail_node("optimization constraint must be a relation", pos_);
            }
            children.push_back(std::move(*constraint));
        }
        return make(NodeKind::Optimization, name, start, pos_, std::move(children));
    }

    std::optional<Node> parse_cases() {
        const std::size_t start = pos_;
        consume("\\begin{cases}");
        const auto end = input_.find("\\end{cases}", pos_);
        if (end == std::string_view::npos) return fail_node("unterminated cases environment", pos_);
        std::vector<Node> clauses;
        std::size_t cursor = pos_;
        while (cursor < end) {
            const auto separator = input_.find("\\\\", cursor);
            const auto clause_end = separator == std::string_view::npos || separator > end ? end : separator;
            if (node_count_ >= max_nodes_) return fail_node("AST node limit exceeded", cursor, 16);
            Parser clause_parser(input_.substr(cursor, clause_end - cursor), max_depth_ - depth_,
                                 max_nodes_ - node_count_, offset_ + cursor, true);
            auto clause = clause_parser.run();
            node_count_ += clause.node_count;
            if (!clause.ok) {
                return fail_node(clause.message, clause.error_begin - offset_, clause.diagnostic_code);
            }
            if (clause.root.kind != NodeKind::Relation && clause.root.kind != NodeKind::Ode) {
                return fail_node("cases entries must be relations", cursor);
            }
            clauses.push_back(std::move(clause.root));
            cursor = clause_end == end ? end : clause_end + 2;
        }
        pos_ = end + std::string_view("\\end{cases}").size();
        if (consume(",")) {
            consume_spacing();
            const auto variable = parse_symbol_text();
            if (!variable || !consume("\\in[") ) return fail_node("invalid ODE interval", pos_);
            auto lower = parse_expression();
            if (!lower || !consume(",")) return fail_node("invalid ODE interval", pos_);
            auto upper = parse_expression();
            if (!upper || !consume("]")) return fail_node("invalid ODE interval", pos_);
            clauses.push_back(std::move(*lower));
            clauses.push_back(std::move(*upper));
            if (!complete_ode_initials(clauses)) {
                return fail_node("ODE requires every initial derivative value", start, 12);
            }
            return make(NodeKind::Ode, *variable, start, pos_, std::move(clauses));
        }
        return make(NodeKind::Relation, "cases", start, pos_, std::move(clauses));
    }

    std::optional<std::string> parse_symbol_text() {
        skip_space();
        const std::size_t start = pos_;
        if (pos_ < input_.size() && std::isalpha(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
            return std::string(input_.substr(start, 1));
        }
        if (starts("\\operatorname{")) {
            consume("\\operatorname{");
            const std::size_t name_start = pos_;
            while (pos_ < input_.size() &&
                   (std::isalnum(static_cast<unsigned char>(input_[pos_])) || input_[pos_] == '_')) ++pos_;
            if (name_start == pos_ || !consume("}")) return std::nullopt;
            return std::string(input_.substr(start, pos_ - start));
        }
        if (pos_ < input_.size() && input_[pos_] == '\\') {
            ++pos_;
            const std::size_t name_start = pos_;
            while (pos_ < input_.size() && std::isalpha(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            const std::string_view name = input_.substr(name_start, pos_ - name_start);
            if (std::find(std::begin(kGreek), std::end(kGreek), name) != std::end(kGreek)) {
                return "\\" + std::string(name);
            }
        }
        pos_ = start;
        return std::nullopt;
    }

    bool complete_ode_initials(const std::vector<Node> &clauses) const {
        std::vector<std::string> required;
        std::vector<std::string> provided;
        for (std::size_t index = 0; index + 2 < clauses.size(); ++index) {
            const auto &clause = clauses[index];
            if (clause.kind != NodeKind::Relation || clause.children.size() != 2) continue;
            const auto &left = clause.children.front();
            if (left.kind == NodeKind::Call) {
                provided.push_back(left.text);
                continue;
            }
            if (left.kind != NodeKind::Derivative) continue;
            const auto separator = left.text.find('|');
            const auto numerator = left.text.substr(0, separator);
            unsigned order = 1;
            std::size_t state_begin = 1;
            if (numerator.starts_with("d^")) {
                const auto state_position = numerator.find_first_not_of("0123456789", 2);
                order = static_cast<unsigned>(std::stoul(numerator.substr(2, state_position - 2)));
                state_begin = state_position;
            }
            const auto state = numerator.substr(state_begin);
            for (unsigned derivative = 0; derivative < order; ++derivative) {
                required.push_back(state + std::string(derivative, '\''));
            }
        }
        return !required.empty() && std::all_of(required.begin(), required.end(), [&](const auto &name) {
            return std::find(provided.begin(), provided.end(), name) != provided.end();
        });
    }

    std::optional<std::string> extract_raw_group() {
        if (!consume("{")) return std::nullopt;
        const std::size_t start = pos_;
        unsigned nested = 0;
        while (pos_ < input_.size()) {
            if (input_[pos_] == '{') ++nested;
            else if (input_[pos_] == '}') {
                if (nested == 0) {
                    const std::string value(input_.substr(start, pos_ - start));
                    ++pos_;
                    return value;
                }
                --nested;
            }
            ++pos_;
        }
        return std::nullopt;
    }

    std::optional<unsigned> parse_positive_integer(std::string_view text) const {
        if (text.empty()) return std::nullopt;
        unsigned value = 0;
        for (const char ch : text) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) return std::nullopt;
            value = value * 10 + static_cast<unsigned>(ch - '0');
        }
        return value == 0 ? std::nullopt : std::optional<unsigned>(value);
    }

    std::optional<std::string> relation_operator() {
        if (consume("\\le")) return "\\le";
        if (consume("\\ge")) return "\\ge";
        if (consume("=")) return "=";
        if (consume("<")) return "<";
        if (consume(">")) return ">";
        return std::nullopt;
    }

    bool starts_primary() const {
        if (pos_ >= input_.size()) return false;
        const char ch = input_[pos_];
        if (std::isdigit(static_cast<unsigned char>(ch)) || std::isalpha(static_cast<unsigned char>(ch)) ||
            ch == '(' || ch == '{') return true;
        if (ch != '\\') return false;
        return !starts("\\right") && !starts("\\end") && !starts("\\,") && !starts("\\;") &&
               !starts("\\le") && !starts("\\ge") && !starts("\\in") && !starts("\\\\");
    }

    void consume_spacing() {
        skip_space();
        if (!consume("\\;")) consume("\\,");
        skip_space();
    }

    void skip_space() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) ++pos_;
    }

    bool starts(std::string_view value) const { return input_.substr(pos_).starts_with(value); }

    bool consume(std::string_view value) {
        if (!starts(value)) return false;
        pos_ += value.size();
        return true;
    }

    std::optional<Node> make(NodeKind kind, std::string text, std::size_t begin, std::size_t end,
                             std::vector<Node> children = {}) {
        if (++node_count_ > max_nodes_) return fail_node("AST node limit exceeded", begin, 16);
        return Node{kind, std::move(text), offset_ + begin, offset_ + end, std::move(children)};
    }

    std::optional<Node> fail_node(std::string message, std::size_t position, int32_t code = 3) {
        fail(std::move(message), position, code);
        return std::nullopt;
    }

    void fail(std::string message, std::size_t position, int32_t code = 3) {
        if (error_) return;
        error_ = true;
        message_ = std::move(message);
        error_pos_ = std::min(position, input_.size());
        diagnostic_code_ = code;
    }

    std::string_view input_;
    uint32_t max_depth_;
    uint32_t max_nodes_;
    std::size_t offset_;
    std::size_t pos_ = 0;
    uint32_t depth_ = 0;
    uint32_t node_count_ = 0;
    bool error_ = false;
    std::string message_;
    std::size_t error_pos_ = 0;
    int32_t diagnostic_code_ = 3;
    bool allow_ode_clause_ = false;
    bool limit_target_ = false;
};

}  // namespace

bool is_valid_utf8(std::string_view input) {
    std::size_t index = 0;
    while (index < input.size()) {
        const auto lead = static_cast<unsigned char>(input[index]);
        if (lead < 0x80) {
            ++index;
            continue;
        }
        std::size_t count = 0;
        uint32_t codepoint = 0;
        if ((lead & 0xE0) == 0xC0) { count = 2; codepoint = lead & 0x1F; }
        else if ((lead & 0xF0) == 0xE0) { count = 3; codepoint = lead & 0x0F; }
        else if ((lead & 0xF8) == 0xF0) { count = 4; codepoint = lead & 0x07; }
        else return false;
        if (index + count > input.size()) return false;
        for (std::size_t offset = 1; offset < count; ++offset) {
            const auto byte = static_cast<unsigned char>(input[index + offset]);
            if ((byte & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (byte & 0x3F);
        }
        if ((count == 2 && codepoint < 0x80) || (count == 3 && codepoint < 0x800) ||
            (count == 4 && codepoint < 0x10000) || codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return false;
        index += count;
    }
    return true;
}

ParseOutput parse_for_debug(std::string_view input, uint32_t max_depth, uint32_t max_nodes) {
    return Parser(input, max_depth, max_nodes).run();
}

}  // namespace texsolve
