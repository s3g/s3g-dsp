#include "s3g/tracker/project_codec.h"
#include "s3g/tracker/asset_pack.h"

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
constexpr std::array<std::pair<std::string_view, ValueInterpolation>, 2u>
    kValueInterpolations {{
        { "step", ValueInterpolation::Step },
        { "linear", ValueInterpolation::Linear },
    }};
constexpr std::array<std::pair<std::string_view, SequencerAction>, 14u>
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
        { "condition", SequencerAction::Condition },
        { "energy", SequencerAction::Energy },
    }};
static_assert(kSequencerActions.size() == kSequencerActionCount,
    "project schema must explicitly name every sequencing action");
constexpr std::array<std::pair<std::string_view, TimingWarpKind>, 3u>
    kTimingWarpKinds {{
        { "exponential", TimingWarpKind::Exponential },
        { "step-quantize", TimingWarpKind::StepQuantize },
        { "euclidean-quantize", TimingWarpKind::EuclideanQuantize },
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
        case NoteCellState::Hold:
            cell.object["state"] = JsonValue::stringValue("hold");
            break;
        case NoteCellState::Note:
            if (cells[index].noteVoiceCount() == 1u) {
                cell.object["note"] = number(
                    static_cast<uint32_t>(cells[index].note));
            } else {
                JsonValue notes = JsonValue::arrayValue();
                for (std::size_t voice = 0u;
                     voice < cells[index].noteVoiceCount(); ++voice) {
                    notes.array.push_back(number(static_cast<uint32_t>(
                        cells[index].noteVoice(voice))));
                }
                cell.object["notes"] = std::move(notes);
            }
            cell.object["state"] = JsonValue::stringValue("note");
            break;
        case NoteCellState::Burst:
            if (cells[index].note >= kBurstDefinitionCount)
                setError(result, ProjectErrorCode::OutOfRange,
                    std::string(path) + "[" + std::to_string(index)
                        + "].burst", "burst slot must be B01..B64");
            cell.object["burst"] = number(
                static_cast<uint32_t>(cells[index].note));
            cell.object["state"] = JsonValue::stringValue("burst");
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
        else if (state->string == "hold")
            candidate.push_back(NoteCell::hold());
        else if (state->string == "note") {
            const auto notes = input.array[index].object.find("notes");
            if (notes == input.array[index].object.end()) {
                const auto* note = requiredField(input.array[index], "note",
                    JsonType::Number, cellPath, result);
                uint32_t value = 0u;
                if (!note || !checkedUint32(*note, value, 127u,
                        cellPath + ".note", result)) return false;
                candidate.push_back(NoteCell::withNote(
                    static_cast<uint8_t>(value)));
            } else {
                if (notes->second.type != JsonType::Array
                    || notes->second.array.empty()
                    || notes->second.array.size() > kMaximumNoteVoices)
                    return setError(result, ProjectErrorCode::OutOfRange,
                        cellPath + ".notes",
                        "note stack must contain 1..8 MIDI notes");
                std::array<uint8_t, kMaximumNoteVoices> voices {};
                uint32_t previous = 0u;
                for (std::size_t voice = 0u;
                     voice < notes->second.array.size(); ++voice) {
                    uint32_t value = 0u;
                    if (!checkedUint32(notes->second.array[voice], value,
                            127u, cellPath + ".notes["
                                + std::to_string(voice) + "]", result))
                        return false;
                    if (voice > 0u && value <= previous)
                        return setError(result,
                            ProjectErrorCode::InconsistentData,
                            cellPath + ".notes",
                            "note stack must be strictly ascending");
                    voices[voice] = static_cast<uint8_t>(value);
                    previous = value;
                }
                candidate.push_back(NoteCell::withNotes(voices,
                    notes->second.array.size()));
            }
        } else if (state->string == "burst") {
            const auto* burst = requiredField(input.array[index], "burst",
                JsonType::Number, cellPath, result);
            uint32_t value = 0u;
            if (!burst || !checkedUint32(*burst, value,
                    static_cast<uint32_t>(kBurstDefinitionCount - 1u),
                    cellPath + ".burst", result)) return false;
            candidate.push_back(NoteCell::withBurst(
                static_cast<uint8_t>(value)));
        } else {
            return setError(result, ProjectErrorCode::OutOfRange,
                cellPath + ".state", "unknown note cell state");
        }
    }
    destination = std::move(candidate);
    return true;
}

JsonValue encodeGateCells(const std::vector<GateCell>& cells,
    std::string_view path, ProjectResult& result)
{
    JsonValue output = JsonValue::arrayValue();
    if (cells.size() > kMaximumPatternRows)
        setError(result, ProjectErrorCode::SizeLimitExceeded,
            std::string(path), "gate column exceeds 65536 cells");
    for (std::size_t row = 0u; row < cells.size(); ++row) {
        JsonValue cell = JsonValue::objectValue();
        JsonValue voices = JsonValue::arrayValue();
        if (cells[row].voiceCount != 0u) {
            for (std::size_t voice = 0u;
                 voice < cells[row].gateVoiceCount(); ++voice) {
                const auto gate = cells[row].gateVoice(voice);
                if (gate.mode == GateVoiceMode::Default)
                    voices.array.push_back(JsonValue::stringValue("default"));
                else if (gate.mode == GateVoiceMode::Tie)
                    voices.array.push_back(JsonValue::stringValue("tie"));
                else {
                    finiteRange(gate.rows, 0.01, 64.0,
                        std::string(path) + "[" + std::to_string(row)
                            + "].values[" + std::to_string(voice) + "]",
                        result);
                    voices.array.push_back(JsonValue::numberValue(gate.rows));
                }
            }
        }
        cell.object["values"] = std::move(voices);
        output.array.push_back(std::move(cell));
    }
    return output;
}

bool decodeGateCells(const JsonValue& input, std::vector<GateCell>& destination,
    std::string_view path, ProjectResult& result)
{
    if (input.type != JsonType::Array || input.array.size() > kMaximumPatternRows)
        return setError(result, ProjectErrorCode::SizeLimitExceeded,
            std::string(path), "invalid gate-cell array");
    std::vector<GateCell> candidate;
    candidate.reserve(input.array.size());
    for (std::size_t row = 0u; row < input.array.size(); ++row) {
        const std::string cellPath = std::string(path) + "["
            + std::to_string(row) + "]";
        const auto* values = requiredField(input.array[row], "values",
            JsonType::Array, cellPath, result);
        if (!values || values->array.size() > kMaximumNoteVoices) return false;
        if (values->array.empty()) {
            candidate.push_back(GateCell::defaultValue());
            continue;
        }
        std::array<GateVoice, kMaximumNoteVoices> voices {};
        for (std::size_t voice = 0u; voice < values->array.size(); ++voice) {
            const auto& value = values->array[voice];
            if (value.type == JsonType::String && value.string == "default")
                voices[voice] = { GateVoiceMode::Default, 1.0f };
            else if (value.type == JsonType::String && value.string == "tie")
                voices[voice] = { GateVoiceMode::Tie, 1.0f };
            else if (value.type == JsonType::Number
                && std::isfinite(value.number) && value.number >= 0.01
                && value.number <= 64.0)
                voices[voice] = { GateVoiceMode::Rows,
                    static_cast<float>(value.number) };
            else return setError(result, ProjectErrorCode::OutOfRange,
                cellPath + ".values[" + std::to_string(voice) + "]",
                "gate voice must be default, tie, or 0.01..64 rows");
        }
        candidate.push_back(GateCell::withVoices(voices,
            values->array.size()));
    }
    destination = std::move(candidate);
    return true;
}

