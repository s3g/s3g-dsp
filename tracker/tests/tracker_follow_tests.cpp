#include "s3g/tracker/clap_document_controller.h"
#include "s3g/tracker/editor_grid_controller.h"
#include "s3g/tracker/tracker_follow.h"
#include <cmath>
#include <iostream>

using namespace s3g::tracker;
int main()
{
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << message << '\n';
        }
    };
    for (double zoom : { .55, .75, 1., 1.16, 1.8 })
        for (std::size_t rows : { 1u, 3u, 16u, 31u, 64u, 1024u }) {
            const double viewport = 478. / zoom;
            TrackerFollowLayout center(TrackerFollowMode::Center, rows, viewport, 10. / zoom);
            TrackerFollowLayout page(TrackerFollowMode::Page, rows, viewport, 10. / zoom);
            TrackerFollowLayout fixed(TrackerFollowMode::Static, rows, viewport, 10. / zoom);
            check(fixed.minimum == 0. && fixed.maximum == std::max(0., 86. + rows * 25. - viewport),
                "STATIC geometry changed");
            check(page.pageRows <= 16 && page.pageRows * 25. <= page.bodyHeight,
                "page includes clipped rows");
            for (std::size_t row = 0; row < rows; ++row) {
                const auto y = center.scrollForRow(row);
                check(y >= center.minimum && y <= center.maximum
                        && std::abs((row * 25. - y + 12.5) - center.bodyHeight * .5) < 1e-9,
                    "CENTER must retain a fixed row center even at pattern boundaries");
                const auto p = page.scrollForRow(row);
                check(p >= page.minimum && p <= page.maximum
                        && p == double(row / page.pageRows * page.pageRows) * 25.,
                    "PAGE alignment drifted");
            }
        }
    check(TrackerFollowLayout(TrackerFollowMode::Page, 64, 478. / .55, 10. / .55).pageRows == 16
            && TrackerFollowLayout(TrackerFollowMode::Page, 64, 478).pageRows == 8
            && TrackerFollowLayout(TrackerFollowMode::Page, 64, 478. / 1.8, 10. / 1.8).pageRows
                == 4,
        "adaptive page sizes must be predictable at supported zooms");
    TrackerFollowLayout empty(TrackerFollowMode::Center, 0, 0);
    check(empty.rows == 1 && std::isfinite(empty.scrollForRow(999)), "empty viewport safe");

    ProjectDocument document, decoded;
    std::string json;
    check(encodeProjectDocument(document, json).ok()
            && json.find("trackerFollow") == std::string::npos,
        "default encoding must remain byte-compatible with old documents");
    decoded.session.trackerFollow = { TrackerFollowMode::Page, true, 7 };
    check(decodeProjectDocument(json, decoded).ok()
            && decoded.session.trackerFollow == TrackerFollowSettings {},
        "missing settings default STATIC");
    for (auto mode :
        { TrackerFollowMode::Static, TrackerFollowMode::Center, TrackerFollowMode::Page })
        for (bool selected : { false, true }) {
            document.session.trackerFollow = { mode, selected, 31 };
            check(encodeProjectDocument(document, json).ok()
                    && decodeProjectDocument(json, decoded).ok()
                    && decoded.session.trackerFollow == document.session.trackerFollow,
                "follow settings failed project roundtrip");
        }
    const auto before = decoded.session.trackerFollow;
    for (const auto& replacement :
        { std::pair<const char*, const char*> { "\"mode\": 2", "\"mode\": 3" },
            { "\"lane\": 31", "\"lane\": 32" }, { "\"lane\": 31", "\"lane\": -1" },
            { "\"selectedLane\": true", "\"selectedLane\": 1" } }) {
        auto bad = json;
        const auto at = bad.find(replacement.first, bad.find("\"trackerFollow\""));
        check(at != std::string::npos, "invalid fixture field missing");
        if (at != std::string::npos)
            bad.replace(at, std::string(replacement.first).size(), replacement.second);
        check(!decodeProjectDocument(bad, decoded).ok() && decoded.session.trackerFollow == before,
            "invalid follow setting must reject transactionally");
    }
    document.session.trackerFollow.mode = static_cast<TrackerFollowMode>(3);
    check(!encodeProjectDocument(document, json).ok(), "invalid enum encoded");
    document.session.trackerFollow = { TrackerFollowMode::Center, false, 32 };
    check(!encodeProjectDocument(document, json).ok(), "invalid lane encoded");

    app::TrackerViewState state;
    ClapDocumentController documents(state);
    state.trackerFollow = { TrackerFollowMode::Center, false, 2 };
    auto saved = documents.snapshot({});
    check(saved.session.trackerFollow == state.trackerFollow, "CLAP snapshot lost follow settings");
    check(documents.resetHistory(saved).ok(), "follow history reset");
    state.trackerFollow = { TrackerFollowMode::Page, true, 1 };
    check(documents.recordHistory(documents.snapshot({})).ok(), "follow history record");
    ProjectDocument restored;
    check(documents.undo(restored).ok(), "follow undo");
    auto revision = state.trackerFollowRevision;
    documents.apply(restored);
    check(state.trackerFollow == saved.session.trackerFollow
            && state.trackerFollowRevision > revision,
        "document apply must restore settings and reset transient hold");
    check(documents.redo(restored).ok(), "follow redo");
    documents.apply(restored);
    check(state.trackerFollow.mode == TrackerFollowMode::Page && state.trackerFollow.selectedLane,
        "redo lost source mode");

    for (std::size_t source = 0; source < 4; ++source)
        for (std::size_t destination = 0; destination < 4; ++destination)
            for (std::size_t pinned = 0; pinned < 4; ++pinned) {
                state.session.pattern.tracks.resize(4);
                for (std::size_t lane = 0; lane < 4; ++lane)
                    state.session.pattern.tracks[lane].name = std::to_string(lane);
                state.session.selectedTrack = source;
                state.trackerFollow.lane = static_cast<uint32_t>(pinned);
                app::WorkspaceCallbacks callbacks;
                editor::GridController grid(state, callbacks, {});
                grid.moveCompleteLane(int(destination));
                check(state.session.pattern.tracks[state.trackerFollow.lane].name
                        == std::to_string(pinned),
                    "pinned source did not travel with reordered lane");
            }
    std::cout << checks << " follow checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
