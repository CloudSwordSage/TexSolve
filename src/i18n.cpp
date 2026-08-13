#include "i18n.hpp"

#include <algorithm>
#include <cctype>
#include <clocale>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <string>

#include <libintl.h>
#include <windows.h>

namespace texsolve::i18n {
namespace {

int module_anchor;

std::filesystem::path module_directory() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&module_anchor), &module)) {
        return {};
    }
    wchar_t path[32768];
    const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    return length == 0 || length == std::size(path)
               ? std::filesystem::path{}
               : std::filesystem::path(path, path + length).parent_path();
}

std::string utf8_path(const std::filesystem::path &path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}

}  // namespace

void initialize() {
    static std::once_flag once;
    std::call_once(once, [] {
        const char *lang_value = std::getenv("LANG");
        std::string lang = lang_value == nullptr ? "" : lang_value;
        std::ranges::transform(lang, lang.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        const char *locale = lang.starts_with("zh_cn") ? "zh_CN.UTF-8"
                           : lang.starts_with("en_us") ? "en_US.UTF-8"
                                                       : "";
        if (libintl_setlocale(LC_ALL, locale) == nullptr) {
            libintl_setlocale(LC_ALL, ".UTF-8");
        }

        std::filesystem::path locale_dir;
        if (const char *configured = std::getenv("TEXSOLVE_LOCALE_DIR");
            configured != nullptr && *configured != '\0') {
            locale_dir = configured;
        } else {
            const auto directory = module_directory();
            const auto build_tree = directory / "locale";
            locale_dir = std::filesystem::exists(build_tree)
                             ? build_tree
                             : directory.parent_path() / "share" / "locale";
        }
        const auto path = utf8_path(locale_dir);
        bindtextdomain("texsolve", path.c_str());
        bind_textdomain_codeset("texsolve", "UTF-8");
        textdomain("texsolve");
    });
}

std::string translate(std::string_view message) {
    initialize();
    static constexpr std::string_view prefix = "multiple integral normalization failed: ";
    if (message.starts_with(prefix)) {
        return std::string(dgettext("texsolve", prefix.data())) + std::string(message.substr(prefix.size()));
    }
    const std::string id(message);
    return dgettext("texsolve", id.c_str());
}

}  // namespace texsolve::i18n
