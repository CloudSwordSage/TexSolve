#include <string.h>

#include <texsolve/texsolve.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int main(int argc, char **argv) {
    const int check_i18n = argc == 2 && strcmp(argv[1], "--i18n") == 0;
    const char *input = check_i18n ? "\\unknown{x}" : "1+1";
    texsolve_context *context = NULL;
    texsolve_result *result = NULL;
    texsolve_request request = {0};
    CHECK(texsolve_abi_version() == TEXSOLVE_ABI_VERSION);
    CHECK(texsolve_context_create(&context) == TEXSOLVE_STATUS_OK);
    request.struct_size = sizeof(request);
    request.abi_version = TEXSOLVE_ABI_VERSION;
    request.latex = (texsolve_string_view){input, strlen(input)};
    if (check_i18n) {
        const char expected[] = "未知命令";
        texsolve_diagnostic diagnostic = {0};
        diagnostic.struct_size = sizeof(diagnostic);
        CHECK(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_PARSE_ERROR);
        CHECK(texsolve_result_diagnostic(result, 0, &diagnostic) == TEXSOLVE_STATUS_OK);
        CHECK(diagnostic.message.size == strlen(expected));
        CHECK(memcmp(diagnostic.message.data, expected, diagnostic.message.size) == 0);
    } else {
        CHECK(texsolve_execute(context, &request, &result) == TEXSOLVE_STATUS_OK);
        CHECK(texsolve_result_kind(result) == TEXSOLVE_RESULT_INTEGER);
        CHECK(texsolve_result_exact_latex(result).size == 1);
    }
    texsolve_result_destroy(result);
    texsolve_context_destroy(context);
    return 0;
}
