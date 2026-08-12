#define BOOST_TEST_MODULE TexSolveLimits
#include <boost/test/included/unit_test.hpp>

#include <algorithm>
#include <chrono>
#include <future>
#include <string>
#include <string_view>
#include <vector>

#include <texsolve/texsolve.h>

namespace {

texsolve_status execute(texsolve_context *context, std::string_view input,
                        uint32_t input_limit = 0, uint32_t depth = 0, uint32_t nodes = 0) {
    texsolve_request request{};
    request.struct_size = sizeof(request);
    request.abi_version = TEXSOLVE_ABI_VERSION;
    request.latex = {input.data(), input.size()};
    request.max_input_bytes = input_limit;
    request.max_nesting_depth = depth;
    request.max_ast_nodes = nodes;
    texsolve_result *result = nullptr;
    const auto status = texsolve_execute(context, &request, &result);
    texsolve_result_destroy(result);
    return status;
}

texsolve_status execute_ginac(texsolve_context *context, std::string_view input) {
    texsolve_request request{};
    request.struct_size = sizeof(request);
    request.abi_version = TEXSOLVE_ABI_VERSION;
    request.symbolic_backend = TEXSOLVE_SYMBOLIC_GINAC;
    request.latex = {input.data(), input.size()};
    texsolve_result *result = nullptr;
    const auto status = texsolve_execute(context, &request, &result);
    texsolve_result_destroy(result);
    return status;
}

int diagnostic_code(const texsolve_result *result) {
    texsolve_diagnostic diagnostic{};
    diagnostic.struct_size = sizeof(diagnostic);
    return texsolve_result_diagnostic(result, 0, &diagnostic) == TEXSOLVE_STATUS_OK
               ? diagnostic.code : TEXSOLVE_DIAGNOSTIC_NONE;
}

}  // namespace

