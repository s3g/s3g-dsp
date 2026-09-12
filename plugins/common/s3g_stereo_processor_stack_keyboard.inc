std::string copyScoreText() const {
  std::ostringstream out;
  for(uint32_t row=0;row<s3g::kProcessorStackScoreRowsPerSection;++row) {
    for(uint32_t player=0;player<2;++player) for(uint32_t string=0;string<6;++string) {
      int fret=_plugin->scoreCells[s3g::processorStackScoreCellIndex(_scoreEditSection,row,player,string)].load(std::memory_order_acquire);
      if(fret==s3g::kProcessorStackScoreHold) out<<'~'; else if(fret>=0) out<<fret; else out<<'-';
      if(player!=1 || string!=5) out<<'\t';
    }
    if(row+1<s3g::kProcessorStackScoreRowsPerSection) out<<'\n';
  }
  return out.str();
}
void pasteScoreText(const std::string& source) {
  auto program=scoreProgramSnapshot(*_plugin);
  std::string normalized=source;
  for(auto& c:normalized) if(c=='\r') c='\n';
  std::istringstream lines(normalized);std::string line;
  uint32_t row=0;
  while(row<s3g::kProcessorStackScoreRowsPerSection && std::getline(lines,line)) {
    std::istringstream cells(line);std::string token;uint32_t col=0;
    while(col<12 && cells>>token) {
      int fret=s3g::kProcessorStackScoreRest;
      if(token=="~" || token=="h" || token=="H" || token=="=") fret=s3g::kProcessorStackScoreHold;
      else if(token!="-" && token!="—" && token!=".") {
        char* end=nullptr;long parsed=std::strtol(token.c_str(),&end,10);
        if(end==token.c_str() || *end || parsed<s3g::kProcessorStackScoreMinimumFret || parsed>s3g::kProcessorStackScoreMaximumFret) {++col;continue;}
        fret=static_cast<int>(parsed);
      }
      s3g::setProcessorStackScoreCell(program,_scoreEditSection,row,col/6,col%6,fret);++col;
    }
    if(col) ++row;
  }
  storeScoreProgram(*_plugin,program,true);invalid();
}
void openScoreLockMenu(uint32_t player,uint32_t row,uint32_t slot,drawing::Point point) {
  using Control=s3g::ProcessorStackScoreLockControl;
  std::vector<std::string> items{drawing::stringFormat("PLAYER %c · ROW %02u · LOCK %u",char('A'+player),row+1,slot+1)};
  std::vector<Control> controls{Control::None};std::vector<bool> selectable{false};
  if(player==1) {items.push_back("B LOCKS USE THAT RIG INDEPENDENTLY FOR THIS ROW");controls.push_back(Control::None);selectable.push_back(false);}
  items.push_back("--- CLEAR");controls.push_back(Control::None);selectable.push_back(true);
  struct Group {const char* name;Control first,last;};
  const Group groups[]{
    {"MATERIAL",Control::Neck,Control::Body},
    {"PEDAL",Control::Circuit,Control::Bias},
    {"AMPLIFIER / SPEAKER",Control::Stack,Control::Mic},
    {"MIC FEEDBACK LOOP",Control::Feedback,Control::OverloadMask},
    {"SPEAKER PROFILE",Control::Speaker,Control::Speaker}};
  auto current=stackScoreLockCell(*_plugin,_scoreEditSection,row,player,slot);
  int selected=int(items.size()-1);
  for(const auto& group:groups) {
   items.push_back(group.name);controls.push_back(Control::None);selectable.push_back(false);
   for(uint32_t n=uint32_t(group.first);n<=uint32_t(group.last);++n) {
    auto control=static_cast<Control>(n);clap_id id=stackScoreLockParamId(player,control);
    char value[64]{};paramsValueToText(&_plugin->plugin,id,paramValue(*_plugin,id),value,sizeof(value));
    items.push_back(drawing::stringFormat("%-16s  %s",s3g::processorStackScoreLockControlName(control),value));
    controls.push_back(control);selectable.push_back(true);if(current.control==n) selected=int(items.size()-1);
   }
  }
  openPopup(drawing::makeRect(point.x,point.y,420,0),items,selected,
    [this,player,row,slot,controls](int index){selectScoreLock(player,row,slot,controls.at(size_t(index)));},1,{},18.,selectable);
}
void onKeyboardEvent(VSTGUI::KeyboardEvent& e) override {
  using VSTGUI::VirtualKey;using VSTGUI::ModifierKey;
  if(e.type!=VSTGUI::EventType::KeyDown || _page!=3u) return;
  std::string key;
  if(e.character>0 && e.character<128) key=std::string(1,char(std::tolower(e.character)));
  if(e.modifiers.has(ModifierKey::Control) || e.modifiers.has(ModifierKey::Super)) {
    if(key=="c") VSTGUI::CClipboard::setString(copyScoreText().c_str());
    else if(key=="v") {auto text=VSTGUI::CClipboard::getString();if(text) pasteScoreText(text->getString());}
    else return;
    e.consumed=true;return;
  }
  if(e.modifiers.has(ModifierKey::Alt)) return;
  uint32_t code=0;
  switch(e.virt) {
    case VirtualKey::Left:code=123;break;case VirtualKey::Right:code=124;break;
    case VirtualKey::Down:code=125;break;case VirtualKey::Up:code=126;break;
    case VirtualKey::Tab:code=48;break;case VirtualKey::Back:code=51;break;
    case VirtualKey::Delete:code=117;break;case VirtualKey::Return:key="\r";break;
    default:break;
  }
  if(!code && (key.size()!=1 || std::string("0123456789-.h~=r[]\r").find(key[0])==std::string::npos)) return;
  keyInput(key,code,drawing::uptime());e.consumed=true;
}
void keyInput(const std::string& key,uint32_t code,double timestamp) {
 auto* instance=_plugin;
    const auto advanceRow = [&]() {
        _scoreSelectedRow = (_scoreSelectedRow + 1u)
            % s3g::kProcessorStackScoreRowsPerSection;
        _scorePendingFret = -1;
    };
    if (code == 123u || code == 124u) {
        int column = static_cast<int>(_scoreSelectedPlayer
            * s3g::kProcessorStackScoreStringCount + _scoreSelectedString);
        column += code == 123u ? -1 : 1;
        const int columnCount = static_cast<int>(
            s3g::kProcessorStackScorePlayerCount
                * s3g::kProcessorStackScoreStringCount);
        column = (column + columnCount) % columnCount;
        _scoreSelectedPlayer = static_cast<uint32_t>(column)
            / s3g::kProcessorStackScoreStringCount;
        _scoreSelectedString = static_cast<uint32_t>(column)
            % s3g::kProcessorStackScoreStringCount;
        _scorePendingFret = -1;
    } else if (code == 125u || code == 126u) {
        const int direction = code == 125u ? 1 : -1;
        const int rows = static_cast<int>(
            s3g::kProcessorStackScoreRowsPerSection);
        _scoreSelectedRow = static_cast<uint32_t>(
            (static_cast<int>(_scoreSelectedRow) + direction + rows) % rows);
        _scorePendingFret = -1;
    } else if (code == 48u) {
        _scoreSelectedPlayer = 1u - _scoreSelectedPlayer;
        _scorePendingFret = -1;
    } else if (key.size() == 1u
        && key[0] >= '0'
        && key[0] <= '9') {
        const int digit = static_cast<int>(key[0] - '0');
        const bool append = _scorePendingFret >= 0
            && timestamp - _scorePendingTime < 0.8;
        const int candidate = append ? _scorePendingFret * 10 + digit : digit;
        if (candidate <= s3g::kProcessorStackScoreMaximumFret) {
            setScoreCell(*instance, _scoreEditSection, _scoreSelectedRow,
                _scoreSelectedPlayer, _scoreSelectedString, candidate);
            if (append) advanceRow();
            else {
                _scorePendingFret = candidate;
                _scorePendingTime = timestamp;
            }
        } else beep();
    } else if ((key == "-")
        || (key == ".")
        || code == 51u || code == 117u) {
        setScoreCell(*instance, _scoreEditSection, _scoreSelectedRow,
            _scoreSelectedPlayer, _scoreSelectedString,
            s3g::kProcessorStackScoreRest);
        advanceRow();
    } else if ((key == "h")
        || (key == "~")
        || (key == "=")) {
        setScoreCell(*instance, _scoreEditSection, _scoreSelectedRow,
            _scoreSelectedPlayer, _scoreSelectedString,
            s3g::kProcessorStackScoreHold);
        advanceRow();
    } else if ((key == "r")) {
        int repeated = s3g::kProcessorStackScoreRest;
        for (uint32_t distance = 1u;
             distance <= s3g::kProcessorStackScoreRowsPerSection;
             ++distance) {
            const uint32_t sourceRow = (_scoreSelectedRow
                + s3g::kProcessorStackScoreRowsPerSection - distance)
                % s3g::kProcessorStackScoreRowsPerSection;
            const size_t sourceIndex = s3g::processorStackScoreCellIndex(
                _scoreEditSection, sourceRow, _scoreSelectedPlayer,
                _scoreSelectedString);
            repeated = instance->scoreCells[sourceIndex].load(
                std::memory_order_acquire);
            if (repeated != s3g::kProcessorStackScoreHold) break;
        }
        if (repeated == s3g::kProcessorStackScoreHold) {
            repeated = s3g::kProcessorStackScoreRest;
        }
        setScoreCell(*instance, _scoreEditSection, _scoreSelectedRow,
            _scoreSelectedPlayer, _scoreSelectedString, repeated);
        advanceRow();
    } else if ((key == "[")
        || (key == "]")) {
        const size_t index = s3g::processorStackScoreCellIndex(
            _scoreEditSection, _scoreSelectedRow, _scoreSelectedPlayer,
            _scoreSelectedString);
        int fret = instance->scoreCells[index].load(
            std::memory_order_acquire);
        if (fret < 0) fret = 0;
        fret += (key == "[") ? -1 : 1;
        setScoreCell(*instance, _scoreEditSection, _scoreSelectedRow,
            _scoreSelectedPlayer, _scoreSelectedString, fret);
        _scorePendingFret = -1;
    } else if ((key == "\r")) {
        advanceRow();
    } else {
        (void)0;
        return;
    }
    setNeedsDisplay(true);

}
void selectScoreLock(uint32_t player,uint32_t row,uint32_t slot,s3g::ProcessorStackScoreLockControl control) {
 auto* instance=_plugin;
    double normalized = 0.0;
    const auto existing = stackScoreLockCell(
        *instance, _scoreEditSection, row, player, slot);
    if (existing.control == static_cast<uint8_t>(control)) {
        normalized = s3g::processorStackScoreLockNormalized(existing);
    } else if (control != s3g::ProcessorStackScoreLockControl::None) {
        const clap_id id = stackScoreLockParamId(player, control);
        const double base = uiNormalizedValue(id, paramValue(*instance, id));
        if (control == s3g::ProcessorStackScoreLockControl::Neck
            || control == s3g::ProcessorStackScoreLockControl::Body) {
            const uint32_t current = static_cast<uint32_t>(
                std::lround(base * 3.0));
            normalized = static_cast<double>((current + 1u) % 4u) / 3.0;
        } else if (control
                == s3g::ProcessorStackScoreLockControl::Circuit) {
            const uint32_t current = static_cast<uint32_t>(
                std::lround(base * 7.0));
            normalized = static_cast<double>((current + 3u) % 8u) / 7.0;
        } else if (control
                == s3g::ProcessorStackScoreLockControl::Speaker) {
            const uint32_t current = static_cast<uint32_t>(
                std::lround(base * 6.0));
            normalized = static_cast<double>((current + 2u) % 7u) / 6.0;
        } else {
            // A newly assigned lock should be audible immediately rather than
            // beginning as a neutral copy of the base slider.
            normalized = std::clamp(
                base < 0.5 ? base + 0.36 : base - 0.36, 0.08, 0.92);
        }
    }
    setScoreLock(*instance, _scoreEditSection, row,
        player, slot, control, normalized);
    _scoreLockPlayer = player;
    _scoreLockRow = row;
    _scoreLockSlot = slot;
    setNeedsDisplayInRect(drawing::inset(
        stackScoreLockRect(player, slot, row), -1.0, -1.0));

}
