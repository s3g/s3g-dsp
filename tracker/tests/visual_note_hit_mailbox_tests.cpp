#include "s3g/tracker/visual_note_hit_mailbox.h"

#include <cstdint>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void testSingleConsumption()
{
    s3g::tracker::VisualNoteHitMailbox mailbox;
    s3g::tracker::VisualNoteHitEvent event;
    uint64_t consumed = 0u;

    check(!mailbox.readLatest(consumed, event),
        "an empty visual mailbox must not invent a hit");
    mailbox.publish(7u, 1234u);
    check(mailbox.readLatest(consumed, event)
            && event.row == 7u
            && event.absoluteSampleTime == 1234u,
        "a published hit must retain its row and sample timestamp");
    check(!mailbox.readLatest(consumed, event),
        "the same visual hit must be consumed only once");
}

void testRepeatedRowStillPublishes()
{
    s3g::tracker::VisualNoteHitMailbox mailbox;
    s3g::tracker::VisualNoteHitEvent event;
    uint64_t consumed = 0u;

    mailbox.publish(3u, 100u);
    check(mailbox.readLatest(consumed, event),
        "the first repeated-row fixture hit must be visible");
    mailbox.publish(3u, 200u);
    check(mailbox.readLatest(consumed, event)
            && event.row == 3u
            && event.absoluteSampleTime == 200u,
        "a later hit on the same row must have a distinct publication");
}

void testLatestHitWinsBetweenGuiPolls()
{
    s3g::tracker::VisualNoteHitMailbox mailbox;
    s3g::tracker::VisualNoteHitEvent event;
    uint64_t consumed = 0u;

    mailbox.publish(1u, 1000u);
    mailbox.publish(2u, 2000u);
    check(mailbox.readLatest(consumed, event)
            && event.row == 2u
            && event.absoluteSampleTime == 2000u,
        "a slow GUI poll must receive the most recent completed hit");
    check(!mailbox.readLatest(consumed, event),
        "the latest coalesced hit must also be single-consumption");
}

} // namespace

int main()
{
    testSingleConsumption();
    testRepeatedRowStillPublishes();
    testLatestHitWinsBetweenGuiPolls();
    if (failures != 0) {
        std::cerr << failures << " visual note-hit mailbox test(s) failed\n";
        return 1;
    }
    std::cout << "visual note-hit mailbox tests passed\n";
    return 0;
}
