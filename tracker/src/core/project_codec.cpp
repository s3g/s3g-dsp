#include "s3g/tracker/project_codec.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <map>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace s3g::tracker {
namespace {

constexpr std::size_t kMaximumJsonDepth = 96u;
constexpr std::size_t kMaximumJsonValues = 2u * 1024u * 1024u;
constexpr std::size_t kMaximumPatternRows = 65536u;
constexpr std::size_t kMaximumStringBytes = 16384u;
constexpr std::size_t kMaximumNameBytes = 1024u;
constexpr std::size_t kMaximumPersistedSongRows = 4096u;
static_assert(kMaximumPersistedSongRows == kMaximumSongRows,
    "project and song-planner row limits must remain aligned");

enum class JsonType : uint8_t { Null, Boolean, Number, String, Array, Object };

struct JsonValue {
    JsonType type = JsonType::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;

    static JsonValue booleanValue(bool value)
    {
        JsonValue result;
        result.type = JsonType::Boolean;
        result.boolean = value;
        return result;
    }

    static JsonValue numberValue(double value)
    {
        JsonValue result;
        result.type = JsonType::Number;
        result.number = value;
        return result;
    }

    static JsonValue stringValue(std::string value)
    {
        JsonValue result;
        result.type = JsonType::String;
        result.string = std::move(value);
        return result;
    }

    static JsonValue arrayValue()
    {
        JsonValue result;
        result.type = JsonType::Array;
        return result;
    }

    static JsonValue objectValue()
    {
        JsonValue result;
        result.type = JsonType::Object;
        return result;
    }
};

ProjectResult failure(ProjectErrorCode code, std::string location,
    std::string message)
{
    return { code, std::move(location), std::move(message) };
}

bool validUtf8(std::string_view text) noexcept
{
    std::size_t index = 0u;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index++]);
        if (first <= 0x7fu) continue;
        uint32_t codePoint = 0u;
        std::size_t continuation = 0u;
        uint32_t minimum = 0u;
        if ((first & 0xe0u) == 0xc0u) {
            codePoint = first & 0x1fu;
            continuation = 1u;
            minimum = 0x80u;
        } else if ((first & 0xf0u) == 0xe0u) {
            codePoint = first & 0x0fu;
            continuation = 2u;
            minimum = 0x800u;
        } else if ((first & 0xf8u) == 0xf0u) {
            codePoint = first & 0x07u;
            continuation = 3u;
            minimum = 0x10000u;
        } else {
            return false;
        }
        if (continuation > text.size() - index) return false;
        for (std::size_t part = 0u; part < continuation; ++part) {
            const auto byte = static_cast<unsigned char>(text[index++]);
            if ((byte & 0xc0u) != 0x80u) return false;
            codePoint = (codePoint << 6u) | (byte & 0x3fu);
        }
        if (codePoint < minimum || codePoint > 0x10ffffu
            || (codePoint >= 0xd800u && codePoint <= 0xdfffu)) return false;
    }
    return true;
}

bool projectInstrumentKindIsActive(InstrumentKind kind) noexcept
{
    return kind != InstrumentKind::Sn76489Psg
        && kind != InstrumentKind::Ym2151Opm;
}

void appendUtf8(uint32_t codePoint, std::string& destination)
{
    if (codePoint <= 0x7fu) {
        destination.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ffu) {
        destination.push_back(static_cast<char>(0xc0u | (codePoint >> 6u)));
        destination.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
    } else if (codePoint <= 0xffffu) {
        destination.push_back(static_cast<char>(0xe0u | (codePoint >> 12u)));
        destination.push_back(static_cast<char>(0x80u
            | ((codePoint >> 6u) & 0x3fu)));
        destination.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
    } else {
        destination.push_back(static_cast<char>(0xf0u | (codePoint >> 18u)));
        destination.push_back(static_cast<char>(0x80u
            | ((codePoint >> 12u) & 0x3fu)));
        destination.push_back(static_cast<char>(0x80u
            | ((codePoint >> 6u) & 0x3fu)));
        destination.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
    }
}

class JsonParser {
public:
    explicit JsonParser(std::string_view source) : source_(source) {}

    ProjectResult parse(JsonValue& destination)
    {
        skipWhitespace();
        if (!parseValue(destination, 0u)) return error_;
        skipWhitespace();
        if (cursor_ != source_.size())
            return fail("unexpected data after the root JSON value");
        return {};
    }

private:
    ProjectResult fail(std::string message)
    {
        if (error_.ok()) {
            error_ = failure(ProjectErrorCode::InvalidJson,
                "byte " + std::to_string(cursor_), std::move(message));
        }
        return error_;
    }

    void skipWhitespace() noexcept
    {
        while (cursor_ < source_.size()) {
            const char value = source_[cursor_];
            if (value != ' ' && value != '\t' && value != '\r'
                && value != '\n') break;
            ++cursor_;
        }
    }

    bool consume(char expected) noexcept
    {
        if (cursor_ >= source_.size() || source_[cursor_] != expected)
            return false;
        ++cursor_;
        return true;
    }

    bool parseValue(JsonValue& destination, std::size_t depth)
    {
        if (depth > kMaximumJsonDepth) {
            fail("JSON nesting exceeds the project limit");
            return false;
        }
        if (++valueCount_ > kMaximumJsonValues) {
            fail("JSON value count exceeds the project limit");
            return false;
        }
        skipWhitespace();
        if (cursor_ >= source_.size()) {
            fail("unexpected end of JSON input");
            return false;
        }
        switch (source_[cursor_]) {
        case 'n': return parseLiteral("null", JsonValue {}, destination);
        case 't': return parseLiteral("true", JsonValue::booleanValue(true),
            destination);
        case 'f': return parseLiteral("false", JsonValue::booleanValue(false),
            destination);
        case '"': {
            destination.type = JsonType::String;
            return parseString(destination.string);
        }
        case '[': return parseArray(destination, depth + 1u);
        case '{': return parseObject(destination, depth + 1u);
        default:
            if (source_[cursor_] == '-'
                || (source_[cursor_] >= '0' && source_[cursor_] <= '9'))
                return parseNumber(destination);
            fail("expected a JSON value");
            return false;
        }
    }

    bool parseLiteral(std::string_view literal, JsonValue value,
        JsonValue& destination)
    {
        if (source_.substr(cursor_, literal.size()) != literal) {
            fail("invalid JSON literal");
            return false;
        }
        cursor_ += literal.size();
        destination = std::move(value);
        return true;
    }

    bool parseHex4(uint32_t& value)
    {
        if (source_.size() - cursor_ < 4u) {
            fail("incomplete JSON Unicode escape");
            return false;
        }
        value = 0u;
        for (std::size_t index = 0u; index < 4u; ++index) {
            const char character = source_[cursor_++];
            uint32_t digit = 0u;
            if (character >= '0' && character <= '9')
                digit = static_cast<uint32_t>(character - '0');
            else if (character >= 'a' && character <= 'f')
                digit = static_cast<uint32_t>(character - 'a' + 10);
            else if (character >= 'A' && character <= 'F')
                digit = static_cast<uint32_t>(character - 'A' + 10);
            else {
                fail("invalid JSON Unicode escape");
                return false;
            }
            value = (value << 4u) | digit;
        }
        return true;
    }

    bool parseString(std::string& destination)
    {
        if (!consume('"')) return false;
        destination.clear();
        while (cursor_ < source_.size()) {
            const auto character = static_cast<unsigned char>(source_[cursor_++]);
            if (character == '"') {
                if (!validUtf8(destination)) {
                    fail("JSON string contains invalid UTF-8");
                    return false;
                }
                return true;
            }
            if (character < 0x20u) {
                fail("unescaped control character in JSON string");
                return false;
            }
            if (character != '\\') {
                destination.push_back(static_cast<char>(character));
                continue;
            }
            if (cursor_ >= source_.size()) {
                fail("incomplete JSON string escape");
                return false;
            }
            const char escaped = source_[cursor_++];
            switch (escaped) {
            case '"': destination.push_back('"'); break;
            case '\\': destination.push_back('\\'); break;
            case '/': destination.push_back('/'); break;
            case 'b': destination.push_back('\b'); break;
            case 'f': destination.push_back('\f'); break;
            case 'n': destination.push_back('\n'); break;
            case 'r': destination.push_back('\r'); break;
            case 't': destination.push_back('\t'); break;
            case 'u': {
                uint32_t first = 0u;
                if (!parseHex4(first)) return false;
                uint32_t codePoint = first;
                if (first >= 0xd800u && first <= 0xdbffu) {
                    if (source_.size() - cursor_ < 6u
                        || source_[cursor_] != '\\'
                        || source_[cursor_ + 1u] != 'u') {
                        fail("high surrogate is missing its low surrogate");
                        return false;
                    }
                    cursor_ += 2u;
                    uint32_t second = 0u;
                    if (!parseHex4(second)) return false;
                    if (second < 0xdc00u || second > 0xdfffu) {
                        fail("invalid low surrogate in JSON string");
                        return false;
                    }
                    codePoint = 0x10000u + ((first - 0xd800u) << 10u)
                        + (second - 0xdc00u);
                } else if (first >= 0xdc00u && first <= 0xdfffu) {
                    fail("unexpected low surrogate in JSON string");
                    return false;
                }
                if (codePoint == 0u) {
                    fail("NUL is not allowed in project strings");
                    return false;
                }
                appendUtf8(codePoint, destination);
                break;
            }
            default:
                fail("invalid JSON string escape");
                return false;
            }
        }
        fail("unterminated JSON string");
        return false;
    }

    bool parseNumber(JsonValue& destination)
    {
        const std::size_t begin = cursor_;
        if (consume('-') && cursor_ >= source_.size()) {
            fail("incomplete JSON number");
            return false;
        }
        if (consume('0')) {
            if (cursor_ < source_.size() && source_[cursor_] >= '0'
                && source_[cursor_] <= '9') {
                fail("leading zero in JSON number");
                return false;
            }
        } else {
            if (cursor_ >= source_.size() || source_[cursor_] < '1'
                || source_[cursor_] > '9') {
                fail("invalid JSON number");
                return false;
            }
            while (cursor_ < source_.size() && source_[cursor_] >= '0'
                && source_[cursor_] <= '9') ++cursor_;
        }
        if (consume('.')) {
            const std::size_t fraction = cursor_;
            while (cursor_ < source_.size() && source_[cursor_] >= '0'
                && source_[cursor_] <= '9') ++cursor_;
            if (fraction == cursor_) {
                fail("JSON fraction requires at least one digit");
                return false;
            }
        }
        if (cursor_ < source_.size()
            && (source_[cursor_] == 'e' || source_[cursor_] == 'E')) {
            ++cursor_;
            if (cursor_ < source_.size()
                && (source_[cursor_] == '+' || source_[cursor_] == '-'))
                ++cursor_;
            const std::size_t exponent = cursor_;
            while (cursor_ < source_.size() && source_[cursor_] >= '0'
                && source_[cursor_] <= '9') ++cursor_;
            if (exponent == cursor_) {
                fail("JSON exponent requires at least one digit");
                return false;
            }
        }
        std::istringstream stream(std::string(source_.substr(
            begin, cursor_ - begin)));
        stream.imbue(std::locale::classic());
        stream >> std::noskipws;
        double value = 0.0;
        if (!(stream >> value) || !stream.eof() || !std::isfinite(value)) {
            fail("JSON number is not finite or representable");
            return false;
        }
        destination = JsonValue::numberValue(value);
        return true;
    }

    bool parseArray(JsonValue& destination, std::size_t depth)
    {
        consume('[');
        destination = JsonValue::arrayValue();
        skipWhitespace();
        if (consume(']')) return true;
        for (;;) {
            JsonValue value;
            if (!parseValue(value, depth)) return false;
            destination.array.push_back(std::move(value));
            skipWhitespace();
            if (consume(']')) return true;
            if (!consume(',')) {
                fail("expected ',' or ']' in JSON array");
                return false;
            }
            skipWhitespace();
        }
    }

    bool parseObject(JsonValue& destination, std::size_t depth)
    {
        consume('{');
        destination = JsonValue::objectValue();
        skipWhitespace();
        if (consume('}')) return true;
        for (;;) {
            if (cursor_ >= source_.size() || source_[cursor_] != '"') {
                fail("expected a quoted JSON object key");
                return false;
            }
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (!consume(':')) {
                fail("expected ':' after JSON object key");
                return false;
            }
            JsonValue value;
            if (!parseValue(value, depth)) return false;
            if (!destination.object.emplace(std::move(key),
                    std::move(value)).second) {
                fail("duplicate JSON object key");
                return false;
            }
            skipWhitespace();
            if (consume('}')) return true;
            if (!consume(',')) {
                fail("expected ',' or '}' in JSON object");
                return false;
            }
            skipWhitespace();
        }
    }

    std::string_view source_;
    std::size_t cursor_ = 0u;
    std::size_t valueCount_ = 0u;
    ProjectResult error_;
};

void appendIndent(std::string& destination, std::size_t depth)
{
    destination.append(depth * 2u, ' ');
}

