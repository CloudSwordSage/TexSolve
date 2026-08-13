#define BOOST_TEST_MODULE TexSolveSolvers
#include <boost/test/included/unit_test.hpp>

#include <cmath>
#include <string>
#include <string_view>

#include <texsolve/texsolve.h>

namespace {

std::string text(texsolve_string_view view) {
    return view.data == nullptr ? std::string{} : std::string(view.data, view.size);
}

texsolve_result *run(texsolve_context *context, std::string_view latex, int operation,
                     const texsolve_binding *bindings = nullptr, std::size_t count = 0) {
    texsolve_request request{};
    request.struct_size = sizeof(request);
    request.abi_version = TEXSOLVE_ABI_VERSION;
    request.operation = operation;
    request.latex = {latex.data(), latex.size()};
    request.bindings = bindings;
    request.binding_count = count;
    request.binding_stride = count == 0 ? 0 : sizeof(texsolve_binding);
    texsolve_result *result = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_execute(context, &request, &result), TEXSOLVE_STATUS_OK);
    BOOST_REQUIRE(result != nullptr);
    return result;
}

texsolve_binding binding(std::string_view name, std::string_view value) {
    texsolve_binding result{};
    result.struct_size = sizeof(result);
    result.name = {name.data(), name.size()};
    result.value_latex = {value.data(), value.size()};
    return result;
}

texsolve_status status_for(texsolve_context *context, texsolve_request &request,
                           texsolve_result **result) {
    return texsolve_execute(context, &request, result);
}

const texsolve_result *named_child(const texsolve_result *result, std::string_view name) {
    for (std::size_t index = 0; index < texsolve_result_child_count(result); ++index) {
        const auto *child = texsolve_result_child(result, index);
        if (text(texsolve_result_name(child)) == name) return child;
    }
    return nullptr;
}

}  // namespace

