#include <cstdint>
#include <string_view>
#include <type_traits>

#include <texsolve/texsolve.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int main() {
    static_assert(std::is_same_v<texsolve_status, std::int32_t>);
    static_assert(TEXSOLVE_STATUS_INTERNAL_ERROR == 14);
    static_assert(TEXSOLVE_OPERATION_DEFINE == 14);
    static_assert(TEXSOLVE_RESULT_METADATA == 15);
    static_assert(TEXSOLVE_REQUEST_V1_SIZE == sizeof(texsolve_request));
    texsolve_context_options options{};
    options.struct_size = sizeof(options);
    options.abi_version = TEXSOLVE_ABI_VERSION;
    options.precision_digits = 30;
    texsolve_context *context = nullptr;
    CHECK(texsolve_context_create(&context) == TEXSOLVE_STATUS_OK);
    CHECK(texsolve_context_configure(context, &options) == TEXSOLVE_STATUS_OK);
    options.symbolic_backend = 3;
    CHECK(texsolve_context_configure(context, &options) == TEXSOLVE_STATUS_INVALID_ARGUMENT);
    options.symbolic_backend = TEXSOLVE_SYMBOLIC_AUTO;
    constexpr std::string_view input = "1+1";
    texsolve_request request{};
    request.struct_size = sizeof(request);
    request.abi_version = TEXSOLVE_ABI_VERSION;
    request.latex = {input.data(), input.size()};
    texsolve_result *result = nullptr;
    CHECK(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_OK);
    CHECK(texsolve_result_kind(result) == TEXSOLVE_RESULT_INTEGER);
    CHECK(texsolve_result_metadata(result) != nullptr);
    texsolve_result_destroy(result);
    CHECK(texsolve_context_reset(context) == TEXSOLVE_STATUS_OK);
    texsolve_context_destroy(context);
    return 0;
}
