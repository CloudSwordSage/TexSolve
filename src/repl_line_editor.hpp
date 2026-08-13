#pragma once

#include <cstddef>
#include <string>
#include <vector>

/** Maintain the editable line and history cursor for the interactive REPL. */
class ReplLineEditor {
public:
    const std::wstring &text() const { return text_; }
    std::size_t cursor() const { return cursor_; }

    void insert(wchar_t character) {
        text_.insert(text_.begin() + static_cast<std::ptrdiff_t>(cursor_), character);
        ++cursor_;
    }

    bool left() {
        if (cursor_ == 0) return false;
        --cursor_;
        if (cursor_ != 0 && low_surrogate(text_[cursor_]) && high_surrogate(text_[cursor_ - 1])) {
            --cursor_;
        }
        return true;
    }

    bool right() {
        if (cursor_ == text_.size()) return false;
        cursor_ += high_surrogate(text_[cursor_]) && cursor_ + 1 < text_.size() &&
                           low_surrogate(text_[cursor_ + 1])
                       ? 2
                       : 1;
        return true;
    }

    void home() { cursor_ = 0; }
    void end() { cursor_ = text_.size(); }

    bool backspace() {
        if (cursor_ == 0) return false;
        const std::size_t end = cursor_;
        left();
        text_.erase(cursor_, end - cursor_);
        return true;
    }

    bool erase() {
        if (cursor_ == text_.size()) return false;
        const std::size_t count = high_surrogate(text_[cursor_]) && cursor_ + 1 < text_.size() &&
                                          low_surrogate(text_[cursor_ + 1])
                                      ? 2
                                      : 1;
        text_.erase(cursor_, count);
        return true;
    }

    bool previous(const std::vector<std::wstring> &history) {
        if (!from_history_) {
            if (!text_.empty() || history.empty()) return false;
            from_history_ = true;
            history_index_ = history.size() - 1;
        } else if (history_index_ != 0) {
            --history_index_;
        } else {
            return false;
        }
        load(history[history_index_]);
        return true;
    }

    bool next(const std::vector<std::wstring> &history) {
        if (!from_history_) return false;
        if (++history_index_ < history.size()) {
            load(history[history_index_]);
        } else {
            text_.clear();
            cursor_ = 0;
            from_history_ = false;
        }
        return true;
    }

    bool cancel() {
        const bool had_input = !text_.empty();
        text_.clear();
        cursor_ = 0;
        from_history_ = false;
        return had_input;
    }

private:
    static bool high_surrogate(wchar_t character) {
        return character >= static_cast<wchar_t>(0xD800) &&
               character <= static_cast<wchar_t>(0xDBFF);
    }

    static bool low_surrogate(wchar_t character) {
        return character >= static_cast<wchar_t>(0xDC00) &&
               character <= static_cast<wchar_t>(0xDFFF);
    }

    void load(const std::wstring &value) {
        text_ = value;
        cursor_ = text_.size();
    }

    std::wstring text_;
    std::size_t cursor_ = 0;
    std::size_t history_index_ = 0;
    bool from_history_ = false;
};