JsonValue encodeBursts(const BurstLibrary& library, std::string_view path,
    ProjectResult& result)
{
    JsonValue output = JsonValue::arrayValue();
    output.array.reserve(library.bursts.size());
    for (std::size_t slot = 0u; slot < library.bursts.size(); ++slot) {
        const auto& burst = library.bursts[slot];
        if (burst.empty() && burst.name.empty()) continue;
        const std::string slotPath = std::string(path) + "["
            + std::to_string(slot) + "]";
        if (burst.eventCount > kMaximumBurstEvents)
            setError(result, ProjectErrorCode::SizeLimitExceeded,
                slotPath + ".events", "burst exceeds eight events");
        JsonValue encoded = JsonValue::objectValue();
        encoded.object["slot"] = number(slot);
        encoded.object["name"] = encodeCheckedString(burst.name,
            kMaximumBurstNameBytes, slotPath + ".name", result);
        JsonValue events = JsonValue::arrayValue();
        const auto count = std::min<std::size_t>(burst.eventCount,
            kMaximumBurstEvents);
        events.array.reserve(count);
        for (std::size_t index = 0u; index < count; ++index) {
            const auto& event = burst.events[index];
            if (event.note > 127u || event.velocity == 0u
                || event.velocity > 127u || event.gatePercent == 0u
                || event.gatePercent > 100u)
                setError(result, ProjectErrorCode::OutOfRange,
                    slotPath + ".events[" + std::to_string(index) + "]",
                    "burst note/velocity/gate is outside its supported range");
            JsonValue item = JsonValue::objectValue();
            item.object["gate"] = number(
                static_cast<uint32_t>(event.gatePercent));
            item.object["note"] = number(static_cast<uint32_t>(event.note));
            item.object["position"] = number(
                static_cast<uint32_t>(event.position));
            item.object["velocity"] = number(
                static_cast<uint32_t>(event.velocity));
            events.array.push_back(std::move(item));
        }
        encoded.object["events"] = std::move(events);
        output.array.push_back(std::move(encoded));
    }
    return output;
}

