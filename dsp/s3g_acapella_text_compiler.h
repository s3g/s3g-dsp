#pragma once

#include "s3g_acapella_source_synth.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace s3g {

struct AcapellaTextCompileResult {
    AcapellaGestureProgram program {};
    uint32_t characterCount = 0u;
    uint32_t syllableCount = 0u;
    uint32_t contextualWordCount = 0u;
};

namespace acapella_text_detail {

inline char lowerAscii(char value)
{
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A')) : value;
}

inline bool isLetter(char value)
{
    value = lowerAscii(value);
    return value >= 'a' && value <= 'z';
}

inline bool isVowelLetter(char value)
{
    value = lowerAscii(value);
    return value == 'a' || value == 'e' || value == 'i'
        || value == 'o' || value == 'u' || value == 'y';
}

inline bool textAt(const char* word, uint32_t length, uint32_t index,
    const char* pattern)
{
    if (!word || !pattern) return false;
    uint32_t offset = 0u;
    while (pattern[offset] != '\0') {
        if (index + offset >= length
            || lowerAscii(word[index + offset]) != pattern[offset]) {
            return false;
        }
        ++offset;
    }
    return true;
}

inline float phonemeDuration(AcapellaPhoneme phoneme, uint8_t stress)
{
    if (acapellaPhonemeIsVowel(phoneme)) {
        if (phoneme == AcapellaPhoneme::AX) return stress == 0u ? 0.42f : 0.58f;
        return stress == 1u ? 0.88f : (stress == 2u ? 0.74f : 0.56f);
    }
    switch (phoneme) {
    case AcapellaPhoneme::P:
    case AcapellaPhoneme::T:
    case AcapellaPhoneme::K: return 0.22f;
    case AcapellaPhoneme::B:
    case AcapellaPhoneme::D:
    case AcapellaPhoneme::G: return 0.25f;
    case AcapellaPhoneme::CH:
    case AcapellaPhoneme::JH: return 0.36f;
    case AcapellaPhoneme::S:
    case AcapellaPhoneme::Z:
    case AcapellaPhoneme::SH:
    case AcapellaPhoneme::ZH: return 0.43f;
    case AcapellaPhoneme::F:
    case AcapellaPhoneme::V:
    case AcapellaPhoneme::TH:
    case AcapellaPhoneme::DH:
    case AcapellaPhoneme::HH: return 0.37f;
    case AcapellaPhoneme::M:
    case AcapellaPhoneme::N:
    case AcapellaPhoneme::NG: return 0.38f;
    case AcapellaPhoneme::L:
    case AcapellaPhoneme::R:
    case AcapellaPhoneme::W:
    case AcapellaPhoneme::Y: return 0.32f;
    case AcapellaPhoneme::Silence: return 0.10f;
    default: return 0.30f;
    }
}

inline float phonemeAmplitude(AcapellaPhoneme phoneme, uint8_t stress)
{
    if (phoneme == AcapellaPhoneme::Silence) return 0.0f;
    if (acapellaPhonemeIsVowel(phoneme)) {
        return stress == 1u ? 1.12f : (stress == 2u ? 1.02f : 0.86f);
    }
    // Consonants define the tract transition; they should not arrive as a
    // second, percussion-like foreground source. Sonorants remain close to
    // vowel level, while stop/fricative noise sits underneath the connected
    // glottal and formant motion.
    const float voicing = acapella_source_detail::phonemeVoicing(phoneme);
    if (voicing > 0.80f) return 0.88f;
    return voicing > 0.20f ? 0.76f : 0.66f;
}

inline bool append(AcapellaTextCompileResult& result,
    AcapellaPhoneme phoneme, uint8_t stress = 0u,
    float durationScale = 0.0f, bool syllableStart = false)
{
    auto& program = result.program;
    if (program.count >= program.steps.size()) {
        program.truncated = true;
        return false;
    }
    if (durationScale <= 0.0f) durationScale = phonemeDuration(phoneme, stress);
    auto& step = program.steps[program.count++];
    step.phoneme = phoneme;
    step.durationScale = durationScale;
    step.amplitude = phonemeAmplitude(phoneme, stress);
    step.stress = std::min<uint8_t>(stress, 2u);
    if (syllableStart) step.flags |= kAcapellaSyllableStart;
    if (syllableStart) ++result.syllableCount;
    return true;
}

inline bool appendDiphthong(AcapellaTextCompileResult& result,
    AcapellaPhoneme start, AcapellaPhoneme end, uint8_t stress)
{
    const float total = stress == 1u ? 1.02f : (stress == 2u ? 0.88f : 0.70f);
    return append(result, start, stress, total * 0.60f, true)
        && append(result, end, 0u, total * 0.40f, false);
}

inline bool appendSymbol(AcapellaTextCompileResult& result,
    const char* token, uint32_t length, uint8_t stress)
{
    const auto equal = [&](const char* symbol) {
        return std::strlen(symbol) == length
            && std::strncmp(token, symbol, length) == 0;
    };
    if (equal("EY")) return appendDiphthong(result,
        AcapellaPhoneme::EH, AcapellaPhoneme::IY, stress);
    if (equal("AY")) return appendDiphthong(result,
        AcapellaPhoneme::AA, AcapellaPhoneme::IY, stress);
    if (equal("OW")) return appendDiphthong(result,
        AcapellaPhoneme::AO, AcapellaPhoneme::UW, stress);
    if (equal("AW")) return appendDiphthong(result,
        AcapellaPhoneme::AE, AcapellaPhoneme::UW, stress);
    if (equal("OY")) return appendDiphthong(result,
        AcapellaPhoneme::AO, AcapellaPhoneme::IY, stress);

    struct Symbol { const char* name; AcapellaPhoneme phoneme; };
    static constexpr Symbol symbols[] {
        { "IY", AcapellaPhoneme::IY }, { "IH", AcapellaPhoneme::IH },
        { "EH", AcapellaPhoneme::EH }, { "AE", AcapellaPhoneme::AE },
        { "AA", AcapellaPhoneme::AA }, { "AO", AcapellaPhoneme::AO },
        { "UH", AcapellaPhoneme::UH }, { "UW", AcapellaPhoneme::UW },
        { "AH", AcapellaPhoneme::AH }, { "AX", AcapellaPhoneme::AX },
        { "ER", AcapellaPhoneme::ER }, { "P", AcapellaPhoneme::P },
        { "B", AcapellaPhoneme::B }, { "T", AcapellaPhoneme::T },
        { "D", AcapellaPhoneme::D }, { "K", AcapellaPhoneme::K },
        { "G", AcapellaPhoneme::G }, { "F", AcapellaPhoneme::F },
        { "V", AcapellaPhoneme::V }, { "TH", AcapellaPhoneme::TH },
        { "DH", AcapellaPhoneme::DH }, { "S", AcapellaPhoneme::S },
        { "Z", AcapellaPhoneme::Z }, { "SH", AcapellaPhoneme::SH },
        { "ZH", AcapellaPhoneme::ZH }, { "HH", AcapellaPhoneme::HH },
        { "CH", AcapellaPhoneme::CH }, { "JH", AcapellaPhoneme::JH },
        { "M", AcapellaPhoneme::M }, { "N", AcapellaPhoneme::N },
        { "NG", AcapellaPhoneme::NG }, { "L", AcapellaPhoneme::L },
        { "R", AcapellaPhoneme::R }, { "W", AcapellaPhoneme::W },
        { "Y", AcapellaPhoneme::Y },
    };
    for (const auto& symbol : symbols) {
        if (equal(symbol.name)) {
            return append(result, symbol.phoneme, stress, 0.0f,
                acapellaPhonemeIsVowel(symbol.phoneme));
        }
    }
    return false;
}

struct LexiconEntry {
    const char* word;
    const char* pronunciation;
};

// A bounded high-frequency/irregular lexicon for conversational and lyric
// English. Productive inflections are derived below, and everything else uses
// deterministic G2P rules, so the plug-in needs no external dictionary or
// runtime service.
inline constexpr LexiconEntry lexicon[] {
    { "a", "AX0" }, { "about", "AX0 B AW1 T" },
    { "above", "AX0 B AH1 V" },
    { "acapella", "AE2 K AX0 P EH1 L AX0" },
    { "after", "AE1 F T ER0" }, { "again", "AX0 G EH1 N" },
    { "against", "AX0 G EH1 N S T" }, { "ain't", "EY1 N T" },
    { "alive", "AX0 L AY1 V" }, { "all", "AO1 L" },
    { "always", "AO1 L W EY2 Z" }, { "am", "AE1 M" },
    { "an", "AE1 N" }, { "and", "AE1 N D" },
    { "another", "AX0 N AH1 DH ER0" }, { "any", "EH1 N IY0" },
    { "anyone", "EH1 N IY0 W AH2 N" }, { "are", "AA1 R" },
    { "aren't", "AA1 R N T" }, { "around", "ER0 AW1 N D" },
    { "as", "AE1 Z" }, { "at", "AE1 T" }, { "away", "AX0 W EY1" },
    { "back", "B AE1 K" }, { "bad", "B AE1 D" },
    { "be", "B IY1" }, { "because", "B IH0 K AO1 Z" },
    { "been", "B IH1 N" }, { "before", "B IH0 F AO1 R" },
    { "being", "B IY1 IH0 NG" }, { "believe", "B IH0 L IY1 V" },
    { "better", "B EH1 T ER0" }, { "black", "B L AE1 K" },
    { "blood", "B L AH1 D" }, { "blue", "B L UW1" },
    { "body", "B AA1 D IY0" }, { "born", "B AO1 R N" },
    { "both", "B OW1 TH" }, { "box", "B AA1 K S" },
    { "break", "B R EY1 K" }, { "breath", "B R EH1 TH" },
    { "bring", "B R IH1 NG" }, { "broken", "B R OW1 K AX0 N" },
    { "burn", "B ER1 N" }, { "but", "B AH1 T" },
    { "by", "B AY1" }, { "call", "K AO1 L" },
    { "came", "K EY1 M" }, { "can", "K AE1 N" },
    { "can't", "K AE1 N T" }, { "change", "CH EY1 N JH" },
    { "child", "CH AY1 L D" }, { "come", "K AH1 M" },
    { "could", "K UH1 D" }, { "couldn't", "K UH1 D AX0 N T" },
    { "cry", "K R AY1" }, { "dark", "D AA1 R K" },
    { "darkness", "D AA1 R K N AX0 S" }, { "day", "D EY1" },
    { "dead", "D EH1 D" }, { "death", "D EH1 TH" },
    { "deep", "D IY1 P" }, { "destroy", "D IH0 S T R OY1" },
    { "did", "D IH1 D" }, { "didn't", "D IH1 D AX0 N T" },
    { "do", "D UW1" }, { "does", "D AH1 Z" },
    { "doesn't", "D AH1 Z AX0 N T" }, { "done", "D AH1 N" },
    { "don't", "D OW1 N T" }, { "door", "D AO1 R" },
    { "down", "D AW1 N" }, { "dream", "D R IY1 M" },
    { "each", "IY1 CH" }, { "earth", "ER1 TH" },
    { "echo", "EH1 K OW0" }, { "eight", "EY1 T" },
    { "end", "EH1 N D" }, { "enough", "IH0 N AH1 F" },
    { "even", "IY1 V AX0 N" }, { "ever", "EH1 V ER0" },
    { "every", "EH1 V R IY0" },
    { "everything", "EH1 V R IY0 TH IH2 NG" },
    { "eyes", "AY1 Z" }, { "face", "F EY1 S" },
    { "fall", "F AO1 L" }, { "feel", "F IY1 L" },
    { "find", "F AY1 N D" }, { "fire", "F AY1 ER0" },
    { "first", "F ER1 S T" }, { "five", "F AY1 V" },
    { "flesh", "F L EH1 SH" }, { "for", "F AO1 R" },
    { "forever", "F ER0 EH1 V ER0" }, { "forget", "F ER0 G EH1 T" },
    { "found", "F AW1 N D" }, { "four", "F AO1 R" },
    { "friend", "F R EH1 N D" }, { "from", "F R AH1 M" },
    { "get", "G EH1 T" }, { "give", "G IH1 V" },
    { "go", "G OW1" }, { "god", "G AA1 D" }, { "gone", "G AO1 N" },
    { "good", "G UH1 D" }, { "got", "G AA1 T" },
    { "great", "G R EY1 T" }, { "ground", "G R AW1 N D" },
    { "growl", "G R AW1 L" }, { "had", "HH AE1 D" },
    { "hadn't", "HH AE1 D AX0 N T" }, { "hand", "HH AE1 N D" },
    { "has", "HH AE1 Z" }, { "hasn't", "HH AE1 Z AX0 N T" },
    { "hate", "HH EY1 T" }, { "have", "HH AE1 V" },
    { "haven't", "HH AE1 V AX0 N T" }, { "he", "HH IY1" },
    { "he'd", "HH IY1 D" }, { "he'll", "HH IY1 L" },
    { "he's", "HH IY1 Z" }, { "head", "HH EH1 D" },
    { "hear", "HH IH1 R" }, { "heart", "HH AA1 R T" },
    { "hell", "HH EH1 L" }, { "hello", "HH AX0 L OW1" },
    { "help", "HH EH1 L P" }, { "her", "HH ER1" },
    { "here", "HH IH1 R" }, { "him", "HH IH1 M" },
    { "his", "HH IH1 Z" }, { "hold", "HH OW1 L D" },
    { "home", "HH OW1 M" }, { "hope", "HH OW1 P" },
    { "how", "HH AW1" }, { "i", "AY1" }, { "i'd", "AY1 D" },
    { "i'll", "AY1 L" }, { "i'm", "AY1 M" }, { "i've", "AY1 V" },
    { "if", "IH1 F" }, { "in", "IH1 N" },
    { "inside", "IH0 N S AY1 D" }, { "into", "IH1 N T UW0" },
    { "is", "IH1 Z" }, { "isn't", "IH1 Z AX0 N T" },
    { "it", "IH1 T" }, { "it's", "IH1 T S" },
    { "just", "JH AH1 S T" }, { "keep", "K IY1 P" },
    { "kill", "K IH1 L" }, { "know", "N OW1" },
    { "last", "L AE1 S T" }, { "leave", "L IY1 V" },
    { "left", "L EH1 F T" }, { "let", "L EH1 T" },
    { "lie", "L AY1" }, { "life", "L AY1 F" },
    { "light", "L AY1 T" }, { "like", "L AY1 K" },
    { "little", "L IH1 T AX0 L" }, { "live", "L IH1 V" },
    { "long", "L AO1 NG" }, { "look", "L UH1 K" },
    { "lost", "L AO1 S T" }, { "loud", "L AW1 D" },
    { "love", "L AH1 V" }, { "made", "M EY1 D" },
    { "make", "M EY1 K" }, { "man", "M AE1 N" },
    { "many", "M EH1 N IY0" }, { "may", "M EY1" },
    { "me", "M IY1" }, { "melody", "M EH1 L AX0 D IY0" },
    { "metal", "M EH1 T AX0 L" }, { "mine", "M AY1 N" },
    { "more", "M AO1 R" }, { "most", "M OW1 S T" },
    { "mother", "M AH1 DH ER0" }, { "mouth", "M AW1 TH" },
    { "move", "M UW1 V" }, { "much", "M AH1 CH" },
    { "music", "M Y UW1 Z IH0 K" }, { "must", "M AH1 S T" },
    { "my", "M AY1" }, { "name", "N EY1 M" },
    { "never", "N EH1 V ER0" }, { "new", "N UW1" },
    { "night", "N AY1 T" }, { "nine", "N AY1 N" },
    { "no", "N OW1" }, { "not", "N AA1 T" },
    { "nothing", "N AH1 TH IH0 NG" }, { "now", "N AW1" },
    { "of", "AX1 V" }, { "off", "AO1 F" }, { "old", "OW1 L D" },
    { "on", "AA1 N" }, { "once", "W AH1 N S" }, { "one", "W AH1 N" },
    { "only", "OW1 N L IY0" }, { "open", "OW1 P AX0 N" },
    { "other", "AH1 DH ER0" }, { "our", "AW1 ER0" },
    { "out", "AW1 T" }, { "outside", "AW1 T S AY1 D" },
    { "over", "OW1 V ER0" }, { "own", "OW1 N" },
    { "pain", "P EY1 N" }, { "people", "P IY1 P AX0 L" },
    { "place", "P L EY1 S" }, { "please", "P L IY1 Z" },
    { "quiet", "K W AY1 AX0 T" }, { "rage", "R EY1 JH" },
    { "rain", "R EY1 N" }, { "real", "R IY1 L" },
    { "red", "R EH1 D" }, { "rhythm", "R IH1 DH AX0 M" },
    { "right", "R AY1 T" }, { "rise", "R AY1 Z" },
    { "rising", "R AY1 Z IH0 NG" }, { "road", "R OW1 D" },
    { "run", "R AH1 N" }, { "said", "S EH1 D" },
    { "same", "S EY1 M" }, { "say", "S EY1" },
    { "scream", "S K R IY1 M" }, { "see", "S IY1" },
    { "set", "S EH1 T" }, { "seven", "S EH1 V AX0 N" },
    { "she", "SH IY1" }, { "she'd", "SH IY1 D" },
    { "she'll", "SH IY1 L" }, { "she's", "SH IY1 Z" },
    { "should", "SH UH1 D" }, { "shouldn't", "SH UH1 D AX0 N T" },
    { "silence", "S AY1 L AX0 N S" }, { "sing", "S IH1 NG" },
    { "six", "S IH1 K S" }, { "skin", "S K IH1 N" },
    { "sky", "S K AY1" }, { "sleep", "S L IY1 P" },
    { "slow", "S L OW1" }, { "some", "S AH1 M" },
    { "someone", "S AH1 M W AH2 N" },
    { "something", "S AH1 M TH IH0 NG" }, { "song", "S AO1 NG" },
    { "soul", "S OW1 L" }, { "sound", "S AW1 N D" },
    { "stand", "S T AE1 N D" }, { "stay", "S T EY1" },
    { "still", "S T IH1 L" }, { "stop", "S T AA1 P" },
    { "strong", "S T R AO1 NG" }, { "take", "T EY1 K" },
    { "tear", "T EH1 R" }, { "tell", "T EH1 L" },
    { "ten", "T EH1 N" }, { "than", "DH AE1 N" },
    { "that", "DH AE1 T" }, { "that's", "DH AE1 T S" },
    { "the", "DH AX0" }, { "their", "DH EH1 R" },
    { "them", "DH EH1 M" }, { "then", "DH EH1 N" },
    { "there", "DH EH1 R" }, { "there's", "DH EH1 R Z" },
    { "these", "DH IY1 Z" }, { "they", "DH EY1" },
    { "they'd", "DH EY1 D" }, { "they'll", "DH EY1 L" },
    { "they're", "DH EH1 R" }, { "they've", "DH EY1 V" },
    { "thing", "TH IH1 NG" }, { "think", "TH IH1 NG K" },
    { "this", "DH IH1 S" }, { "those", "DH OW1 Z" },
    { "three", "TH R IY1" }, { "through", "TH R UW1" },
    { "time", "T AY1 M" }, { "to", "T UW1" },
    { "tonight", "T AX0 N AY1 T" }, { "tongue", "T AH1 NG" },
    { "too", "T UW1" }, { "touch", "T AH1 CH" },
    { "true", "T R UW1" }, { "turn", "T ER1 N" },
    { "two", "T UW1" }, { "under", "AH1 N D ER0" },
    { "up", "AH1 P" }, { "us", "AH1 S" },
    { "violence", "V AY1 AX0 L AX0 N S" },
    { "voice", "V OY1 S" }, { "wait", "W EY1 T" },
    { "walk", "W AO1 K" }, { "want", "W AA1 N T" },
    { "was", "W AH1 Z" }, { "wasn't", "W AA1 Z AX0 N T" },
    { "water", "W AO1 T ER0" }, { "way", "W EY1" },
    { "we", "W IY1" }, { "we'd", "W IY1 D" },
    { "we'll", "W IY1 L" }, { "we're", "W IY1 R" },
    { "we've", "W IY1 V" }, { "were", "W ER1" },
    { "weren't", "W ER1 N T" }, { "what", "W AH1 T" },
    { "what's", "W AH1 T S" }, { "when", "W EH1 N" },
    { "where", "W EH1 R" }, { "which", "W IH1 CH" },
    { "while", "W AY1 L" }, { "white", "W AY1 T" },
    { "who", "HH UW1" }, { "who's", "HH UW1 Z" },
    { "why", "W AY1" }, { "will", "W IH1 L" },
    { "with", "W IH1 TH" }, { "without", "W IH0 DH AW1 T" },
    { "woman", "W UH1 M AX0 N" }, { "women", "W IH1 M AX0 N" },
    { "won't", "W OW1 N T" }, { "word", "W ER1 D" },
    { "words", "W ER1 D Z" }, { "world", "W ER1 L D" },
    { "worlds", "W ER1 L D Z" }, { "would", "W UH1 D" },
    { "wouldn't", "W UH1 D AX0 N T" }, { "wrong", "R AO1 NG" },
    { "yeah", "Y AE1" }, { "yes", "Y EH1 S" }, { "yet", "Y EH1 T" },
    { "you", "Y UW1" }, { "you'd", "Y UW1 D" },
    { "you'll", "Y UW1 L" }, { "you're", "Y UH1 R" },
    { "you've", "Y UW1 V" }, { "young", "Y AH1 NG" },
    { "your", "Y AO1 R" }, { "zero", "Z IH1 R OW0" },
};

inline const char* lexiconPronunciation(const char* word)
{
    for (const auto& entry : lexicon) {
        if (std::strcmp(word, entry.word) == 0) return entry.pronunciation;
    }
    return nullptr;
}

inline bool compilePronunciation(AcapellaTextCompileResult& result,
    const char* pronunciation)
{
    uint32_t index = 0u;
    while (pronunciation[index] != '\0') {
        while (pronunciation[index] == ' ') ++index;
        if (pronunciation[index] == '\0') break;
        const uint32_t begin = index;
        while (pronunciation[index] != '\0'
            && pronunciation[index] != ' ') ++index;
        uint32_t end = index;
        uint8_t stress = 0u;
        if (end > begin && pronunciation[end - 1u] >= '0'
            && pronunciation[end - 1u] <= '2') {
            stress = static_cast<uint8_t>(pronunciation[end - 1u] - '0');
            --end;
        }
        if (!appendSymbol(result, pronunciation + begin, end - begin,
                stress)) return false;
    }
    return true;
}

inline bool wordEndsWith(const char* word, uint32_t length,
    const char* suffix)
{
    const uint32_t suffixLength = static_cast<uint32_t>(std::strlen(suffix));
    return suffixLength <= length
        && std::strncmp(word + length - suffixLength,
            suffix, suffixLength) == 0;
}

inline const char* stemPronunciation(const char* word, uint32_t stemLength,
    std::array<char, 48u>& scratch, char appended = '\0')
{
    if (!word || stemLength == 0u
        || stemLength + (appended != '\0' ? 1u : 0u) >= scratch.size()) {
        return nullptr;
    }
    std::copy_n(word, stemLength, scratch.data());
    uint32_t outputLength = stemLength;
    if (appended != '\0') scratch[outputLength++] = appended;
    scratch[outputLength] = '\0';
    return lexiconPronunciation(scratch.data());
}

enum class DerivedEnding : uint8_t {
    Plural,
    Past,
    Ing,
    Er,
    Ly,
    Ness,
    Less,
    Possessive,
    ContractM,
    ContractD,
    ContractLl,
    ContractRe,
    ContractVe,
    ContractNt,
};

inline bool appendDerivedEnding(AcapellaTextCompileResult& result,
    DerivedEnding ending)
{
    if (result.program.count == 0u) return false;
    const auto finalPhoneme =
        result.program.steps[result.program.count - 1u].phoneme;
    const auto appendSchwaConsonant = [&](AcapellaPhoneme consonant) {
        return append(result, AcapellaPhoneme::AX, 0u, 0.34f, true)
            && append(result, consonant);
    };
    switch (ending) {
    case DerivedEnding::Plural:
    case DerivedEnding::Possessive:
        switch (finalPhoneme) {
        case AcapellaPhoneme::S:
        case AcapellaPhoneme::Z:
        case AcapellaPhoneme::SH:
        case AcapellaPhoneme::ZH:
        case AcapellaPhoneme::CH:
        case AcapellaPhoneme::JH:
            return append(result, AcapellaPhoneme::IH, 0u, 0.34f, true)
                && append(result, AcapellaPhoneme::Z);
        case AcapellaPhoneme::P:
        case AcapellaPhoneme::T:
        case AcapellaPhoneme::K:
        case AcapellaPhoneme::F:
        case AcapellaPhoneme::TH:
            return append(result, AcapellaPhoneme::S);
        default:
            return append(result, AcapellaPhoneme::Z);
        }
    case DerivedEnding::Past:
        if (finalPhoneme == AcapellaPhoneme::T
            || finalPhoneme == AcapellaPhoneme::D) {
            return append(result, AcapellaPhoneme::IH, 0u, 0.34f, true)
                && append(result, AcapellaPhoneme::D);
        }
        switch (finalPhoneme) {
        case AcapellaPhoneme::P:
        case AcapellaPhoneme::K:
        case AcapellaPhoneme::F:
        case AcapellaPhoneme::S:
        case AcapellaPhoneme::SH:
        case AcapellaPhoneme::CH:
        case AcapellaPhoneme::TH:
            return append(result, AcapellaPhoneme::T);
        default:
            return append(result, AcapellaPhoneme::D);
        }
    case DerivedEnding::Ing:
        return append(result, AcapellaPhoneme::IH, 0u, 0.42f, true)
            && append(result, AcapellaPhoneme::NG);
    case DerivedEnding::Er:
        return append(result, AcapellaPhoneme::ER, 0u, 0.50f, true);
    case DerivedEnding::Ly:
        return append(result, AcapellaPhoneme::L)
            && append(result, AcapellaPhoneme::IY, 0u, 0.48f, true);
    case DerivedEnding::Ness:
        return append(result, AcapellaPhoneme::N)
            && appendSchwaConsonant(AcapellaPhoneme::S);
    case DerivedEnding::Less:
        return append(result, AcapellaPhoneme::L)
            && appendSchwaConsonant(AcapellaPhoneme::S);
    case DerivedEnding::ContractM:
        return append(result, AcapellaPhoneme::M);
    case DerivedEnding::ContractD:
        return append(result, AcapellaPhoneme::D);
    case DerivedEnding::ContractLl:
        return append(result, AcapellaPhoneme::L);
    case DerivedEnding::ContractRe:
        return append(result, AcapellaPhoneme::R);
    case DerivedEnding::ContractVe:
        return append(result, AcapellaPhoneme::V);
    case DerivedEnding::ContractNt:
        return appendSchwaConsonant(AcapellaPhoneme::N)
            && append(result, AcapellaPhoneme::T);
    }
    return false;
}

inline bool compileDerivedWord(AcapellaTextCompileResult& result,
    const char* word, uint32_t length)
{
    std::array<char, 48u> scratch {};
    const auto compileStem = [&](const char* pronunciation,
                                 DerivedEnding ending) {
        if (!pronunciation) return false;
        (void)compilePronunciation(result, pronunciation);
        if (!result.program.truncated) (void)appendDerivedEnding(result, ending);
        return true;
    };
    const auto compilePrefix = [&](uint32_t stemLength,
                                   DerivedEnding ending,
                                   char appended = '\0') {
        return compileStem(stemPronunciation(
            word, stemLength, scratch, appended), ending);
    };

    // Apostrophe forms retain the base vowel and add only the reduced clitic.
    if (length > 2u && wordEndsWith(word, length, "'s")) {
        return compilePrefix(length - 2u, DerivedEnding::Possessive);
    }
    if (length > 2u && wordEndsWith(word, length, "'m")) {
        return compilePrefix(length - 2u, DerivedEnding::ContractM);
    }
    if (length > 2u && wordEndsWith(word, length, "'d")) {
        return compilePrefix(length - 2u, DerivedEnding::ContractD);
    }
    if (length > 3u && wordEndsWith(word, length, "'ll")) {
        return compilePrefix(length - 3u, DerivedEnding::ContractLl);
    }
    if (length > 3u && wordEndsWith(word, length, "'re")) {
        return compilePrefix(length - 3u, DerivedEnding::ContractRe);
    }
    if (length > 3u && wordEndsWith(word, length, "'ve")) {
        return compilePrefix(length - 3u, DerivedEnding::ContractVe);
    }
    if (length > 3u && wordEndsWith(word, length, "n't")) {
        return compilePrefix(length - 3u, DerivedEnding::ContractNt);
    }

    // Regular plurals and third-person verbs. Trying -s first also recovers
    // spelling-preserving forms such as voice -> voices.
    if (length > 2u && word[length - 1u] == 's') {
        if (compilePrefix(length - 1u, DerivedEnding::Plural)) return true;
        if (length > 3u && wordEndsWith(word, length, "es")
            && compilePrefix(length - 2u, DerivedEnding::Plural)) return true;
        if (length > 4u && wordEndsWith(word, length, "ies")
            && compilePrefix(length - 3u, DerivedEnding::Plural, 'y')) {
            return true;
        }
    }

    if (length > 3u && wordEndsWith(word, length, "ed")) {
        if (compilePrefix(length - 2u, DerivedEnding::Past)) return true;
        // love+d and make+d retain a silent e in their dictionary stem.
        if (compilePrefix(length - 1u, DerivedEnding::Past)) return true;
        const uint32_t stemLength = length - 2u;
        if (stemLength > 2u && word[stemLength - 1u] == word[stemLength - 2u]
            && compilePrefix(stemLength - 1u, DerivedEnding::Past)) {
            return true;
        }
    }

    if (length > 4u && wordEndsWith(word, length, "ing")) {
        const uint32_t stemLength = length - 3u;
        if (compilePrefix(stemLength, DerivedEnding::Ing)) return true;
        if (compilePrefix(stemLength, DerivedEnding::Ing, 'e')) return true;
        if (stemLength > 2u && word[stemLength - 1u] == word[stemLength - 2u]
            && compilePrefix(stemLength - 1u, DerivedEnding::Ing)) {
            return true;
        }
    }

    if (length > 3u && wordEndsWith(word, length, "er")
        && compilePrefix(length - 2u, DerivedEnding::Er)) return true;
    if (length > 3u && wordEndsWith(word, length, "ly")
        && compilePrefix(length - 2u, DerivedEnding::Ly)) return true;
    if (length > 5u && wordEndsWith(word, length, "ness")
        && compilePrefix(length - 4u, DerivedEnding::Ness)) return true;
    if (length > 5u && wordEndsWith(word, length, "less")
        && compilePrefix(length - 4u, DerivedEnding::Less)) return true;
    return false;
}

inline uint32_t countNuclei(const char* word, uint32_t length)
{
    uint32_t count = 0u;
    bool previousVowel = false;
    for (uint32_t index = 0u; index < length; ++index) {
        const bool vowel = isVowelLetter(word[index])
            && !(index + 1u == length && lowerAscii(word[index]) == 'e'
                && count > 0u);
        if (vowel && !previousVowel) ++count;
        previousVowel = vowel;
    }
    return std::max(1u, count);
}

inline bool appendRuleVowel(AcapellaTextCompileResult& result,
    const char* word, uint32_t length, uint32_t& index, uint8_t stress)
{
    const char value = lowerAscii(word[index]);
    const bool magicE = index + 2u < length
        && !isVowelLetter(word[index + 1u])
        && lowerAscii(word[index + 2u]) == 'e'
        && index + 3u == length;
    if (textAt(word, length, index, "eigh")) {
        index += 4u;
        return appendDiphthong(result, AcapellaPhoneme::EH,
            AcapellaPhoneme::IY, stress);
    }
    if (textAt(word, length, index, "igh")) {
        index += 3u;
        return appendDiphthong(result, AcapellaPhoneme::AA,
            AcapellaPhoneme::IY, stress);
    }
    if (textAt(word, length, index, "air")) {
        index += 3u;
        return append(result, AcapellaPhoneme::EH, stress, 0.0f, true)
            && append(result, AcapellaPhoneme::R);
    }
    if (textAt(word, length, index, "ear")) {
        index += 3u;
        return append(result, AcapellaPhoneme::IY, stress, 0.0f, true)
            && append(result, AcapellaPhoneme::R);
    }
    if (textAt(word, length, index, "ee")
        || textAt(word, length, index, "ea")
        || textAt(word, length, index, "ie")) {
        index += 2u;
        return append(result, AcapellaPhoneme::IY, stress, 0.0f, true);
    }
    if (textAt(word, length, index, "oo")) {
        index += 2u;
        return append(result, AcapellaPhoneme::UW, stress, 0.0f, true);
    }
    if (textAt(word, length, index, "ew")) {
        index += 2u;
        return append(result, AcapellaPhoneme::Y)
            && append(result, AcapellaPhoneme::UW, stress, 0.0f, true);
    }
    if (textAt(word, length, index, "ui")
        || (textAt(word, length, index, "ue") && index + 2u == length)) {
        index += 2u;
        return append(result, AcapellaPhoneme::UW, stress, 0.0f, true);
    }
    if (textAt(word, length, index, "ai")
        || textAt(word, length, index, "ay")) {
        index += 2u;
        return appendDiphthong(result, AcapellaPhoneme::EH,
            AcapellaPhoneme::IY, stress);
    }
    if (textAt(word, length, index, "ei")
        || textAt(word, length, index, "ey")) {
        index += 2u;
        return appendDiphthong(result, AcapellaPhoneme::EH,
            AcapellaPhoneme::IY, stress);
    }
    if (textAt(word, length, index, "oa")
        || textAt(word, length, index, "oe")) {
        index += 2u;
        return appendDiphthong(result, AcapellaPhoneme::AO,
            AcapellaPhoneme::UW, stress);
    }
    if (textAt(word, length, index, "oi")
        || textAt(word, length, index, "oy")) {
        index += 2u;
        return appendDiphthong(result, AcapellaPhoneme::AO,
            AcapellaPhoneme::IY, stress);
    }
    if (textAt(word, length, index, "ou")
        || textAt(word, length, index, "ow")) {
        index += 2u;
        return appendDiphthong(result, AcapellaPhoneme::AE,
            AcapellaPhoneme::UW, stress);
    }
    if (textAt(word, length, index, "au")
        || textAt(word, length, index, "aw")) {
        index += 2u;
        return append(result, AcapellaPhoneme::AO, stress, 0.0f, true);
    }
    if (textAt(word, length, index, "er")
        || textAt(word, length, index, "ir")
        || textAt(word, length, index, "ur")) {
        index += 2u;
        return append(result, AcapellaPhoneme::ER, stress, 0.0f, true);
    }
    if (textAt(word, length, index, "ar")) {
        index += 2u;
        return append(result, AcapellaPhoneme::AA, stress, 0.0f, true)
            && append(result, AcapellaPhoneme::R);
    }
    if (textAt(word, length, index, "or")) {
        index += 2u;
        return append(result, AcapellaPhoneme::AO, stress, 0.0f, true)
            && append(result, AcapellaPhoneme::R);
    }

    ++index;
    if (magicE) {
        switch (value) {
        case 'a': return appendDiphthong(result, AcapellaPhoneme::EH,
            AcapellaPhoneme::IY, stress);
        case 'i': return appendDiphthong(result, AcapellaPhoneme::AA,
            AcapellaPhoneme::IY, stress);
        case 'o': return appendDiphthong(result, AcapellaPhoneme::AO,
            AcapellaPhoneme::UW, stress);
        case 'u': return append(result, AcapellaPhoneme::UW,
            stress, 0.0f, true);
        default: break;
        }
    }
    const bool reduced = stress == 0u && length > 3u
        && value != 'i' && value != 'y';
    if (reduced) return append(result, AcapellaPhoneme::AX,
        stress, 0.0f, true);
    switch (value) {
    case 'a': return append(result, AcapellaPhoneme::AE,
        stress, 0.0f, true);
    case 'e': return append(result, AcapellaPhoneme::EH,
        stress, 0.0f, true);
    case 'i': return append(result, AcapellaPhoneme::IH,
        stress, 0.0f, true);
    case 'o': return append(result, AcapellaPhoneme::AA,
        stress, 0.0f, true);
    case 'u': return append(result, AcapellaPhoneme::AH,
        stress, 0.0f, true);
    case 'y': return append(result, index == length
        ? AcapellaPhoneme::IY : AcapellaPhoneme::IH,
        stress, 0.0f, true);
    default: return true;
    }
}

inline bool compileRules(AcapellaTextCompileResult& result,
    const char* word, uint32_t length)
{
    const uint32_t nuclei = countNuclei(word, length);
    const uint32_t primary = nuclei <= 1u ? 0u : nuclei - 2u;
    uint32_t syllable = 0u;
    uint32_t index = 0u;
    while (index < length && !result.program.truncated) {
        const char value = lowerAscii(word[index]);
        if (value == '\'') { ++index; continue; }
        if (index + 1u == length && value == 'e' && syllable > 0u) {
            ++index;
            continue;
        }
        if (index + 2u == length && textAt(word, length, index, "ed")) {
            const char previous = index > 0u ? lowerAscii(word[index - 1u]) : 0;
            index += 2u;
            if (previous == 't' || previous == 'd') {
                if (!append(result, AcapellaPhoneme::IH, 0u, 0.34f, true)) {
                    return false;
                }
                ++syllable;
            }
            return append(result,
                previous == 'p' || previous == 'k' || previous == 'f'
                    || previous == 's' || previous == 'x'
                    ? AcapellaPhoneme::T : AcapellaPhoneme::D);
        }
        if (isVowelLetter(value) && !(value == 'y' && index == 0u)) {
            const uint8_t stress = syllable == primary ? 1u : 0u;
            if (!appendRuleVowel(result, word, length, index, stress)) {
                return false;
            }
            ++syllable;
            continue;
        }
        if (textAt(word, length, index, "tion")) {
            index += 4u;
            if (!append(result, AcapellaPhoneme::SH)
                || !append(result, AcapellaPhoneme::AX, 0u, 0.38f, true)
                || !append(result, AcapellaPhoneme::N)) return false;
            ++syllable;
            continue;
        }
        if (textAt(word, length, index, "sion")) {
            index += 4u;
            if (!append(result, AcapellaPhoneme::ZH)
                || !append(result, AcapellaPhoneme::AX, 0u, 0.38f, true)
                || !append(result, AcapellaPhoneme::N)) return false;
            ++syllable;
            continue;
        }
        if (textAt(word, length, index, "cian")) {
            index += 4u;
            if (!append(result, AcapellaPhoneme::SH)
                || !append(result, AcapellaPhoneme::AX, 0u, 0.38f, true)
                || !append(result, AcapellaPhoneme::N)) return false;
            ++syllable;
            continue;
        }
        if (textAt(word, length, index, "ture")) {
            index += 4u;
            if (!append(result, AcapellaPhoneme::CH)
                || !append(result, AcapellaPhoneme::ER, 0u, 0.46f, true)) {
                return false;
            }
            ++syllable;
            continue;
        }
        if (index + 2u == length && textAt(word, length, index, "le")
            && index > 0u && !isVowelLetter(word[index - 1u])) {
            index += 2u;
            if (!append(result, AcapellaPhoneme::AX, 0u, 0.38f, true)
                || !append(result, AcapellaPhoneme::L)) return false;
            ++syllable;
            continue;
        }
        const auto pair = [&](const char* pattern, AcapellaPhoneme phoneme) {
            if (!textAt(word, length, index, pattern)) return false;
            index += static_cast<uint32_t>(std::strlen(pattern));
            return append(result, phoneme);
        };
        if (textAt(word, length, index, "nk")) {
            index += 2u;
            if (!append(result, AcapellaPhoneme::NG)
                || !append(result, AcapellaPhoneme::K)) return false;
            continue;
        }
        if (pair("tch", AcapellaPhoneme::CH)
            || pair("dge", AcapellaPhoneme::JH)
            || (index == 0u && pair("ps", AcapellaPhoneme::S))
            || (index == 0u && pair("gn", AcapellaPhoneme::N))
            || pair("ch", AcapellaPhoneme::CH)
            || pair("sh", AcapellaPhoneme::SH)
            || pair("ph", AcapellaPhoneme::F)
            || pair("th", AcapellaPhoneme::TH)
            || (index + 2u == length && pair("mb", AcapellaPhoneme::M))
            || pair("ng", AcapellaPhoneme::NG)
            || pair("ck", AcapellaPhoneme::K)
            || (index == 0u && pair("wh", AcapellaPhoneme::W))
            || (index == 0u && pair("wr", AcapellaPhoneme::R))
            || (index == 0u && pair("kn", AcapellaPhoneme::N))) {
            continue;
        }
        if (textAt(word, length, index, "qu")) {
            index += 2u;
            if (!append(result, AcapellaPhoneme::K)
                || !append(result, AcapellaPhoneme::W)) return false;
            continue;
        }
        if (index > 0u && value == lowerAscii(word[index - 1u])) {
            ++index;
            continue;
        }
        ++index;
        switch (value) {
        case 'b': if (!append(result, AcapellaPhoneme::B)) return false; break;
        case 'c': if (!append(result,
            index < length && (word[index] == 'e' || word[index] == 'i'
                || word[index] == 'y') ? AcapellaPhoneme::S
                                       : AcapellaPhoneme::K)) return false; break;
        case 'd': if (!append(result, AcapellaPhoneme::D)) return false; break;
        case 'f': if (!append(result, AcapellaPhoneme::F)) return false; break;
        case 'g': if (!append(result,
            index < length && (word[index] == 'e' || word[index] == 'i'
                || word[index] == 'y') ? AcapellaPhoneme::JH
                                       : AcapellaPhoneme::G)) return false; break;
        case 'h':
            if (!(index > 1u && (word[index - 2u] == 'g'
                    || word[index - 2u] == 'c'))
                && !append(result, AcapellaPhoneme::HH)) return false;
            break;
        case 'j': if (!append(result, AcapellaPhoneme::JH)) return false; break;
        case 'k': if (!append(result, AcapellaPhoneme::K)) return false; break;
        case 'l': if (!append(result, AcapellaPhoneme::L)) return false; break;
        case 'm': if (!append(result, AcapellaPhoneme::M)) return false; break;
        case 'n': if (!append(result, AcapellaPhoneme::N)) return false; break;
        case 'p': if (!append(result, AcapellaPhoneme::P)) return false; break;
        case 'q': if (!append(result, AcapellaPhoneme::K)) return false; break;
        case 'r': if (!append(result, AcapellaPhoneme::R)) return false; break;
        case 's': {
            const bool voiced = index > 1u && index < length
                && isVowelLetter(word[index - 2u])
                && isVowelLetter(word[index]);
            if (!append(result, voiced ? AcapellaPhoneme::Z
                                      : AcapellaPhoneme::S)) return false;
            break;
        }
        case 't': if (!append(result, AcapellaPhoneme::T)) return false; break;
        case 'v': if (!append(result, AcapellaPhoneme::V)) return false; break;
        case 'w': if (!append(result, AcapellaPhoneme::W)) return false; break;
        case 'x':
            if (!append(result, AcapellaPhoneme::K)
                || !append(result, AcapellaPhoneme::S)) return false;
            break;
        case 'y': if (!append(result, AcapellaPhoneme::Y)) return false; break;
        case 'z': if (!append(result, AcapellaPhoneme::Z)) return false; break;
        default: break;
        }
    }
    return true;
}

inline bool sameWord(const char* value, const char* expected)
{
    return value && value[0] != '\0' && expected
        && std::strcmp(value, expected) == 0;
}

template <std::size_t Count>
inline bool wordIn(const char* value, const char* const (&choices)[Count])
{
    if (!value || value[0] == '\0') return false;
    for (const char* choice : choices) {
        if (std::strcmp(value, choice) == 0) return true;
    }
    return false;
}

struct ContextPronunciation {
    const char* pronunciation = nullptr;
    bool selected = false;
};

inline ContextPronunciation contextualPronunciation(const char* word,
    const char* previous2, const char* previous,
    const char* next, const char* next2)
{
    static constexpr const char* verbLeaders[] {
        "to", "will", "would", "can", "could", "should", "must",
        "may", "might", "do", "does", "did", "please", "let",
        "i", "we", "you", "they", "he", "she",
    };
    static constexpr const char* determiners[] {
        "a", "an", "the", "this", "that", "my", "your", "our",
        "his", "her", "their", "one", "every", "another", "no",
    };
    const bool verbCue = wordIn(previous, verbLeaders);
    const bool determinerCue = wordIn(previous, determiners);
    const bool objectFollows = sameWord(next, "a")
        || sameWord(next, "an") || sameWord(next, "it")
        || sameWord(next, "the") || sameWord(next, "this")
        || sameWord(next, "that") || sameWord(next, "your");

    if (sameWord(word, "live")) {
        static constexpr const char* eventWords[] {
            "music", "show", "set", "album", "recording", "broadcast",
            "stream", "concert", "performance", "audience", "band",
            "sound", "stage", "television",
        };
        static constexpr const char* verbFollowers[] {
            "again", "alone", "forever", "here", "in", "on", "there",
            "through", "together", "with", "without",
        };
        const bool event = wordIn(next, eventWords)
            || (determinerCue && !wordIn(next, verbFollowers));
        return { event ? "L AY1 V" : "L IH1 V", true };
    }

    if (sameWord(word, "read")) {
        static constexpr const char* perfectAuxiliaries[] {
            "have", "has", "had",
        };
        static constexpr const char* pastMarkers[] {
            "already", "ago", "before", "earlier", "last", "yesterday",
        };
        const bool past = wordIn(previous, perfectAuxiliaries)
            || wordIn(previous, pastMarkers) || wordIn(next, pastMarkers)
            || wordIn(next2, pastMarkers);
        return { past ? "R EH1 D" : "R IY1 D", true };
    }

    if (sameWord(word, "wind") || sameWord(word, "winds")) {
        static constexpr const char* windingFollowers[] {
            "around", "back", "down", "it", "the", "through", "up",
        };
        const bool winding = verbCue || wordIn(next, windingFollowers);
        return { sameWord(word, "winds")
                ? (winding ? "W AY1 N D Z" : "W IH1 N D Z")
                : (winding ? "W AY1 N D" : "W IH1 N D"),
            true };
    }

    if (sameWord(word, "tear") || sameWord(word, "tears")) {
        static constexpr const char* rippingFollowers[] {
            "apart", "away", "down", "into", "off", "open", "through",
            "up",
        };
        const bool ripping = verbCue || wordIn(next, rippingFollowers);
        const bool droplet = !ripping && (determinerCue
            || sameWord(previous, "one") || sameWord(next, "fell")
            || sameWord(next, "drop") || sameWord(next, "drops"));
        const bool plural = sameWord(word, "tears");
        return { droplet
                ? (plural ? "T IH1 R Z" : "T IH1 R")
                : (plural ? "T EH1 R Z" : "T EH1 R"),
            true };
    }

    if (sameWord(word, "lead") || sameWord(word, "leads")) {
        static constexpr const char* metalBefore[] {
            "heavy", "molten", "of",
        };
        static constexpr const char* metalAfter[] {
            "metal", "paint", "pipe", "poison", "weight",
        };
        const bool metal = wordIn(previous, metalBefore)
            || wordIn(next, metalAfter);
        const bool plural = sameWord(word, "leads");
        return { metal
                ? (plural ? "L EH1 D Z" : "L EH1 D")
                : (plural ? "L IY1 D Z" : "L IY1 D"),
            true };
    }

    if (sameWord(word, "bass")) {
        static constexpr const char* fishBefore[] {
            "catch", "caught", "freshwater", "sea", "striped",
        };
        static constexpr const char* fishAfter[] {
            "fish", "lake", "river",
        };
        const bool fish = wordIn(previous, fishBefore)
            || wordIn(next, fishAfter);
        return { fish ? "B AE1 S" : "B EY1 S", true };
    }

    if (sameWord(word, "close")) {
        static constexpr const char* objects[] {
            "door", "eyes", "gate", "it", "me", "mouth", "the", "them",
            "this", "us", "window", "you", "your",
        };
        const bool action = verbCue || wordIn(next, objects);
        return { action ? "K L OW1 Z" : "K L OW1 S", true };
    }

    if (sameWord(word, "use") || sameWord(word, "uses")) {
        const bool action = verbCue || (!determinerCue
            && (sameWord(next, "it") || sameWord(next, "the")
                || sameWord(next, "this") || sameWord(next, "your")));
        const bool plural = sameWord(word, "uses");
        return { action
                ? (plural ? "Y UW1 Z IH0 Z" : "Y UW1 Z")
                : (plural ? "Y UW1 S IH0 Z" : "Y UW1 S"),
            true };
    }

    if (sameWord(word, "house")) {
        const bool action = (verbCue || objectFollows) && !determinerCue;
        return { action ? "HH AW1 Z" : "HH AW1 S", true };
    }

    if (sameWord(word, "bow")) {
        static constexpr const char* curvedFollowers[] {
            "and", "arrow", "hair", "string", "tie",
        };
        static constexpr const char* bendingFollowers[] {
            "before", "down", "low", "out",
        };
        const bool takeABow = sameWord(previous, "a")
            && sameWord(previous2, "take");
        const bool bending = takeABow || wordIn(next, bendingFollowers)
            || (verbCue && !wordIn(next, curvedFollowers));
        return { bending ? "B AW1" : "B OW1", true };
    }

    if (sameWord(word, "row")) {
        const bool argument = sameWord(next, "about")
            || (sameWord(previous, "a")
                && (sameWord(previous2, "had")
                    || sameWord(previous2, "have")));
        return { argument ? "R AW1" : "R OW1", true };
    }

    if (sameWord(word, "wound")) {
        const bool pastWind = sameWord(previous, "had")
            || sameWord(previous, "has") || sameWord(next, "around")
            || sameWord(next, "up");
        return { pastWind ? "W AW1 N D" : "W UW1 N D", true };
    }

    if (sameWord(word, "record")) {
        return { (verbCue || objectFollows)
                ? "R IH0 K AO1 R D" : "R EH1 K ER0 D", true };
    }
    if (sameWord(word, "present")) {
        return { (verbCue || objectFollows) ? "P R IH0 Z EH1 N T"
                                            : "P R EH1 Z AX0 N T", true };
    }
    if (sameWord(word, "project")) {
        return { (verbCue || objectFollows) ? "P R AX0 JH EH1 K T"
                                            : "P R AA1 JH EH0 K T", true };
    }
    if (sameWord(word, "refuse")) {
        return { (verbCue || objectFollows) ? "R IH0 F Y UW1 Z"
                                            : "R EH1 F Y UW2 S", true };
    }
    if (sameWord(word, "minute")) {
        static constexpr const char* tinyFollowers[] {
            "amount", "detail", "difference", "particle",
        };
        const bool tiny = sameWord(previous, "very")
            || wordIn(next, tinyFollowers);
        return { tiny ? "M AY0 N UW1 T" : "M IH1 N AX0 T", true };
    }
    if (sameWord(word, "content")) {
        static constexpr const char* linkingVerbs[] {
            "am", "are", "be", "feel", "felt", "is", "seem", "seems",
            "was", "were",
        };
        return { wordIn(previous, linkingVerbs)
                ? "K AX0 N T EH1 N T" : "K AA1 N T EH0 N T",
            true };
    }
    return {};
}

inline void compileWord(AcapellaTextCompileResult& result,
    const char* word, uint32_t length,
    const char* previous2, const char* previous,
    const char* next, const char* next2)
{
    if (!word || length == 0u || result.program.truncated) return;
    ++result.program.wordCount;
    const uint32_t first = result.program.count;
    const auto context = contextualPronunciation(
        word, previous2, previous, next, next2);
    const char* pronunciation = context.pronunciation
        ? context.pronunciation : lexiconPronunciation(word);
    if (pronunciation) {
        (void)compilePronunciation(result, pronunciation);
    } else if (!compileDerivedWord(result, word, length)) {
        (void)compileRules(result, word, length);
    }
    if (result.program.count > first) {
        result.program.steps[first].flags |= kAcapellaWordStart;
        if (context.selected) {
            result.program.steps[first].flags
                |= kAcapellaContextualPronunciation;
            ++result.contextualWordCount;
        }
        result.program.steps[result.program.count - 1u].flags
            |= kAcapellaWordEnd;
    }
}

inline void appendForcedRest(AcapellaTextCompileResult& result)
{
    if (result.program.count == 0u || result.program.truncated) return;
    auto& previous = result.program.steps[result.program.count - 1u];
    if (previous.phoneme == AcapellaPhoneme::Silence
        && (previous.flags & kAcapellaForcedRest) != 0u) {
        previous.durationScale = std::min(8.0f,
            previous.durationScale + 1.0f);
        return;
    }
    if (append(result, AcapellaPhoneme::Silence, 0u, 1.0f)) {
        result.program.steps[result.program.count - 1u].flags
            |= kAcapellaForcedRest;
    }
}

inline uint32_t scanNextContextWord(const char* text, uint32_t cursor,
    std::array<char, 48u>& output, bool& found)
{
    output.fill('\0');
    found = false;
    if (!text) return cursor;
    while (text[cursor] != '\0') {
        const char value = text[cursor];
        if (value == '|' || value == ';' || value == ':'
            || value == '.' || value == '!' || value == '?') {
            return cursor;
        }
        if (isLetter(value)) break;
        ++cursor;
    }
    uint32_t length = 0u;
    while (text[cursor] != '\0') {
        const bool apostrophe = text[cursor] == '\'' && length > 0u
            && isLetter(text[cursor + 1u]);
        if (!isLetter(text[cursor]) && !apostrophe) break;
        if (length + 1u < output.size()) {
            output[length++] = apostrophe ? '\'' : lowerAscii(text[cursor]);
        }
        ++cursor;
    }
    output[length] = '\0';
    found = length > 0u;
    return cursor;
}

} // namespace acapella_text_detail

