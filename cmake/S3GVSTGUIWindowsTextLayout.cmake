# Build a Windows-only adapter from the pinned, unmodified VSTGUI source.
# The public D2DFont layout method and class ABI remain unchanged.
if(NOT WIN32)
  return()
endif()
set(s3g_text_original "${vstgui_SOURCE_DIR}/vstgui/lib/platform/win32/direct2d/d2dfont.cpp")
file(SHA256 "${s3g_text_original}" s3g_text_hash)
if(NOT s3g_text_hash STREQUAL "193edb97d7aa3afc1e4a8895f57273d15d5e256aaeb95e59656c6a751b205b13")
  message(FATAL_ERROR "Pinned VSTGUI Windows font source changed; review the text-layout adapter")
endif()
file(READ "${s3g_text_original}" s3g_text_source)
string(REPLACE "#include <dwrite.h>" "#include <dwrite.h>\n#include \"s3g_windows_text_layout_cache.h\""
  s3g_text_source "${s3g_text_source}")
set(s3g_text_helpers [=[
namespace S3GWindowsText {
s3g::windows_gui::TextLayoutCache& cache ()
{
    static s3g::windows_gui::TextLayoutCache instance;
    return instance;
}
IDWriteTextLayout* layout (IPlatformString* string, IDWriteTextFormat* format, int32_t style)
{
    const auto* winString = dynamic_cast<const WinString*> (string);
    return winString ? cache ().get (getDWriteFactory (), format, winString->getWideString (),
        (style & kUnderlineFace) != 0, (style & kStrikethroughFace) != 0) : nullptr;
}
} // S3GWindowsText
]=])
string(REPLACE "namespace VSTGUI {" "namespace VSTGUI {\n${s3g_text_helpers}"
  s3g_text_source "${s3g_text_source}")
string(REPLACE "void D2DFont::terminate () {" "void D2DFont::terminate () { S3GWindowsText::cache ().clear ();"
  s3g_text_source "${s3g_text_source}")
string(REPLACE "D2DFont::~D2DFont ()\n{" "D2DFont::~D2DFont ()\n{\n\tS3GWindowsText::cache ().erase (textFormat);"
  s3g_text_source "${s3g_text_source}")
string(REPLACE "IDWriteTextLayout* textLayout = createTextLayout (string);"
  "IDWriteTextLayout* textLayout = S3GWindowsText::layout (string, textFormat, style);"
  s3g_text_source "${s3g_text_source}")
# Apply font decoration once when creating the cached immutable layout.
set(s3g_text_decoration [=[
		if (style & kUnderlineFace)
		{
			DWRITE_TEXT_RANGE range = {0, UINT_MAX};
			textLayout->SetUnderline (true, range);
		}
		if (style & kStrikethroughFace)
		{
			DWRITE_TEXT_RANGE range = {0, UINT_MAX};
			textLayout->SetStrikethrough (true, range);
		}
]=])
string(REPLACE "${s3g_text_decoration}" "" s3g_text_source "${s3g_text_source}")
set(s3g_text_generated "${CMAKE_BINARY_DIR}/s3g-vstgui-windows/d2dfont.cpp")
file(GENERATE OUTPUT "${s3g_text_generated}" CONTENT "${s3g_text_source}")
get_target_property(s3g_text_sources vstgui SOURCES)
set(s3g_text_matches ${s3g_text_sources})
list(FILTER s3g_text_matches INCLUDE REGEX "(^|/)d2dfont[.]cpp$")
list(LENGTH s3g_text_matches s3g_text_match_count)
if(NOT s3g_text_match_count EQUAL 1)
  message(FATAL_ERROR "Expected exactly one Windows font source")
endif()
list(REMOVE_ITEM s3g_text_sources ${s3g_text_matches})
list(APPEND s3g_text_sources "${s3g_text_generated}")
set_property(TARGET vstgui PROPERTY SOURCES ${s3g_text_sources})
target_include_directories(vstgui PRIVATE
  "${vstgui_SOURCE_DIR}/vstgui/lib/platform/win32/direct2d"
  "${CMAKE_CURRENT_LIST_DIR}/windows")
if(BUILD_TESTING)
  add_executable(s3g_windows_text_layout_cache "${PROJECT_SOURCE_DIR}/tests/windows_text_layout_cache.cpp")
  target_include_directories(s3g_windows_text_layout_cache PRIVATE "${CMAKE_CURRENT_LIST_DIR}/windows")
  target_link_libraries(s3g_windows_text_layout_cache PRIVATE dwrite d2d1 windowscodecs ole32)
  add_test(NAME s3g_windows_text_layout_cache COMMAND s3g_windows_text_layout_cache)
  set_tests_properties(s3g_windows_text_layout_cache PROPERTIES LABELS "non_nim;windows;gui;performance" TIMEOUT 60)
endif()