BOOST_AUTO_TEST_CASE(linear_algebra_returns_structured_results) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    auto *determinant = run(context, R"(\det\begin{pmatrix}1&2\\3&4\end{pmatrix})",
                            TEXSOLVE_OPERATION_LINEAR_ALGEBRA);
    BOOST_TEST(text(texsolve_result_exact_latex(determinant)) == "-2");
    BOOST_TEST(text(texsolve_result_backend(determinant)) == "eigen");
    texsolve_result_destroy(determinant);
    auto *inverse = run(context, R"(\operatorname{inv}(\begin{pmatrix}1&2\\3&4\end{pmatrix}))",
                        TEXSOLVE_OPERATION_LINEAR_ALGEBRA);
    BOOST_TEST(texsolve_result_kind(inverse) == TEXSOLVE_RESULT_MATRIX);
    BOOST_TEST(texsolve_result_child_count(inverse) == 2u);
    const auto *inverse_element = texsolve_result_child(texsolve_result_child(inverse, 0), 0);
    BOOST_TEST(text(texsolve_result_backend(inverse_element)) == "eigen");
    BOOST_TEST(texsolve_result_metadata(inverse_element) == texsolve_result_metadata(inverse));
    texsolve_result_destroy(inverse);
    auto *eigenvectors = run(context,
        R"(\operatorname{eigenvectors}(\begin{pmatrix}2&0\\0&3\end{pmatrix}))",
        TEXSOLVE_OPERATION_LINEAR_ALGEBRA);
    BOOST_TEST(texsolve_result_kind(eigenvectors) == TEXSOLVE_RESULT_MATRIX);
    texsolve_result_destroy(eigenvectors);

    const std::string armadillo_input = R"(\det\begin{pmatrix}1&2\\3&4\end{pmatrix})";
    texsolve_request armadillo{};
    armadillo.struct_size = sizeof(armadillo);
    armadillo.abi_version = TEXSOLVE_ABI_VERSION;
    armadillo.operation = TEXSOLVE_OPERATION_LINEAR_ALGEBRA;
    armadillo.linear_algebra_backend = TEXSOLVE_LINEAR_ALGEBRA_ARMADILLO;
    armadillo.latex = {armadillo_input.data(), armadillo_input.size()};
    texsolve_result *armadillo_result = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_execute(context, &armadillo, &armadillo_result), TEXSOLVE_STATUS_OK);
    BOOST_TEST(text(texsolve_result_backend(armadillo_result)) == "armadillo");
    texsolve_result_destroy(armadillo_result);

    auto *transpose = run(context,
        R"(\begin{pmatrix}1&2\\3&4\end{pmatrix}^{T})",
        TEXSOLVE_OPERATION_LINEAR_ALGEBRA);
    BOOST_TEST(texsolve_result_kind(transpose) == TEXSOLVE_RESULT_MATRIX);
    BOOST_TEST(text(texsolve_result_exact_latex(
        texsolve_result_child(texsolve_result_child(transpose, 0), 1))) == "3");
    texsolve_result_destroy(transpose);

    auto *symbolic = run(context, R"(\det\begin{pmatrix}x&0\\0&1\end{pmatrix})",
                         TEXSOLVE_OPERATION_LINEAR_ALGEBRA);
    BOOST_TEST(texsolve_result_kind(symbolic) == TEXSOLVE_RESULT_SYMBOLIC);
    BOOST_TEST(text(texsolve_result_exact_latex(symbolic)) == "x");
    texsolve_result_destroy(symbolic);

    const std::string precision_input = R"(\det\begin{pmatrix}1&2\\3&4\end{pmatrix})";
    texsolve_request precision_request{};
    precision_request.struct_size = sizeof(precision_request);
    precision_request.abi_version = TEXSOLVE_ABI_VERSION;
    precision_request.operation = TEXSOLVE_OPERATION_LINEAR_ALGEBRA;
    precision_request.precision_digits = 10000;
    precision_request.latex = {precision_input.data(), precision_input.size()};
    texsolve_result *precision_result = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_execute(context, &precision_request, &precision_result), TEXSOLVE_STATUS_OK);
    const auto *precision = named_child(texsolve_result_metadata(precision_result), "precision_digits");
    BOOST_REQUIRE(precision != nullptr);
    BOOST_TEST(text(texsolve_result_exact_latex(precision)) == "15");
    texsolve_result_destroy(precision_result);

    auto *complex_vectors = run(context,
        R"(\operatorname{eigenvectors}(\begin{pmatrix}0&-1\\1&0\end{pmatrix}))",
        TEXSOLVE_OPERATION_LINEAR_ALGEBRA);
    bool found_complex = false;
    for (std::size_t row = 0; row < texsolve_result_child_count(complex_vectors); ++row) {
        const auto *row_node = texsolve_result_child(complex_vectors, row);
        for (std::size_t col = 0; col < texsolve_result_child_count(row_node); ++col) {
            const auto *value_node = texsolve_result_child(row_node, col);
            if (texsolve_result_kind(value_node) == TEXSOLVE_RESULT_COMPLEX) {
                found_complex = texsolve_result_child_count(value_node) == 2;
            }
        }
    }
    BOOST_TEST(found_complex);
    texsolve_result_destroy(complex_vectors);
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(polynomial_solver_returns_all_roots) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    auto *roots = run(context, "x^4-1=0", TEXSOLVE_OPERATION_SOLVE);
    BOOST_TEST(texsolve_result_kind(roots) == TEXSOLVE_RESULT_ROOT_SET);
    BOOST_TEST(texsolve_result_child_count(roots) == 4u);
    for (std::size_t index = 0; index < 4; ++index) {
        const auto *root = texsolve_result_child(roots, index);
        BOOST_TEST(texsolve_result_kind(root) == TEXSOLVE_RESULT_ROOT);
        BOOST_TEST(texsolve_result_child_count(root) == 3u);
    }
    texsolve_result_destroy(roots);

    auto *repeated = run(context, "x^2=0", TEXSOLVE_OPERATION_SOLVE);
    BOOST_REQUIRE_EQUAL(texsolve_result_child_count(repeated), 1u);
    const auto *multiplicity = texsolve_result_child(texsolve_result_child(repeated, 0), 1);
    BOOST_TEST(text(texsolve_result_exact_latex(multiplicity)) == "2");
    texsolve_result_destroy(repeated);

    const std::string high_multiplicity_input = "(x-1)^4=0";
    texsolve_request high_multiplicity{};
    high_multiplicity.struct_size = sizeof(high_multiplicity);
    high_multiplicity.abi_version = TEXSOLVE_ABI_VERSION;
    high_multiplicity.operation = TEXSOLVE_OPERATION_SOLVE;
    high_multiplicity.max_iterations = 1;
    high_multiplicity.latex = {high_multiplicity_input.data(), high_multiplicity_input.size()};
    texsolve_result *high_multiplicity_result = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_execute(context, &high_multiplicity, &high_multiplicity_result), TEXSOLVE_STATUS_OK);
    multiplicity = texsolve_result_child(texsolve_result_child(high_multiplicity_result, 0), 1);
    BOOST_TEST(text(texsolve_result_exact_latex(multiplicity)) == "4");
    texsolve_result_destroy(high_multiplicity_result);

    auto *ordered = run(context, "x^2-12x+20=0", TEXSOLVE_OPERATION_SOLVE);
    BOOST_REQUIRE_EQUAL(texsolve_result_child_count(ordered), 2u);
    const auto *first_value = texsolve_result_child(texsolve_result_child(ordered, 0), 0);
    BOOST_TEST(text(texsolve_result_exact_latex(first_value)) == "2");
    texsolve_result_destroy(ordered);
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(linear_system_is_supported) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    auto *system = run(context, R"(\begin{cases}x+y=2\\x-y=0\end{cases})", TEXSOLVE_OPERATION_SOLVE);
    BOOST_TEST(texsolve_result_kind(system) == TEXSOLVE_RESULT_MAPPING);
    BOOST_TEST(texsolve_result_child_count(system) == 2u);
    texsolve_result_destroy(system);
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(local_transcendental_root_is_supported) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    const auto start = binding("x", "3");
    auto *local = run(context, R"(\sin{x}=0)", TEXSOLVE_OPERATION_SOLVE, &start, 1);
    BOOST_REQUIRE_EQUAL(texsolve_result_child_count(local), 1u);
    const auto *search_kind = texsolve_result_child(texsolve_result_child(local, 0), 2);
    BOOST_TEST(text(texsolve_result_exact_latex(search_kind)) == "local");
    texsolve_result_destroy(local);
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(symbolic_equations_use_general_solver_before_numeric_fallback) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    auto *polynomial = run(context, "(x+1)*(x-1)-2=0", TEXSOLVE_OPERATION_SOLVE);
    BOOST_TEST(texsolve_result_child_count(polynomial) == 2u);
    texsolve_result_destroy(polynomial);
    auto *radical = run(context, R"(\sqrt{x-1}=\sqrt{3})", TEXSOLVE_OPERATION_SOLVE);
    BOOST_REQUIRE_EQUAL(texsolve_result_child_count(radical), 1u);
    BOOST_TEST(text(texsolve_result_exact_latex(
        texsolve_result_child(texsolve_result_child(radical, 0), 0))) == "4");
    texsolve_result_destroy(radical);
    auto *single_radical = run(context, R"(\sqrt{x}=2)", TEXSOLVE_OPERATION_SOLVE);
    BOOST_REQUIRE_EQUAL(texsolve_result_child_count(single_radical), 1u);
    BOOST_TEST(text(texsolve_result_exact_latex(
        texsolve_result_child(texsolve_result_child(single_radical, 0), 0))) == "4");
    texsolve_result_destroy(single_radical);
    for (const std::string_view no_solution : {
             std::string_view{R"(\sqrt{x}=-2)"},
             std::string_view{R"(\sqrt{x}=\sqrt{x+1})"}}) {
        auto *empty = run(context, no_solution, TEXSOLVE_OPERATION_SOLVE);
        BOOST_TEST(texsolve_result_kind(empty) == TEXSOLVE_RESULT_ROOT_SET);
        BOOST_TEST(texsolve_result_child_count(empty) == 0u);
        texsolve_result_destroy(empty);
    }
    auto *trigonometric = run(context, R"(\sin{x}=0)", TEXSOLVE_OPERATION_SOLVE);
    BOOST_REQUIRE_EQUAL(texsolve_result_child_count(trigonometric), 1u);
    const auto *family = texsolve_result_child(texsolve_result_child(trigonometric, 0), 0);
    BOOST_TEST(text(texsolve_result_exact_latex(family)).find(R"(\pi)") != std::string::npos);
    BOOST_TEST(text(texsolve_result_exact_latex(
        texsolve_result_child(texsolve_result_child(trigonometric, 0), 2))) == "analytic");
    texsolve_result_destroy(trigonometric);
    auto *factorial = run(context, R"(\prod_{i=1}^{x}i=6)", TEXSOLVE_OPERATION_SOLVE);
    BOOST_REQUIRE_EQUAL(texsolve_result_child_count(factorial), 1u);
    BOOST_TEST(text(texsolve_result_exact_latex(
        texsolve_result_child(texsolve_result_child(factorial, 0), 0))) == "3");
    texsolve_result_destroy(factorial);
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(explicit_optimization_backend_capabilities_are_enforced) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    const auto start = binding("x", "0");
    const std::string objective = R"(\min_{x}{x^2},\;x\ge 1)";
    texsolve_request request{};
    request.struct_size = sizeof(request);
    request.abi_version = TEXSOLVE_ABI_VERSION;
    request.operation = TEXSOLVE_OPERATION_OPTIMIZE;
    request.optimization_kind = TEXSOLVE_OPTIMIZATION_KIND_GENERAL;
    request.latex = {objective.data(), objective.size()};
    request.bindings = &start;
    request.binding_count = 1;
    request.binding_stride = sizeof(start);
    request.optimization_backend = TEXSOLVE_OPTIMIZATION_CERES;
    texsolve_result *result = nullptr;
    BOOST_TEST(status_for(context, request, &result) == TEXSOLVE_STATUS_BACKEND_UNSUPPORTED);
    texsolve_result_destroy(result);

    request.optimization_backend = TEXSOLVE_OPTIMIZATION_NLOPT;
    BOOST_REQUIRE_EQUAL(status_for(context, request, &result), TEXSOLVE_STATUS_OK);
    const auto *variables = texsolve_result_child(result, 0);
    BOOST_TEST(std::stod(text(texsolve_result_approximation(texsolve_result_child(variables, 0)))) >= 0.999);
    texsolve_result_destroy(result);

    const std::string bounded_objective = R"(\min_{x}{x^2})";
    const std::string lower = "1";
    auto bounded_start = start;
    bounded_start.lower_latex = {lower.data(), lower.size()};
    request.latex = {bounded_objective.data(), bounded_objective.size()};
    request.bindings = &bounded_start;
    request.optimization_backend = TEXSOLVE_OPTIMIZATION_NLOPT;
    BOOST_REQUIRE_EQUAL(status_for(context, request, &result), TEXSOLVE_STATUS_OK);
    variables = texsolve_result_child(result, 0);
    BOOST_TEST(std::stod(text(texsolve_result_approximation(texsolve_result_child(variables, 0)))) >= 0.999);
    texsolve_result_destroy(result);

    const std::string residual_text = "x-2";
    const texsolve_string_view residual{residual_text.data(), residual_text.size()};
    request.latex = {nullptr, 0};
    request.bindings = &start;
    request.optimization_kind = TEXSOLVE_OPTIMIZATION_KIND_LEAST_SQUARES;
    request.optimization_backend = TEXSOLVE_OPTIMIZATION_CERES;
    request.residuals = &residual;
    request.residual_count = 1;
    BOOST_REQUIRE_EQUAL(status_for(context, request, &result), TEXSOLVE_STATUS_OK);
    BOOST_TEST(text(texsolve_result_backend(result)) == "ceres");
    variables = texsolve_result_child(result, 0);
    BOOST_TEST(std::stod(text(texsolve_result_approximation(texsolve_result_child(variables, 0)))) == 2.0,
               boost::test_tools::tolerance(1e-8));
    texsolve_result_destroy(result);

    request.optimization_backend = TEXSOLVE_OPTIMIZATION_NLOPT;
    BOOST_REQUIRE_EQUAL(status_for(context, request, &result), TEXSOLVE_STATUS_OK);
    BOOST_TEST(text(texsolve_result_backend(result)) == "nlopt");
    texsolve_result_destroy(result);

    const std::string invalid_objective = R"(\min_{x}{\sqrt{-1}})";
    request.latex = {invalid_objective.data(), invalid_objective.size()};
    request.optimization_kind = TEXSOLVE_OPTIMIZATION_KIND_GENERAL;
    request.residuals = nullptr;
    request.residual_count = 0;
    BOOST_TEST(status_for(context, request, &result) == TEXSOLVE_STATUS_NOT_CONVERGED);
    texsolve_result_destroy(result);
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(optimization_does_not_report_ceres_iteration_limit_as_success) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    const auto x = binding("x", "-1.2");
    const auto y = binding("y", "1");
    const texsolve_binding bindings[] = {x, y};
    const std::string first = "10(y-x^2)";
    const std::string second = "1-x";
    const texsolve_string_view residuals[] = {
        {first.data(), first.size()}, {second.data(), second.size()}};
    texsolve_request request{};
    request.struct_size = sizeof(request);
    request.abi_version = TEXSOLVE_ABI_VERSION;
    request.operation = TEXSOLVE_OPERATION_OPTIMIZE;
    request.optimization_kind = TEXSOLVE_OPTIMIZATION_KIND_LEAST_SQUARES;
    request.optimization_backend = TEXSOLVE_OPTIMIZATION_CERES;
    request.max_iterations = 1;
    request.bindings = bindings;
    request.binding_count = 2;
    request.binding_stride = sizeof(texsolve_binding);
    request.residuals = residuals;
    request.residual_count = 2;
    texsolve_result *result = nullptr;
    BOOST_TEST(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_NOT_CONVERGED);
    BOOST_REQUIRE(result != nullptr);
    BOOST_TEST(texsolve_result_status(result) == TEXSOLVE_STATUS_NOT_CONVERGED);
    texsolve_result_destroy(result);
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(high_order_and_system_ode_are_normalized) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    auto *second_order = run(context,
        R"(\frac{d^2y}{dt^2}=-y,\;y(0)=0,\;y'(0)=1,\;t\in[0,1])",
        TEXSOLVE_OPERATION_ODE_IVP);
    const auto *last = texsolve_result_child(second_order, texsolve_result_child_count(second_order) - 1);
    BOOST_TEST(std::stod(text(texsolve_result_approximation(texsolve_result_child(last, 1)))) == std::sin(1.0),
               boost::test_tools::tolerance(1e-4));
    texsolve_result_destroy(second_order);

    auto *system = run(context,
        R"(\begin{cases}\frac{dx}{dt}=y\\\frac{dy}{dt}=-x\\x(0)=0\\y(0)=1\end{cases},\;t\in[0,1])",
        TEXSOLVE_OPERATION_ODE_IVP);
    last = texsolve_result_child(system, texsolve_result_child_count(system) - 1);
    BOOST_TEST(texsolve_result_child_count(last) == 3u);
    texsolve_result_destroy(system);

    const std::string invalid_ode = R"(\frac{dy}{dt}=\sqrt{-1},\;y(0)=1,\;t\in[0,1])";
    texsolve_request invalid_request{};
    invalid_request.struct_size = sizeof(invalid_request);
    invalid_request.abi_version = TEXSOLVE_ABI_VERSION;
    invalid_request.operation = TEXSOLVE_OPERATION_ODE_IVP;
    invalid_request.latex = {invalid_ode.data(), invalid_ode.size()};
    texsolve_result *invalid_result = nullptr;
    BOOST_TEST(texsolve_execute(context, &invalid_request, &invalid_result) == TEXSOLVE_STATUS_NOT_CONVERGED);
    texsolve_result_destroy(invalid_result);
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(nlopt_and_sundials_paths_report_structured_metadata) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    const std::string_view name = "x";
    const std::string_view initial = "0";
    const auto start = binding(name, initial);
    auto *optimum = run(context, R"(\min_{x}{(x-1)^2})", TEXSOLVE_OPERATION_OPTIMIZE, &start, 1);
    BOOST_TEST(texsolve_result_kind(optimum) == TEXSOLVE_RESULT_OPTIMUM);
    BOOST_TEST(text(texsolve_result_backend(optimum)) == "nlopt");
    BOOST_REQUIRE(texsolve_result_metadata(optimum) != nullptr);
    texsolve_result_destroy(optimum);

    auto *trajectory = run(context, R"(\frac{dy}{dt}=y,\;y(0)=1,\;t\in[0,1])",
                           TEXSOLVE_OPERATION_ODE_IVP);
    BOOST_TEST(texsolve_result_kind(trajectory) == TEXSOLVE_RESULT_TRAJECTORY);
    BOOST_TEST(text(texsolve_result_backend(trajectory)) == "sundials");
    BOOST_REQUIRE(texsolve_result_child_count(trajectory) >= 2u);
    const auto *last = texsolve_result_child(trajectory, texsolve_result_child_count(trajectory) - 1);
    const auto *y = texsolve_result_child(last, 1);
    BOOST_TEST(std::stod(text(texsolve_result_approximation(y))) == std::exp(1.0), boost::test_tools::tolerance(1e-4));
    texsolve_result_destroy(trajectory);
    texsolve_context_destroy(context);
}
