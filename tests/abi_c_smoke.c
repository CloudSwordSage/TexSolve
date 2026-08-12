#include <assert.h>
#include <string.h>

#include <texsolve/texsolve.h>

int main(void) {
    texsolve_context *context = NULL;
    texsolve_result *snapshot = NULL;
    assert(texsolve_abi_version() == TEXSOLVE_ABI_VERSION);
    assert(texsolve_context_create(&context) == TEXSOLVE_STATUS_OK);
    assert(context != NULL);
    assert(texsolve_context_snapshot(context, &snapshot) == TEXSOLVE_STATUS_OK);
    assert(snapshot != NULL);
    assert(texsolve_result_kind(snapshot) == TEXSOLVE_RESULT_MAPPING);
    assert(texsolve_result_child_count(snapshot) == 3);
    texsolve_result_destroy(snapshot);
    texsolve_context_destroy(context);
    texsolve_result_destroy(NULL);
    texsolve_context_destroy(NULL);
    return 0;
}