bool decodeBursts(const JsonValue& input, BurstLibrary& library,
    std::string_view path, ProjectResult& result)
{
    if (input.type != JsonType::Array
        || input.array.size() > kBurstDefinitionCount)
        return setError(result, ProjectErrorCode::InconsistentData,
            std::string(path), "burst library exceeds 64 occupied slots");
    std::array<bool, kBurstDefinitionCount> occupied {};
    for (std::size_t index = 0u; index < input.array.size(); ++index) {
        const std::string slotPath = std::string(path) + "["
            + std::to_string(index) + "]";
        const auto* slotValue = requiredField(input.array[index], "slot",
            JsonType::Number, slotPath, result);
        const auto* name = requiredField(input.array[index], "name",
            JsonType::String, slotPath, result);
        const auto* events = requiredField(input.array[index], "events",
            JsonType::Array, slotPath, result);
        if (!slotValue || !name || !events) return false;
        uint32_t slot = 0u;
        if (!checkedUint32(*slotValue, slot,
                static_cast<uint32_t>(kBurstDefinitionCount - 1u),
                slotPath + ".slot", result)) return false;
        if (occupied[slot])
            return setError(result, ProjectErrorCode::InconsistentData,
                slotPath + ".slot", "burst slot is duplicated");
        occupied[slot] = true;
        if (events->array.size() > kMaximumBurstEvents)
            return setError(result, ProjectErrorCode::SizeLimitExceeded,
                slotPath + ".events", "burst exceeds eight events");
        auto& burst = library.bursts[slot];
        if (!checkedString(*name, burst.name, kMaximumBurstNameBytes,
                slotPath + ".name", result)) return false;
        burst.eventCount = static_cast<uint8_t>(events->array.size());
        for (std::size_t eventIndex = 0u;
             eventIndex < events->array.size(); ++eventIndex) {
            const std::string eventPath = slotPath + ".events["
                + std::to_string(eventIndex) + "]";
            const auto* position = requiredField(events->array[eventIndex],
                "position", JsonType::Number, eventPath, result);
            const auto* note = requiredField(events->array[eventIndex], "note",
                JsonType::Number, eventPath, result);
            const auto* velocity = requiredField(events->array[eventIndex],
                "velocity", JsonType::Number, eventPath, result);
            const auto* gate = requiredField(events->array[eventIndex], "gate",
                JsonType::Number, eventPath, result);
            if (!position || !note || !velocity || !gate) return false;
            uint32_t positionValue = 0u;
            uint32_t noteValue = 0u;
            uint32_t velocityValue = 0u;
            uint32_t gateValue = 0u;
            if (!checkedUint32(*position, positionValue, 65535u,
                    eventPath + ".position", result)
                || !checkedUint32(*note, noteValue, 127u,
                    eventPath + ".note", result)
                || !checkedUint32(*velocity, velocityValue, 127u,
                    eventPath + ".velocity", result)
                || velocityValue == 0u
                || !checkedUint32(*gate, gateValue, 100u,
                    eventPath + ".gate", result)
                || gateValue == 0u) {
                if (result.ok())
                    setError(result, ProjectErrorCode::OutOfRange,
                        eventPath, "burst velocity and gate must be nonzero");
                return false;
            }
            burst.events[eventIndex] = {
                static_cast<uint16_t>(positionValue),
                static_cast<uint8_t>(noteValue),
                static_cast<uint8_t>(velocityValue),
                static_cast<uint8_t>(gateValue),
            };
        }
    }
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
            cell.object["state"] = JsonValue::stringValue("value");
            if (cells[index].valueVoiceCount() == 1u) {
                finiteRange(cells[index].normalized, 0.0, 1.0,
                    std::string(path) + "[" + std::to_string(index)
                        + "].value", result);
                cell.object["value"] = JsonValue::numberValue(
                    cells[index].normalized);
            } else {
                JsonValue values = JsonValue::arrayValue();
                for (std::size_t voice = 0u;
                     voice < cells[index].valueVoiceCount(); ++voice) {
                    const float value = cells[index].valueVoice(voice);
                    finiteRange(value, 0.0, 1.0,
                        std::string(path) + "[" + std::to_string(index)
                            + "].values[" + std::to_string(voice) + "]",
                        result);
                    values.array.push_back(JsonValue::numberValue(value));
                }
                cell.object["values"] = std::move(values);
            }
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
            const auto values = input.array[index].object.find("values");
            if (values == input.array[index].object.end()) {
                const auto* value = requiredField(input.array[index], "value",
                    JsonType::Number, cellPath, result);
                double normalized = 0.0;
                if (!value || !checkedNumber(*value, normalized, 0.0, 1.0,
                        cellPath + ".value", result)) return false;
                candidate.push_back(ValueCell::withValue(
                    static_cast<float>(normalized)));
            } else {
                if (values->second.type != JsonType::Array
                    || values->second.array.empty()
                    || values->second.array.size() > kMaximumNoteVoices)
                    return setError(result, ProjectErrorCode::OutOfRange,
                        cellPath + ".values",
                        "velocity stack must contain 1..8 values");
                std::array<float, kMaximumNoteVoices> voices {};
                for (std::size_t voice = 0u;
                     voice < values->second.array.size(); ++voice) {
                    double normalized = 0.0;
                    if (!checkedNumber(values->second.array[voice],
                            normalized, 0.0, 1.0,
                            cellPath + ".values["
                                + std::to_string(voice) + "]", result))
                        return false;
                    voices[voice] = static_cast<float>(normalized);
                }
                candidate.push_back(ValueCell::withValues(voices,
                    values->second.array.size()));
            }
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
            setError(result, ProjectErrorCode::InconsistentData,
                cellPath + ".state",
                "internal parameter actions are not part of the MIDI composition format");
            break;
        case FxActionCellState::Sequencer:
            cell.object["action"] = encodeEnum(cells[index].sequencerAction,
                kSequencerActions, cellPath + ".action", result);
            cell.object["state"] = JsonValue::stringValue("sequencer");
            break;
        case FxActionCellState::MidiControlChange:
            if (cells[index].midiController > 127u)
                setError(result, ProjectErrorCode::OutOfRange,
                    cellPath + ".controller",
                    "MIDI controller must be 0..127");
            cell.object["controller"] = number(
                static_cast<uint32_t>(cells[index].midiController));
            cell.object["state"] = JsonValue::stringValue(
                "midi-control-change");
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
        else if (state->string == "sequencer") {
            const auto* action = requiredField(input.array[index], "action",
                JsonType::String, cellPath, result);
            SequencerAction value = SequencerAction::Ratchet;
            if (!action || !decodeEnum(*action, kSequencerActions, value,
                    cellPath + ".action", result)) return false;
            candidate.push_back(FxActionCell::sequencer(value));
        } else if (state->string == "midi-control-change") {
            const auto* controller = requiredField(input.array[index],
                "controller", JsonType::Number, cellPath, result);
            uint32_t value = 0u;
            if (!controller || !checkedUint32(*controller, value, 127u,
                    cellPath + ".controller", result)) return false;
            candidate.push_back(FxActionCell::midiControlChange(
                static_cast<uint8_t>(value)));
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
            cell.object["state"] = JsonValue::stringValue("value");
            if (cells[index].valueVoiceCount() == 1u) {
                finiteRange(cells[index].normalized, 0.0, 1.0,
                    std::string(path) + "[" + std::to_string(index)
                        + "].value", result);
                cell.object["value"] = JsonValue::numberValue(
                    cells[index].normalized);
            } else {
                JsonValue values = JsonValue::arrayValue();
                for (std::size_t voice = 0u;
                     voice < cells[index].valueVoiceCount(); ++voice) {
                    const auto value = cells[index].valueVoice(voice);
                    finiteRange(value, 0.0, 1.0,
                        std::string(path) + "[" + std::to_string(index)
                            + "].values[" + std::to_string(voice) + "]",
                        result);
                    values.array.push_back(JsonValue::numberValue(value));
                }
                cell.object["values"] = std::move(values);
            }
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
            const auto values = input.array[index].object.find("values");
            if (values == input.array[index].object.end()) {
                const auto* value = requiredField(input.array[index], "value",
                    JsonType::Number, cellPath, result);
                double normalized = 0.0;
                if (!value || !checkedNumber(*value, normalized, 0.0, 1.0,
                        cellPath + ".value", result)) return false;
                candidate.push_back(FxValueCell::withValue(
                    static_cast<float>(normalized)));
            } else {
                if (values->second.type != JsonType::Array
                    || values->second.array.empty()
                    || values->second.array.size() > kMaximumNoteVoices)
                    return setError(result, ProjectErrorCode::OutOfRange,
                        cellPath + ".values",
                        "FX value stack must contain 1..8 values");
                std::array<float, kMaximumNoteVoices> voices {};
                for (std::size_t voice = 0u;
                     voice < values->second.array.size(); ++voice) {
                    double normalized = 0.0;
                    if (!checkedNumber(values->second.array[voice],
                            normalized, 0.0, 1.0,
                            cellPath + ".values["
                                + std::to_string(voice) + "]", result))
                        return false;
                    voices[voice] = static_cast<float>(normalized);
                }
                candidate.push_back(FxValueCell::withValues(voices,
                    values->second.array.size()));
            }
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
    output.object["valueInterpolation"] = encodeEnum(
        pair.valueInterpolation, kValueInterpolations,
        std::string(path) + ".valueInterpolation", result);
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
    const auto interpolation = input.object.find("valueInterpolation");
    if (interpolation != input.object.end()
        && !decodeEnum(interpolation->second, kValueInterpolations,
            candidate.valueInterpolation,
            std::string(path) + ".valueInterpolation", result)) return false;
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
    if (track.destination != EventDestination::Midi
        || track.initialInstrumentNodeId != kMidiOutInstrumentNode)
        setError(result, ProjectErrorCode::InconsistentData,
            std::string(path),
            "lane must target the Tracker MIDI output");
    for (std::size_t row = 0u; row < track.instruments.size(); ++row) {
        if (track.instruments[row].state == InstrumentCellState::Empty)
            continue;
        setError(result, ProjectErrorCode::InconsistentData,
            std::string(path) + ".instruments[" + std::to_string(row) + "]",
            "instrument cells are not part of the MIDI composition format");
        break;
    }
    JsonValue output = JsonValue::objectValue();
    output.object["chokeGroup"] = number(track.chokeGroup);
    JsonValue fxPairs = JsonValue::arrayValue();
    for (std::size_t pair = 0u; pair < track.fxPairs.size(); ++pair) {
        fxPairs.array.push_back(encodeFxPair(track.fxPairs[pair],
            std::string(path) + ".fxPairs[" + std::to_string(pair) + "]",
            result));
    }
    output.object["fxPairs"] = std::move(fxPairs);
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
    output.object["gateColumn"] = encodeColumn(track.gateColumn,
        track.gates.size(), std::string(path) + ".gateColumn", result);
    output.object["gates"] = encodeGateCells(track.gates,
        std::string(path) + ".gates", result);
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
    const auto* chokeGroup = requiredField(input, "chokeGroup",
        JsonType::Number, path, result);
    const auto* notes = requiredField(input, "notes", JsonType::Array,
        path, result);
    const auto* velocities = requiredField(input, "velocities",
        JsonType::Array, path, result);
    const auto* noteColumn = requiredField(input, "noteColumn",
        JsonType::Object, path, result);
    const auto* velocityColumn = requiredField(input, "velocityColumn",
        JsonType::Object, path, result);
    const auto* fxPairs = requiredField(input, "fxPairs", JsonType::Array,
        path, result);
    if (!name || !velocityScale || !midiChannel || !chokeGroup || !notes
        || !velocities || !noteColumn || !velocityColumn
        || !fxPairs) return false;
    if (fxPairs->array.size() != kFxPairCount)
        return setError(result, ProjectErrorCode::InconsistentData,
            std::string(path) + ".fxPairs", "exactly two FX pairs are required");

    Track candidate;
    double scale = 0.0;
    uint32_t channel = 0u;
    if (!checkedString(*name, candidate.name, kMaximumNameBytes,
            std::string(path) + ".name", result)
        || !checkedNumber(*velocityScale, scale, 0.0, 1.0,
            std::string(path) + ".velocityScale", result)
        || !checkedUint32(*midiChannel, channel, 16u,
            std::string(path) + ".midiChannel", result)
        || channel == 0u
        || !checkedUint32(*chokeGroup, candidate.chokeGroup,
            std::numeric_limits<uint32_t>::max(),
            std::string(path) + ".chokeGroup", result)) {
        if (result.ok())
            setError(result, ProjectErrorCode::OutOfRange, std::string(path),
                "track MIDI channel is invalid");
        return false;
    }
    candidate.velocityScale = static_cast<float>(scale);
    candidate.midiChannel = static_cast<uint8_t>(channel);
    candidate.destination = EventDestination::Midi;
    candidate.initialInstrumentNodeId = kMidiOutInstrumentNode;
    candidate.instruments.clear();
    candidate.instrumentColumn = {};
    candidate.instrumentColumn.length = 0u;
    if (!decodeNoteCells(*notes, candidate.notes,
            std::string(path) + ".notes", result)
        || !decodeValueCells(*velocities, candidate.velocities,
            std::string(path) + ".velocities", result)
        || !decodeColumn(*noteColumn, candidate.noteColumn,
            candidate.notes.size(), std::string(path) + ".noteColumn", result)
        || !decodeColumn(*velocityColumn, candidate.velocityColumn,
            candidate.velocities.size(),
            std::string(path) + ".velocityColumn", result)) return false;
    const auto gates = input.object.find("gates");
    const auto gateColumn = input.object.find("gateColumn");
    if ((gates == input.object.end()) != (gateColumn == input.object.end()))
        return setError(result, ProjectErrorCode::InconsistentData,
            std::string(path) + ".gates",
            "gates and gateColumn must appear together");
    if (gates != input.object.end()) {
        if (!decodeGateCells(gates->second, candidate.gates,
                std::string(path) + ".gates", result)
            || !decodeColumn(gateColumn->second, candidate.gateColumn,
                candidate.gates.size(), std::string(path) + ".gateColumn",
                result)) return false;
    } else {
        candidate.gates.resize(candidate.notes.size(), GateCell::defaultValue());
        candidate.gateColumn = candidate.noteColumn;
        candidate.gateColumn.muted = false;
    }
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

JsonValue encodePhraseLibrary(const PhraseLibrary& library,
    ProjectResult& result)
{
    JsonValue output = JsonValue::arrayValue();
    std::size_t count = 0u;
    for (std::size_t index = 0u; index < library.phrases.size(); ++index)
        if (!library.phrases[index].empty()
            || !library.phrases[index].name.empty()) count = index + 1u;
    output.array.reserve(count);
    for (std::size_t index = 0u; index < count; ++index) {
        const auto& phrase = library.phrases[index];
        const std::string path = "$.phrases[" + std::to_string(index) + "]";
        if (phrase.length < kMinimumPhraseRows
            || phrase.length > kMaximumPhraseRows)
            setError(result, ProjectErrorCode::OutOfRange, path + ".length",
                "phrase length must be 2..64 rows");
        JsonValue item = JsonValue::objectValue();
        item.object["name"] = encodeCheckedString(phrase.name,
            kMaximumPhraseNameBytes, path + ".name", result);
        item.object["length"] = number(phrase.length);
        item.object["previewMidiChannel"] = number(
            static_cast<uint32_t>(std::clamp<int>(
                phrase.previewMidiChannel, 1, 16)));
        item.object["notes"] = encodeNoteCells(phrase.notes,
            path + ".notes", result);
        item.object["velocities"] = encodeValueCells(phrase.velocities,
            path + ".velocities", result);
        item.object["gates"] = encodeGateCells(phrase.gates,
            path + ".gates", result);
        JsonValue pairs = JsonValue::arrayValue();
        for (std::size_t pair = 0u; pair < phrase.fxPairs.size(); ++pair)
            pairs.array.push_back(encodeFxPair(phrase.fxPairs[pair],
                path + ".fxPairs[" + std::to_string(pair) + "]", result));
        item.object["fxPairs"] = std::move(pairs);
        output.array.push_back(std::move(item));
    }
    return output;
}

bool decodePhraseLibrary(const JsonValue& input, PhraseLibrary& destination,
    ProjectResult& result)
{
    if (input.type != JsonType::Array)
        return setError(result, ProjectErrorCode::TypeMismatch, "$.phrases",
            "phrase library must be an array");
    if (input.array.size() > kPhraseLibrarySlots)
        return setError(result, ProjectErrorCode::SizeLimitExceeded,
            "$.phrases", "phrase library exceeds 64 slots");
    PhraseLibrary candidate;
    for (std::size_t index = 0u; index < input.array.size(); ++index) {
        const auto& item = input.array[index];
        const std::string path = "$.phrases[" + std::to_string(index) + "]";
        const auto* name = requiredField(item, "name", JsonType::String,
            path, result);
        const auto* length = requiredField(item, "length", JsonType::Number,
            path, result);
        const auto* notes = requiredField(item, "notes", JsonType::Array,
            path, result);
        const auto* velocities = requiredField(item, "velocities",
            JsonType::Array, path, result);
        const auto* fxPairs = requiredField(item, "fxPairs", JsonType::Array,
            path, result);
        const auto* gates = requiredField(item, "gates", JsonType::Array,
            path, result);
        if (!name || !length || !notes || !velocities || !gates || !fxPairs)
            return false;
        if (fxPairs->array.size() != kFxPairCount)
            return setError(result, ProjectErrorCode::InconsistentData,
                path + ".fxPairs", "exactly two FX pairs are required");
        auto& phrase = candidate.phrases[index];
        const auto previewChannel = item.object.find("previewMidiChannel");
        if (!checkedString(*name, phrase.name, kMaximumPhraseNameBytes,
                path + ".name", result)
            || !checkedSize(*length, phrase.length, kMaximumPhraseRows,
                path + ".length", result)
            || phrase.length < kMinimumPhraseRows
            || !decodeNoteCells(*notes, phrase.notes, path + ".notes", result)
            || !decodeValueCells(*velocities, phrase.velocities,
                path + ".velocities", result)
            || !decodeGateCells(*gates, phrase.gates,
                path + ".gates", result)) {
            if (result.ok())
                setError(result, ProjectErrorCode::OutOfRange,
                    path + ".length", "phrase length must be 2..64 rows");
            return false;
        }
        if (previewChannel != item.object.end()) {
            uint32_t channel = 1u;
            if (!checkedUint32(previewChannel->second, channel, 16u,
                    path + ".previewMidiChannel", result)
                || channel < 1u) {
                if (result.ok()) setError(result,
                    ProjectErrorCode::OutOfRange,
                    path + ".previewMidiChannel",
                    "preview MIDI channel must be 1..16");
                return false;
            }
            phrase.previewMidiChannel = static_cast<uint8_t>(channel);
        }
        for (std::size_t pair = 0u; pair < kFxPairCount; ++pair) {
            if (!decodeFxPair(fxPairs->array[pair], phrase.fxPairs[pair],
                    path + ".fxPairs[" + std::to_string(pair) + "]", result))
                return false;
        }
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

std::string packBurstId(std::size_t slot)
{
    std::ostringstream stream;
    stream << "burst-b" << std::setfill('0') << std::setw(2) << slot + 1u;
    return stream.str();
}

std::string packPhraseId(std::size_t slot)
{
    std::ostringstream stream;
    stream << "phrase-p" << std::setfill('0') << std::setw(2) << slot + 1u;
    return stream.str();
}

JsonValue encodeAssetPackDocument(const TrackerAssetPack& pack,
    ProjectResult& result)
{
    JsonValue root = JsonValue::objectValue();
    root.object["format"] = JsonValue::stringValue(kTrackerAssetPackFormat);
    root.object["version"] = number(kTrackerAssetPackVersion);
    root.object["name"] = encodeCheckedString(pack.name, kMaximumNameBytes,
        "$.name", result);
    JsonValue bursts = encodeBursts(pack.burstLibrary, "$.bursts", result);
    for (auto& item : bursts.array) {
        const auto slot = item.object.find("slot");
        if (slot == item.object.end()) continue;
        item.object["id"] = JsonValue::stringValue(packBurstId(
            static_cast<std::size_t>(slot->second.number)));
    }
    root.object["bursts"] = std::move(bursts);
    JsonValue phrases = encodePhraseLibrary(pack.phraseLibrary, result);
    for (std::size_t index = 0u; index < phrases.array.size(); ++index) {
        auto& item = phrases.array[index];
        item.object["id"] = JsonValue::stringValue(packPhraseId(index));
        JsonValue dependencies = JsonValue::arrayValue();
        std::array<bool, kBurstDefinitionCount> seen {};
        for (const auto& cell : pack.phraseLibrary.phrases[index].notes) {
            if (cell.state != NoteCellState::Burst
                || cell.note >= seen.size() || seen[cell.note]) continue;
            seen[cell.note] = true;
            if (pack.burstLibrary.bursts[cell.note].empty()) {
                setError(result, ProjectErrorCode::InconsistentData,
                    "$.phrases[" + std::to_string(index) + "]",
                    "Phrase references a Burst missing from the pack");
                continue;
            }
            dependencies.array.push_back(JsonValue::stringValue(
                packBurstId(cell.note)));
        }
        item.object["burstDependencies"] = std::move(dependencies);
    }
    root.object["phrases"] = std::move(phrases);
    return root;
}

bool decodeAssetPackDocument(const JsonValue& root,
    TrackerAssetPack& destination, ProjectResult& result)
{
    if (root.type != JsonType::Object)
        return setError(result, ProjectErrorCode::TypeMismatch, "$",
            "asset pack root must be an object");
    const auto* format = requiredField(root, "format", JsonType::String,
        "$", result);
    const auto* version = requiredField(root, "version", JsonType::Number,
        "$", result);
    const auto* name = requiredField(root, "name", JsonType::String,
        "$", result);
    const auto* bursts = requiredField(root, "bursts", JsonType::Array,
        "$", result);
    const auto* phrases = requiredField(root, "phrases", JsonType::Array,
        "$", result);
    if (!format || !version || !name || !bursts || !phrases) return false;
    if (format->string != kTrackerAssetPackFormat)
        return setError(result, ProjectErrorCode::InvalidArgument,
            "$.format", "file is not an s3g Tracker asset pack");
    uint32_t schema = 0u;
    if (!checkedUint32(*version, schema,
            std::numeric_limits<uint32_t>::max(), "$.version", result))
        return false;
    if (schema != kTrackerAssetPackVersion)
        return setError(result, ProjectErrorCode::UnsupportedSchemaVersion,
            "$.version", "asset pack version is not supported");

    TrackerAssetPack candidate;
    if (!checkedString(*name, candidate.name, kMaximumNameBytes,
            "$.name", result)
        || !decodeBursts(*bursts, candidate.burstLibrary,
            "$.bursts", result)
        || !decodePhraseLibrary(*phrases, candidate.phraseLibrary, result))
        return false;

    std::map<std::string, std::size_t> burstIds;
    for (std::size_t index = 0u; index < bursts->array.size(); ++index) {
        const auto& item = bursts->array[index];
        const auto* id = requiredField(item, "id", JsonType::String,
            "$.bursts[" + std::to_string(index) + "]", result);
        const auto* slot = requiredField(item, "slot", JsonType::Number,
            "$.bursts[" + std::to_string(index) + "]", result);
        if (!id || !slot || id->string.empty()) return false;
        const auto inserted = burstIds.emplace(id->string,
            static_cast<std::size_t>(slot->number));
        if (!inserted.second)
            return setError(result, ProjectErrorCode::InconsistentData,
                "$.bursts[" + std::to_string(index) + "].id",
                "Burst asset ID is duplicated");
    }
    std::map<std::string, bool> phraseIds;
    for (std::size_t index = 0u; index < phrases->array.size(); ++index) {
        const auto& item = phrases->array[index];
        const std::string path = "$.phrases[" + std::to_string(index) + "]";
        const auto* id = requiredField(item, "id", JsonType::String,
            path, result);
        const auto* dependencies = requiredField(item, "burstDependencies",
            JsonType::Array, path, result);
        if (!id || !dependencies || id->string.empty()) return false;
        if (!phraseIds.emplace(id->string, true).second)
            return setError(result, ProjectErrorCode::InconsistentData,
                path + ".id", "Phrase asset ID is duplicated");
        std::array<bool, kBurstDefinitionCount> declared {};
        for (std::size_t dependency = 0u;
             dependency < dependencies->array.size(); ++dependency) {
            const auto& value = dependencies->array[dependency];
            if (value.type != JsonType::String)
                return setError(result, ProjectErrorCode::TypeMismatch,
                    path + ".burstDependencies["
                        + std::to_string(dependency) + "]",
                    "Burst dependency must be an asset ID");
            const auto found = burstIds.find(value.string);
            if (found == burstIds.end() || found->second >= declared.size())
                return setError(result, ProjectErrorCode::InconsistentData,
                    path + ".burstDependencies["
                        + std::to_string(dependency) + "]",
                    "Burst dependency ID is missing from the pack");
            declared[found->second] = true;
        }
        for (const auto& cell : candidate.phraseLibrary.phrases[index].notes) {
            if (cell.state != NoteCellState::Burst) continue;
            if (cell.note >= declared.size() || !declared[cell.note]
                || candidate.burstLibrary.bursts[cell.note].empty())
                return setError(result, ProjectErrorCode::InconsistentData,
                    path + ".burstDependencies",
                    "Phrase Burst reference is not declared by asset ID");
        }
    }
    destination = std::move(candidate);
    return true;
}

} // namespace

ProjectResult encodeTrackerAssetPack(const TrackerAssetPack& pack,
    std::string& destination)
{
    try {
        ProjectResult result;
        JsonValue root = encodeAssetPackDocument(pack, result);
        if (!result) return result;
        std::string candidate;
        candidate.reserve(8192u);
        appendJson(root, candidate, 0u);
        candidate.push_back('\n');
        if (candidate.size() > kMaximumProjectDocumentBytes)
            return failure(ProjectErrorCode::SizeLimitExceeded, "$",
                "encoded asset pack exceeds the 64 MiB file limit");
        destination = std::move(candidate);
        return {};
    } catch (const std::bad_alloc&) {
        return failure(ProjectErrorCode::SizeLimitExceeded, "$",
            "not enough memory to encode the asset pack");
    } catch (const std::exception& error) {
        return failure(ProjectErrorCode::InvalidArgument, "$", error.what());
    }
}

ProjectResult decodeTrackerAssetPack(std::string_view source,
    TrackerAssetPack& destination)
{
    if (source.empty())
        return failure(ProjectErrorCode::InvalidJson, "byte 0",
            "asset pack is empty");
    if (source.size() > kMaximumProjectDocumentBytes)
        return failure(ProjectErrorCode::SizeLimitExceeded, "$",
            "asset pack exceeds the 64 MiB file limit");
    try {
        JsonValue root;
        JsonParser parser(source);
        ProjectResult result = parser.parse(root);
        if (!result) return result;
        TrackerAssetPack candidate;
        if (!decodeAssetPackDocument(root, candidate, result)) return result;
        destination = std::move(candidate);
        return {};
    } catch (const std::bad_alloc&) {
        return failure(ProjectErrorCode::SizeLimitExceeded, "$",
            "not enough memory to decode the asset pack");
    } catch (const std::exception& error) {
        return failure(ProjectErrorCode::InvalidJson, "$", error.what());
    }
}

} // namespace s3g::tracker

namespace s3g::tracker {
namespace {

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
        encoded.object["energy"] = JsonValue::numberValue(row.energy);
        encoded.object["mutedTracks"] = number(row.mutedTracks);
        encoded.object["patternId"] = encodeCheckedString(row.patternId,
            kMaximumNameBytes, path + ".patternId", result);
        encoded.object["repeats"] = number(row.repeats);
        if (row.patternLoop) {
            JsonValue loopRange = JsonValue::objectValue();
            loopRange.object["endRow"] = number(row.patternLoop->endRow);
            loopRange.object["startRow"] = number(
                row.patternLoop->startRow);
            encoded.object["patternLoop"] = std::move(loopRange);
        }
        if (row.swing.has_value())
            encoded.object["swing"] = JsonValue::numberValue(*row.swing);
        encoded.object["warpSlot"] = number(static_cast<std::size_t>(
            row.timingWarpLibraryIndex
                ? *row.timingWarpLibraryIndex + 1u : 0u));
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
        const auto energy = inputRow.object.find("energy");
        if (energy != inputRow.object.end()) {
            double value = 0.0;
            if (!checkedNumber(energy->second, value, 0.0, 1.0,
                    path + ".energy", result)) return false;
            row.energy = static_cast<float>(value);
        }
        const auto swing = inputRow.object.find("swing");
        if (swing != inputRow.object.end()) {
            double value = 0.0;
            if (!checkedNumber(swing->second, value, 0.5, 0.75,
                    path + ".swing", result)) return false;
            row.swing = value;
        }
        const auto warpSlot = inputRow.object.find("warpSlot");
        if (warpSlot != inputRow.object.end()) {
            uint32_t oneBased = 0u;
            if (!checkedUint32(warpSlot->second, oneBased,
                    static_cast<uint32_t>(
                        kMaximumTimingWarpLibraryEntries),
                    path + ".warpSlot", result)) return false;
            if (oneBased > 0u)
                row.timingWarpLibraryIndex
                    = static_cast<std::size_t>(oneBased - 1u);
        }
        const auto patternLoop = inputRow.object.find("patternLoop");
        if (patternLoop != inputRow.object.end()) {
            const auto* start = requiredField(patternLoop->second,
                "startRow", JsonType::Number, path + ".patternLoop", result);
            const auto* end = requiredField(patternLoop->second,
                "endRow", JsonType::Number, path + ".patternLoop", result);
            if (!start || !end) return false;
            SongPatternLoop range;
            if (!checkedUint32(*start, range.startRow,
                    kMaximumSongPatternRows - 1u,
                    path + ".patternLoop.startRow", result)
                || !checkedUint32(*end, range.endRow,
                    kMaximumSongPatternRows,
                    path + ".patternLoop.endRow", result)
                || range.startRow >= range.endRow) {
                if (result.ok())
                    setError(result, ProjectErrorCode::OutOfRange,
                        path + ".patternLoop",
                        "song pattern loop must be an increasing row range within 1..256");
                return false;
            }
            row.patternLoop = range;
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

bool validateBurstReferences(const ProjectDocument& document,
    ProjectResult& result)
{
    const auto validateNotes = [&](const std::vector<NoteCell>& notes,
                                   const std::string& path) {
        for (std::size_t row = 0u; row < notes.size(); ++row) {
            const auto& cell = notes[row];
            if (cell.state != NoteCellState::Burst) continue;
            if (cell.note < document.burstLibrary.bursts.size()
                && !document.burstLibrary.bursts[cell.note].empty())
                continue;
            return setError(result, ProjectErrorCode::InconsistentData,
                path + "[" + std::to_string(row) + "].burst",
                "note cell references an empty project Burst slot");
        }
        return true;
    };
    for (std::size_t pattern = 0u;
         pattern < document.patternBank.entries.size(); ++pattern) {
        const auto& tracks = document.patternBank.entries[pattern].pattern.tracks;
        for (std::size_t track = 0u; track < tracks.size(); ++track) {
            if (!validateNotes(tracks[track].notes,
                    "$.patterns.patterns[" + std::to_string(pattern)
                        + "].pattern.tracks[" + std::to_string(track)
                        + "].notes")) return false;
        }
    }
    for (std::size_t phrase = 0u;
         phrase < document.phraseLibrary.phrases.size(); ++phrase) {
        if (!validateNotes(document.phraseLibrary.phrases[phrase].notes,
                "$.phrases[" + std::to_string(phrase) + "].notes"))
            return false;
    }
    return true;
}

// Implemented beside the timing-warp codec below; declared here because the
// document root is assembled before that specialized section.
JsonValue encodeTimingWarpLibrary(const TimingWarpLibrary& library,
    ProjectResult& result);
bool decodeTimingWarpLibrary(const JsonValue& input,
    TimingWarpLibrary& destination, ProjectResult& result);

JsonValue encodeDocument(const ProjectDocument& document,
    ProjectResult& result)
{
    JsonValue root = JsonValue::objectValue();
    root.object["format"] = JsonValue::stringValue(kProjectFormatIdentifier);
    root.object["patterns"] = encodePatternBank(
        document.patternBank, result);
    root.object["bursts"] = encodeBursts(
        document.burstLibrary, "$.bursts", result);
    root.object["phrases"] = encodePhraseLibrary(
        document.phraseLibrary, result);
    root.object["version"] = number(kProjectFormatVersion);
    root.object["workspace"] = encodeSession(document.session, result);
    root.object["arrangement"] = encodeSong(document.song, result);
    root.object["playback"] = encodeTransport(document.transport, result);
    root.object["warps"] = encodeTimingWarpLibrary(
        document.warpLibrary, result);
    validateSongReferences(document, result);
    validateBurstReferences(document, result);
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
    const auto* schema = requiredField(root, "version",
        JsonType::Number, "$", result);
    const auto* patternBank = requiredField(root, "patterns",
        JsonType::Object, "$", result);
    const auto* burstLibrary = requiredField(root, "bursts",
        JsonType::Array, "$", result);
    const auto* transport = requiredField(root, "playback", JsonType::Object,
        "$", result);
    const auto* session = requiredField(root, "workspace", JsonType::Object,
        "$", result);
    const auto* song = requiredField(root, "arrangement", JsonType::Object,
        "$", result);
    const auto* warpLibrary = requiredField(root, "warps",
        JsonType::Array, "$", result);
    if (!format || !schema || !patternBank || !burstLibrary
        || !transport || !session
        || !song || !warpLibrary) return false;
    if (format->string != kProjectFormatIdentifier)
        return setError(result, ProjectErrorCode::InvalidArgument, "$.format",
            "file is not an s3g Tracker project");
    uint32_t version = 0u;
    if (!checkedUint32(*schema, version,
            std::numeric_limits<uint32_t>::max(), "$.version", result))
        return false;
    if (version != kProjectFormatVersion)
        return setError(result, ProjectErrorCode::UnsupportedSchemaVersion,
            "$.version", "MIDI composition version is not supported");

    ProjectDocument candidate;
    const auto phrases = root.object.find("phrases");
    if (!decodePatternBank(*patternBank, candidate.patternBank, result)
        || !decodeBursts(*burstLibrary, candidate.burstLibrary,
            "$.bursts", result)
        || !decodeTransport(*transport, candidate.transport, result)
        || !decodeSession(*session, candidate.session, result)
        || !decodeSong(*song, candidate.song, result)
        || !decodeTimingWarpLibrary(*warpLibrary, candidate.warpLibrary,
            result)
        || (phrases != root.object.end()
            && !decodePhraseLibrary(phrases->second,
                candidate.phraseLibrary, result))
        || !validateSongReferences(candidate, result)
        || !validateBurstReferences(candidate, result)) return false;
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

JsonValue encodeTimingWarpLibrary(const TimingWarpLibrary& library,
    ProjectResult& result)
{
    JsonValue output = JsonValue::arrayValue();
    output.array.reserve(library.size());
    for (std::size_t index = 0u;
         index < kMaximumTimingWarpLibraryEntries; ++index) {
        const auto* entry = library.entry(index);
        if (!entry) continue;
        const std::string path = "$.warpLibrary["
            + std::to_string(output.array.size()) + "]";
        if (entry->name.size() > kMaximumTimingWarpLibraryNameBytes)
            setError(result, ProjectErrorCode::SizeLimitExceeded,
                path + ".name", "warp name exceeds 64 UTF-8 bytes");
        if (entry->cycleTicks == 0u
            || entry->cycleTicks > kMaximumLiveWarpCycleTicks)
            setError(result, ProjectErrorCode::OutOfRange,
                path + ".cycleTicks", "warp cycle must be 1..16 ticks");
        JsonValue encoded = JsonValue::objectValue();
        encoded.object["cycleTicks"] = number(entry->cycleTicks);
        encoded.object["index"] = number(index + 1u);
        encoded.object["name"] = JsonValue::stringValue(entry->name);
        JsonValue stack = JsonValue::arrayValue();
        stack.array.reserve(entry->stack.size());
        for (std::size_t transformIndex = 0u;
             transformIndex < entry->stack.size(); ++transformIndex) {
            const auto* transform = entry->stack.transform(transformIndex);
            if (!transform) {
                setError(result, ProjectErrorCode::InconsistentData,
                    path + ".stack", "warp stack contains a missing entry");
                break;
            }
            stack.array.push_back(encodeTimingWarp(*transform,
                path + ".stack[" + std::to_string(transformIndex) + "]",
                result));
        }
        encoded.object["stack"] = std::move(stack);
        output.array.push_back(std::move(encoded));
    }
    return output;
}

bool decodeTimingWarpLibrary(const JsonValue& input,
    TimingWarpLibrary& destination, ProjectResult& result)
{
    if (input.type != JsonType::Array)
        return setError(result, ProjectErrorCode::TypeMismatch,
            "$.warpLibrary", "expected a timing-warp library array");
    if (input.array.size() > kMaximumTimingWarpLibraryEntries)
        return setError(result, ProjectErrorCode::SizeLimitExceeded,
            "$.warpLibrary", "warp library exceeds 64 entries");
    TimingWarpLibrary candidate;
    std::array<bool, kMaximumTimingWarpLibraryEntries> occupied {};
    for (std::size_t item = 0u; item < input.array.size(); ++item) {
        const std::string path = "$.warpLibrary[" + std::to_string(item)
            + "]";
        const auto* indexValue = requiredField(input.array[item], "index",
            JsonType::Number, path, result);
        const auto* name = requiredField(input.array[item], "name",
            JsonType::String, path, result);
        const auto* cycle = requiredField(input.array[item], "cycleTicks",
            JsonType::Number, path, result);
        const auto* stack = requiredField(input.array[item], "stack",
            JsonType::Array, path, result);
        if (!indexValue || !name || !cycle || !stack) return false;
        uint32_t oneBased = 0u;
        uint32_t cycleTicks = 0u;
        if (!checkedUint32(*indexValue, oneBased,
                kMaximumTimingWarpLibraryEntries, path + ".index", result)
            || oneBased == 0u
            || !checkedUint32(*cycle, cycleTicks,
                kMaximumLiveWarpCycleTicks, path + ".cycleTicks", result)
            || cycleTicks == 0u) {
            if (result.ok())
                setError(result, ProjectErrorCode::OutOfRange, path,
                    "warp index and cycle are outside supported ranges");
            return false;
        }
        const std::size_t index = oneBased - 1u;
        if (occupied[index])
            return setError(result, ProjectErrorCode::InconsistentData,
                path + ".index", "warp library index is duplicated");
        if (name->string.size() > kMaximumTimingWarpLibraryNameBytes)
            return setError(result, ProjectErrorCode::SizeLimitExceeded,
                path + ".name", "warp name exceeds 64 UTF-8 bytes");
        if (stack->array.size() > TimingWarpStack::kMaximumTransforms)
            return setError(result, ProjectErrorCode::SizeLimitExceeded,
                path + ".stack", "warp stack exceeds 32 transforms");
        TimingWarpStack compiled;
        for (std::size_t transformIndex = 0u;
             transformIndex < stack->array.size(); ++transformIndex) {
            TimingWarpTransform transform;
            const std::string transformPath = path + ".stack["
                + std::to_string(transformIndex) + "]";
            if (!decodeTimingWarp(stack->array[transformIndex], transform,
                    transformPath, result)) return false;
            const auto appended = compiled.append(transform);
            if (!appended.added()
                || appended.corrections != TimingWarpCorrection::None)
                return setError(result, ProjectErrorCode::InconsistentData,
                    transformPath, "validated warp could not be appended");
        }
        if (!candidate.store(index, name->string, cycleTicks, compiled))
            return setError(result, ProjectErrorCode::InconsistentData,
                path, "validated warp could not be stored");
        occupied[index] = true;
    }
    destination = std::move(candidate);
    return true;
}

JsonValue encodeTransport(const TransportSettings& transport,
    ProjectResult& result)
{
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
    output.object["swing"] = JsonValue::numberValue(transport.swing);
    output.object["ticksPerBeat"] = number(transport.ticksPerBeat);
    output.object["timingLookaheadMilliseconds"] = JsonValue::numberValue(
        transport.timingLookaheadMilliseconds);
    output.object["warpEnabled"] = JsonValue::booleanValue(
        transport.timingWarpEnabled);
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
    const auto* warpEnabled = requiredField(input, "warpEnabled",
        JsonType::Boolean, "$.transport", result);
    const auto* lookahead = requiredField(input,
        "timingLookaheadMilliseconds", JsonType::Number, "$.transport",
        result);
    const auto* microRange = requiredField(input,
        "microTimingRangeMilliseconds", JsonType::Number, "$.transport",
        result);
    const auto* loop = requiredField(input, "loop", JsonType::Object,
        "$.transport", result);
    if (!bpm || !ticks || !swing || !warpCycle || !warps || !warpEnabled
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
    if (!checkedBoolean(*warpEnabled, candidate.timingWarpEnabled,
            "$.transport.warpEnabled", result)
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
    finiteRange(session.tempoScale, 0.25, 4.0,
        "$.session.tempoScale", result);
    if (session.trackerRowJump < 1u || session.trackerRowJump > 16u)
        setError(result, ProjectErrorCode::OutOfRange,
            "$.session.trackerRowJump", "row jump must be 1..16");
    JsonValue output = JsonValue::objectValue();
    output.object["commandRngState"] = JsonValue::stringValue(
        std::to_string(session.commandRngState));
    output.object["gateMilliseconds"] = JsonValue::numberValue(
        session.gateMilliseconds);
    output.object["tempoScale"] = JsonValue::numberValue(session.tempoScale);
    output.object["songPlaybackEnabled"] = JsonValue::booleanValue(
        session.songPlaybackEnabled);
    output.object["showMidiNoteValues"] = JsonValue::booleanValue(
        session.showMidiNoteValues);
    output.object["trackerRowJump"] = number(session.trackerRowJump);
    output.object["playbackSeed"] = number(session.playbackSeed);
    return output;
}

bool decodeSession(const JsonValue& input, ProjectSessionState& destination,
    ProjectResult& result)
{
    const auto* gate = requiredField(input, "gateMilliseconds",
        JsonType::Number, "$.session", result);
    const auto* tempoScale = requiredField(input, "tempoScale",
        JsonType::Number, "$.session", result);
    const auto* commandSeed = requiredField(input, "commandRngState",
        JsonType::String, "$.session", result);
    const auto* playbackSeed = requiredField(input, "playbackSeed",
        JsonType::Number, "$.session", result);
    const auto* songEnabled = requiredField(input, "songPlaybackEnabled",
        JsonType::Boolean, "$.session", result);
    const auto* showMidi = requiredField(input, "showMidiNoteValues",
        JsonType::Boolean, "$.session", result);
    if (!gate || !tempoScale || !commandSeed || !playbackSeed
        || !songEnabled || !showMidi)
        return false;
    ProjectSessionState candidate;
    if (!checkedNumber(*gate, candidate.gateMilliseconds, 1.0, 10000.0,
            "$.session.gateMilliseconds", result)
        || !checkedNumber(*tempoScale, candidate.tempoScale, 0.25, 4.0,
            "$.session.tempoScale", result)
        || !checkedBoolean(*songEnabled, candidate.songPlaybackEnabled,
            "$.session.songPlaybackEnabled", result)
        || !checkedUint64String(*commandSeed, candidate.commandRngState,
            "$.session.commandRngState", result)
        || !checkedUint32(*playbackSeed, candidate.playbackSeed,
            std::numeric_limits<uint32_t>::max(), "$.session.playbackSeed",
            result)) return false;
    const auto rowJump = input.object.find("trackerRowJump");
    if (rowJump != input.object.end()
        && !checkedUint32(rowJump->second, candidate.trackerRowJump, 16u,
            "$.session.trackerRowJump", result)) return false;
    if (candidate.trackerRowJump < 1u)
        return setError(result, ProjectErrorCode::OutOfRange,
            "$.session.trackerRowJump", "row jump must be 1..16");
    if (!checkedBoolean(*showMidi,
            candidate.showMidiNoteValues,
            "$.session.showMidiNoteValues", result)) return false;
    destination = std::move(candidate);
    return true;
}

} // namespace
} // namespace s3g::tracker
