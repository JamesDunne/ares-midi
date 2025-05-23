struct APU : Thread {
  using MIDIEmitter = function<auto (u8 cmd, u8 data1, u8 data2) -> void>;

  Node::Object node;
  Node::Audio::Stream stream;
  Node::Audio::MIDI midi;

  auto rate() const -> u32 { return Region::PAL() ? 16 : 12; }

  //apu.cpp
  auto load(Node::Object) -> void;
  auto unload() -> void;

  auto main() -> void;
  auto tick() -> void;
  auto setIRQ() -> void;

  auto power(bool reset) -> void;

  auto readIO(n16 address) -> n8;
  auto writeIO(n16 address, n8 data) -> void;

  //serialization.cpp
  auto serialize(serializer&) -> void;

  struct MidiState {
    u8  noteNew;
    u8  noteOn;
    u8  noteChan;
    u8  noteVel;
    n14 noteWheel;
    double noteFreq;
    u2  noteDuty;
    u32 clocksSinceNoteOn;

    u8 chanProgram;
    u8 chanVolume;

    u8  lastNoteOn;
    u8  lastChan;
    u8  lastVel;
    n14 lastWheel;
    double lastFreq;

    u32 periodWriteCountdown;

    u16 lastPeriod;
    u32 lastCycleVolume;

    n8  lastAddressLatch;
    n8  lastLengthLatch;
    n16 lastLengthCounter;

    bool triggered;
    bool triggeredStop;

    u8  chans[4];
    u8  chanVel[4];
    
    auto rateLimit() -> bool;
    auto rateControl() -> void;
    
    auto applyNoteWheel(double n) -> void;
    
    u32 messages;
    u32 clocks;
  };

  struct Length {
    //length.cpp
    auto main() -> void;

    auto power(bool reset, bool isTriangle) -> void;

    auto setEnable(n1 value) -> void;
    auto setHalt(bool lengthClocking, n1 value) -> void;
    auto setCounter(bool lengthClocking, n5 index) -> void;

    //serialization.cpp
    auto serialize(serializer&) -> void;

    n8 counter;
    n1 halt;
    n1 enable;

    bool delayHalt;
    n1 newHalt;

    bool delayCounter;
    n5 counterIndex;

    constexpr static u8 table[32] = {
      0x0a, 0xfe, 0x14, 0x02, 0x28, 0x04, 0x50, 0x06,
      0xa0, 0x08, 0x3c, 0x0a, 0x0e, 0x0c, 0x1a, 0x0e,
      0x0c, 0x10, 0x18, 0x12, 0x30, 0x14, 0x60, 0x16,
      0xc0, 0x18, 0x48, 0x1a, 0x10, 0x1c, 0x20, 0x1e,
    };
  };

  struct Envelope {
    //envelope.cpp
    auto volume() const -> u32;
    auto clock() -> void;

    //serialization.cpp
    auto serialize(serializer&) -> void;

    n4 speed;
    n1 useSpeedAsVolume;
    n1 loopMode;
    n1 reloadDecay;
    n8 decayCounter;
    n4 decayVolume;
  };

  struct Sweep {
    //sweep.cpp
    auto checkPeriod() -> bool;
    auto clock(u32 channel) -> void;

    //serialization.cpp
    auto serialize(serializer&) -> void;

    n8  shift;
    n1  decrement;
    n3  period;
    n8  counter = 1;
    n1  enable;
    n1  reload;
    n11 pulsePeriod;
  };

  struct Pulse {
    //pulse.cpp
    auto clock() -> n8;

    auto power(bool reset) -> void;

    //serialization.cpp
    auto serialize(serializer&) -> void;

    auto calculateMidi() -> void;
    auto generateMidi(MIDIEmitter&) -> void;
    MidiState m;

    Length length;
    Envelope envelope;
    Sweep sweep;

    n16 periodCounter;
    n2  duty;
    n3  dutyCounter;
    n11 period;
  } pulse1, pulse2;

  struct Triangle {
    //triangle.cpp
    auto clockLinearLength() -> void;
    auto clock() -> n8;

    auto power(bool reset) -> void;

    //serialization.cpp
    auto serialize(serializer&) -> void;

    auto calculateMidi() -> void;
    auto generateMidi(MIDIEmitter&) -> void;
    MidiState m;

    Length length;

    n16 periodCounter;
    n8  linearLength;
    n11 period;
    n5  stepCounter;
    n8  linearLengthCounter;
    n1  reloadLinear;
  } triangle;

  struct Noise {
    //noise.cpp
    auto clock() -> n8;

    auto power(bool reset) -> void;

    //serialization.cpp
    auto serialize(serializer&) -> void;

    auto calculateMidi() -> void;
    auto generateMidi(MIDIEmitter&) -> void;
    MidiState m;

    Length length;
    Envelope envelope;

    n16 periodCounter;
    n4  period;
    n1  shortMode;
    n15 lfsr;

    auto dump() -> void;
    auto dumpMidi(u8 cmd, u8 d1, u8 d2) -> void;
    file_buffer f;
    u64 cycles;
  } noise;

  struct DMC {
    //dmc.cpp
    auto start() -> void;
    auto stop() -> void;
    auto clock() -> n8;

    auto power(bool reset) -> void;
    auto setDMABuffer(n8 data) -> void;
    auto dmaAddress() -> n16 const { return 0x8000 | readAddress; }

    //serialization.cpp
    auto serialize(serializer&) -> void;

    auto calculateMidi() -> void;
    auto generateMidi(MIDIEmitter&) -> void;
    MidiState m;

    using generateMidiFromPeriod = function<auto (n4 p, u8& ch_o, u8& cp_o, u8& cv_o, double& n_o, u8& v_o) -> void>;

    nall::map<u64, generateMidiFromPeriod> sampleDescriptors;
    nall::map<u64, nall::map<n4, bool> > samplesMissing;
    u32 clocksSinceStart;

    n16 lengthCounter;
    n16 periodCounter;
    n1  irqPending;
    n4  period;
    n1  irqEnable;
    n1  loopMode;
    n8  dacLatch;
    n8  addressLatch;
    n8  lengthLatch;
    n15 readAddress;
    n3  bitCounter;
    n1  dmaBufferValid;
    n8  dmaBuffer;
    n1  sampleValid;
    n8  sample;
    n8  dmaDelayCounter;
  } dmc;

  struct FrameCounter {
    enum mode: u8 { Freq60Hz = 0, Freq48Hz = 1 };

    auto getPeriod() const -> u16 {
      return Region::PAL() ? periodPAL[mode][step] : periodNTSC[mode][step];
    }
    auto lengthClocking() const -> bool {
      // Because the CPU writes before the APU clock,
      // the counter must be 1
      return counter == 1 && (step == 1 || step == 4);
    }

    auto main() -> void;
    auto power(bool reset) -> void;
    auto write(n8 data) -> void;

    //serialization.cpp
    auto serialize(serializer&) -> void;

    n1  irqInhibit;
    n1  mode;

    n1  irqPending;
    n3  step;
    n16 counter;

    bool odd;
    bool delay;
    n3   delayCounter;

    constexpr static u16 periodNTSC[2][6] = {
      { 7457, 7456, 7458, 7457,    1, 1, },
      { 7457, 7456, 7458, 7458, 7452, 1, },
    };
    constexpr static u16 periodPAL[2][6] = {
      { 8313, 8314, 8312, 8313,    1, 1, },
      { 8313, 8314, 8312, 8320, 8312, 1,},
    };
  };

  //apu.cpp
  auto clockQuarterFrame() -> void;
  auto clockHalfFrame() -> void;

  FrameCounter frame;

  MIDIEmitter midiEmitter;
  u32 midiMessages;
  u32 midiClocks;
  u32 bpsMidiMessages;
  u32 bpsCycles;
  u8 chanProgram[16];
  u8 chanCC[16][128];
  auto midiInit() -> void;
  auto midiReset() -> void;
  auto generateMidi() -> void;
  auto midiProgram(u8 chan, u8 program) -> void;
  auto midiCC(u8 chan, u8 controller, u8 value) -> void;

//unserialized:
  u16 pulseDAC[32];
  u16 dmcTriangleNoiseDAC[128][16][16];

  static const n16 dmcPeriodTableNTSC[16];
  static const n16 dmcPeriodTablePAL[16];
  static const n16 noisePeriodTableNTSC[16];
  static const n16 noisePeriodTablePAL[16];
};

extern APU apu;
