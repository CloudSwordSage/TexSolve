#define BOOST_TEST_MODULE TexSolveCore
#include <boost/test/included/unit_test.hpp>

#include <string>
#include <string_view>
#include <cmath>

#include <texsolve/texsolve.h>

namespace {

std::string text(texsolve_string_view value) {
    return value.data == nullptr ? std::string{} : std::string(value.data, value.size);
}

texsolve_result *execute(texsolve_context *context, std::string_view latex,
                         int32_t operation = TEXSOLVE_OPERATION_AUTO,
                         int32_t backend = TEXSOLVE_SYMBOLIC_AUTO,
                         int32_t integration_backend = TEXSOLVE_INTEGRATION_AUTO,
                         uint32_t precision_digits = 0) {
    texsolve_request request{};
    request.struct_size = sizeof(request);
    request.abi_version = TEXSOLVE_ABI_VERSION;
    request.operation = operation;
    request.symbolic_backend = backend;
    request.integration_backend = integration_backend;
    request.precision_digits = precision_digits;
    request.latex = {latex.data(), latex.size()};
    texsolve_result *result = nullptr;
    const auto status = texsolve_execute(context, &request, &result);
    BOOST_REQUIRE_EQUAL(status, TEXSOLVE_STATUS_OK);
    BOOST_REQUIRE(result != nullptr);
    return result;
}

BOOST_AUTO_TEST_CASE(limits_and_integrals_follow_analytic_then_numeric_policy) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    auto *limit = execute(context, R"(\lim_{x\to 0}\frac{\sin{x}}{x})");
    BOOST_TEST(text(texsolve_result_exact_latex(limit)) == "1");
    texsolve_result_destroy(limit);
    auto *right_limit = execute(context, R"(\lim_{x\to 0^+}\frac{1}{x})");
    BOOST_TEST(text(texsolve_result_exact_latex(right_limit)) == R"(\infty)");
    texsolve_result_destroy(right_limit);
    auto *left_limit = execute(context, R"(\lim_{x\to 0^-}\frac{1}{x})");
    BOOST_TEST(text(texsolve_result_exact_latex(left_limit)) == R"(-\infty)");
    texsolve_result_destroy(left_limit);

    auto *double_integral = execute(context, R"(\iint_{0}^{1}xy\,dx\,dy)");
    BOOST_TEST(text(texsolve_result_exact_latex(double_integral)) == R"(\frac{1}{4})");
    texsolve_result_destroy(double_integral);

    for (const int backend : {TEXSOLVE_INTEGRATION_GSL, TEXSOLVE_INTEGRATION_BOOST_MATH}) {
        auto *integral = execute(context, R"(\int_{0}^{1}\exp{-x^2}\,dx)",
                                 TEXSOLVE_OPERATION_INTEGRATE, TEXSOLVE_SYMBOLIC_AUTO, backend, 10000);
        BOOST_TEST(std::stod(text(texsolve_result_approximation(integral))) == 0.746824132812427,
                   boost::test_tools::tolerance(1e-9));
        BOOST_TEST(text(texsolve_result_backend(integral)) == (backend == 1 ? "gsl" : "boost_math"));
        BOOST_TEST(text(texsolve_result_name(integral)) == "value");
        const auto *metadata = texsolve_result_metadata(integral);
        BOOST_REQUIRE(metadata != nullptr);
        const auto *reported_precision = texsolve_result_child(metadata, 0);
        BOOST_TEST(text(texsolve_result_name(reported_precision)) == "precision_digits");
        BOOST_TEST(text(texsolve_result_exact_latex(reported_precision)) == "15");
        texsolve_result_destroy(integral);
    }

    const std::string invalid_integral = R"(\int_{0}^{1}\sqrt{-1}\,dx)";
    texsolve_request invalid_request{};
    invalid_request.struct_size = sizeof(invalid_request);
    invalid_request.abi_version = TEXSOLVE_ABI_VERSION;
    invalid_request.operation = TEXSOLVE_OPERATION_INTEGRATE;
    invalid_request.integration_backend = TEXSOLVE_INTEGRATION_GSL;
    invalid_request.latex = {invalid_integral.data(), invalid_integral.size()};
    texsolve_result *invalid_result = nullptr;
    BOOST_TEST(texsolve_execute(context, &invalid_request, &invalid_result) == TEXSOLVE_STATUS_NOT_CONVERGED);
    texsolve_result_destroy(invalid_result);
    texsolve_context_destroy(context);
}

}  // namespace

