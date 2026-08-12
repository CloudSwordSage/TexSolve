#define BOOST_TEST_MODULE TexSolveContext
#include <boost/test/included/unit_test.hpp>

#include <cstring>
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