BOOST_AUTO_TEST_CASE(enforces_input_depth_node_and_precision_limits) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    BOOST_TEST(execute(context, "1234", 4) == TEXSOLVE_STATUS_OK);
    BOOST_TEST(execute(context, "12345", 4) == TEXSOLVE_STATUS_RESOURCE_LIMIT);
    BOOST_TEST(execute(context, "((x))", 0, 2) == TEXSOLVE_STATUS_OK);
    BOOST_TEST(execute(context, "(((x)))", 0, 2) == TEXSOLVE_STATUS_RESOURCE_LIMIT);
    BOOST_TEST(execute(context, "1+2", 0, 0, 3) == TEXSOLVE_STATUS_OK);
    BOOST_TEST(execute(context, "1+2+3", 0, 0, 3) == TEXSOLVE_STATUS_RESOURCE_LIMIT);

    texsolve_request request{};
    request.struct_size = sizeof(request);
    request.abi_version = TEXSOLVE_ABI_VERSION;
    request.precision_digits = 10001;
    const std::string input = "1";
    request.latex = {input.data(), input.size()};
    texsolve_result *result = nullptr;
    BOOST_TEST(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_RESOURCE_LIMIT);
    texsolve_result_destroy(result);
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(enforces_every_request_and_context_hard_limit) {
    struct Limit {
        uint32_t texsolve_request::*request_field;
        uint32_t texsolve_context_options::*option_field;
        uint32_t maximum;
        int diagnostic;
    };
    const Limit limits[] = {
        {&texsolve_request::precision_digits, &texsolve_context_options::precision_digits, 10000, TEXSOLVE_DIAGNOSTIC_PRECISION_LIMIT},
        {&texsolve_request::max_iterations, &texsolve_context_options::max_iterations, 10000000, TEXSOLVE_DIAGNOSTIC_ITERATION_LIMIT},
        {&texsolve_request::deadline_ms, &texsolve_context_options::deadline_ms, 3600000, TEXSOLVE_DIAGNOSTIC_DEADLINE},
        {&texsolve_request::max_input_bytes, &texsolve_context_options::max_input_bytes, 16777216, TEXSOLVE_DIAGNOSTIC_INPUT_LIMIT},
        {&texsolve_request::max_nesting_depth, &texsolve_context_options::max_nesting_depth, 4096, TEXSOLVE_DIAGNOSTIC_NESTING_LIMIT},
        {&texsolve_request::max_ast_nodes, &texsolve_context_options::max_ast_nodes, 5000000, TEXSOLVE_DIAGNOSTIC_AST_NODE_LIMIT},
    };
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    const std::string input = "1";
    for (const auto &limit : limits) {
        texsolve_request request{};
        request.struct_size = sizeof(request);
        request.abi_version = TEXSOLVE_ABI_VERSION;
        request.latex = {input.data(), input.size()};
        request.*limit.request_field = limit.maximum;
        texsolve_result *result = nullptr;
        BOOST_TEST(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_OK);
        texsolve_result_destroy(result);
        request.*limit.request_field = limit.maximum + 1;
        result = nullptr;
        BOOST_TEST(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_RESOURCE_LIMIT);
        BOOST_REQUIRE(result != nullptr);
        BOOST_TEST(diagnostic_code(result) == limit.diagnostic);
        texsolve_result_destroy(result);

        texsolve_context_options options{};
        options.struct_size = sizeof(options);
        options.abi_version = TEXSOLVE_ABI_VERSION;
        options.*limit.option_field = limit.maximum;
        BOOST_TEST(texsolve_context_configure(context, &options) == TEXSOLVE_STATUS_OK);
        options.*limit.option_field = limit.maximum + 1;
        BOOST_TEST(texsolve_context_configure(context, &options) == TEXSOLVE_STATUS_RESOURCE_LIMIT);
    }
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(cooperative_deadline_stops_a_long_fold) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    const std::string input = R"(\sum_{k=1}^{10000000}k)";
    texsolve_request request{};
    request.struct_size = sizeof(request);
    request.abi_version = TEXSOLVE_ABI_VERSION;
    request.deadline_ms = 1;
    request.max_iterations = 10000000;
    request.latex = {input.data(), input.size()};
    texsolve_result *result = nullptr;
    BOOST_TEST(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_DEADLINE_EXCEEDED);
    BOOST_REQUIRE(result != nullptr);
    BOOST_TEST(diagnostic_code(result) == TEXSOLVE_DIAGNOSTIC_DEADLINE);
    texsolve_result_destroy(result);
    texsolve_context_destroy(context);
}

BOOST_AUTO_TEST_CASE(independent_contexts_execute_in_parallel) {
    std::vector<std::future<bool>> workers;
    for (int worker = 0; worker < 8; ++worker) {
        workers.push_back(std::async(std::launch::async, [] {
            texsolve_context *context = nullptr;
            if (texsolve_context_create(&context) != TEXSOLVE_STATUS_OK) return false;
            bool ok = true;
            for (int iteration = 0; iteration < 50; ++iteration) {
                ok = ok && execute(context, "1+1") == TEXSOLVE_STATUS_OK;
                ok = ok && execute_ginac(context, "2+2") == TEXSOLVE_STATUS_OK;
            }
            texsolve_context_destroy(context);
            return ok;
        }));
    }
    for (auto &worker : workers) BOOST_TEST(worker.get());
}

BOOST_AUTO_TEST_CASE(release_scalar_performance_meets_p95_budget) {
    texsolve_context *context = nullptr;
    BOOST_REQUIRE_EQUAL(texsolve_context_create(&context), TEXSOLVE_STATUS_OK);
    const std::string input = R"(\frac{1}{3}+x^2+\sin{x})";
    const std::string name = "x";
    const std::string value = "2";
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
    std::vector<double> timings;
    for (int index = 0; index < 105; ++index) {
        texsolve_result *result = nullptr;
        const auto start = std::chrono::steady_clock::now();
        BOOST_REQUIRE_EQUAL(texsolve_execute(context, &request, &result), TEXSOLVE_STATUS_OK);
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        texsolve_result_destroy(result);
        if (index >= 5) timings.push_back(elapsed);
    }
    std::sort(timings.begin(), timings.end());
    BOOST_TEST(timings[94] <= 50.0);
    texsolve_context_destroy(context);
}