BOOST_AUTO_TEST_CASE(preserves_exact_numbers_and_result_lifetime) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    auto *rational = execute(context, R"(\frac{1}{3}+\frac{1}{6})");
    BOOST_TEST(texsolve_result_kind(rational) == TEXSOLVE_RESULT_RATIONAL);
    BOOST_TEST(text(texsolve_result_exact_latex(rational)) == R"(\frac{1}{2})");
    texsolve_context_destroy(context);
    BOOST_TEST(text(texsolve_result_exact_latex(rational)) == R"(\frac{1}{2})");
    texsolve_result_destroy(rational);
}

BOOST_AUTO_TEST_CASE(finite_decimals_and_real_square_roots_are_simplified_exactly) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    auto *decimal = execute(context, "0.5");
    BOOST_TEST(texsolve_result_kind(decimal) == TEXSOLVE_RESULT_RATIONAL);
    BOOST_TEST(text(texsolve_result_exact_latex(decimal)) == R"(\frac{1}{2})");
    texsolve_result_destroy(decimal);

    auto *root = execute(context, R"(\sqrt{9/(x*x)}+1/x)");
    const auto exact = text(texsolve_result_exact_latex(root));
    BOOST_TEST(exact.find(R"(\left|x\right|)") != std::string::npos);
    BOOST_TEST(exact.find(R"(\sqrt)") == std::string::npos);
    texsolve_result_destroy(root);

    const std::string expression = R"(\sqrt{9/(x*x)})";
    const std::string name = "x";
    const std::string value = "i";
    texsolve_binding complex_binding{};
    complex_binding.struct_size = sizeof(complex_binding);
    complex_binding.name = {name.data(), name.size()};
    complex_binding.value_latex = {value.data(), value.size()};
    texsolve_request request{};
    request.struct_size = sizeof(request);
    request.abi_version = TEXSOLVE_ABI_VERSION;
    request.latex = {expression.data(), expression.size()};
    request.bindings = &complex_binding;
    request.binding_count = 1;
    request.binding_stride = sizeof(complex_binding);
    texsolve_result *complex = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_execute(context, &request, &complex), TEXSOLVE_STATUS_OK);
    BOOST_TEST(texsolve_result_kind(complex) == TEXSOLVE_RESULT_COMPLEX);
    texsolve_result_destroy(complex);
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(classifies_expression_types_and_builds_complex_children) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    auto *sine = execute(context, R"(\sin{x})");
    BOOST_TEST(texsolve_result_kind(sine) == TEXSOLVE_RESULT_SYMBOLIC);
    texsolve_result_destroy(sine);
    auto *complex = execute(context, R"(\sqrt{-1})");
    BOOST_TEST(texsolve_result_kind(complex) == TEXSOLVE_RESULT_COMPLEX);
    BOOST_REQUIRE_EQUAL(texsolve_result_child_count(complex), 2u);
    BOOST_TEST(text(texsolve_result_name(texsolve_result_child(complex, 0))) == "real");
    BOOST_TEST(text(texsolve_result_name(texsolve_result_child(complex, 1))) == "imag");
    BOOST_TEST(texsolve_result_metadata(texsolve_result_child(complex, 0)) == texsolve_result_metadata(complex));
    texsolve_result_destroy(complex);
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(rejects_non_symbol_binding_names) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    const std::string input = "x+1";
    const std::string value = "2";
    for (const std::string name : {std::string{}, std::string{"x+y"}, std::string{"{x}"}, std::string{"\\pi"}}) {
        texsolve_binding binding{};
        binding.struct_size = sizeof(binding);
        binding.name = {name.data(), name.size()};
        binding.value_latex = {value.data(), value.size()};
        texsolve_request request{};
        request.struct_size = sizeof(request);
        request.abi_version = TEXSOLVE_ABI_VERSION;
        request.latex = {input.data(), input.size()};
        request.bindings = &binding;
        request.binding_count = 1;
        request.binding_stride = sizeof(binding);
        texsolve_result *result = nullptr;
        BOOST_TEST(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_SEMANTIC_ERROR);
        texsolve_result_destroy(result);
    }
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(substitutes_bindings_and_reports_backend_metadata) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    const std::string_view latex = "x+1";
    const std::string_view name = "x";
    const std::string_view value = "2";
    texsolve_binding binding{};
    binding.struct_size = sizeof(binding);
    binding.name = {name.data(), name.size()};
    binding.value_latex = {value.data(), value.size()};
    texsolve_request request{};
    request.struct_size = sizeof(request);
    request.abi_version = TEXSOLVE_ABI_VERSION;
    request.latex = {latex.data(), latex.size()};
    request.bindings = &binding;
    request.binding_count = 1;
    request.binding_stride = sizeof(binding);
    request.symbolic_backend = TEXSOLVE_SYMBOLIC_SYMENGINE;
    texsolve_result *result = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_execute(context, &request, &result), TEXSOLVE_STATUS_OK);
    BOOST_TEST(text(texsolve_result_exact_latex(result)) == "3");
    BOOST_TEST(text(texsolve_result_backend(result)) == "symengine");
    const auto *metadata = texsolve_result_metadata(result);
    BOOST_REQUIRE(metadata != nullptr);
    BOOST_TEST(texsolve_result_kind(metadata) == TEXSOLVE_RESULT_METADATA);
    texsolve_result_destroy(result);
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(symbolic_backends_and_calculus_follow_operation_contract) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    for (const int backend : {TEXSOLVE_SYMBOLIC_SYMENGINE, TEXSOLVE_SYMBOLIC_GINAC}) {
        auto *expanded = execute(context, "(x+1)^2", TEXSOLVE_OPERATION_EXPAND, backend);
        BOOST_TEST(text(texsolve_result_exact_latex(expanded)).find("x") != std::string::npos);
        BOOST_TEST(text(texsolve_result_backend(expanded)) == (backend == 1 ? "symengine" : "ginac"));
        texsolve_result_destroy(expanded);
    }
    auto *derivative = execute(context, R"(\frac{d}{dx}{x^3})");
    BOOST_TEST(text(texsolve_result_exact_latex(derivative)).find("3") != std::string::npos);
    texsolve_result_destroy(derivative);
    auto *integral = execute(context, R"(\int x^2\,dx)");
    BOOST_TEST(text(texsolve_result_exact_latex(integral)).find("x") != std::string::npos);
    texsolve_result_destroy(integral);
    auto *sum = execute(context, R"(\sum_{k=1}^{10}k)");
    BOOST_TEST(text(texsolve_result_exact_latex(sum)) == "55");
    texsolve_result_destroy(sum);
    auto *product = execute(context, R"(\prod_{k=1}^{5}k)");
    BOOST_TEST(text(texsolve_result_exact_latex(product)) == "120");
    texsolve_result_destroy(product);
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(finite_folds_work_with_index_i_and_inside_scalar_expressions) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    auto *shifted = execute(context, R"(\sum_{i=1}^{3}(i+1))");
    BOOST_TEST(text(texsolve_result_exact_latex(shifted)) == "9");
    texsolve_result_destroy(shifted);
    auto *squares = execute(context, R"(\sum_{i=1}^{4}i^2)");
    BOOST_TEST(text(texsolve_result_exact_latex(squares)) == "30");
    texsolve_result_destroy(squares);
    auto *largest_index = execute(context,
        R"(\sum_{i=9223372036854775807}^{9223372036854775807}i)");
    BOOST_TEST(text(texsolve_result_exact_latex(largest_index)) == "9223372036854775807");
    texsolve_result_destroy(largest_index);
    auto *nested = execute(context, R"(1+\sum_{i=1}^{3}i-\prod_{j=1}^{2}j)");
    BOOST_TEST(text(texsolve_result_exact_latex(nested)) == "5");
    texsolve_result_destroy(nested);
    auto *composite = execute(context,
        R"(\frac{1}{2}x^2+3\cdot x-\sqrt{4}+\sin{0}+\ln{1}+\sum_{i=1}^{3}i-\prod_{j=1}^{2}j)");
    BOOST_TEST(texsolve_result_kind(composite) == TEXSOLVE_RESULT_SYMBOLIC);
    BOOST_TEST(text(texsolve_result_exact_latex(composite)).find("x") != std::string::npos);
    texsolve_result_destroy(composite);
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(explicit_operation_rejects_incompatible_top_level_syntax) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    const std::string input = "x+1";
    texsolve_request request{};
    request.struct_size = sizeof(request);
    request.abi_version = TEXSOLVE_ABI_VERSION;
    request.operation = TEXSOLVE_OPERATION_ODE_IVP;
    request.latex = {input.data(), input.size()};
    texsolve_result *result = nullptr;
    BOOST_TEST(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_OPERATION_MISMATCH);
    texsolve_result_destroy(result);
    texsolve_context_destroy(context);
}
