#define BOOST_TEST_MODULE TexSolveCore
#include <boost/test/included/unit_test.hpp>

#include <string>
#include <string_view>

#include <texsolve/texsolve.h>

namespace {

std::string text(texsolve_string_view value) {
    return value.data == nullptr ? std::string{} : std::string(value.data, value.size);
}

texsolve_result *execute(texsolve_context *context, std::string_view latex,
                         int32_t operation = TEXSOLVE_OPERATION_AUTO,
                         int32_t backend = TEXSOLVE_SYMBOLIC_AUTO) {
    texsolve_request request{};
    request.struct_size = sizeof(request);
    request.abi_version = TEXSOLVE_ABI_VERSION;
    request.operation = operation;
    request.symbolic_backend = backend;
    request.latex = {latex.data(), latex.size()};
    texsolve_result *result = nullptr;
    const auto status = texsolve_execute(context, &request, &result);
    BOOST_REQUIRE_EQUAL(status, TEXSOLVE_STATUS_OK);
    BOOST_REQUIRE(result != nullptr);
    return result;
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
