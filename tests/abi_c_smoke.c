#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <texsolve/texsolve.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static texsolve_request request_for(const char *text, size_t size) {
    texsolve_request request;
    memset(&request, 0, sizeof(request));
    request.struct_size = sizeof(request);
    request.abi_version = TEXSOLVE_ABI_VERSION;
    request.latex.data = text;
    request.latex.size = size;
    return request;
}

int main(void) {
    texsolve_context *context = NULL;
    texsolve_result *result = NULL;
    CHECK(texsolve_abi_version() == TEXSOLVE_ABI_VERSION);
    CHECK(texsolve_context_create(&context) == TEXSOLVE_STATUS_OK && context != NULL);
    CHECK(texsolve_context_snapshot(context, &result) == TEXSOLVE_STATUS_OK && result != NULL);
    CHECK(texsolve_result_kind(result) == TEXSOLVE_RESULT_MAPPING);
    CHECK(texsolve_result_child_count(result) == 3);
    texsolve_result_destroy(result);

    texsolve_request request = request_for("1", 1);
    request.struct_size = TEXSOLVE_REQUEST_V1_SIZE - 1;
    result = (texsolve_result *)(uintptr_t)1;
    CHECK(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_ABI_MISMATCH);
    CHECK(result == NULL);
    request = request_for("1", 1);
    request.abi_version = TEXSOLVE_ABI_VERSION + 1;
    CHECK(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_ABI_MISMATCH);
    CHECK(texsolve_execute(context, &request, NULL) == TEXSOLVE_STATUS_INVALID_ARGUMENT);

    {
        struct extended_request { texsolve_request base; uint64_t tail; } extended;
        memset(&extended, 0, sizeof(extended));
        extended.base = request_for("1", 1);
        extended.base.struct_size = sizeof(extended);
        extended.tail = UINT64_C(0xfeedface);
        CHECK(texsolve_execute(context, &extended.base, &result) == TEXSOLVE_STATUS_OK);
        texsolve_result_destroy(result);
    }

    texsolve_binding binding;
    memset(&binding, 0, sizeof(binding));
    binding.struct_size = sizeof(binding);
    binding.name = (texsolve_string_view){"x", 1};
    binding.value_latex = (texsolve_string_view){"2", 1};
    request = request_for("x+1", 3);
    request.bindings = &binding;
    request.binding_count = 1;
    request.binding_stride = TEXSOLVE_BINDING_V1_SIZE - 1;
    CHECK(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_INVALID_ARGUMENT);
    request.binding_count = SIZE_MAX;
    request.binding_stride = TEXSOLVE_BINDING_V1_SIZE;
    CHECK(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_INVALID_ARGUMENT);
    request = request_for("x+1", 3);
    request.bindings = &binding;
    request.binding_count = 1;
    request.binding_stride = sizeof(binding);
    request.max_input_bytes = 5;
    CHECK(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_OK);
    texsolve_result_destroy(result);
    request.max_input_bytes = 4;
    CHECK(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_RESOURCE_LIMIT);
    texsolve_result_destroy(result);
    {
        const char bad_utf8[] = {(char)0xc3, (char)0x28};
        binding.value_latex = (texsolve_string_view){bad_utf8, sizeof(bad_utf8)};
        request = request_for("x", 1);
        request.bindings = &binding;
        request.binding_count = 1;
        request.binding_stride = sizeof(binding);
        CHECK(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_INVALID_UTF8);
        texsolve_result_destroy(result);
        binding.value_latex = (texsolve_string_view){"2", 1};
    }

    {
        struct extended_binding { texsolve_binding base; uint64_t tail; } bindings[2];
        memset(bindings, 0, sizeof(bindings));
        bindings[0].base.struct_size = sizeof(bindings[0]);
        bindings[0].base.name = (texsolve_string_view){"x", 1};
        bindings[0].base.value_latex = (texsolve_string_view){"2", 1};
        bindings[1].base.struct_size = sizeof(bindings[1]);
        bindings[1].base.name = (texsolve_string_view){"y", 1};
        bindings[1].base.value_latex = (texsolve_string_view){"3", 1};
        request = request_for("x+y", 3);
        request.bindings = (const texsolve_binding *)bindings;
        request.binding_count = 2;
        request.binding_stride = sizeof(bindings[0]);
        CHECK(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_OK);
        CHECK(texsolve_result_exact_latex(result).size == 1);
        texsolve_result_destroy(result);
    }

    request = request_for("", 0);
    request.residual_count = 1;
    CHECK(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_INVALID_ARGUMENT);
    request.residual_count = SIZE_MAX;
    CHECK(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_INVALID_ARGUMENT);
    {
        const char bad_utf8[] = {(char)0xc3, (char)0x28};
        const texsolve_string_view residual = {bad_utf8, sizeof(bad_utf8)};
        request.residuals = &residual;
        request.residual_count = 1;
        CHECK(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_INVALID_UTF8);
        texsolve_result_destroy(result);
    }

    {
        const char embedded_nul[] = {'1', '\0', '+', '1'};
        request = request_for(embedded_nul, sizeof(embedded_nul));
        CHECK(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_PARSE_ERROR);
        CHECK(result != NULL && texsolve_result_diagnostic_count(result) == 1);
        texsolve_diagnostic diagnostic;
        memset(&diagnostic, 0x5a, sizeof(diagnostic));
        diagnostic.struct_size = TEXSOLVE_DIAGNOSTIC_V1_MIN_SIZE - 1;
        CHECK(texsolve_result_diagnostic(result, 0, &diagnostic) == TEXSOLVE_STATUS_ABI_MISMATCH);
        CHECK(diagnostic.severity == (int32_t)0x5a5a5a5a);
        memset(&diagnostic, 0, sizeof(diagnostic));
        diagnostic.struct_size = sizeof(diagnostic);
        CHECK(texsolve_result_diagnostic(result, 0, &diagnostic) == TEXSOLVE_STATUS_OK);
        CHECK(diagnostic.begin_byte <= diagnostic.end_byte && diagnostic.end_byte <= sizeof(embedded_nul));
        CHECK(diagnostic.message.data != NULL && diagnostic.message.size != 0);
        texsolve_result_destroy(result);
    }

    texsolve_context_destroy(context);
    texsolve_result_destroy(NULL);
    texsolve_context_destroy(NULL);
    return 0;
}
