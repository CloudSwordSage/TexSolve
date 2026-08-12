#include <cassert>
#include <cstdint>
#include <type_traits>

#include <texsolve/texsolve.h>

int main() {
    static_assert(std::is_same_v<texsolve_status, std::int32_t>);
    static_assert(TEXSOLVE_STATUS_INTERNAL_ERROR == 14);
    static_assert(TEXSOLVE_OPERATION_DEFINE == 14);
    static_assert(TEXSOLVE_RESULT_METADATA == 15);
    texsolve_context_options options{};
    options.struct_size = sizeof(options);
    options.abi_version = TEXSOLVE_ABI_VERSION;
    options.precision_digits = 30;
    texsolve_context *context = nullptr;
    assert(texsolve_context_create(&context) == TEXSOLVE_STATUS_OK);
    assert(texsolve_context_configure(context, &options) == TEXSOLVE_STATUS_OK);
    assert(texsolve_context_reset(context) == TEXSOLVE_STATUS_OK);
    texsolve_context_destroy(context);
}
