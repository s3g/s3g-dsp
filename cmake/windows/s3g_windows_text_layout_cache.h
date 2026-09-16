#pragma once

#if defined(_WIN32)
#include <dwrite.h>
#include <cstdint>
#include <cwchar>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace s3g::windows_gui {

// Used only by the Windows renderer's private measure/draw paths. Returned
// layouts are immutable and carry a caller reference. Public VSTGUI path
// construction continues to receive a fresh, mutable layout.
class TextLayoutCache {
public:
    static constexpr std::size_t capacity = 256;
    static constexpr std::size_t maximumTextLength = 512;

    IDWriteTextLayout* get(IDWriteFactory* factory, IDWriteTextFormat* format,
        const wchar_t* text, bool underline, bool strike) {
        if (!factory || !format || !text) return nullptr;
        const auto length = std::wcslen(text);
        if (length > maximumTextLength)
            return create(factory, format, text, length, underline, strike);
        Key key {format, std::wstring(text, length), underline, strike};
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = index_.find(key);
        if (found != index_.end()) {
            entries_.splice(entries_.begin(), entries_, found->second);
            auto* layout = found->second->layout.get();
            layout->AddRef();
            return layout;
        }
        Layout layout(create(factory, format, text, length, underline, strike));
        if (!layout) return nullptr;
        if (entries_.size() == capacity) {
            index_.erase(entries_.back().key);
            entries_.pop_back();
        }
        entries_.push_front({std::move(key), std::move(layout)});
        try { index_.emplace(entries_.front().key, entries_.begin()); }
        catch (...) { entries_.pop_front(); throw; }
        auto* result = entries_.front().layout.get();
        result->AddRef();
        return result;
    }

    // Called before the owning VSTGUI font releases its immutable text format.
    // No format address may survive here and collide with a later allocation.
    void erase(IDWriteTextFormat* format) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->key.format == format) {
                index_.erase(it->key);
                it = entries_.erase(it);
            } else ++it;
        }
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        index_.clear();
        entries_.clear();
    }
    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

private:
    struct Release { void operator()(IDWriteTextLayout* p) const { if (p) p->Release(); } };
    using Layout = std::unique_ptr<IDWriteTextLayout, Release>;
    struct Key {
        IDWriteTextFormat* format;
        std::wstring text;
        bool underline, strike;
        bool operator==(const Key& other) const {
            return format == other.format && underline == other.underline
                && strike == other.strike && text == other.text;
        }
    };
    struct Hash {
        std::size_t operator()(const Key& key) const {
            return std::hash<void*>{}(key.format) ^ std::hash<std::wstring>{}(key.text)
                ^ (std::size_t(key.underline) << 1) ^ (std::size_t(key.strike) << 2);
        }
    };
    struct Entry { Key key; Layout layout; };
    static IDWriteTextLayout* create(IDWriteFactory* factory, IDWriteTextFormat* format,
        const wchar_t* text, std::size_t length, bool underline, bool strike) {
        IDWriteTextLayout* layout = nullptr;
        if (FAILED(factory->CreateTextLayout(text, static_cast<UINT32>(length),
                format, 10000, 1000, &layout))) return nullptr;
        const DWRITE_TEXT_RANGE range {0, UINT32_MAX};
        if (underline) layout->SetUnderline(true, range);
        if (strike) layout->SetStrikethrough(true, range);
        return layout;
    }
    mutable std::mutex mutex_;
    std::list<Entry> entries_;
    std::unordered_map<Key, std::list<Entry>::iterator, Hash> index_;
};

} // namespace s3g::windows_gui
#endif
