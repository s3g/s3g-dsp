#pragma once

#include <algorithm>
#include <cstddef>

namespace s3g::tracker::app {

struct GridSelectionRange {
    std::size_t page = 0u;
    std::size_t firstTrack = 0u;
    std::size_t lastTrack = 0u;
    std::size_t firstField = 0u;
    std::size_t lastField = 0u;
    std::size_t firstRow = 0u;
    std::size_t lastRow = 0u;

    constexpr std::size_t trackCount() const noexcept
    {
        return lastTrack >= firstTrack ? lastTrack - firstTrack + 1u : 0u;
    }

    constexpr std::size_t fieldCount() const noexcept
    {
        return lastField >= firstField ? lastField - firstField + 1u : 0u;
    }

    constexpr std::size_t rowCount() const noexcept
    {
        return lastRow >= firstRow ? lastRow - firstRow + 1u : 0u;
    }

    constexpr bool contains(std::size_t candidatePage,
        std::size_t track, std::size_t field, std::size_t row) const noexcept
    {
        return page == candidatePage && track >= firstTrack
            && track <= lastTrack && field >= firstField
            && field <= lastField && row >= firstRow && row <= lastRow;
    }
};

struct GridSelection {
    bool active = false;
    std::size_t page = 0u;
    std::size_t anchorTrack = 0u;
    std::size_t anchorField = 0u;
    std::size_t anchorRow = 0u;
    std::size_t focusTrack = 0u;
    std::size_t focusField = 0u;
    std::size_t focusRow = 0u;

    constexpr GridSelectionRange range() const noexcept
    {
        return {
            page,
            std::min(anchorTrack, focusTrack),
            std::max(anchorTrack, focusTrack),
            std::min(anchorField, focusField),
            std::max(anchorField, focusField),
            std::min(anchorRow, focusRow),
            std::max(anchorRow, focusRow),
        };
    }

    constexpr bool isSingleCell() const noexcept
    {
        const auto value = range();
        return value.trackCount() == 1u && value.fieldCount() == 1u
            && value.rowCount() == 1u;
    }
};

constexpr std::size_t gridClipboardColumn(std::size_t track,
    std::size_t field, std::size_t fieldsPerTrack) noexcept
{
    return track * fieldsPerTrack + field;
}

constexpr void gridAddressForClipboardColumn(std::size_t column,
    std::size_t fieldsPerTrack, std::size_t& track,
    std::size_t& field) noexcept
{
    if (fieldsPerTrack == 0u) {
        track = 0u;
        field = 0u;
        return;
    }
    track = column / fieldsPerTrack;
    field = column % fieldsPerTrack;
}

} // namespace s3g::tracker::app
