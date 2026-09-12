static std::string voxWorldCachePath(const std::filesystem::path &wav) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(wav, ec);
  if (ec)
    return {};
  const auto modified = std::filesystem::last_write_time(wav, ec);
  if (ec)
    return {};
  const auto identity =
      foundation::pathToUtf8(wav) + "|" + std::to_string(size) + "|" +
      std::to_string(
          static_cast<long long>(modified.time_since_epoch().count())) +
      "|world-cache-v4";
  std::filesystem::path folder;
#if defined(_WIN32)
  // _wgetenv avoids a lossy ANSI conversion for non-ASCII account names.
  const auto *root = _wgetenv(L"LOCALAPPDATA");
  if (!root)
    return {};
  folder = std::filesystem::path(root) / "org.s3g.s3g-dsp" / "AmbiVoxWorld";
#else
  const auto *root = std::getenv("HOME");
  if (!root)
    return {};
  folder = std::filesystem::path(root) / "Library" / "Caches" /
           "org.s3g.s3g-dsp" / "AmbiVoxWorld";
#endif
  std::filesystem::create_directories(folder, ec);
  if (ec)
    return {};
  char name[40]{};
  std::snprintf(name, sizeof(name), "%016llx.s3gworld",
                static_cast<unsigned long long>(voxCacheHash(identity)));
  return foundation::pathToUtf8(folder / name);
}
