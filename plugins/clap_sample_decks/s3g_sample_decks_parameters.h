#pragma once
#include "s3g_sample_decks.h"
#include <string>
#include <vector>

namespace s3g::decks {
using namespace s3g::sample;
enum Global : unsigned { Output, Crossfade, Curve, Format, Stems, Controller, MidiChannel,
    Storage, RecordSeconds, FocusDeck, RecordSource, RecordInputGroup,
    HeadphoneOutput, HeadphoneMix, HeadphoneLevel, HeadphoneFold, HeadphoneA, HeadphoneB, GlobalCount };
enum Control : unsigned { Playback, Layer, SourceMode, Level, Tune, Start, End, Direction,
    Repeat, Clock, Cycle, CycleBeats, StackCycle, StackBeats, Position, GrainSize, Density,
    Spray, PitchSpray, ReverseChance, Attack, Release, Character, Amount, PressureDepth,
    SourceBpm, Technique0, Technique1, Technique2, Technique3, PadMode, PadBank, Slip,
    Rate, ZeroCross, SliceCount, High, Mid, Low, Filter, MotionPath, Gain, CuePosition, StackNavigation, StackPosition,
    SamplerPads, KeyboardRoot, KeyboardOctave, KeyboardVoices, ScreenVelocity, ScreenPressure, ScreenLatch,
    CueAction, SliceHold, SliceRaw, FxLatch, RideTime, CueLayer, CueStackPosition, ControlCount };
static_assert(ControlCount<=64);
constexpr unsigned kDeckBase=32, kDeckStride=512, kFamily=64, kFx=400;
constexpr unsigned kParamCount=kDeckBase+2*kDeckStride;
inline constexpr unsigned param(unsigned deck, unsigned control) { return kDeckBase+deck*kDeckStride+control; }
inline constexpr unsigned family(NeonFamily key) { return kFamily+neonFamilyIndex(key); }
inline constexpr const char* kMethods[] {"SAMPLE","MOTION","GRAINS","SLICE SEQUENCE","STRETCH","WAVESETS","LANES","SPECTRAL","CUTUPS"};
inline constexpr const char* kPadModes[] {"CUES","LOOPS","FX PUNCH","SLICES","LAYERS / KEYS","ROLL","STACK RIDE","CAPTURE / HANDOFF"};
inline constexpr const char* kLoopLabels[]{"1/16","1/8","1/4","1/2","1","2","4","8"};
inline constexpr const char* kRollLabels[]{"1/32","1/16","1/8","1/4","1/2","1","2","4"};
struct Definition { std::string name; double lo=0, hi=1, initial=0; bool integer=false; std::vector<std::string> choices; };
inline Definition definition(unsigned index) {
    if (index<kDeckBase) {
        switch(index) {
        case Output:return {"OUT",-60,12,-6};
        case Crossfade:return {"CROSSFADE",0,1,.5};
        case Curve:return {"FADE CURVE",0,2,0,true,{"LINEAR","EQUAL POWER","CUT"}};
        case Format:return {"FORMAT",0,5,0,true,{"STEREO","QUAD","OCTO","ACN/SN3D 1OA","ACN/SN3D 2OA","ACN/SN3D 3OA"}};
        case Stems:return {"STEMS",0,1,0,true,{"OFF","A + B"}};
        case Controller:return {"MIDI INPUT",0,1,1,true,{"NOTES","BEATPAD 2"}};
        case MidiChannel:return {"NOTE CHANNEL",0,16,0,true};
        case Storage:return {"STORE",0,2,0,true,{"PROJECT","LINK","EMBED"}};
        case RecordSeconds:return {"MAX TAKE / S",1,120,30};
        case FocusDeck:return {"FOCUS",0,1,0,true,{"A","B"}};
        case RecordSource:return {"RECORD SOURCE",0,2,0,true,{"DECK OUTPUT","TRACK INPUT","INPUT + THRU"}};
        case RecordInputGroup:return {"INPUT GROUP",0,15,0,true};
        case HeadphoneOutput:{std::vector<std::string> choices{"OFF","AUTO"};
            for(unsigned ch=1;ch<32;ch+=2)choices.push_back("OUT "+std::to_string(ch)+"–"+std::to_string(ch+1));
            return {"CUE OUTPUT",0,17,1,true,std::move(choices)};}
        case HeadphoneMix:return {"CUE → MASTER",0,1,0};
        case HeadphoneLevel:return {"CUE LEVEL / DB",-60,0,-6};
        case HeadphoneFold:return {"QUAD / OCTO FOLD",0,2,0,true,{"STEREO PAIRS","CLOCKWISE RING","MONO SUM"}};
        case HeadphoneA:return {"CUE A",0,1,0,true,{"OFF","ON"}};
        case HeadphoneB:return {"CUE B",0,1,0,true,{"OFF","ON"}};
        default:return {};
        }
    }
    const unsigned key=(index-kDeckBase)%kDeckStride;
    switch(key) {
    case Playback:return {"PLAYBACK",0,8,0,true,{std::begin(kMethods),std::end(kMethods)}};
    case Layer:return {"LAYER",0,31,0,true};
    case SourceMode:return {"LAYER SOURCE",0,4,1,true,{"PRIMARY","SELECTED","VELOCITY","RANDOM / TRIGGER","STACK PATH"}};
    case Level:return {"LEVEL",0,1,1};
    case Tune:return {"TUNE / ST",-48,48,0};
    case Start:return {"START",0,1,0};
    case End:return {"END",0,1,1};
    case Direction:return {"DIRECTION",0,3,0,true,{"FORWARD","REVERSE","PING PONG","REV PING PONG"}};
    case Repeat:return {"REPEAT",0,1,1,true,{"OFF","ON"}};
    case Clock:return {"CLOCK",0,1,0,true,{"FREE","HOST"}};
    case Cycle:return {"SOURCE CYCLE / S",.05,30,4};
    case CycleBeats:return {"SOURCE BEATS",.25,32,8};
    case StackCycle:return {"STACK CYCLE / S",.05,30,4};
    case StackBeats:return {"STACK BEATS",.25,32,8};
    case Position:return {"SOURCE POSITION",0,1,0};
    case GrainSize:return {"GRAIN SIZE / MS",1,500,80};
    case Density:return {"DENSITY / HZ",1,80,12};
    case Spray:return {"POSITION SPRAY",0,1,.15};
    case PitchSpray:return {"PITCH SPRAY / ST",0,48,0};
    case ReverseChance:return {"REVERSE CHANCE",0,1,0};
    case Attack:return {"ATTACK / S",0,2,.005};
    case Release:return {"RELEASE / S",0,2,.02};
    case Character:return {"CHARACTER",0,7,0,true,{"FILTER","ECHO","SPACE","SHIFT","VOWEL","PUNCH","DRIVE","CRUSH"}};
    case Amount:return {"FX AMOUNT",0,1,0};
    case PressureDepth:return {"PRESSURE DEPTH",0,1,.75};
    case SourceBpm:return {"SOURCE BPM",20,400,120};
    case Technique0:return {"TECHNIQUE 1",0,1,.35};
    case Technique1:return {"TECHNIQUE 2",0,1,0};
    case Technique2:return {"TECHNIQUE 3",0,1,0};
    case Technique3:return {"TECHNIQUE 4",0,1,1};
    case PadMode:return {"PAD MODE",0,7,4,true,{std::begin(kPadModes),std::end(kPadModes)}};
    case PadBank:return {"PAD BANK",0,3,0,true,{"1-8","9-16","17-24","25-32"}};
    case Slip:return {"SLIP",0,1,0,true,{"OFF","ON"}};
    case Rate:return {"TIME RATE",.125,4,1};
    case ZeroCross:return {"ZERO CROSS",0,1,1,true,{"OFF","ON"}};
    case SliceCount:return {"SLICE COUNT",1,32,8,true};
    case High:return {"HIGH / DB",-24,12,0};
    case Mid:return {"MID / DB",-24,12,0};
    case Low:return {"LOW / DB",-24,12,0};
    case Filter:return {"DJ FILTER",-1,1,0};
    case MotionPath:return {"SOURCE PATH",0,3,0,true,{"FORWARD","REVERSE","BOUNCE","WANDER"}};
    case Gain:return {"GAIN / DB",-24,12,0};
    case CuePosition:return {"CUE POSITION",0,1,0};
    // -1 preserves the position-only behavior of sets saved before v7.
    case CueLayer:return {"CUE LAYER",-1,31,-1,true};
    case CueStackPosition:return {"CUE STACK POSITION",-1,31,-1};
    case StackNavigation:return {"STACK NAV",0,1,0,true,{"FOLLOW SOURCE","MANUAL STACK"}};
    case StackPosition:return {"STACK POSITION",0,1,0};
    case SamplerPads:return {"SAMPLER PADS",0,1,0,true,{"LAYERS","KEYBOARD"}};
    case KeyboardRoot:return {"ROOT NOTE",0,127,60,true};
    case KeyboardOctave:return {"KEY OCTAVE",-4,4,0,true};
    case KeyboardVoices:return {"KEY VOICES",1,16,8,true};
    case ScreenVelocity:return {"MOUSE VELOCITY",.01,1,1};
    case ScreenPressure:return {"MOUSE PRESSURE",0,1,0};
    case ScreenLatch:return {"MOUSE PADS",0,1,0,true,{"HOLD","LATCH"}};
    case CueAction:return {"CUE ACTION",0,2,0,true,{"JUMP","STORE","CLEAR"}};
    case SliceHold:return {"SLICE TRIGGER",0,1,0,true,{"ONE SHOT","HELD"}};
    case SliceRaw:return {"SLICE ENGINE",0,1,0,true,{"PLAYBACK METHOD","RAW SAMPLE"}};
    case FxLatch:return {"FX PUNCH",0,1,0,true,{"HOLD","TOGGLE"}};
    case RideTime:return {"RIDE GLIDE / S",.005,.1,.06};
    default:break;
    }
    if (key>=kFamily && key<kFamily+kNeonFamilyCount) {
        const auto f=neonFamilyDef(key-kFamily);
        std::string name;
        switch(static_cast<NeonFamily>(key-kFamily)) {
        case NeonFamily::StackShape: name="STACK SHAPE"; break;
        case NeonFamily::StackJump: name="STACK JUMP"; break;
        case NeonFamily::StackOffset: name="STACK OFFSET"; break;
        case NeonFamily::StackCurve: name="STACK CURVE"; break;
        case NeonFamily::StackAdvance: name="STACK ADVANCE"; break;
        case NeonFamily::PathCount: name="PATH COUNT"; break;
        case NeonFamily::MotionSound: name="MOTION SOUND"; break;
        case NeonFamily::PacketRate: name="PACKET RATE"; break;
        case NeonFamily::PacketDuty: name="PACKET DUTY"; break;
        case NeonFamily::MotorRate: name="MOTOR RATE"; break;
        case NeonFamily::MotorSymmetry: name="MOTOR SYMMETRY"; break;
        case NeonFamily::MotorShape: name="MOTOR SHAPE"; break;
        case NeonFamily::MotionLocus: name="MOTION LOCUS"; break;
        case NeonFamily::MotionField: name="MOTION FIELD"; break;
        case NeonFamily::MotionJitter: name="MOTION JITTER"; break;
        case NeonFamily::MotionWindow: name="MOTION WINDOW"; break;
        case NeonFamily::MotionModel: name="MOTION MODEL"; break;
        case NeonFamily::EventRate: name="EVENT RATE"; break;
        case NeonFamily::EventRepeats: name="EVENT REPEATS"; break;
        case NeonFamily::EventStep: name="EVENT STEP"; break;
        case NeonFamily::EventPitch: name="EVENT PITCH"; break;
        case NeonFamily::EventLevel: name="EVENT LEVEL"; break;
        case NeonFamily::EventCurve: name="EVENT CURVE"; break;
        case NeonFamily::MotionTrajectory: name="MOTION TRAJECTORY"; break;
        case NeonFamily::MotionTravel: name="MOTION TRAVEL"; break;
        case NeonFamily::GrainSource: name="GRAIN SOURCE"; break;
        case NeonFamily::GrainProcess: name="GRAIN PROCESS"; break;
        case NeonFamily::GrainAmount: name="GRAIN AMOUNT"; break;
        case NeonFamily::GrainRegions: name="GRAIN REGIONS"; break;
        case NeonFamily::GrainWindow: name="GRAIN WINDOW"; break;
        case NeonFamily::GrainSkew: name="GRAIN SKEW"; break;
        case NeonFamily::GrainSizeVariation: name="GRAIN SIZE VARIATION"; break;
        case NeonFamily::GrainLevelVariation: name="GRAIN LEVEL VARIATION"; break;
        case NeonFamily::GrainScatter: name="GRAIN SCATTER"; break;
        case NeonFamily::GrainBias: name="GRAIN BIAS"; break;
        case NeonFamily::GrainPitch: name="GRAIN PITCH"; break;
        case NeonFamily::GrainTimeSync: name="GRAIN TIME SYNC"; break;
        case NeonFamily::GrainSizeScale: name="GRAIN SIZE SCALE"; break;
        case NeonFamily::GrainDensityScale: name="GRAIN DENSITY SCALE"; break;
        case NeonFamily::PathTime: name="PATH TIME"; break;
        case NeonFamily::PathValue: name="PATH VALUE"; break;
        case NeonFamily::LanePosition: name="LANE POSITION"; break;
        case NeonFamily::LaneAuto: name="LANE AUTO"; break;
        case NeonFamily::LaneRate: name="LANE RATE"; break;
        case NeonFamily::LaneSlew: name="LANE SLEW"; break;
        case NeonFamily::LaneJoin: name="LANE JOIN"; break;
        case NeonFamily::SliceAttack: name="SLICE ATTACK"; break;
        case NeonFamily::SliceDecay: name="SLICE DECAY"; break;
        case NeonFamily::SliceSustain: name="SLICE SUSTAIN"; break;
        case NeonFamily::SliceRelease: name="SLICE RELEASE"; break;
        case NeonFamily::RoutingMode: name="ROUTING MODE"; break;
        case NeonFamily::RoutingWidth: name="ROUTING WIDTH"; break;
        case NeonFamily::RoutingTraversal: name="ROUTING TRAVERSAL"; break;
        case NeonFamily::GrainStereoLink: name="GRAIN STEREO LINK"; break;
        case NeonFamily::SpectralBlur: name="SPECTRAL BLUR"; break;
        case NeonFamily::SpectralAdvance: name="SPECTRAL ADVANCE"; break;
        case NeonFamily::SpectralPressure: name="SPECTRAL PRESSURE"; break;
        case NeonFamily::MosaicMode: name="MOSAIC MODE"; break;
        case NeonFamily::MosaicScope: name="MOSAIC SCOPE"; break;
        case NeonFamily::MosaicTarget: name="MOSAIC TARGET"; break;
        case NeonFamily::WavesetEngine: name="WAVESET ENGINE"; break;
        case NeonFamily::OscPosition: name="OSC POSITION"; break;
        case NeonFamily::OscFrequency: name="OSC FREQUENCY"; break;
        case NeonFamily::OscScan: name="OSC SCAN"; break;
        case NeonFamily::OscGroup: name="OSC GROUP"; break;
        case NeonFamily::SpectralSmear: name="SPECTRAL SMEAR"; break;
        case NeonFamily::SpectralFocus: name="SPECTRAL FOCUS"; break;
        case NeonFamily::SpectralTilt: name="SPECTRAL TILT"; break;
        case NeonFamily::SpectralThin: name="SPECTRAL THIN"; break;
        case NeonFamily::CutDivision: name="CUT DIVISION"; break;
        case NeonFamily::CutRate: name="CUT RATE"; break;
        case NeonFamily::CutRegions: name="CUT REGIONS"; break;
        case NeonFamily::CutRegionMode: name="CUT REGION MODE"; break;
        case NeonFamily::CutRepeat: name="CUT REPEAT"; break;
        case NeonFamily::CutFileOrder: name="CUT FILE ORDER"; break;
        case NeonFamily::CutSourceOrder: name="CUT SOURCE ORDER"; break;
        case NeonFamily::CutSwing: name="CUT SWING"; break;
        case NeonFamily::CutTimeVariation: name="CUT TIME VARIATION"; break;
        case NeonFamily::CutGate: name="CUT GATE"; break;
        case NeonFamily::CutJoin: name="CUT JOIN"; break;
        case NeonFamily::CutReverse: name="CUT REVERSE"; break;
        case NeonFamily::CutPitchVariation: name="CUT PITCH VARIATION"; break;
        case NeonFamily::CutLevelVariation: name="CUT LEVEL VARIATION"; break;
        case NeonFamily::CutTempoSync: name="CUT TEMPO SYNC"; break;
        case NeonFamily::CutSeed: name="CUT SEED"; break;
        case NeonFamily::CutVoiceMode: name="CUT VOICE MODE"; break;
        case NeonFamily::CutPolyPath: name="CUT POLY PATH"; break;
        case NeonFamily::CutAllocation: name="CUT ALLOCATION"; break;
        case NeonFamily::CutAttack: name="CUT ATTACK"; break;
        case NeonFamily::CutRelease: name="CUT RELEASE"; break;
        case NeonFamily::CutPatternLane: name="CUT PATTERN LANE"; break;
        case NeonFamily::CutPatternSource: name="CUT PATTERN SOURCE"; break;
        default:name="POINT / STEP "+std::to_string(key-kFamily);break;
        }
        std::vector<std::string> choices;
        switch(static_cast<NeonFamily>(key-kFamily)) {
        case NeonFamily::StackShape: choices={"MANUAL","RAMP UP","RAMP DOWN","TRIANGLE","SINE","SQUARE","WANDER"};break;
        case NeonFamily::StackJump:choices={"CROSSFADE","JUMP"};break;
        case NeonFamily::StackAdvance:choices={"TIME","EVENT"};break;
        case NeonFamily::MotionSound:choices={"CONTINUOUS","PACKETS","MOTOR"};break;
        case NeonFamily::MotorShape:choices={"LINEAR","ROUNDED","EXPONENTIAL","PLATEAU"};break;
        case NeonFamily::MotionModel:choices={"SMOOTH","FREEZE","ITERATE","DOUBLETS","BOUNCE"};break;
        case NeonFamily::MotionTrajectory:choices={"SOURCE PATH","HOVER","MIRROR","ZIGZAG","MOVING LOOP"};break;
        case NeonFamily::GrainSource:choices={"FREEZE","SCAN","CLOUD","SLICE"};break;
        case NeonFamily::GrainProcess:choices={"ORDINARY","SORTER","STUTTER","SHRINK","DOUBLETS"};break;
        case NeonFamily::GrainWindow:choices={"LEGACY ADSR","PARZEN","SINE","HANN","TRIANGLE","GAUSSIAN"};break;
        case NeonFamily::GrainBias:choices={"BACKWARD","BIPOLAR","FORWARD"};break;
        case NeonFamily::LaneAuto:name="NAVIGATION";choices={"MANUAL","STACK PATH"};break;
        case NeonFamily::RoutingMode:choices={"PRESERVE FIELD","DISTRIBUTE"};break;
        case NeonFamily::RoutingWidth:choices={"MONO","STEREO"};break;
        case NeonFamily::RoutingTraversal:choices={"FORWARD","REVERSE","PALINDROME","RANDOM","RANDOM CYCLE"};break;
        case NeonFamily::GrainStereoLink:choices={"LINKED","INDEPENDENT"};break;
        case NeonFamily::MosaicMode:choices={"SEQUENCE","SIMILAR","CONTRAST","ENERGY","BRIGHTNESS"};break;
        case NeonFamily::MosaicScope:choices={"PRIMARY","ALL LAYERS"};break;
        case NeonFamily::WavesetEngine:choices={"REARRANGE","OSCILLATOR"};break;
        case NeonFamily::CutRegionMode:choices={"EQUAL","TRANSIENT"};break;
        case NeonFamily::CutFileOrder:choices={"DOWN","UP","PALINDROME","RANDOM","RANDOM CYCLE","MANUAL","PAIRS","OUTSIDE IN","STAGGER","CENTER OUT"};break;
        case NeonFamily::CutSourceOrder:choices={"TIMELINE","FORWARD","REVERSE","PALINDROME","RANDOM","WALK","MANUAL"};break;
        case NeonFamily::CutDivision:choices={"1 BAR","1/2","1/4","1/8","1/16","1/32","1/8 T","1/16 T"};break;
        case NeonFamily::CutVoiceMode:choices={"POLY","MONO","LEGATO"};break;
        case NeonFamily::CutPolyPath:choices={"TOGETHER","STEP OFFSET","QUARTER SPREAD","MIRROR PAIRS"};break;
        case NeonFamily::CutAllocation:choices={"NOTE","CUT","PATTERN"};break;
        default:if(f.stepped&&f.maximum==1)choices={"OFF","ON"};break;
        }
        return {name,f.minimum,f.maximum,f.initial,f.stepped,choices};
    }
    if (key>=kFx && key<kFx+8*kNeonCharacterControls) {
        const unsigned n=key-kFx,e=n/kNeonCharacterControls,c=n%kNeonCharacterControls;
        const auto& f=kNeonCharacterDefs[e][c];
        if (*f.label) return {std::string(kNeonCharacterNames[e])+" / "+f.label,0,1,f.initial};
    }
    return {};
}
inline bool known(unsigned index) { return index<kParamCount && !definition(index).name.empty(); }
} // namespace s3g::decks
