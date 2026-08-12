#define BOOST_TEST_MODULE TexSolveContext
#include <boost/test/included/unit_test.hpp>

#include <cstring>
#include <string>
#include <string_view>

#include <texsolve/texsolve.h>

namespace {

texsolve_request request_for(std::string_view latex) {
    texsolve_request request{};
    request.struct_size = sizeof(request);
    request.abi_version = TEXSOLVE_ABI_VERSION;
    request.operation = TEXSOLVE_OPERATION_AUTO;
    request.latex = {latex.data(), latex.size()};
    return request;
}

std::string_view name_of(const texsolve_result *result) {
    const auto value = texsolve_result_name(result);
    return {value.data, value.size};
}

std::string text_of(texsolve_string_view value) {
    return value.data == nullptr ? std::string{} : std::string(value.data, value.size);
}

}  // namespace

BOOST_AUTO_TEST_CASE(definitions_commit_atomically_and_reset) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);

    auto variable = request_for("x:=1");
    texsolve_result *result = nullptr;
    BOOST_TEST(texsolve_execute(context, &variable, &result) == TEXSOLVE_STATUS_OK);
    texsolve_result_destroy(result);

    auto invalid = request_for("f(x,x):=x");
    BOOST_TEST(texsolve_execute(context, &invalid, &result) == TEXSOLVE_STATUS_SEMANTIC_ERROR);
    texsolve_result_destroy(result);

    texsolve_result *snapshot = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_snapshot(context, &snapshot), TEXSOLVE_STATUS_OK);
    const auto *variables = texsolve_result_child(snapshot, 0);
    const auto *functions = texsolve_result_child(snapshot, 1);
    BOOST_REQUIRE_EQUAL(texsolve_result_child_count(variables), 1u);
    BOOST_TEST(name_of(texsolve_result_child(variables, 0)) == "x");
    BOOST_TEST(texsolve_result_child_count(functions) == 0u);
    texsolve_result_destroy(snapshot);

    BOOST_TEST(texsolve_context_reset(context) == TEXSOLVE_STATUS_OK);
    BOOST_REQUIRE_EQUAL(texsolve_context_snapshot(context, &snapshot), TEXSOLVE_STATUS_OK);
    BOOST_TEST(texsolve_result_child_count(texsolve_result_child(snapshot, 0)) == 0u);
    texsolve_result_destroy(snapshot);
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(user_functions_expand_with_checked_arity) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    auto definition = request_for("f(x,y):=x^2+y");
    texsolve_result *result = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_execute(context, &definition, &result), TEXSOLVE_STATUS_OK);
    texsolve_result_destroy(result);

    auto call = request_for("f(3,4)");
    BOOST_REQUIRE_EQUAL(texsolve_execute(context, &call, &result), TEXSOLVE_STATUS_OK);
    BOOST_TEST(text_of(texsolve_result_exact_latex(result)) == "13");
    texsolve_result_destroy(result);

    auto wrong_arity = request_for("f(3)");
    BOOST_TEST(texsolve_execute(context, &wrong_arity, &result) == TEXSOLVE_STATUS_SEMANTIC_ERROR);
    texsolve_result_destroy(result);
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(user_function_expansion_honors_request_limits) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    texsolve_result *result = nullptr;
    for (const std::string definition : {"f(x):=x+x", "g(x):=f(x)+f(x)", "h(x):=g(x)+g(x)"}) {
        auto request = request_for(definition);
        BOOST_REQUIRE_EQUAL(texsolve_execute(context, &request, &result), TEXSOLVE_STATUS_OK);
        texsolve_result_destroy(result);
    }
    auto nodes = request_for("h(1)");
    nodes.max_ast_nodes = 10;
    BOOST_TEST(texsolve_execute(context, &nodes, &result) == TEXSOLVE_STATUS_RESOURCE_LIMIT);
    texsolve_result_destroy(result);
    auto depth = request_for("g(1)");
    depth.max_nesting_depth = 1;
    BOOST_TEST(texsolve_execute(context, &depth, &result) == TEXSOLVE_STATUS_RESOURCE_LIMIT);
    texsolve_result_destroy(result);
    auto unary_definition = request_for("u(x):=-----x");
    BOOST_REQUIRE_EQUAL(texsolve_execute(context, &unary_definition, &result), TEXSOLVE_STATUS_OK);
    texsolve_result_destroy(result);
    auto combined_depth = request_for("u(-----1)");
    combined_depth.max_nesting_depth = 5;
    BOOST_TEST(texsolve_execute(context, &combined_depth, &result) == TEXSOLVE_STATUS_RESOURCE_LIMIT);
    texsolve_result_destroy(result);
    texsolve_context_destroy(context);
}
