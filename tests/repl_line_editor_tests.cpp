#include "repl_line_editor.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
#include <vector>

int main() {
    const std::vector<std::wstring> history{L"first", L"second"};
    ReplLineEditor editor;

    editor.insert(L'x');
    assert(!editor.previous(history));
    assert(editor.text() == L"x");
    assert(editor.cancel());
    assert(editor.text().empty());

    assert(editor.previous(history));
    assert(editor.text() == L"second");
    assert(editor.previous(history));
    assert(editor.text() == L"first");
    assert(editor.next(history));
    assert(editor.text() == L"second");
    assert(editor.next(history));
    assert(editor.text().empty());

    assert(editor.previous(history));
    assert(editor.left());
    assert(editor.right());
    assert(editor.left());
    editor.insert(L'!');
    assert(editor.text() == L"secon!d");
    editor.home();
    assert(editor.erase());
    editor.end();
    assert(editor.backspace());
    assert(editor.text() == L"econ!");
    assert(editor.cancel());
    assert(!editor.cancel());

    ReplLineEditor unicode;
    unicode.insert(static_cast<wchar_t>(0xD83D));
    unicode.insert(static_cast<wchar_t>(0xDE00));
    assert(unicode.left() && unicode.cursor() == 0);
    assert(unicode.right() && unicode.cursor() == 2);
    assert(unicode.backspace() && unicode.text().empty());
}
