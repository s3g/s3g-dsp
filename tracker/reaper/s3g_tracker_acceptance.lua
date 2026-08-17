-- s3g Tracker real-REAPER acceptance harness, contract version 1.
-- Creates and owns a new project tab. The caller's existing tabs are not
-- modified. Results are shown in REAPER and written to the OS temporary dir.

local VERSION = 1
local MAGIC = 1395872820
local LOG_PATH = "/tmp/s3g-tracker-reaper-acceptance.log"
local PROJECT_PATH = "/tmp/s3g-tracker-reaper-acceptance-v1.rpp"
local state = "setup"
local deadline = 0
local baseline = 0
local results = {}
local test_track = nil

local function result(name, passed, detail)
  results[#results + 1] = {
    name = name,
    passed = passed and true or false,
    detail = detail or ""
  }
end

local function capture_value(index)
  return math.floor(reaper.gmem_read(index) + 0.5)
end

local function reset_capture()
  reaper.gmem_attach("s3g_tracker_acceptance")
  for index = 1, 8 do reaper.gmem_write(index, 0) end
end

local function add_first_fx(track, candidates)
  for _, name in ipairs(candidates) do
    local index = reaper.TrackFX_AddByName(track, name, false, 1)
    if index >= 0 then return index, name end
  end
  return -1, ""
end

local function contains_fx(track, needle)
  if not track then return false end
  for index = 0, reaper.TrackFX_GetCount(track) - 1 do
    local ok, name = reaper.TrackFX_GetFXName(track, index, "")
    if ok and string.find(string.lower(name), string.lower(needle), 1, true)
    then return true end
  end
  return false
end

local function stop_transport()
  reaper.OnStopButton()
  reaper.GetSetRepeat(0)
  reaper.GetSet_LoopTimeRange(true, false, 0, 0, false)
end

local function write_report()
  stop_transport()
  local passed = 0
  local lines = {
    "s3g Tracker real-REAPER acceptance v" .. VERSION,
    "REAPER " .. reaper.GetAppVersion(),
    "project " .. PROJECT_PATH,
    ""
  }
  for _, entry in ipairs(results) do
    if entry.passed then passed = passed + 1 end
    lines[#lines + 1] = (entry.passed and "PASS " or "FAIL ")
      .. entry.name .. (entry.detail ~= "" and " — " .. entry.detail or "")
  end
  lines[#lines + 1] = ""
  lines[#lines + 1] = string.format("%d/%d checks passed", passed, #results)
  local text = table.concat(lines, "\n") .. "\n"
  local file = io.open(LOG_PATH, "w")
  if file then file:write(text) file:close() end
  reaper.ShowConsoleMsg(text)
  reaper.ShowMessageBox(text .. "\nLog: " .. LOG_PATH,
    "s3g Tracker acceptance", passed == #results and 0 or 0)
end

local function fail_and_finish(name, detail)
  result(name, false, detail)
  write_report()
end

local function begin_play(position)
  reaper.SetEditCurPos(position, true, false)
  reaper.OnPlayButton()
  deadline = reaper.time_precise() + 4.0
end

local function loop()
  if state == "setup" then
    reaper.ClearConsole()
    reaper.Main_OnCommand(40859, 0) -- New project tab.
    reaper.InsertTrackAtIndex(0, true)
    test_track = reaper.GetTrack(0, 0)
    if not test_track then
      fail_and_finish("setup", "could not create the acceptance track")
      return
    end
    reaper.GetSetMediaTrackInfo_String(test_track, "P_NAME",
      "s3g Tracker Acceptance", true)
    local tracker_index, tracker_name = add_first_fx(test_track, {
      "CLAPi: s3g Tracker (s3g)",
      "CLAP: s3g Tracker (s3g)",
      "s3g Tracker"
    })
    local capture_index, capture_name = add_first_fx(test_track, {
      "JS: s3g/s3g_tracker_acceptance_capture",
      "s3g/s3g_tracker_acceptance_capture",
      "s3g_tracker_acceptance_capture"
    })
    result("instantiate Tracker", tracker_index >= 0, tracker_name)
    result("instantiate MIDI capture", capture_index >= 0, capture_name)
    if tracker_index < 0 or capture_index < 0 then
      write_report()
      return
    end
    reaper.SetCurrentBPM(0, 120, false)
    reset_capture()
    result("capture contract", capture_value(0) == MAGIC,
      "gmem magic " .. capture_value(0))
    begin_play(0)
    state = "initial_play"

  elseif state == "initial_play" then
    if capture_value(2) > 0 and reaper.GetPlayPosition() > 0.35 then
      stop_transport()
      local total, ons, offs = capture_value(1), capture_value(2), capture_value(3)
      result("host transport MIDI", total > 0 and ons > 0,
        string.format("events=%d on=%d off=%d", total, ons, offs))
      result("MIDI byte/channel validity",
        capture_value(4) == 0 and capture_value(5) > 0,
        string.format("invalid=%d mask=0x%X", capture_value(4), capture_value(5)))
      result("sample offsets", capture_value(6) >= 0 and capture_value(7) >= 0,
        string.format("first=%d last=%d", capture_value(6), capture_value(7)))
      reset_capture()
      begin_play(1.0)
      state = "seek_play"
    elseif reaper.time_precise() > deadline then
      fail_and_finish("host transport MIDI", "no Tracker note-ons reached JSFX")
    end

  elseif state == "seek_play" then
    if capture_value(2) > 0 then
      stop_transport()
      result("seek and restart", true,
        "note-ons=" .. capture_value(2))
      reset_capture()
      reaper.GetSet_LoopTimeRange(true, false, 0, 0.25, false)
      reaper.GetSetRepeat(1)
      begin_play(0)
      baseline = reaper.time_precise()
      state = "loop_play"
    elseif reaper.time_precise() > deadline then
      fail_and_finish("seek and restart", "no MIDI after starting at beat 3")
    end

  elseif state == "loop_play" then
    if reaper.time_precise() - baseline > 1.0 then
      stop_transport()
      result("REAPER loop discontinuity", capture_value(2) >= 2,
        "note-ons=" .. capture_value(2))
      reaper.Main_SaveProjectEx(0, PROJECT_PATH, 0)
      result("save acceptance project", true, PROJECT_PATH)
      reaper.Main_openProject("noprompt:" .. PROJECT_PATH)
      deadline = reaper.time_precise() + 5.0
      state = "reopen_wait"
    end

  elseif state == "reopen_wait" then
    local track = reaper.GetTrack(0, 0)
    if track and contains_fx(track, "s3g Tracker")
        and contains_fx(track, "Acceptance MIDI Capture") then
      result("save/reopen FX state", true, "Tracker and capture restored")
      test_track = track
      reset_capture()
      begin_play(0)
      state = "reopen_play"
    elseif reaper.time_precise() > deadline then
      fail_and_finish("save/reopen FX state", "FX chain was not restored")
    end

  elseif state == "reopen_play" then
    if capture_value(2) > 0 then
      result("playback after reopen", true,
        "note-ons=" .. capture_value(2))
      write_report()
      return
    elseif reaper.time_precise() > deadline then
      fail_and_finish("playback after reopen", "restored project emitted no MIDI")
      return
    end
  end
  reaper.defer(loop)
end

loop()
