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
    texsolve_result_destroy(inverse);
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
