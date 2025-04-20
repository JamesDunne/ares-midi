auto APU::Pulse::clock() -> n8 {
  if(!sweep.checkPeriod()) return 0;
  if(length.counter == 0) return 0;

  static constexpr u32 dutyTable[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1},  //12.5%
    {0, 0, 0, 0, 0, 0, 1, 1},  //25.0%
    {0, 0, 0, 0, 1, 1, 1, 1},  //50.0%
    {1, 1, 1, 1, 1, 1, 0, 0},  //25.0% (negated)
  };
  n8 result = dutyTable[duty][dutyCounter] ? envelope.volume() : 0;
  if(sweep.pulsePeriod < 0x008) result = 0;

  if(--periodCounter == 0) {
    periodCounter = (sweep.pulsePeriod + 1) * 2;
    dutyCounter--;
  }

  return result;
}

auto APU::Pulse::power(bool reset) -> void {
  length.power(reset, false);
  envelope = {};
  sweep = {};

  periodCounter = 1;
  duty = 0;
  dutyCounter = 0;
  period = 0;
}

auto APU::Pulse::generateMidi(MIDIEmitter &emit) -> void {
  u32 volume = envelope.volume();

  if (volume == 0 || !sweep.checkPeriod() || length.counter == 0 || sweep.pulsePeriod < 0x008) {
    // silence:

    if (m.noteOn) {
      // note off:
      emit(0x80 | m.noteChan, m.noteOn, 0x00);
      m.noteOn = 0;
      m.noteVel = 0;
    }
    return;
  }

  // audible note:

  // velocity (0..127):
  int v;
  //v = 1024.0 / (15.0 / (double)volume + 10.0);
  v = 16.0 + 384.0 * 95.88 / (8128.0 / (volume*2.0) + 100.0);

  double f = 1'789'773.0 / (16.0 * ((double)sweep.pulsePeriod + 1.0));
  // A0 = midi note 21; 27.5Hz
  double n = 12.0 * log2(f / 27.5) + 21.0;
  if (n > 127) {
    return;
  }

  // nearest whole semitone:
  double k = round(n);
  // pitch difference in semitones (-0.5 <= b < +0.5):
  double b = (n - k);

  // midi note:
  u8 kn = (u8)(int)k;
  // pitch bend: (assuming +/- 2 semitone range)
  n14 wheel = 8192 + (b * 4096.0);

  if (!m.noteOn) {
    // new note:
    m.noteOn = kn;
    m.noteChan = m.chans[duty];
    emit(0x90 | m.noteChan, m.noteOn, 96);
  } else if ((m.noteOn != kn) || (m.noteChan != m.chans[duty])) {
    // changed note or duty:
    if (m.noteOn != 0) {
      // note off:
      emit(0x80 | m.noteChan, m.noteOn, 0x00);
    }

    // note on:
    m.noteOn = kn;
    m.noteChan = m.chans[duty];
    emit(0x90 | m.noteChan, m.noteOn, 96);
  }

  // adjust volume:
  if (m.noteVel != v) {
    // channel volume:
    m.noteVel = v;
    emit(0xB0 | m.noteChan, 0x07, m.noteVel);
  }

  // adjust pitch bend:
  if (m.noteWheel != wheel) {
    m.noteWheel = wheel;
    emit(0xE0 | m.noteChan, m.noteWheel & 0x7F, (m.noteWheel >> 7) & 0x7F);
  }
}