inline AcapellaTextCompileResult compileAcapellaText(const char* text)
{
    using namespace acapella_text_detail;
    AcapellaTextCompileResult result;
    if (!text) return result;
    std::array<char, 48u> word {};
    std::array<char, 48u> previous {};
    std::array<char, 48u> previous2 {};
    uint32_t wordLength = 0u;
    uint32_t hash = 2166136261u;
    bool pendingBoundary = false;
    const auto finishWord = [&](uint32_t contextCursor) {
        if (wordLength == 0u) return;
        word[wordLength] = '\0';
        std::array<char, 48u> next {};
        std::array<char, 48u> next2 {};
        bool foundNext = false;
        bool foundNext2 = false;
        const uint32_t afterNext = scanNextContextWord(
            text, contextCursor, next, foundNext);
        if (foundNext) {
            (void)scanNextContextWord(
                text, afterNext, next2, foundNext2);
        }
        compileWord(result, word.data(), wordLength,
            previous2.data(), previous.data(),
            foundNext ? next.data() : nullptr,
            foundNext2 ? next2.data() : nullptr);
        previous2 = previous;
        previous = word;
        wordLength = 0u;
        pendingBoundary = result.program.count > 0u;
    };
    uint32_t index = 0u;
    for (; text[index] != '\0'; ++index) {
        const unsigned char byte = static_cast<unsigned char>(text[index]);
        if (result.characterCount < 255u) ++result.characterCount;
        hash ^= byte;
        hash *= 16777619u;
        const bool apostrophe = byte == '\'' && wordLength > 0u
            && isLetter(text[index + 1u]);
        if (isLetter(static_cast<char>(byte)) || apostrophe) {
            if (pendingBoundary && result.program.count > 0u) {
                if (!append(result, AcapellaPhoneme::Silence,
                        0u, 0.075f)) break;
                pendingBoundary = false;
            }
            if (wordLength + 1u < word.size()) {
                word[wordLength++] = apostrophe ? '\''
                    : lowerAscii(static_cast<char>(byte));
            }
            continue;
        }
        finishWord(index);
        if (byte == '|') {
            // A bar is a scored rest, not punctuation. Repeated bars extend
            // the same event so || consumes exactly two phrase divisions.
            appendForcedRest(result);
            pendingBoundary = false;
            previous.fill('\0');
            previous2.fill('\0');
            if (result.program.truncated) break;
            continue;
        }
        float pause = 0.0f;
        if (byte == ',' || byte == ';' || byte == ':') pause = 0.34f;
        if (byte == '.' || byte == '!' || byte == '?') pause = 0.68f;
        if (pause > 0.0f && result.program.count > 0u) {
            (void)append(result, AcapellaPhoneme::Silence, 0u, pause);
            pendingBoundary = false;
        }
        if (byte == ';' || byte == ':' || byte == '.'
            || byte == '!' || byte == '?') {
            previous.fill('\0');
            previous2.fill('\0');
        }
        if (result.program.truncated) break;
    }
    finishWord(index);
    result.program.revision = hash == 0u ? 1u : hash;
    return result;
}

} // namespace s3g
