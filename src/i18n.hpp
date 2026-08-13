#ifndef TEXSOLVE_I18N_HPP
#define TEXSOLVE_I18N_HPP

#include <cstddef>
#include <string>
#include <string_view>

namespace texsolve::i18n {

/** Initialize the TexSolve GetText domain from the process locale environment. */
void initialize();

/**
 * Translate one user-facing message.
 *
 * Args:
 *     message: English GetText message id.
 * Returns:
 *     std::string: UTF-8 translation selected by LANG.
 */
std::string translate(std::string_view message);

}  // namespace texsolve::i18n

#endif