void appendEscaped(std::string_view source, std::string& destination)
{
    constexpr char digits[] = "0123456789abcdef";
    destination.push_back('"');
    for (const char sourceCharacter : source) {
        const auto character = static_cast<unsigned char>(sourceCharacter);
        switch (character) {
        case '"': destination += "\\\""; break;
        case '\\': destination += "\\\\"; break;
        case '\b': destination += "\\b"; break;
        case '\f': destination += "\\f"; break;
        case '\n': destination += "\\n"; break;
        case '\r': destination += "\\r"; break;
        case '\t': destination += "\\t"; break;
        default:
            if (character < 0x20u) {
                destination += "\\u00";
                destination.push_back(digits[character >> 4u]);
                destination.push_back(digits[character & 0x0fu]);
            } else {
                destination.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    destination.push_back('"');
}

void appendJson(const JsonValue& value, std::string& destination,
    std::size_t depth)
{
    switch (value.type) {
    case JsonType::Null: destination += "null"; break;
    case JsonType::Boolean: destination += value.boolean ? "true" : "false"; break;
    case JsonType::Number: {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::setprecision(std::numeric_limits<double>::max_digits10)
               << value.number;
        destination += stream.str();
        break;
    }
    case JsonType::String: appendEscaped(value.string, destination); break;
    case JsonType::Array:
        if (value.array.empty()) { destination += "[]"; break; }
        destination += "[\n";
        for (std::size_t index = 0u; index < value.array.size(); ++index) {
            appendIndent(destination, depth + 1u);
            appendJson(value.array[index], destination, depth + 1u);
            destination += index + 1u == value.array.size() ? "\n" : ",\n";
        }
        appendIndent(destination, depth);
        destination.push_back(']');
        break;
    case JsonType::Object:
        if (value.object.empty()) { destination += "{}"; break; }
        destination += "{\n";
        for (auto iterator = value.object.begin();
             iterator != value.object.end(); ++iterator) {
            appendIndent(destination, depth + 1u);
            appendEscaped(iterator->first, destination);
            destination += ": ";
            appendJson(iterator->second, destination, depth + 1u);
            destination += std::next(iterator) == value.object.end()
                ? "\n" : ",\n";
        }
        appendIndent(destination, depth);
        destination.push_back('}');
        break;
    }
}

bool setError(ProjectResult& result, ProjectErrorCode code,
    std::string location, std::string message)
{
    if (result.ok()) result = failure(code, std::move(location),
        std::move(message));
    return false;
}

const JsonValue* requiredField(const JsonValue& object,
    std::string_view key, JsonType type, std::string_view path,
    ProjectResult& result)
{
    if (object.type != JsonType::Object) {
        setError(result, ProjectErrorCode::TypeMismatch, std::string(path),
            "expected an object");
        return nullptr;
    }
    const auto found = object.object.find(std::string(key));
    const std::string location = std::string(path) + "." + std::string(key);
    if (found == object.object.end()) {
        setError(result, ProjectErrorCode::MissingField, location,
            "required field is missing");
        return nullptr;
    }
    if (found->second.type != type) {
        setError(result, ProjectErrorCode::TypeMismatch, location,
            "field has the wrong JSON type");
        return nullptr;
    }
    return &found->second;
}

bool checkedString(const JsonValue& value, std::string& destination,
    std::size_t maximumBytes, std::string_view path, ProjectResult& result)
{
    if (value.type != JsonType::String)
        return setError(result, ProjectErrorCode::TypeMismatch,
            std::string(path), "expected a string");
    if (value.string.size() > maximumBytes || !validUtf8(value.string)
        || value.string.find('\0') != std::string::npos)
        return setError(result, ProjectErrorCode::OutOfRange,
            std::string(path), "string is invalid or exceeds its size limit");
    destination = value.string;
    return true;
}

bool checkedNumber(const JsonValue& value, double& destination,
    double minimum, double maximum, std::string_view path,
    ProjectResult& result)
{
    if (value.type != JsonType::Number)
        return setError(result, ProjectErrorCode::TypeMismatch,
            std::string(path), "expected a number");
    if (!std::isfinite(value.number) || value.number < minimum
        || value.number > maximum)
        return setError(result, ProjectErrorCode::OutOfRange,
            std::string(path), "number is outside the supported range");
    destination = value.number;
    return true;
}

bool checkedUnsigned(const JsonValue& value, uint64_t& destination,
    uint64_t maximum, std::string_view path, ProjectResult& result)
{
    double number = 0.0;
    if (!checkedNumber(value, number, 0.0, static_cast<double>(maximum),
            path, result)) return false;
    if (std::floor(number) != number)
        return setError(result, ProjectErrorCode::TypeMismatch,
            std::string(path), "expected an unsigned integer");
    destination = static_cast<uint64_t>(number);
    return true;
}

bool checkedUint32(const JsonValue& value, uint32_t& destination,
    uint32_t maximum, std::string_view path, ProjectResult& result)
{
    uint64_t candidate = 0u;
    if (!checkedUnsigned(value, candidate, maximum, path, result)) return false;
    destination = static_cast<uint32_t>(candidate);
    return true;
}

bool checkedSize(const JsonValue& value, std::size_t& destination,
    std::size_t maximum, std::string_view path, ProjectResult& result)
{
    uint64_t candidate = 0u;
    if (!checkedUnsigned(value, candidate, maximum, path, result)) return false;
    destination = static_cast<std::size_t>(candidate);
    return true;
}

bool checkedInt32(const JsonValue& value, int32_t& destination,
    std::string_view path, ProjectResult& result)
{
    if (value.type != JsonType::Number || !std::isfinite(value.number)
        || std::floor(value.number) != value.number
        || value.number < static_cast<double>(std::numeric_limits<int32_t>::min())
        || value.number > static_cast<double>(std::numeric_limits<int32_t>::max()))
        return setError(result, ProjectErrorCode::OutOfRange,
            std::string(path), "expected a signed 32-bit integer");
    destination = static_cast<int32_t>(value.number);
    return true;
}

bool checkedUint64String(const JsonValue& value, uint64_t& destination,
    std::string_view path, ProjectResult& result)
{
    if (value.type != JsonType::String || value.string.empty())
        return setError(result, ProjectErrorCode::TypeMismatch,
            std::string(path), "expected a decimal uint64 string");
    uint64_t candidate = 0u;
    const auto parsed = std::from_chars(value.string.data(),
        value.string.data() + value.string.size(), candidate);
    if (parsed.ec != std::errc {}
        || parsed.ptr != value.string.data() + value.string.size())
        return setError(result, ProjectErrorCode::OutOfRange,
            std::string(path), "invalid decimal uint64 string");
    destination = candidate;
    return true;
}

bool checkedBoolean(const JsonValue& value, bool& destination,
    std::string_view path, ProjectResult& result)
{
    if (value.type != JsonType::Boolean)
        return setError(result, ProjectErrorCode::TypeMismatch,
            std::string(path), "expected a boolean");
    destination = value.boolean;
    return true;
}

template <typename Enum>
bool decodeEnum(const JsonValue& value,
    const std::pair<std::string_view, Enum>* choices, std::size_t count,
    Enum& destination, std::string_view path, ProjectResult& result)
{
    if (value.type != JsonType::String)
        return setError(result, ProjectErrorCode::TypeMismatch,
            std::string(path), "expected an enum string");
    for (std::size_t index = 0u; index < count; ++index) {
        if (value.string == choices[index].first) {
            destination = choices[index].second;
            return true;
        }
    }
    return setError(result, ProjectErrorCode::OutOfRange,
        std::string(path), "unknown enum value '" + value.string + "'");
}

template <typename Enum, std::size_t Size>
bool decodeEnum(const JsonValue& value,
    const std::array<std::pair<std::string_view, Enum>, Size>& choices,
    Enum& destination, std::string_view path, ProjectResult& result)
{
    return decodeEnum(value, choices.data(), choices.size(), destination,
        path, result);
}

template <typename Enum, std::size_t Size>
JsonValue encodeEnum(Enum value,
    const std::array<std::pair<std::string_view, Enum>, Size>& choices,
    std::string_view path, ProjectResult& result)
{
    for (const auto& choice : choices) {
        if (choice.second == value)
            return JsonValue::stringValue(std::string(choice.first));
    }
    setError(result, ProjectErrorCode::OutOfRange, std::string(path),
        "enum has an unsupported value");
    return {};
}

constexpr std::array<std::pair<std::string_view, Direction>, 4u>
    kDirections {{
        { "forward", Direction::Forward },
        { "reverse", Direction::Reverse },
        { "random", Direction::Random },
        { "palindrome", Direction::Palindrome },
    }};
constexpr std::array<std::pair<std::string_view, EventDestination>, 4u>
    kDestinations {{
        { "none", EventDestination::None },
        { "internal", EventDestination::Internal },
        { "midi", EventDestination::Midi },
        { "both", EventDestination::Both },
    }};
constexpr std::array<std::pair<std::string_view, ParameterScope>, 3u>
    kParameterScopes {{
        { "global", ParameterScope::Global },
        { "channel", ParameterScope::Channel },
        { "note", ParameterScope::Note },
    }};
constexpr std::array<std::pair<std::string_view, SequencerAction>, 12u>
    kSequencerActions {{
        { "ratchet", SequencerAction::Ratchet },
        { "microtime", SequencerAction::MicroTime },
        { "delay", SequencerAction::Delay },
        { "flam", SequencerAction::Flam },
        { "stutter", SequencerAction::Stutter },
        { "accent", SequencerAction::Accent },
        { "ghost", SequencerAction::Ghost },
        { "probability", SequencerAction::Probability },
        { "skip", SequencerAction::Skip },
        { "offset", SequencerAction::Offset },
        { "repeat-previous", SequencerAction::RepeatPrevious },
        { "euclid", SequencerAction::Euclid },
    }};
static_assert(kSequencerActions.size() == kSequencerActionCount,
    "project schema must explicitly name every sequencing action");
constexpr std::array<std::pair<std::string_view, TimingWarpKind>, 3u>
    kTimingWarpKinds {{
        { "exponential", TimingWarpKind::Exponential },
        { "step-quantize", TimingWarpKind::StepQuantize },
        { "euclidean-quantize", TimingWarpKind::EuclideanQuantize },
    }};
constexpr std::array<std::pair<std::string_view, MembraneInstrumentRole>, 5u>
    kMembraneRoles {{
        { "kick", MembraneInstrumentRole::Kick },
        { "snare-body", MembraneInstrumentRole::SnareBody },
        { "floor-tom", MembraneInstrumentRole::FloorTom },
        { "low-tom", MembraneInstrumentRole::LowTom },
        { "high-tom", MembraneInstrumentRole::HighTom },
    }};
constexpr std::array<std::pair<std::string_view, MidiInstrumentRouteKind>, 2u>
    kMidiRouteKinds {{
        { "virtual-source", MidiInstrumentRouteKind::VirtualSource },
        { "destination", MidiInstrumentRouteKind::Destination },
    }};

JsonValue number(std::size_t value)
{
    return JsonValue::numberValue(static_cast<double>(value));
}

JsonValue number(uint32_t value)
{
    return JsonValue::numberValue(static_cast<double>(value));
}

} // namespace

// Encoding and model decoding continue below. Keeping the JSON machinery
// private prevents a general-purpose DOM from becoming an accidental public
// dependency of the tracker core.

} // namespace s3g::tracker

namespace s3g::tracker {
namespace {

JsonValue encodeCheckedString(std::string_view text, std::size_t maximumBytes,
    std::string_view path, ProjectResult& result)
{
    if (text.size() > maximumBytes || !validUtf8(text)
        || text.find('\0') != std::string_view::npos) {
        setError(result, ProjectErrorCode::OutOfRange, std::string(path),
            "string is invalid or exceeds its size limit");
        return {};
    }
    return JsonValue::stringValue(std::string(text));
}

bool finiteRange(double value, double minimum, double maximum,
    std::string_view path, ProjectResult& result)
{
    return std::isfinite(value) && value >= minimum && value <= maximum
        ? true
        : setError(result, ProjectErrorCode::OutOfRange,
            std::string(path), "number is outside the supported range");
}

JsonValue encodeColumn(const ColumnDefinition& column,
    std::size_t cellCount, std::string_view path, ProjectResult& result)
{
    if (column.length > kMaximumPatternRows)
        setError(result, ProjectErrorCode::OutOfRange,
            std::string(path) + ".length", "column length exceeds 65536");
    if (column.stride == 0u || column.stride > kMaximumPatternRows)
        setError(result, ProjectErrorCode::OutOfRange,
            std::string(path) + ".stride", "column stride must be 1..65536");
    const std::size_t activeLength = std::min(column.length, cellCount);
    if ((activeLength == 0u && column.phase != 0u)
        || (activeLength != 0u && column.phase >= activeLength))
        setError(result, ProjectErrorCode::OutOfRange,
            std::string(path) + ".phase",
            "column phase must be normalized to its active length");

    JsonValue output = JsonValue::objectValue();
    output.object["direction"] = encodeEnum(column.direction, kDirections,
        std::string(path) + ".direction", result);
    output.object["length"] = number(column.length);
    output.object["muted"] = JsonValue::booleanValue(column.muted);
    output.object["phase"] = number(column.phase);
    output.object["stride"] = number(column.stride);
    return output;
}

bool decodeColumn(const JsonValue& input, ColumnDefinition& destination,
    std::size_t cellCount, std::string_view path, ProjectResult& result)
{
    const auto* length = requiredField(input, "length", JsonType::Number,
        path, result);
    const auto* stride = requiredField(input, "stride", JsonType::Number,
        path, result);
    const auto* phase = requiredField(input, "phase", JsonType::Number,
        path, result);
    const auto* direction = requiredField(input, "direction",
        JsonType::String, path, result);
    const auto* muted = requiredField(input, "muted", JsonType::Boolean,
        path, result);
    if (!length || !stride || !phase || !direction || !muted) return false;
    if (!checkedSize(*length, destination.length, kMaximumPatternRows,
            std::string(path) + ".length", result)
        || !checkedUint32(*stride, destination.stride,
            static_cast<uint32_t>(kMaximumPatternRows),
            std::string(path) + ".stride", result)
        || destination.stride == 0u
        || !checkedSize(*phase, destination.phase, kMaximumPatternRows,
            std::string(path) + ".phase", result)
        || !decodeEnum(*direction, kDirections, destination.direction,
            std::string(path) + ".direction", result)
        || !checkedBoolean(*muted, destination.muted,
            std::string(path) + ".muted", result)) {
        if (result.ok() && destination.stride == 0u)
            setError(result, ProjectErrorCode::OutOfRange,
                std::string(path) + ".stride", "column stride cannot be zero");
        return false;
    }
    const std::size_t activeLength = std::min(destination.length, cellCount);
    if ((activeLength == 0u && destination.phase != 0u)
        || (activeLength != 0u && destination.phase >= activeLength))
        return setError(result, ProjectErrorCode::OutOfRange,
            std::string(path) + ".phase",
            "column phase must be normalized to its active length");
    return true;
}

JsonValue encodeNoteCells(const std::vector<NoteCell>& cells,
    std::string_view path, ProjectResult& result)
{
    if (cells.size() > kMaximumPatternRows)
        setError(result, ProjectErrorCode::SizeLimitExceeded,
            std::string(path), "note column exceeds 65536 cells");
    JsonValue output = JsonValue::arrayValue();
    output.array.reserve(cells.size());
    for (std::size_t index = 0u; index < cells.size(); ++index) {
        JsonValue cell = JsonValue::objectValue();
        switch (cells[index].state) {
        case NoteCellState::Rest:
            cell.object["state"] = JsonValue::stringValue("rest");
            break;
        case NoteCellState::RetriggerPrevious:
            cell.object["state"] = JsonValue::stringValue("retrigger");
            break;
        case NoteCellState::Kill:
            cell.object["state"] = JsonValue::stringValue("kill");
            break;
        case NoteCellState::Note:
            if (cells[index].note > 127u)
                setError(result, ProjectErrorCode::OutOfRange,
                    std::string(path) + "[" + std::to_string(index)
                        + "].note", "MIDI note must be 0..127");
            cell.object["note"] = number(static_cast<uint32_t>(cells[index].note));
            cell.object["state"] = JsonValue::stringValue("note");
            break;
        default:
            setError(result, ProjectErrorCode::OutOfRange,
                std::string(path) + "[" + std::to_string(index) + "].state",
                "invalid note cell state");
            break;
        }
        output.array.push_back(std::move(cell));
    }
    return output;
}

bool decodeNoteCells(const JsonValue& input, std::vector<NoteCell>& destination,
    std::string_view path, ProjectResult& result)
{
    if (input.type != JsonType::Array)
        return setError(result, ProjectErrorCode::TypeMismatch,
            std::string(path), "expected a note-cell array");
    if (input.array.size() > kMaximumPatternRows)
        return setError(result, ProjectErrorCode::SizeLimitExceeded,
            std::string(path), "note column exceeds 65536 cells");
    std::vector<NoteCell> candidate;
    candidate.reserve(input.array.size());
    for (std::size_t index = 0u; index < input.array.size(); ++index) {
        const std::string cellPath = std::string(path) + "["
            + std::to_string(index) + "]";
        const auto* state = requiredField(input.array[index], "state",
            JsonType::String, cellPath, result);
        if (!state) return false;
        if (state->string == "rest") candidate.push_back(NoteCell::rest());
        else if (state->string == "retrigger")
            candidate.push_back(NoteCell::retriggerPrevious());
        else if (state->string == "kill")
            candidate.push_back(NoteCell::kill());
        else if (state->string == "note") {
            const auto* note = requiredField(input.array[index], "note",
                JsonType::Number, cellPath, result);
            uint32_t value = 0u;
            if (!note || !checkedUint32(*note, value, 127u,
                    cellPath + ".note", result)) return false;
            candidate.push_back(NoteCell::withNote(static_cast<uint8_t>(value)));
        } else {
            return setError(result, ProjectErrorCode::OutOfRange,
                cellPath + ".state", "unknown note cell state");
        }
    }
    destination = std::move(candidate);
    return true;
}

JsonValue encodeInstrumentCells(const std::vector<InstrumentCell>& cells,
    std::string_view path, ProjectResult& result)
{
    if (cells.size() > kMaximumPatternRows)
        setError(result, ProjectErrorCode::SizeLimitExceeded,
            std::string(path), "instrument column exceeds 65536 cells");
    JsonValue output = JsonValue::arrayValue();
    output.array.reserve(cells.size());
    for (std::size_t index = 0u; index < cells.size(); ++index) {
        JsonValue cell = JsonValue::objectValue();
        switch (cells[index].state) {
        case InstrumentCellState::Empty:
            cell.object["state"] = JsonValue::stringValue("empty");
            break;
        case InstrumentCellState::Previous:
            cell.object["state"] = JsonValue::stringValue("previous");
            break;
        case InstrumentCellState::Instrument:
            if (cells[index].nodeId >= kInstrumentRackSlotCount)
                setError(result, ProjectErrorCode::OutOfRange,
                    std::string(path) + "[" + std::to_string(index)
                        + "].node", "instrument node is outside the rack");
            cell.object["node"] = number(cells[index].nodeId);
            cell.object["state"] = JsonValue::stringValue("instrument");
            break;
        default:
            setError(result, ProjectErrorCode::OutOfRange,
                std::string(path) + "[" + std::to_string(index) + "].state",
                "invalid instrument cell state");
            break;
        }
        output.array.push_back(std::move(cell));
    }
    return output;
}

bool decodeInstrumentCells(const JsonValue& input,
    std::vector<InstrumentCell>& destination, std::string_view path,
    ProjectResult& result)
{
    if (input.type != JsonType::Array)
        return setError(result, ProjectErrorCode::TypeMismatch,
            std::string(path), "expected an instrument-cell array");
    if (input.array.size() > kMaximumPatternRows)
        return setError(result, ProjectErrorCode::SizeLimitExceeded,
            std::string(path), "instrument column exceeds 65536 cells");
    std::vector<InstrumentCell> candidate;
    candidate.reserve(input.array.size());
    for (std::size_t index = 0u; index < input.array.size(); ++index) {
        const std::string cellPath = std::string(path) + "["
            + std::to_string(index) + "]";
        const auto* state = requiredField(input.array[index], "state",
            JsonType::String, cellPath, result);
        if (!state) return false;
        if (state->string == "empty")
            candidate.push_back(InstrumentCell::empty());
        else if (state->string == "previous")
            candidate.push_back(InstrumentCell::previous());
        else if (state->string == "instrument") {
            const auto* node = requiredField(input.array[index], "node",
                JsonType::Number, cellPath, result);
            uint32_t value = 0u;
            if (!node || !checkedUint32(*node, value,
                    static_cast<uint32_t>(kInstrumentRackSlotCount - 1u),
                    cellPath + ".node", result)) return false;
            candidate.push_back(InstrumentCell::withInstrument(value));
        } else {
            return setError(result, ProjectErrorCode::OutOfRange,
                cellPath + ".state", "unknown instrument cell state");
        }
    }
    destination = std::move(candidate);
    return true;
}

JsonValue encodeValueCells(const std::vector<ValueCell>& cells,
    std::string_view path, ProjectResult& result)
{
    if (cells.size() > kMaximumPatternRows)
        setError(result, ProjectErrorCode::SizeLimitExceeded,
            std::string(path), "value column exceeds 65536 cells");
    JsonValue output = JsonValue::arrayValue();
    output.array.reserve(cells.size());
    for (std::size_t index = 0u; index < cells.size(); ++index) {
        JsonValue cell = JsonValue::objectValue();
        switch (cells[index].state) {
        case ValueCellState::Default:
            cell.object["state"] = JsonValue::stringValue("default");
            break;
        case ValueCellState::Previous:
            cell.object["state"] = JsonValue::stringValue("previous");
            break;
        case ValueCellState::Value:
            finiteRange(cells[index].normalized, 0.0, 1.0,
                std::string(path) + "[" + std::to_string(index) + "].value",
                result);
            cell.object["state"] = JsonValue::stringValue("value");
            cell.object["value"] = JsonValue::numberValue(cells[index].normalized);
            break;
        default:
            setError(result, ProjectErrorCode::OutOfRange,
                std::string(path) + "[" + std::to_string(index) + "].state",
                "invalid value cell state");
            break;
        }
        output.array.push_back(std::move(cell));
    }
    return output;
}

bool decodeValueCells(const JsonValue& input,
    std::vector<ValueCell>& destination, std::string_view path,
    ProjectResult& result)
{
    if (input.type != JsonType::Array)
        return setError(result, ProjectErrorCode::TypeMismatch,
            std::string(path), "expected a value-cell array");
    if (input.array.size() > kMaximumPatternRows)
        return setError(result, ProjectErrorCode::SizeLimitExceeded,
            std::string(path), "value column exceeds 65536 cells");
    std::vector<ValueCell> candidate;
    candidate.reserve(input.array.size());
    for (std::size_t index = 0u; index < input.array.size(); ++index) {
        const std::string cellPath = std::string(path) + "["
            + std::to_string(index) + "]";
        const auto* state = requiredField(input.array[index], "state",
            JsonType::String, cellPath, result);
        if (!state) return false;
        if (state->string == "default")
            candidate.push_back(ValueCell::defaultValue());
        else if (state->string == "previous")
            candidate.push_back(ValueCell::previous());
        else if (state->string == "value") {
            const auto* value = requiredField(input.array[index], "value",
                JsonType::Number, cellPath, result);
            double normalized = 0.0;
            if (!value || !checkedNumber(*value, normalized, 0.0, 1.0,
                    cellPath + ".value", result)) return false;
            candidate.push_back(ValueCell::withValue(
                static_cast<float>(normalized)));
        } else {
            return setError(result, ProjectErrorCode::OutOfRange,
                cellPath + ".state", "unknown value cell state");
        }
    }
    destination = std::move(candidate);
    return true;
}

JsonValue encodeFxActions(const std::vector<FxActionCell>& cells,
    std::string_view path, ProjectResult& result)
{
    if (cells.size() > kMaximumPatternRows)
        setError(result, ProjectErrorCode::SizeLimitExceeded,
            std::string(path), "FX action column exceeds 65536 cells");
    JsonValue output = JsonValue::arrayValue();
    output.array.reserve(cells.size());
    for (std::size_t index = 0u; index < cells.size(); ++index) {
        const std::string cellPath = std::string(path) + "["
            + std::to_string(index) + "]";
        JsonValue cell = JsonValue::objectValue();
        switch (cells[index].state) {
        case FxActionCellState::Empty:
            cell.object["state"] = JsonValue::stringValue("empty");
            break;
        case FxActionCellState::Previous:
            cell.object["state"] = JsonValue::stringValue("previous");
            break;
        case FxActionCellState::Parameter:
            cell.object["parameter"] = number(cells[index].parameterId);
            cell.object["scope"] = encodeEnum(cells[index].scope,
                kParameterScopes, cellPath + ".scope", result);
            cell.object["state"] = JsonValue::stringValue("parameter");
            cell.object["targetNode"] = number(cells[index].targetNode);
            break;
        case FxActionCellState::Sequencer:
            cell.object["action"] = encodeEnum(cells[index].sequencerAction,
                kSequencerActions, cellPath + ".action", result);
            cell.object["state"] = JsonValue::stringValue("sequencer");
            break;
        default:
            setError(result, ProjectErrorCode::OutOfRange,
                cellPath + ".state", "invalid FX action cell state");
            break;
        }
        output.array.push_back(std::move(cell));
    }
    return output;
}

bool decodeFxActions(const JsonValue& input,
    std::vector<FxActionCell>& destination, std::string_view path,
    ProjectResult& result)
{
    if (input.type != JsonType::Array)
        return setError(result, ProjectErrorCode::TypeMismatch,
            std::string(path), "expected an FX action-cell array");
    if (input.array.size() > kMaximumPatternRows)
        return setError(result, ProjectErrorCode::SizeLimitExceeded,
            std::string(path), "FX action column exceeds 65536 cells");
    std::vector<FxActionCell> candidate;
    candidate.reserve(input.array.size());
    for (std::size_t index = 0u; index < input.array.size(); ++index) {
        const std::string cellPath = std::string(path) + "["
            + std::to_string(index) + "]";
        const auto* state = requiredField(input.array[index], "state",
            JsonType::String, cellPath, result);
        if (!state) return false;
        if (state->string == "empty")
            candidate.push_back(FxActionCell::empty());
        else if (state->string == "previous")
            candidate.push_back(FxActionCell::previous());
        else if (state->string == "parameter") {
            const auto* parameter = requiredField(input.array[index],
                "parameter", JsonType::Number, cellPath, result);
            const auto* scope = requiredField(input.array[index], "scope",
                JsonType::String, cellPath, result);
            const auto* target = requiredField(input.array[index],
                "targetNode", JsonType::Number, cellPath, result);
            uint32_t parameterId = 0u;
            uint32_t targetNode = 0u;
            ParameterScope parameterScope = ParameterScope::Global;
            if (!parameter || !scope || !target
                || !checkedUint32(*parameter, parameterId,
                    std::numeric_limits<uint32_t>::max(),
                    cellPath + ".parameter", result)
                || !checkedUint32(*target, targetNode,
                    std::numeric_limits<uint32_t>::max(),
                    cellPath + ".targetNode", result)
                || !decodeEnum(*scope, kParameterScopes, parameterScope,
                    cellPath + ".scope", result)) return false;
            candidate.push_back(FxActionCell::parameter(parameterId,
                parameterScope, targetNode));
        } else if (state->string == "sequencer") {
            const auto* action = requiredField(input.array[index], "action",
                JsonType::String, cellPath, result);
            SequencerAction value = SequencerAction::Ratchet;
            if (!action || !decodeEnum(*action, kSequencerActions, value,
                    cellPath + ".action", result)) return false;
            candidate.push_back(FxActionCell::sequencer(value));
        } else {
            return setError(result, ProjectErrorCode::OutOfRange,
                cellPath + ".state", "unknown FX action cell state");
        }
    }
    destination = std::move(candidate);
    return true;
}

JsonValue encodeFxValues(const std::vector<FxValueCell>& cells,
    std::string_view path, ProjectResult& result)
{
    if (cells.size() > kMaximumPatternRows)
        setError(result, ProjectErrorCode::SizeLimitExceeded,
            std::string(path), "FX value column exceeds 65536 cells");
    JsonValue output = JsonValue::arrayValue();
    output.array.reserve(cells.size());
    for (std::size_t index = 0u; index < cells.size(); ++index) {
        JsonValue cell = JsonValue::objectValue();
        if (cells[index].state == FxValueCellState::Previous) {
            cell.object["state"] = JsonValue::stringValue("previous");
        } else if (cells[index].state == FxValueCellState::Value) {
            finiteRange(cells[index].normalized, 0.0, 1.0,
                std::string(path) + "[" + std::to_string(index) + "].value",
                result);
            cell.object["state"] = JsonValue::stringValue("value");
            cell.object["value"] = JsonValue::numberValue(cells[index].normalized);
        } else {
            setError(result, ProjectErrorCode::OutOfRange,
                std::string(path) + "[" + std::to_string(index) + "].state",
                "invalid FX value cell state");
        }
        output.array.push_back(std::move(cell));
    }
    return output;
}

bool decodeFxValues(const JsonValue& input,
    std::vector<FxValueCell>& destination, std::string_view path,
    ProjectResult& result)
{
    if (input.type != JsonType::Array)
        return setError(result, ProjectErrorCode::TypeMismatch,
            std::string(path), "expected an FX value-cell array");
    if (input.array.size() > kMaximumPatternRows)
        return setError(result, ProjectErrorCode::SizeLimitExceeded,
            std::string(path), "FX value column exceeds 65536 cells");
    std::vector<FxValueCell> candidate;
    candidate.reserve(input.array.size());
    for (std::size_t index = 0u; index < input.array.size(); ++index) {
        const std::string cellPath = std::string(path) + "["
            + std::to_string(index) + "]";
        const auto* state = requiredField(input.array[index], "state",
            JsonType::String, cellPath, result);
        if (!state) return false;
        if (state->string == "previous")
            candidate.push_back(FxValueCell::previous());
        else if (state->string == "value") {
            const auto* value = requiredField(input.array[index], "value",
                JsonType::Number, cellPath, result);
            double normalized = 0.0;
            if (!value || !checkedNumber(*value, normalized, 0.0, 1.0,
                    cellPath + ".value", result)) return false;
            candidate.push_back(FxValueCell::withValue(
                static_cast<float>(normalized)));
        } else {
            return setError(result, ProjectErrorCode::OutOfRange,
                cellPath + ".state", "unknown FX value cell state");
        }
    }
    destination = std::move(candidate);
    return true;
}

JsonValue encodeFxPair(const FxPair& pair, std::string_view path,
    ProjectResult& result)
{
    JsonValue output = JsonValue::objectValue();
    output.object["actionColumn"] = encodeColumn(pair.actionColumn,
        pair.actions.size(), std::string(path) + ".actionColumn", result);
    output.object["actions"] = encodeFxActions(pair.actions,
        std::string(path) + ".actions", result);
    output.object["valueColumn"] = encodeColumn(pair.valueColumn,
        pair.values.size(), std::string(path) + ".valueColumn", result);
    output.object["values"] = encodeFxValues(pair.values,
        std::string(path) + ".values", result);
    return output;
}

bool decodeFxPair(const JsonValue& input, FxPair& destination,
    std::string_view path, ProjectResult& result)
{
    const auto* actions = requiredField(input, "actions", JsonType::Array,
        path, result);
    const auto* values = requiredField(input, "values", JsonType::Array,
        path, result);
    const auto* actionColumn = requiredField(input, "actionColumn",
        JsonType::Object, path, result);
    const auto* valueColumn = requiredField(input, "valueColumn",
        JsonType::Object, path, result);
    if (!actions || !values || !actionColumn || !valueColumn) return false;
    FxPair candidate;
    if (!decodeFxActions(*actions, candidate.actions,
            std::string(path) + ".actions", result)
        || !decodeFxValues(*values, candidate.values,
            std::string(path) + ".values", result)
        || !decodeColumn(*actionColumn, candidate.actionColumn,
            candidate.actions.size(), std::string(path) + ".actionColumn",
            result)
        || !decodeColumn(*valueColumn, candidate.valueColumn,
            candidate.values.size(), std::string(path) + ".valueColumn",
            result)) return false;
    destination = std::move(candidate);
    return true;
}

JsonValue encodeTrack(const Track& track, std::string_view path,
    ProjectResult& result)
{
    finiteRange(track.velocityScale, 0.0, 1.0,
        std::string(path) + ".velocityScale", result);
    if (track.midiChannel < 1u || track.midiChannel > 16u)
        setError(result, ProjectErrorCode::OutOfRange,
            std::string(path) + ".midiChannel", "MIDI channel must be 1..16");
    if (track.initialInstrumentNodeId != kInvalidInstrumentNode
        && track.initialInstrumentNodeId >= kInstrumentRackSlotCount)
        setError(result, ProjectErrorCode::OutOfRange,
            std::string(path) + ".initialInstrumentNode",
            "initial instrument node is outside the rack");

    JsonValue output = JsonValue::objectValue();
    output.object["chokeGroup"] = number(track.chokeGroup);
    output.object["destination"] = encodeEnum(track.destination,
        kDestinations, std::string(path) + ".destination", result);
    JsonValue fxPairs = JsonValue::arrayValue();
    for (std::size_t pair = 0u; pair < track.fxPairs.size(); ++pair) {
        fxPairs.array.push_back(encodeFxPair(track.fxPairs[pair],
            std::string(path) + ".fxPairs[" + std::to_string(pair) + "]",
            result));
    }
    output.object["fxPairs"] = std::move(fxPairs);
    output.object["initialInstrumentNode"] = number(
        track.initialInstrumentNodeId);
    output.object["instrumentColumn"] = encodeColumn(track.instrumentColumn,
        track.instruments.size(), std::string(path) + ".instrumentColumn",
        result);
    output.object["instruments"] = encodeInstrumentCells(track.instruments,
        std::string(path) + ".instruments", result);
    output.object["midiChannel"] = number(static_cast<uint32_t>(track.midiChannel));
    output.object["name"] = encodeCheckedString(track.name, kMaximumNameBytes,
        std::string(path) + ".name", result);
    output.object["noteColumn"] = encodeColumn(track.noteColumn,
        track.notes.size(), std::string(path) + ".noteColumn", result);
    output.object["notes"] = encodeNoteCells(track.notes,
        std::string(path) + ".notes", result);
    output.object["velocityColumn"] = encodeColumn(track.velocityColumn,
        track.velocities.size(), std::string(path) + ".velocityColumn", result);
    output.object["velocities"] = encodeValueCells(track.velocities,
        std::string(path) + ".velocities", result);
    output.object["velocityScale"] = JsonValue::numberValue(track.velocityScale);
    return output;
}

bool decodeTrack(const JsonValue& input, Track& destination,
    std::string_view path, ProjectResult& result)
{
    const auto* name = requiredField(input, "name", JsonType::String,
        path, result);
    const auto* velocityScale = requiredField(input, "velocityScale",
        JsonType::Number, path, result);
    const auto* midiChannel = requiredField(input, "midiChannel",
        JsonType::Number, path, result);
    const auto* eventDestination = requiredField(input, "destination",
        JsonType::String, path, result);
    const auto* initialNode = requiredField(input, "initialInstrumentNode",
        JsonType::Number, path, result);
    const auto* chokeGroup = requiredField(input, "chokeGroup",
        JsonType::Number, path, result);
    const auto* notes = requiredField(input, "notes", JsonType::Array,
        path, result);
    const auto* instruments = requiredField(input, "instruments",
        JsonType::Array, path, result);
    const auto* velocities = requiredField(input, "velocities",
        JsonType::Array, path, result);
    const auto* noteColumn = requiredField(input, "noteColumn",
        JsonType::Object, path, result);
    const auto* instrumentColumn = requiredField(input, "instrumentColumn",
        JsonType::Object, path, result);
    const auto* velocityColumn = requiredField(input, "velocityColumn",
        JsonType::Object, path, result);
    const auto* fxPairs = requiredField(input, "fxPairs", JsonType::Array,
        path, result);
    if (!name || !velocityScale || !midiChannel || !eventDestination
        || !initialNode || !chokeGroup || !notes || !instruments
        || !velocities || !noteColumn || !instrumentColumn || !velocityColumn
        || !fxPairs) return false;
    if (fxPairs->array.size() != kFxPairCount)
        return setError(result, ProjectErrorCode::InconsistentData,
            std::string(path) + ".fxPairs", "exactly two FX pairs are required");

    Track candidate;
    double scale = 0.0;
    uint32_t channel = 0u;
    uint32_t initial = 0u;
    if (!checkedString(*name, candidate.name, kMaximumNameBytes,
            std::string(path) + ".name", result)
        || !checkedNumber(*velocityScale, scale, 0.0, 1.0,
            std::string(path) + ".velocityScale", result)
        || !checkedUint32(*midiChannel, channel, 16u,
            std::string(path) + ".midiChannel", result)
        || channel == 0u
        || !decodeEnum(*eventDestination, kDestinations,
            candidate.destination, std::string(path) + ".destination", result)
        || !checkedUint32(*initialNode, initial,
            std::numeric_limits<uint32_t>::max(),
            std::string(path) + ".initialInstrumentNode", result)
        || (initial != kInvalidInstrumentNode
            && initial >= kInstrumentRackSlotCount)
        || !checkedUint32(*chokeGroup, candidate.chokeGroup,
            std::numeric_limits<uint32_t>::max(),
            std::string(path) + ".chokeGroup", result)) {
        if (result.ok())
            setError(result, ProjectErrorCode::OutOfRange, std::string(path),
                "track MIDI channel or initial instrument is invalid");
        return false;
    }
    candidate.velocityScale = static_cast<float>(scale);
    candidate.midiChannel = static_cast<uint8_t>(channel);
    candidate.initialInstrumentNodeId = initial;
    if (!decodeNoteCells(*notes, candidate.notes,
            std::string(path) + ".notes", result)
        || !decodeInstrumentCells(*instruments, candidate.instruments,
            std::string(path) + ".instruments", result)
        || !decodeValueCells(*velocities, candidate.velocities,
            std::string(path) + ".velocities", result)
        || !decodeColumn(*noteColumn, candidate.noteColumn,
            candidate.notes.size(), std::string(path) + ".noteColumn", result)
        || !decodeColumn(*instrumentColumn, candidate.instrumentColumn,
            candidate.instruments.size(),
            std::string(path) + ".instrumentColumn", result)
        || !decodeColumn(*velocityColumn, candidate.velocityColumn,
            candidate.velocities.size(),
            std::string(path) + ".velocityColumn", result)) return false;
    for (std::size_t pair = 0u; pair < kFxPairCount; ++pair) {
        if (!decodeFxPair(fxPairs->array[pair], candidate.fxPairs[pair],
                std::string(path) + ".fxPairs[" + std::to_string(pair) + "]",
                result)) return false;
    }
    destination = std::move(candidate);
    return true;
}

JsonValue encodePattern(const Pattern& pattern, std::string_view path,
    ProjectResult& result)
{
    if (pattern.visibleRows == 0u || pattern.visibleRows > kMaximumPatternRows)
        setError(result, ProjectErrorCode::OutOfRange,
            std::string(path) + ".visibleRows",
            "visible row count must be 1..65536");
    if (pattern.tracks.size() > kMaximumTrackCount)
        setError(result, ProjectErrorCode::SizeLimitExceeded,
            std::string(path) + ".tracks",
            "pattern exceeds the 32-track limit");
    JsonValue output = JsonValue::objectValue();
    output.object["name"] = encodeCheckedString(pattern.name,
        kMaximumNameBytes, std::string(path) + ".name", result);
    JsonValue tracks = JsonValue::arrayValue();
    tracks.array.reserve(pattern.tracks.size());
    for (std::size_t index = 0u; index < pattern.tracks.size(); ++index) {
        tracks.array.push_back(encodeTrack(pattern.tracks[index],
            std::string(path) + ".tracks[" + std::to_string(index) + "]",
            result));
    }
    output.object["tracks"] = std::move(tracks);
    output.object["visibleRows"] = number(pattern.visibleRows);
    return output;
}

bool decodePattern(const JsonValue& input, Pattern& destination,
    std::string_view path, ProjectResult& result)
{
    const auto* name = requiredField(input, "name", JsonType::String,
        path, result);
    const auto* visibleRows = requiredField(input, "visibleRows",
        JsonType::Number, path, result);
    const auto* tracks = requiredField(input, "tracks", JsonType::Array,
        path, result);
    if (!name || !visibleRows || !tracks) return false;
    if (tracks->array.size() > kMaximumTrackCount)
        return setError(result, ProjectErrorCode::SizeLimitExceeded,
            std::string(path) + ".tracks",
            "pattern exceeds the 32-track limit");
    Pattern candidate;
    if (!checkedString(*name, candidate.name, kMaximumNameBytes,
            std::string(path) + ".name", result)
        || !checkedSize(*visibleRows, candidate.visibleRows,
            kMaximumPatternRows, std::string(path) + ".visibleRows", result)
        || candidate.visibleRows == 0u) {
        if (result.ok())
            setError(result, ProjectErrorCode::OutOfRange,
                std::string(path) + ".visibleRows",
                "visible row count cannot be zero");
        return false;
    }
    candidate.tracks.reserve(tracks->array.size());
    for (std::size_t index = 0u; index < tracks->array.size(); ++index) {
        Track track;
        if (!decodeTrack(tracks->array[index], track,
                std::string(path) + ".tracks[" + std::to_string(index) + "]",
                result))
            return false;
        candidate.tracks.push_back(std::move(track));
    }
    destination = std::move(candidate);
    return true;
}

JsonValue encodePatternBank(const PatternBank& bank, ProjectResult& result)
{
    const auto validation = validatePatternBank(bank);
    if (!validation.ok()) {
        const std::string path = validation.entry == kNoPatternBankEntry
            ? "$.patternBank"
            : "$.patternBank.patterns[" + std::to_string(validation.entry)
                + "]";
        const auto code = validation.code
                    == PatternBankValidationCode::TooManyPatterns
                || validation.code
                    == PatternBankValidationCode::TooManyAliases
            ? ProjectErrorCode::SizeLimitExceeded
            : validation.code == PatternBankValidationCode::InvalidAlias
                    || validation.code
                        == PatternBankValidationCode::AliasTrackMissing
                    || validation.code
                        == PatternBankValidationCode::InvalidLaneDefaultNote
                ? ProjectErrorCode::OutOfRange
                : ProjectErrorCode::InconsistentData;
        setError(result, code, path, "pattern bank failed core validation");
    }
    JsonValue output = JsonValue::objectValue();
    output.object["activePatternId"] = encodeCheckedString(
        bank.activePatternId, kMaximumPatternIdBytes,
        "$.patternBank.activePatternId", result);
    JsonValue patterns = JsonValue::arrayValue();
    patterns.array.reserve(bank.entries.size());
    for (std::size_t index = 0u; index < bank.entries.size(); ++index) {
        const auto& entry = bank.entries[index];
        const std::string path = "$.patternBank.patterns["
            + std::to_string(index) + "]";
        JsonValue encoded = JsonValue::objectValue();
        encoded.object["id"] = encodeCheckedString(entry.id,
            kMaximumPatternIdBytes, path + ".id", result);
        if (entry.aliases.size() > kMaximumPatternAliases)
            setError(result, ProjectErrorCode::SizeLimitExceeded,
                path + ".aliases", "alias table exceeds 1024 entries");
        JsonValue aliases = JsonValue::objectValue();
        for (const auto& alias : entry.aliases) {
            if (!isValidPatternAlias(alias.first))
                setError(result, ProjectErrorCode::OutOfRange,
                    path + ".aliases",
                    "alias must be lowercase ASCII, start with a letter, and contain only letters, digits, or underscore");
            if (alias.second >= entry.pattern.tracks.size())
                setError(result, ProjectErrorCode::OutOfRange,
                    path + ".aliases." + alias.first,
                    "alias points outside this pattern");
            aliases.object[alias.first] = number(alias.second);
        }
        encoded.object["aliases"] = std::move(aliases);
        JsonValue defaultNotes = JsonValue::arrayValue();
        defaultNotes.array.reserve(entry.laneDefaultNotes.size());
        for (std::size_t lane = 0u; lane < entry.laneDefaultNotes.size();
             ++lane) {
            const uint8_t note = entry.laneDefaultNotes[lane];
            if (note > 127u)
                setError(result, ProjectErrorCode::OutOfRange,
                    path + ".laneDefaultNotes[" + std::to_string(lane) + "]",
                    "default MIDI note must be 0..127");
            defaultNotes.array.push_back(number(static_cast<uint32_t>(note)));
        }
        encoded.object["laneDefaultNotes"] = std::move(defaultNotes);
        encoded.object["pattern"] = encodePattern(entry.pattern,
            path + ".pattern", result);
        patterns.array.push_back(std::move(encoded));
    }
    output.object["patterns"] = std::move(patterns);
    return output;
}

bool decodePatternBank(const JsonValue& input, PatternBank& destination,
    ProjectResult& result)
{
    const auto* active = requiredField(input, "activePatternId",
        JsonType::String, "$.patternBank", result);
    const auto* patterns = requiredField(input, "patterns", JsonType::Array,
        "$.patternBank", result);
    if (!active || !patterns) return false;
    if (patterns->array.size() > kMaximumPatternBankEntries)
        return setError(result, ProjectErrorCode::SizeLimitExceeded,
            "$.patternBank.patterns", "pattern bank exceeds 256 patterns");

    PatternBank candidate;
    if (!checkedString(*active, candidate.activePatternId,
            kMaximumPatternIdBytes, "$.patternBank.activePatternId", result))
        return false;
    candidate.entries.reserve(patterns->array.size());
    for (std::size_t index = 0u; index < patterns->array.size(); ++index) {
        const auto& inputEntry = patterns->array[index];
        const std::string path = "$.patternBank.patterns["
            + std::to_string(index) + "]";
        const auto* id = requiredField(inputEntry, "id", JsonType::String,
            path, result);
        const auto* aliases = requiredField(inputEntry, "aliases",
            JsonType::Object, path, result);
        const auto* defaultNotes = requiredField(inputEntry,
            "laneDefaultNotes", JsonType::Array, path, result);
        const auto* pattern = requiredField(inputEntry, "pattern",
            JsonType::Object, path, result);
        if (!id || !aliases || !defaultNotes || !pattern) return false;
        if (aliases->object.size() > kMaximumPatternAliases)
            return setError(result, ProjectErrorCode::SizeLimitExceeded,
                path + ".aliases", "alias table exceeds 1024 entries");
        if (defaultNotes->array.size() > kMaximumTrackCount)
            return setError(result, ProjectErrorCode::SizeLimitExceeded,
                path + ".laneDefaultNotes",
                "default-note table exceeds 32 lanes");
        PatternBankEntry entry;
        if (!checkedString(*id, entry.id, kMaximumPatternIdBytes,
                path + ".id", result)
            || !decodePattern(*pattern, entry.pattern, path + ".pattern",
                result)) return false;
        entry.laneDefaultNotes.reserve(defaultNotes->array.size());
        for (std::size_t lane = 0u; lane < defaultNotes->array.size(); ++lane) {
            uint32_t note = 0u;
            if (!checkedUint32(defaultNotes->array[lane], note, 127u,
                    path + ".laneDefaultNotes[" + std::to_string(lane) + "]",
                    result)) return false;
            entry.laneDefaultNotes.push_back(static_cast<uint8_t>(note));
        }
        for (const auto& alias : aliases->object) {
            if (!isValidPatternAlias(alias.first))
                return setError(result, ProjectErrorCode::OutOfRange,
                    path + ".aliases",
                    "alias must be lowercase ASCII, start with a letter, and contain only letters, digits, or underscore");
            std::size_t lane = 0u;
            if (!checkedSize(alias.second, lane,
                    entry.pattern.tracks.empty()
                        ? 0u : entry.pattern.tracks.size() - 1u,
                    path + ".aliases." + alias.first, result)
                || entry.pattern.tracks.empty())
                return setError(result, ProjectErrorCode::OutOfRange,
                    path + ".aliases." + alias.first,
                    "alias points outside this pattern");
            entry.aliases.emplace(alias.first, lane);
        }
        candidate.entries.push_back(std::move(entry));
    }
    const auto validation = validatePatternBank(candidate);
    if (!validation.ok()) {
        const std::string path = validation.entry == kNoPatternBankEntry
            ? "$.patternBank"
            : "$.patternBank.patterns[" + std::to_string(validation.entry)
                + "]";
        const auto code = validation.code
                    == PatternBankValidationCode::TooManyPatterns
                || validation.code
                    == PatternBankValidationCode::TooManyAliases
            ? ProjectErrorCode::SizeLimitExceeded
            : validation.code == PatternBankValidationCode::InvalidAlias
                    || validation.code
                        == PatternBankValidationCode::AliasTrackMissing
                    || validation.code
                        == PatternBankValidationCode::InvalidLaneDefaultNote
                ? ProjectErrorCode::OutOfRange
                : ProjectErrorCode::InconsistentData;
        return setError(result, code, path,
            "pattern bank failed core validation");
    }
    destination = std::move(candidate);
    return true;
}

} // namespace
} // namespace s3g::tracker

namespace s3g::tracker {
namespace {

JsonValue encodeInstrumentRack(const InstrumentRackState& rack,
    ProjectResult& result);
bool decodeInstrumentRack(const JsonValue& input,
    InstrumentRackState& destination, ProjectResult& result);
JsonValue encodeTransport(const TransportSettings& transport,
    ProjectResult& result);
bool decodeTransport(const JsonValue& input, TransportSettings& destination,
    ProjectResult& result);
JsonValue encodeSession(const ProjectSessionState& session,
    ProjectResult& result);
bool decodeSession(const JsonValue& input, ProjectSessionState& destination,
    ProjectResult& result);

JsonValue encodeSong(const SongArrangement& song, ProjectResult& result)
{
    if (song.rows.size() > kMaximumPersistedSongRows)
        setError(result, ProjectErrorCode::SizeLimitExceeded, "$.song.rows",
            "song exceeds 4096 rows");
    if (song.ticksPerBeat == 0u
        || song.ticksPerBeat > kMaximumSongTicksPerBeat)
        setError(result, ProjectErrorCode::OutOfRange,
            "$.song.ticksPerBeat", "song ticks per beat must be 1..96");
    if (!song.rows.empty()) {
        const auto validation = validateSongArrangement(song);
        if (!validation.ok())
            setError(result, ProjectErrorCode::InconsistentData,
                validation.row == kNoSongRow ? "$.song"
                    : "$.song.rows[" + std::to_string(validation.row) + "]",
                "song arrangement failed core validation");
    }
    JsonValue output = JsonValue::objectValue();
    output.object["loop"] = JsonValue::booleanValue(song.loop);
    output.object["name"] = encodeCheckedString(song.name, kMaximumNameBytes,
        "$.song.name", result);
    JsonValue rows = JsonValue::arrayValue();
    rows.array.reserve(song.rows.size());
    for (std::size_t index = 0u; index < song.rows.size(); ++index) {
        const auto& row = song.rows[index];
        const std::string path = "$.song.rows[" + std::to_string(index) + "]";
        JsonValue encoded = JsonValue::objectValue();
        if (row.bpm.has_value())
            encoded.object["bpm"] = JsonValue::numberValue(*row.bpm);
        encoded.object["durationTicks"] = number(row.durationTicks);
        encoded.object["mutedTracks"] = number(row.mutedTracks);
        encoded.object["patternId"] = encodeCheckedString(row.patternId,
            kMaximumNameBytes, path + ".patternId", result);
        encoded.object["repeats"] = number(row.repeats);
        if (row.swing.has_value())
            encoded.object["swing"] = JsonValue::numberValue(*row.swing);
        rows.array.push_back(std::move(encoded));
    }
    output.object["rows"] = std::move(rows);
    output.object["ticksPerBeat"] = number(song.ticksPerBeat);
    return output;
}

bool decodeSong(const JsonValue& input, SongArrangement& destination,
    ProjectResult& result)
{
    const auto* name = requiredField(input, "name", JsonType::String,
        "$.song", result);
    const auto* rows = requiredField(input, "rows", JsonType::Array,
        "$.song", result);
    const auto* loop = requiredField(input, "loop", JsonType::Boolean,
        "$.song", result);
    const auto* ticks = requiredField(input, "ticksPerBeat", JsonType::Number,
        "$.song", result);
    if (!name || !rows || !loop || !ticks) return false;
    if (rows->array.size() > kMaximumPersistedSongRows)
        return setError(result, ProjectErrorCode::SizeLimitExceeded,
            "$.song.rows", "song exceeds 4096 rows");
    SongArrangement candidate;
    if (!checkedString(*name, candidate.name, kMaximumNameBytes,
            "$.song.name", result)
        || !checkedBoolean(*loop, candidate.loop, "$.song.loop", result)
        || !checkedUint32(*ticks, candidate.ticksPerBeat,
            kMaximumSongTicksPerBeat, "$.song.ticksPerBeat", result)
        || candidate.ticksPerBeat == 0u) {
        if (result.ok())
            setError(result, ProjectErrorCode::OutOfRange,
                "$.song.ticksPerBeat", "song ticks per beat cannot be zero");
        return false;
    }
    candidate.rows.reserve(rows->array.size());
    for (std::size_t index = 0u; index < rows->array.size(); ++index) {
        const auto& inputRow = rows->array[index];
        const std::string path = "$.song.rows[" + std::to_string(index) + "]";
        const auto* patternId = requiredField(inputRow, "patternId",
            JsonType::String, path, result);
        const auto* duration = requiredField(inputRow, "durationTicks",
            JsonType::Number, path, result);
        const auto* repeats = requiredField(inputRow, "repeats",
            JsonType::Number, path, result);
        const auto* muted = requiredField(inputRow, "mutedTracks",
            JsonType::Number, path, result);
        if (!patternId || !duration || !repeats || !muted) return false;
        SongRow row;
        if (!checkedString(*patternId, row.patternId, kMaximumNameBytes,
                path + ".patternId", result)
            || row.patternId.empty()
            || !checkedUint32(*duration, row.durationTicks,
                kMaximumSongDurationTicks, path + ".durationTicks", result)
            || row.durationTicks == 0u
            || !checkedUint32(*repeats, row.repeats, kMaximumSongRepeats,
                path + ".repeats", result)
            || row.repeats == 0u
            || !checkedUint32(*muted, row.mutedTracks,
                std::numeric_limits<uint32_t>::max(), path + ".mutedTracks",
                result)) {
            if (result.ok())
                setError(result, ProjectErrorCode::OutOfRange, path,
                    "song row pattern/duration/repeats is invalid");
            return false;
        }
        const auto bpm = inputRow.object.find("bpm");
        if (bpm != inputRow.object.end()) {
            double value = 0.0;
            if (!checkedNumber(bpm->second, value, 20.0, 400.0,
                    path + ".bpm", result)) return false;
            row.bpm = value;
        }
        const auto swing = inputRow.object.find("swing");
        if (swing != inputRow.object.end()) {
            double value = 0.0;
            if (!checkedNumber(swing->second, value, 0.5, 0.75,
                    path + ".swing", result)) return false;
            row.swing = value;
        }
        candidate.rows.push_back(std::move(row));
    }
    if (!candidate.rows.empty()) {
        const auto validation = validateSongArrangement(candidate);
        if (!validation.ok())
            return setError(result, ProjectErrorCode::InconsistentData,
                validation.row == kNoSongRow ? "$.song"
                    : "$.song.rows[" + std::to_string(validation.row) + "]",
                "song arrangement failed core validation");
    }
    destination = std::move(candidate);
    return true;
}

bool validatePatternRackReferences(const ProjectDocument& document,
    ProjectResult& result)
{
    std::array<bool, kInstrumentRackSlotCount> active {};
    for (const auto& instrument : document.instrumentRack.instruments) {
        if (instrument.active && instrument.nodeId < active.size())
            active[instrument.nodeId] = true;
    }
    for (std::size_t patternIndex = 0u;
         patternIndex < document.patternBank.entries.size(); ++patternIndex) {
        const auto& pattern = document.patternBank.entries[patternIndex].pattern;
        const std::string patternPath = "$.patternBank.patterns["
            + std::to_string(patternIndex) + "].pattern";
        for (std::size_t trackIndex = 0u;
             trackIndex < pattern.tracks.size(); ++trackIndex) {
            const auto& track = pattern.tracks[trackIndex];
            const std::string trackPath = patternPath + ".tracks["
                + std::to_string(trackIndex) + "]";
            if (track.initialInstrumentNodeId != kInvalidInstrumentNode
                && (track.initialInstrumentNodeId >= active.size()
                    || !active[track.initialInstrumentNodeId]))
                return setError(result, ProjectErrorCode::InconsistentData,
                    trackPath + ".initialInstrumentNode",
                    "track references an instrument that is not active in the rack");
            for (std::size_t row = 0u; row < track.instruments.size(); ++row) {
                const auto& cell = track.instruments[row];
                if (cell.state == InstrumentCellState::Instrument
                    && (cell.nodeId >= active.size() || !active[cell.nodeId]))
                    return setError(result, ProjectErrorCode::InconsistentData,
                        trackPath + ".instruments[" + std::to_string(row)
                            + "].node",
                        "instrument cell references an inactive rack node");
            }
        }
    }
    return true;
}

bool validateSongReferences(const ProjectDocument& document,
    ProjectResult& result)
{
    const auto validation = validateSongPatternReferences(
        document.song, document.patternBank);
    if (validation.ok()) return true;
    return setError(result, ProjectErrorCode::InconsistentData,
        "$.song.rows[" + std::to_string(validation.row) + "].patternId",
        "song row references a pattern that is not present in the bank");
}

JsonValue encodeDocument(const ProjectDocument& document,
    ProjectResult& result)
{
    JsonValue root = JsonValue::objectValue();
    root.object["format"] = JsonValue::stringValue(kProjectFormatIdentifier);
    root.object["instrumentRack"] = encodeInstrumentRack(
        document.instrumentRack, result);
    root.object["patternBank"] = encodePatternBank(
        document.patternBank, result);
    root.object["schemaVersion"] = number(kProjectSchemaVersion);
    root.object["session"] = encodeSession(document.session, result);
    root.object["song"] = encodeSong(document.song, result);
    root.object["transport"] = encodeTransport(document.transport, result);
    validatePatternRackReferences(document, result);
    validateSongReferences(document, result);
    return root;
}

bool decodeDocument(const JsonValue& root, ProjectDocument& destination,
    ProjectResult& result)
{
    if (root.type != JsonType::Object)
        return setError(result, ProjectErrorCode::TypeMismatch, "$",
            "project root must be an object");
    const auto* format = requiredField(root, "format", JsonType::String,
        "$", result);
    const auto* schema = requiredField(root, "schemaVersion",
        JsonType::Number, "$", result);
    const auto* patternBank = requiredField(root, "patternBank",
        JsonType::Object, "$", result);
    const auto* transport = requiredField(root, "transport", JsonType::Object,
        "$", result);
    const auto* session = requiredField(root, "session", JsonType::Object,
        "$", result);
    const auto* rack = requiredField(root, "instrumentRack", JsonType::Object,
        "$", result);
    const auto* song = requiredField(root, "song", JsonType::Object,
        "$", result);
    if (!format || !schema || !patternBank || !transport || !session || !rack
        || !song) return false;
    if (format->string != kProjectFormatIdentifier)
        return setError(result, ProjectErrorCode::InvalidArgument, "$.format",
            "file is not an s3g Tracker project");
    uint32_t schemaVersion = 0u;
    if (!checkedUint32(*schema, schemaVersion,
            std::numeric_limits<uint32_t>::max(), "$.schemaVersion", result))
        return false;
    if (schemaVersion != kProjectSchemaVersion)
        return setError(result, ProjectErrorCode::UnsupportedSchemaVersion,
            "$.schemaVersion", "project schema version is not supported");

    ProjectDocument candidate;
    if (!decodePatternBank(*patternBank, candidate.patternBank, result)
        || !decodeTransport(*transport, candidate.transport, result)
        || !decodeSession(*session, candidate.session, result)
        || !decodeInstrumentRack(*rack, candidate.instrumentRack, result)
        || !decodeSong(*song, candidate.song, result)
        || !validatePatternRackReferences(candidate, result)
        || !validateSongReferences(candidate, result)) return false;
    destination = std::move(candidate);
    return true;
}

} // namespace

ProjectResult encodeProjectDocument(const ProjectDocument& document,
    std::string& destination)
{
    try {
        ProjectResult result;
        JsonValue root = encodeDocument(document, result);
        if (!result) return result;
        std::string candidate;
        candidate.reserve(16384u);
        appendJson(root, candidate, 0u);
        candidate.push_back('\n');
        if (candidate.size() > kMaximumProjectDocumentBytes)
            return failure(ProjectErrorCode::SizeLimitExceeded, "$",
                "encoded project exceeds the 64 MiB file limit");
        destination = std::move(candidate);
        return {};
    } catch (const std::bad_alloc&) {
        return failure(ProjectErrorCode::SizeLimitExceeded, "$",
            "not enough memory to encode the project");
    } catch (const std::exception& error) {
        return failure(ProjectErrorCode::InvalidArgument, "$", error.what());
    }
}

ProjectResult decodeProjectDocument(std::string_view source,
    ProjectDocument& destination)
{
    if (source.empty())
        return failure(ProjectErrorCode::InvalidJson, "byte 0",
            "project document is empty");
    if (source.size() > kMaximumProjectDocumentBytes)
        return failure(ProjectErrorCode::SizeLimitExceeded, "$",
            "project exceeds the 64 MiB file limit");
    try {
        JsonValue root;
        JsonParser parser(source);
        ProjectResult result = parser.parse(root);
        if (!result) return result;
        ProjectDocument candidate;
        if (!decodeDocument(root, candidate, result)) return result;
        destination = std::move(candidate);
        return {};
    } catch (const std::bad_alloc&) {
        return failure(ProjectErrorCode::SizeLimitExceeded, "$",
            "not enough memory to decode the project");
    } catch (const std::exception& error) {
        return failure(ProjectErrorCode::InvalidJson, "$", error.what());
    }
}

} // namespace s3g::tracker

namespace s3g::tracker {
namespace {

template <typename PatchArray>
JsonValue encodePatchArray(const PatchArray& patches,
    std::size_t valueCount, std::string_view path, ProjectResult& result)
{
    JsonValue output = JsonValue::arrayValue();
    output.array.reserve(patches.size());
    for (std::size_t patchIndex = 0u; patchIndex < patches.size();
         ++patchIndex) {
        JsonValue patch = JsonValue::arrayValue();
        patch.array.reserve(valueCount);
        for (std::size_t parameter = 0u; parameter < valueCount; ++parameter) {
            const float value = patches[patchIndex].normalized[parameter];
            finiteRange(value, 0.0, 1.0,
                std::string(path) + "[" + std::to_string(patchIndex) + "]["
                    + std::to_string(parameter) + "]", result);
            patch.array.push_back(JsonValue::numberValue(value));
        }
        output.array.push_back(std::move(patch));
    }
    return output;
}

template <typename PatchArray>
bool decodePatchArray(const JsonValue& input, PatchArray& destination,
    std::size_t valueCount, std::string_view path, ProjectResult& result)
{
    if (input.type != JsonType::Array)
        return setError(result, ProjectErrorCode::TypeMismatch,
            std::string(path), "expected a patch array");
    if (input.array.size() != destination.size())
        return setError(result, ProjectErrorCode::InconsistentData,
            std::string(path), "patch array has the wrong fixed size");
    PatchArray candidate {};
    for (std::size_t patchIndex = 0u; patchIndex < input.array.size();
         ++patchIndex) {
        const auto& patch = input.array[patchIndex];
        const std::string patchPath = std::string(path) + "["
            + std::to_string(patchIndex) + "]";
        if (patch.type != JsonType::Array || patch.array.size() != valueCount)
            return setError(result, ProjectErrorCode::InconsistentData,
                patchPath, "patch has the wrong parameter count");
        for (std::size_t parameter = 0u; parameter < valueCount; ++parameter) {
            double value = 0.0;
            if (!checkedNumber(patch.array[parameter], value, 0.0, 1.0,
                    patchPath + "[" + std::to_string(parameter) + "]",
                    result)) return false;
            candidate[patchIndex].normalized[parameter]
                = static_cast<float>(value);
        }
    }
    destination = candidate;
    return true;
}

JsonValue encodeMembraneSlots(const InstrumentRackState& rack,
    ProjectResult& result)
{
    JsonValue output = JsonValue::arrayValue();
    output.array.reserve(rack.slots.size());
    for (std::size_t index = 0u; index < rack.slots.size(); ++index) {
        const auto& slot = rack.slots[index];
        const std::string path = "$.instrumentRack.membraneSlots["
            + std::to_string(index) + "]";
        if (slot.nodeId != index)
            setError(result, ProjectErrorCode::InconsistentData,
                path + ".node", "membrane slot node does not match its index");
        JsonValue patch = JsonValue::arrayValue();
        patch.array.reserve(slot.basePatch.normalized.size());
        for (std::size_t parameter = 0u;
             parameter < slot.basePatch.normalized.size(); ++parameter) {
            const float value = slot.basePatch.normalized[parameter];
            finiteRange(value, 0.0, 1.0,
                path + ".patch[" + std::to_string(parameter) + "]", result);
            patch.array.push_back(JsonValue::numberValue(value));
        }
        JsonValue encoded = JsonValue::objectValue();
        encoded.object["node"] = number(slot.nodeId);
        encoded.object["patch"] = std::move(patch);
        encoded.object["role"] = encodeEnum(slot.role, kMembraneRoles,
            path + ".role", result);
        output.array.push_back(std::move(encoded));
    }
    return output;
}

bool decodeMembraneSlots(const JsonValue& input, InstrumentRackState& rack,
    ProjectResult& result)
{
    if (input.type != JsonType::Array || input.array.size() != rack.slots.size())
        return setError(result, ProjectErrorCode::InconsistentData,
            "$.instrumentRack.membraneSlots",
            "membrane slot array has the wrong fixed size");
    for (std::size_t index = 0u; index < input.array.size(); ++index) {
        const std::string path = "$.instrumentRack.membraneSlots["
            + std::to_string(index) + "]";
        const auto* node = requiredField(input.array[index], "node",
            JsonType::Number, path, result);
        const auto* role = requiredField(input.array[index], "role",
            JsonType::String, path, result);
        const auto* patch = requiredField(input.array[index], "patch",
            JsonType::Array, path, result);
        if (!node || !role || !patch) return false;
        uint32_t nodeId = 0u;
        if (!checkedUint32(*node, nodeId,
                static_cast<uint32_t>(rack.slots.size() - 1u), path + ".node",
                result)
            || nodeId != index)
            return setError(result, ProjectErrorCode::InconsistentData,
                path + ".node", "membrane slot node does not match its index");
        if (!decodeEnum(*role, kMembraneRoles, rack.slots[index].role,
                path + ".role", result)) return false;
        if (patch->array.size() != kMembraneParameterCount)
            return setError(result, ProjectErrorCode::InconsistentData,
                path + ".patch", "membrane patch has the wrong parameter count");
        rack.slots[index].nodeId = nodeId;
        for (std::size_t parameter = 0u; parameter < patch->array.size();
             ++parameter) {
            double value = 0.0;
            if (!checkedNumber(patch->array[parameter], value, 0.0, 1.0,
                    path + ".patch[" + std::to_string(parameter) + "]",
                    result)) return false;
            rack.slots[index].basePatch.normalized[parameter]
                = static_cast<float>(value);
        }
    }
    return true;
}

JsonValue encodeSamplerSlots(const InstrumentRackState& rack,
    ProjectResult& result)
{
    JsonValue output = JsonValue::arrayValue();
    output.array.reserve(rack.samplerSlots.size());
    for (std::size_t index = 0u; index < rack.samplerSlots.size(); ++index) {
        const auto& slot = rack.samplerSlots[index];
        const std::string path = "$.instrumentRack.samplerSlots["
            + std::to_string(index) + "]";
        const uint32_t expectedNode = stereoSamplerNodeForRackSlot(index);
        if (slot.nodeId != expectedNode)
            setError(result, ProjectErrorCode::InconsistentData,
                path + ".node", "sampler slot node does not match its index");
        if (slot.baseNote > 127u)
            setError(result, ProjectErrorCode::OutOfRange,
                path + ".baseNote", "sampler base note must be 0..127");
        if (slot.sliceCount > slot.slices.size())
            setError(result, ProjectErrorCode::SizeLimitExceeded,
                path + ".slices", "sampler exceeds 128 slices");
        if (slot.filePath.empty() && slot.sliceCount != 0u)
            setError(result, ProjectErrorCode::InconsistentData, path,
                "sampler slices require a source-file reference");
        if (!slot.envelope.valid())
            setError(result, ProjectErrorCode::OutOfRange,
                path + ".envelope", "sampler envelope is invalid");

        JsonValue encoded = JsonValue::objectValue();
        encoded.object["baseNote"] = number(static_cast<uint32_t>(slot.baseNote));
        JsonValue envelope = JsonValue::objectValue();
        envelope.object["attackMilliseconds"]
            = JsonValue::numberValue(slot.envelope.attackMilliseconds);
        envelope.object["decayMilliseconds"]
            = JsonValue::numberValue(slot.envelope.decayMilliseconds);
        envelope.object["releaseMilliseconds"]
            = JsonValue::numberValue(slot.envelope.releaseMilliseconds);
        envelope.object["sustain"]
            = JsonValue::numberValue(slot.envelope.sustain);
        encoded.object["envelope"] = std::move(envelope);
        encoded.object["file"] = encodeCheckedString(slot.filePath,
            kMaximumStringBytes, path + ".file", result);
        encoded.object["node"] = number(slot.nodeId);
        JsonValue slices = JsonValue::arrayValue();
        const std::size_t safeSliceCount = std::min(slot.sliceCount,
            slot.slices.size());
        slices.array.reserve(safeSliceCount);
        const uint32_t assetFrames = slot.asset ? slot.asset->frameCount() : 0u;
        for (std::size_t sliceIndex = 0u; sliceIndex < safeSliceCount;
             ++sliceIndex) {
            const auto& slice = slot.slices[sliceIndex];
            const std::string slicePath = path + ".slices["
                + std::to_string(sliceIndex) + "]";
            if (slice.startFrame >= slice.endFrame
                || (slot.asset && slice.endFrame > assetFrames))
                setError(result, ProjectErrorCode::OutOfRange, slicePath,
                    "sampler slice frame range is invalid");
            finiteRange(slice.gain, 0.0, 2.0, slicePath + ".gain", result);
            JsonValue value = JsonValue::objectValue();
            value.object["endFrame"] = number(slice.endFrame);
            value.object["gain"] = JsonValue::numberValue(slice.gain);
            value.object["reverse"] = JsonValue::booleanValue(slice.reverse);
            value.object["startFrame"] = number(slice.startFrame);
            slices.array.push_back(std::move(value));
        }
        encoded.object["slices"] = std::move(slices);
        output.array.push_back(std::move(encoded));
    }
    return output;
}

bool decodeSamplerSlots(const JsonValue& input, InstrumentRackState& rack,
    ProjectResult& result)
{
    if (input.type != JsonType::Array
        || input.array.size() != rack.samplerSlots.size())
        return setError(result, ProjectErrorCode::InconsistentData,
            "$.instrumentRack.samplerSlots",
            "sampler slot array has the wrong fixed size");
    for (std::size_t index = 0u; index < input.array.size(); ++index) {
        const std::string path = "$.instrumentRack.samplerSlots["
            + std::to_string(index) + "]";
        const auto* node = requiredField(input.array[index], "node",
            JsonType::Number, path, result);
        const auto* file = requiredField(input.array[index], "file",
            JsonType::String, path, result);
        const auto* baseNote = requiredField(input.array[index], "baseNote",
            JsonType::Number, path, result);
        const auto* envelope = requiredField(input.array[index], "envelope",
            JsonType::Object, path, result);
        const auto* slices = requiredField(input.array[index], "slices",
            JsonType::Array, path, result);
        if (!node || !file || !baseNote || !envelope || !slices) return false;
        if (slices->array.size() > audio::kMaximumSamplerSlices)
            return setError(result, ProjectErrorCode::SizeLimitExceeded,
                path + ".slices", "sampler exceeds 128 slices");
        auto& slot = rack.samplerSlots[index];
        uint32_t nodeId = 0u;
        uint32_t note = 0u;
        if (!checkedUint32(*node, nodeId,
                static_cast<uint32_t>(kInstrumentRackSlotCount - 1u),
                path + ".node", result)
            || nodeId != stereoSamplerNodeForRackSlot(index)
            || !checkedString(*file, slot.filePath, kMaximumStringBytes,
                path + ".file", result)
            || !checkedUint32(*baseNote, note, 127u, path + ".baseNote",
                result)) {
            if (result.ok())
                setError(result, ProjectErrorCode::InconsistentData,
                    path + ".node", "sampler slot node does not match its index");
            return false;
        }
        if (slot.filePath.empty() && !slices->array.empty())
            return setError(result, ProjectErrorCode::InconsistentData, path,
                "sampler slices require a source-file reference");
        const auto* attack = requiredField(*envelope, "attackMilliseconds",
            JsonType::Number, path + ".envelope", result);
        const auto* decay = requiredField(*envelope, "decayMilliseconds",
            JsonType::Number, path + ".envelope", result);
        const auto* sustain = requiredField(*envelope, "sustain",
            JsonType::Number, path + ".envelope", result);
        const auto* release = requiredField(*envelope, "releaseMilliseconds",
            JsonType::Number, path + ".envelope", result);
        if (!attack || !decay || !sustain || !release) return false;
        double sustainValue = 0.0;
        if (!checkedNumber(*attack, slot.envelope.attackMilliseconds, 0.0,
                audio::kMaximumSamplerEnvelopeMilliseconds,
                path + ".envelope.attackMilliseconds", result)
            || !checkedNumber(*decay, slot.envelope.decayMilliseconds, 0.0,
                audio::kMaximumSamplerEnvelopeMilliseconds,
                path + ".envelope.decayMilliseconds", result)
            || !checkedNumber(*sustain, sustainValue, 0.0, 1.0,
                path + ".envelope.sustain", result)
            || !checkedNumber(*release, slot.envelope.releaseMilliseconds,
                0.0, audio::kMaximumSamplerEnvelopeMilliseconds,
                path + ".envelope.releaseMilliseconds", result)) {
            return false;
        }
        slot.envelope.sustain = static_cast<float>(sustainValue);
        slot.nodeId = nodeId;
        slot.baseNote = static_cast<uint8_t>(note);
        slot.asset.reset();
        slot.analysis.reset();
        slot.slices = {};
        slot.sliceCount = slices->array.size();
        for (std::size_t sliceIndex = 0u; sliceIndex < slices->array.size();
             ++sliceIndex) {
            const std::string slicePath = path + ".slices["
                + std::to_string(sliceIndex) + "]";
            const auto& inputSlice = slices->array[sliceIndex];
            const auto* start = requiredField(inputSlice, "startFrame",
                JsonType::Number, slicePath, result);
            const auto* end = requiredField(inputSlice, "endFrame",
                JsonType::Number, slicePath, result);
            const auto* gain = requiredField(inputSlice, "gain",
                JsonType::Number, slicePath, result);
            const auto* reverse = requiredField(inputSlice, "reverse",
                JsonType::Boolean, slicePath, result);
            if (!start || !end || !gain || !reverse) return false;
            auto& slice = slot.slices[sliceIndex];
            double gainValue = 0.0;
            if (!checkedUint32(*start, slice.startFrame,
                    std::numeric_limits<uint32_t>::max(),
                    slicePath + ".startFrame", result)
                || !checkedUint32(*end, slice.endFrame,
                    std::numeric_limits<uint32_t>::max(),
                    slicePath + ".endFrame", result)
                || slice.startFrame >= slice.endFrame
                || !checkedNumber(*gain, gainValue, 0.0, 2.0,
                    slicePath + ".gain", result)
                || !checkedBoolean(*reverse, slice.reverse,
                    slicePath + ".reverse", result)) {
                if (result.ok())
                    setError(result, ProjectErrorCode::OutOfRange, slicePath,
                        "sampler slice frame range is invalid");
                return false;
            }
            slice.gain = static_cast<float>(gainValue);
        }
    }
    return true;
}

JsonValue encodeMidiRoutes(const InstrumentRackState& rack,
    ProjectResult& result)
{
    JsonValue output = JsonValue::arrayValue();
    output.array.reserve(rack.midiRoutes.size());
    for (std::size_t index = 0u; index < rack.midiRoutes.size(); ++index) {
        const auto& route = rack.midiRoutes[index];
        const std::string path = "$.instrumentRack.midiRoutes["
            + std::to_string(index) + "]";
        if (route.virtualSource == 0u
            || route.virtualSource > kMidiOutRackSlotCount)
            setError(result, ProjectErrorCode::OutOfRange,
                path + ".virtualSource", "virtual source must be 1..8");
        if (route.channel == 0u || route.channel > 16u)
            setError(result, ProjectErrorCode::OutOfRange,
                path + ".channel", "MIDI channel must be 1..16");
        if (route.kind == MidiInstrumentRouteKind::VirtualSource
            && route.destinationId != 0)
            setError(result, ProjectErrorCode::InconsistentData,
                path + ".destinationId",
                "virtual-source routes must not retain a destination ID");
        JsonValue value = JsonValue::objectValue();
        value.object["channel"] = number(static_cast<uint32_t>(route.channel));
        value.object["destinationId"] = JsonValue::numberValue(
            static_cast<double>(route.destinationId));
        value.object["kind"] = encodeEnum(route.kind, kMidiRouteKinds,
            path + ".kind", result);
        value.object["node"] = number(midiOutNodeForRackSlot(index));
        value.object["virtualSource"] = number(
            static_cast<uint32_t>(route.virtualSource));
        output.array.push_back(std::move(value));
    }
    return output;
}

bool decodeMidiRoutes(const JsonValue& input, InstrumentRackState& rack,
    ProjectResult& result)
{
    if (input.type != JsonType::Array
        || input.array.size() != rack.midiRoutes.size())
        return setError(result, ProjectErrorCode::InconsistentData,
            "$.instrumentRack.midiRoutes",
            "MIDI route array has the wrong fixed size");
    for (std::size_t index = 0u; index < input.array.size(); ++index) {
        const std::string path = "$.instrumentRack.midiRoutes["
            + std::to_string(index) + "]";
        const auto* node = requiredField(input.array[index], "node",
            JsonType::Number, path, result);
        const auto* kind = requiredField(input.array[index], "kind",
            JsonType::String, path, result);
        const auto* destinationId = requiredField(input.array[index],
            "destinationId", JsonType::Number, path, result);
        const auto* virtualSource = requiredField(input.array[index],
            "virtualSource", JsonType::Number, path, result);
        const auto* channel = requiredField(input.array[index], "channel",
            JsonType::Number, path, result);
        if (!node || !kind || !destinationId || !virtualSource || !channel)
            return false;
        uint32_t nodeId = 0u;
        uint32_t source = 0u;
        uint32_t channelValue = 0u;
        auto& route = rack.midiRoutes[index];
        if (!checkedUint32(*node, nodeId,
                static_cast<uint32_t>(kInstrumentRackSlotCount - 1u),
                path + ".node", result)
            || nodeId != midiOutNodeForRackSlot(index)
            || !decodeEnum(*kind, kMidiRouteKinds, route.kind,
                path + ".kind", result)
            || !checkedInt32(*destinationId, route.destinationId,
                path + ".destinationId", result)
            || !checkedUint32(*virtualSource, source,
                static_cast<uint32_t>(kMidiOutRackSlotCount),
                path + ".virtualSource", result)
            || source == 0u
            || !checkedUint32(*channel, channelValue, 16u,
                path + ".channel", result)
            || channelValue == 0u) {
            if (result.ok())
                setError(result, ProjectErrorCode::OutOfRange, path,
                    "MIDI route node/source/channel is invalid");
            return false;
        }
        if (route.kind == MidiInstrumentRouteKind::VirtualSource
            && route.destinationId != 0)
            return setError(result, ProjectErrorCode::InconsistentData,
                path + ".destinationId",
                "virtual-source routes must not retain a destination ID");
        route.virtualSource = static_cast<uint8_t>(source);
        route.channel = static_cast<uint8_t>(channelValue);
    }
    return true;
}

JsonValue encodeInstrumentRack(const InstrumentRackState& rack,
    ProjectResult& result)
{
    JsonValue output = JsonValue::objectValue();
    JsonValue activeNodes = JsonValue::arrayValue();
    std::array<bool, kInstrumentRackSlotCount> seen {};
    for (std::size_t index = 0u; index < rack.instruments.size(); ++index) {
        const auto& instrument = rack.instruments[index];
        if (!instrument.active) continue;
        if (instrument.nodeId >= kInstrumentRackSlotCount
            || seen[instrument.nodeId]) {
            setError(result, ProjectErrorCode::InconsistentData,
                "$.instrumentRack.activeNodes",
                "active rack nodes must be unique and in range");
            continue;
        }
        seen[instrument.nodeId] = true;
        const auto* canonical = defaultRackInstrument(instrument.nodeId);
        if (!canonical || canonical->kind != instrument.kind
            || canonical->name != instrument.name
            || canonical->mnemonic != instrument.mnemonic
            || !projectInstrumentKindIsActive(instrument.kind))
            setError(result, ProjectErrorCode::InconsistentData,
                "$.instrumentRack.activeNodes",
                "active rack instrument is unavailable or not canonical");
        activeNodes.array.push_back(number(instrument.nodeId));
    }
    if (activeNodes.array.empty())
        setError(result, ProjectErrorCode::InconsistentData,
            "$.instrumentRack.activeNodes", "rack must contain an active instrument");
    if (rack.selectedNode >= kInstrumentRackSlotCount
        || !seen[rack.selectedNode])
        setError(result, ProjectErrorCode::InconsistentData,
            "$.instrumentRack.selectedNode",
            "selected rack node must identify an active instrument");
    output.object["activeNodes"] = std::move(activeNodes);
    output.object["daisyDrumPatches"] = encodePatchArray(
        rack.daisyDrumPatches, kDaisyDrumParameterCapacity,
        "$.instrumentRack.daisyDrumPatches", result);
    output.object["membraneSlots"] = encodeMembraneSlots(rack, result);
    output.object["midiRoutes"] = encodeMidiRoutes(rack, result);
    output.object["samplerSlots"] = encodeSamplerSlots(rack, result);
    output.object["selectedNode"] = number(rack.selectedNode);
    output.object["sn76489Patches"] = encodePatchArray(rack.sn76489Patches,
        kSn76489ParameterCount, "$.instrumentRack.sn76489Patches", result);
    output.object["ym2151Patches"] = encodePatchArray(rack.ym2151Patches,
        kYm2151ParameterCount, "$.instrumentRack.ym2151Patches", result);
    return output;
}

bool decodeInstrumentRack(const JsonValue& input,
    InstrumentRackState& destination, ProjectResult& result)
{
    const auto* activeNodes = requiredField(input, "activeNodes",
        JsonType::Array, "$.instrumentRack", result);
    const auto* selectedNode = requiredField(input, "selectedNode",
        JsonType::Number, "$.instrumentRack", result);
    const auto* membraneSlots = requiredField(input, "membraneSlots",
        JsonType::Array, "$.instrumentRack", result);
    const auto* snPatches = requiredField(input, "sn76489Patches",
        JsonType::Array, "$.instrumentRack", result);
    const auto* ymPatches = requiredField(input, "ym2151Patches",
        JsonType::Array, "$.instrumentRack", result);
    const auto* daisyPatches = requiredField(input, "daisyDrumPatches",
        JsonType::Array, "$.instrumentRack", result);
    const auto* samplerSlots = requiredField(input, "samplerSlots",
        JsonType::Array, "$.instrumentRack", result);
    const auto* midiRoutes = requiredField(input, "midiRoutes",
        JsonType::Array, "$.instrumentRack", result);
    if (!activeNodes || !selectedNode || !membraneSlots || !snPatches
        || !ymPatches || !daisyPatches || !samplerSlots || !midiRoutes)
        return false;
    if (activeNodes->array.empty()
        || activeNodes->array.size() > kInstrumentRackSlotCount)
        return setError(result, ProjectErrorCode::InconsistentData,
            "$.instrumentRack.activeNodes",
            "rack must contain 1..39 active instruments");

    InstrumentRackState candidate = makeDefaultInstrumentRack();
    candidate.instruments = {};
    std::array<bool, kInstrumentRackSlotCount> seen {};
    for (std::size_t index = 0u; index < activeNodes->array.size(); ++index) {
        uint32_t nodeId = 0u;
        const std::string path = "$.instrumentRack.activeNodes["
            + std::to_string(index) + "]";
        if (!checkedUint32(activeNodes->array[index], nodeId,
                static_cast<uint32_t>(kInstrumentRackSlotCount - 1u), path,
                result)) return false;
        if (seen[nodeId])
            return setError(result, ProjectErrorCode::InconsistentData, path,
                "active rack node is duplicated");
        seen[nodeId] = true;
        const auto* instrument = defaultRackInstrument(nodeId);
        if (!instrument || !projectInstrumentKindIsActive(instrument->kind))
            return setError(result, ProjectErrorCode::OutOfRange, path,
                "active rack node is unavailable in this tracker build");
        candidate.instruments[index] = *instrument;
    }
    if (!checkedUint32(*selectedNode, candidate.selectedNode,
            static_cast<uint32_t>(kInstrumentRackSlotCount - 1u),
            "$.instrumentRack.selectedNode", result)) return false;
    if (!seen[candidate.selectedNode])
        return setError(result, ProjectErrorCode::InconsistentData,
            "$.instrumentRack.selectedNode",
            "selected rack node must identify an active instrument");
    if (!decodeMembraneSlots(*membraneSlots, candidate, result)
        || !decodePatchArray(*snPatches, candidate.sn76489Patches,
            kSn76489ParameterCount, "$.instrumentRack.sn76489Patches", result)
        || !decodePatchArray(*ymPatches, candidate.ym2151Patches,
            kYm2151ParameterCount, "$.instrumentRack.ym2151Patches", result)
        || !decodePatchArray(*daisyPatches, candidate.daisyDrumPatches,
            kDaisyDrumParameterCapacity,
            "$.instrumentRack.daisyDrumPatches", result)
        || !decodeSamplerSlots(*samplerSlots, candidate, result)
        || !decodeMidiRoutes(*midiRoutes, candidate, result)) return false;
    destination = std::move(candidate);
    return true;
}

} // namespace
} // namespace s3g::tracker

namespace s3g::tracker {
namespace {

JsonValue encodeTimingWarp(const TimingWarpTransform& transform,
    std::string_view path, ProjectResult& result)
{
    TimingWarpStack validation;
    const auto append = validation.append(transform);
    if (!append.added() || append.corrections != TimingWarpCorrection::None)
        setError(result, ProjectErrorCode::OutOfRange, std::string(path),
            "timing warp is not in canonical supported form");
    JsonValue output = JsonValue::objectValue();
    output.object["alpha"] = JsonValue::numberValue(transform.options.alpha);
    output.object["exponent"] = JsonValue::numberValue(transform.exponent);
    output.object["kind"] = encodeEnum(transform.kind, kTimingWarpKinds,
        std::string(path) + ".kind", result);
    output.object["phaseBegin"] = JsonValue::numberValue(
        transform.options.phaseBegin);
    output.object["phaseEnd"] = JsonValue::numberValue(
        transform.options.phaseEnd);
    output.object["pulses"] = number(transform.pulses);
    output.object["repetitions"] = number(transform.options.repetitions);
    output.object["steps"] = number(transform.steps);
    return output;
}

bool decodeTimingWarp(const JsonValue& input, TimingWarpTransform& destination,
    std::string_view path, ProjectResult& result)
{
    const auto* kind = requiredField(input, "kind", JsonType::String,
        path, result);
    const auto* phaseBegin = requiredField(input, "phaseBegin",
        JsonType::Number, path, result);
    const auto* phaseEnd = requiredField(input, "phaseEnd", JsonType::Number,
        path, result);
    const auto* repetitions = requiredField(input, "repetitions",
        JsonType::Number, path, result);
    const auto* alpha = requiredField(input, "alpha", JsonType::Number,
        path, result);
    const auto* exponent = requiredField(input, "exponent", JsonType::Number,
        path, result);
    const auto* pulses = requiredField(input, "pulses", JsonType::Number,
        path, result);
    const auto* steps = requiredField(input, "steps", JsonType::Number,
        path, result);
    if (!kind || !phaseBegin || !phaseEnd || !repetitions || !alpha
        || !exponent || !pulses || !steps) return false;

    TimingWarpTransform candidate;
    if (!decodeEnum(*kind, kTimingWarpKinds, candidate.kind,
            std::string(path) + ".kind", result)
        || !checkedNumber(*phaseBegin, candidate.options.phaseBegin, 0.0, 1.0,
            std::string(path) + ".phaseBegin", result)
        || !checkedNumber(*phaseEnd, candidate.options.phaseEnd, 0.0, 1.0,
            std::string(path) + ".phaseEnd", result)
        || !checkedUint32(*repetitions, candidate.options.repetitions,
            TimingWarpStack::kMaximumRepetitions,
            std::string(path) + ".repetitions", result)
        || !checkedNumber(*alpha, candidate.options.alpha, 0.0, 1.0,
            std::string(path) + ".alpha", result)
        || !checkedNumber(*exponent, candidate.exponent,
            TimingWarpStack::kMinimumExponent,
            TimingWarpStack::kMaximumExponent,
            std::string(path) + ".exponent", result)
        || !checkedUint32(*pulses, candidate.pulses,
            TimingWarpStack::kMaximumSteps,
            std::string(path) + ".pulses", result)
        || !checkedUint32(*steps, candidate.steps,
            TimingWarpStack::kMaximumSteps,
            std::string(path) + ".steps", result)) return false;

    TimingWarpStack validation;
    const auto append = validation.append(candidate);
    if (!append.added() || append.corrections != TimingWarpCorrection::None)
        return setError(result, ProjectErrorCode::OutOfRange,
            std::string(path),
            "timing warp is degenerate or not in canonical supported form");
    const auto* canonical = validation.transform(0u);
    if (!canonical)
        return setError(result, ProjectErrorCode::InconsistentData,
            std::string(path), "timing warp validation failed");
    destination = *canonical;
    return true;
}

JsonValue encodeTransport(const TransportSettings& transport,
    ProjectResult& result)
{
    finiteRange(transport.sampleRate, 8000.0, 768000.0,
        "$.transport.sampleRate", result);
    finiteRange(transport.bpm, 20.0, 400.0, "$.transport.bpm", result);
    finiteRange(transport.swing, 0.5, 0.75, "$.transport.swing", result);
    finiteRange(transport.microTimingRangeMilliseconds, 0.0, 500.0,
        "$.transport.microTimingRangeMilliseconds", result);
    finiteRange(transport.timingLookaheadMilliseconds,
        transport.microTimingRangeMilliseconds, 500.0,
        "$.transport.timingLookaheadMilliseconds", result);
    if (transport.ticksPerBeat == 0u || transport.ticksPerBeat > 64u)
        setError(result, ProjectErrorCode::OutOfRange,
            "$.transport.ticksPerBeat", "ticks per beat must be 1..64");
    if (transport.warpCycleTicks == 0u || transport.warpCycleTicks > 1024u)
        setError(result, ProjectErrorCode::OutOfRange,
            "$.transport.warpCycleTicks", "warp cycle must be 1..1024 ticks");
    if (transport.loopStartRow > 65535u
        || transport.loopEndRow <= transport.loopStartRow
        || transport.loopEndRow > 65536u)
        setError(result, ProjectErrorCode::OutOfRange,
            "$.transport.loop", "loop rows must form a non-empty 0..65536 range");

    JsonValue output = JsonValue::objectValue();
    output.object["bpm"] = JsonValue::numberValue(transport.bpm);
    JsonValue loop = JsonValue::objectValue();
    loop.object["enabled"] = JsonValue::booleanValue(transport.loopEnabled);
    loop.object["endRow"] = number(transport.loopEndRow);
    loop.object["startRow"] = number(transport.loopStartRow);
    output.object["loop"] = std::move(loop);
    output.object["microTimingRangeMilliseconds"] = JsonValue::numberValue(
        transport.microTimingRangeMilliseconds);
    output.object["sampleRate"] = JsonValue::numberValue(transport.sampleRate);
    output.object["swing"] = JsonValue::numberValue(transport.swing);
    output.object["ticksPerBeat"] = number(transport.ticksPerBeat);
    output.object["timingLookaheadMilliseconds"] = JsonValue::numberValue(
        transport.timingLookaheadMilliseconds);
    JsonValue warpStack = JsonValue::arrayValue();
    warpStack.array.reserve(transport.timingWarp.size());
    for (std::size_t index = 0u; index < transport.timingWarp.size(); ++index) {
        const auto* transform = transport.timingWarp.transform(index);
        if (!transform) {
            setError(result, ProjectErrorCode::InconsistentData,
                "$.transport.warpStack", "warp stack contains a missing entry");
            break;
        }
        warpStack.array.push_back(encodeTimingWarp(*transform,
            "$.transport.warpStack[" + std::to_string(index) + "]", result));
    }
    output.object["warpCycleTicks"] = number(transport.warpCycleTicks);
    output.object["warpStack"] = std::move(warpStack);
    return output;
}

bool decodeTransport(const JsonValue& input, TransportSettings& destination,
    ProjectResult& result)
{
    const auto* sampleRate = requiredField(input, "sampleRate",
        JsonType::Number, "$.transport", result);
    const auto* bpm = requiredField(input, "bpm", JsonType::Number,
        "$.transport", result);
    const auto* ticks = requiredField(input, "ticksPerBeat", JsonType::Number,
        "$.transport", result);
    const auto* swing = requiredField(input, "swing", JsonType::Number,
        "$.transport", result);
    const auto* warpCycle = requiredField(input, "warpCycleTicks",
        JsonType::Number, "$.transport", result);
    const auto* warps = requiredField(input, "warpStack", JsonType::Array,
        "$.transport", result);
    const auto* lookahead = requiredField(input,
        "timingLookaheadMilliseconds", JsonType::Number, "$.transport",
        result);
    const auto* microRange = requiredField(input,
        "microTimingRangeMilliseconds", JsonType::Number, "$.transport",
        result);
    const auto* loop = requiredField(input, "loop", JsonType::Object,
        "$.transport", result);
    if (!sampleRate || !bpm || !ticks || !swing || !warpCycle || !warps
        || !lookahead || !microRange || !loop) return false;
    if (warps->array.size() > TimingWarpStack::kMaximumTransforms)
        return setError(result, ProjectErrorCode::SizeLimitExceeded,
            "$.transport.warpStack", "warp stack exceeds 32 transforms");
    const auto* loopEnabled = requiredField(*loop, "enabled",
        JsonType::Boolean, "$.transport.loop", result);
    const auto* loopStart = requiredField(*loop, "startRow",
        JsonType::Number, "$.transport.loop", result);
    const auto* loopEnd = requiredField(*loop, "endRow", JsonType::Number,
        "$.transport.loop", result);
    if (!loopEnabled || !loopStart || !loopEnd) return false;

    TransportSettings candidate;
    if (!checkedNumber(*sampleRate, candidate.sampleRate, 8000.0, 768000.0,
            "$.transport.sampleRate", result)
        || !checkedNumber(*bpm, candidate.bpm, 20.0, 400.0,
            "$.transport.bpm", result)
        || !checkedUint32(*ticks, candidate.ticksPerBeat, 64u,
            "$.transport.ticksPerBeat", result)
        || candidate.ticksPerBeat == 0u
        || !checkedNumber(*swing, candidate.swing, 0.5, 0.75,
            "$.transport.swing", result)
        || !checkedUint32(*warpCycle, candidate.warpCycleTicks, 1024u,
            "$.transport.warpCycleTicks", result)
        || candidate.warpCycleTicks == 0u
        || !checkedNumber(*microRange,
            candidate.microTimingRangeMilliseconds, 0.0, 500.0,
            "$.transport.microTimingRangeMilliseconds", result)
        || !checkedNumber(*lookahead,
            candidate.timingLookaheadMilliseconds,
            candidate.microTimingRangeMilliseconds, 500.0,
            "$.transport.timingLookaheadMilliseconds", result)
        || !checkedBoolean(*loopEnabled, candidate.loopEnabled,
            "$.transport.loop.enabled", result)
        || !checkedUint32(*loopStart, candidate.loopStartRow, 65535u,
            "$.transport.loop.startRow", result)
        || !checkedUint32(*loopEnd, candidate.loopEndRow, 65536u,
            "$.transport.loop.endRow", result)) {
        if (result.ok())
            setError(result, ProjectErrorCode::OutOfRange, "$.transport",
                "transport contains a zero or invalid range");
        return false;
    }
    if (candidate.loopEndRow <= candidate.loopStartRow)
        return setError(result, ProjectErrorCode::OutOfRange,
            "$.transport.loop", "loop end must be greater than loop start");
    for (std::size_t index = 0u; index < warps->array.size(); ++index) {
        TimingWarpTransform transform;
        const std::string path = "$.transport.warpStack["
            + std::to_string(index) + "]";
        if (!decodeTimingWarp(warps->array[index], transform, path, result))
            return false;
        const auto appended = candidate.timingWarp.append(transform);
        if (!appended.added() || appended.corrections != TimingWarpCorrection::None)
            return setError(result, ProjectErrorCode::InconsistentData, path,
                "validated timing warp could not be appended");
    }
    destination = candidate;
    return true;
}

JsonValue encodeSession(const ProjectSessionState& session,
    ProjectResult& result)
{
    finiteRange(session.gateMilliseconds, 1.0, 10000.0,
        "$.session.gateMilliseconds", result);
    finiteRange(session.mainOutputGain, 0.0, 1.0,
        "$.session.mainOutputGain", result);
    JsonValue output = JsonValue::objectValue();
    output.object["commandRngState"] = JsonValue::stringValue(
        std::to_string(session.commandRngState));
    output.object["gateMilliseconds"] = JsonValue::numberValue(
        session.gateMilliseconds);
    output.object["mainOutputGain"] = JsonValue::numberValue(
        session.mainOutputGain);
    output.object["mainOutputMuted"] = JsonValue::booleanValue(
        session.mainOutputMuted);
    output.object["songPlaybackEnabled"] = JsonValue::booleanValue(
        session.songPlaybackEnabled);
    output.object["playbackSeed"] = number(session.playbackSeed);
    return output;
}

bool decodeSession(const JsonValue& input, ProjectSessionState& destination,
    ProjectResult& result)
{
    const auto* gate = requiredField(input, "gateMilliseconds",
        JsonType::Number, "$.session", result);
    const auto* commandSeed = requiredField(input, "commandRngState",
        JsonType::String, "$.session", result);
    const auto* playbackSeed = requiredField(input, "playbackSeed",
        JsonType::Number, "$.session", result);
    const auto* mainGain = requiredField(input, "mainOutputGain",
        JsonType::Number, "$.session", result);
    const auto* mainMuted = requiredField(input, "mainOutputMuted",
        JsonType::Boolean, "$.session", result);
    const auto* songEnabled = requiredField(input, "songPlaybackEnabled",
        JsonType::Boolean, "$.session", result);
    if (!gate || !commandSeed || !playbackSeed
        || !mainGain || !mainMuted || !songEnabled)
        return false;
    ProjectSessionState candidate;
    double decodedMainGain = 1.0;
    if (!checkedNumber(*gate, candidate.gateMilliseconds, 1.0, 10000.0,
            "$.session.gateMilliseconds", result)
        || !checkedNumber(*mainGain, decodedMainGain, 0.0, 1.0,
            "$.session.mainOutputGain", result)
        || !checkedBoolean(*mainMuted, candidate.mainOutputMuted,
            "$.session.mainOutputMuted", result)
        || !checkedBoolean(*songEnabled, candidate.songPlaybackEnabled,
            "$.session.songPlaybackEnabled", result)
        || !checkedUint64String(*commandSeed, candidate.commandRngState,
            "$.session.commandRngState", result)
        || !checkedUint32(*playbackSeed, candidate.playbackSeed,
            std::numeric_limits<uint32_t>::max(), "$.session.playbackSeed",
            result)) return false;
    candidate.mainOutputGain = static_cast<float>(decodedMainGain);
    destination = std::move(candidate);
    return true;
}

} // namespace
} // namespace s3g::tracker
