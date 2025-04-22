auto APU::Noise::clock() -> n8 {
  if(length.counter == 0) return 0;

  n8 result = (lfsr & 1) ? envelope.volume() : 0;

  if(--periodCounter == 0) {
    u32 feedback;

    if(shortMode) {
      feedback = ((lfsr >> 0) & 1) ^ ((lfsr >> 6) & 1);
    } else {
      feedback = ((lfsr >> 0) & 1) ^ ((lfsr >> 1) & 1);
    }

    lfsr = (lfsr >> 1) | (feedback << 14);
    periodCounter = Region::PAL() ? apu.noisePeriodTablePAL[period] : apu.noisePeriodTableNTSC[period];
  }

  return result;
}

auto APU::Noise::power(bool reset) -> void {
  length.power(reset, false);
  envelope = {};

  periodCounter = 1;
  period = 0;
  shortMode = 0;
  lfsr = 1;
}

auto APU::Noise::calculateMidi() -> void {
  if (length.counter == 0) {
    // silence:
    m.noteOn = 0;
    m.noteVel = 0;
    m.lastPeriod = period;
    m.lastCycleVolume = 0;
    return;
  }

  u32 volume = envelope.volume();
  if (volume == 0) {
    // silence:
    m.noteOn = 0;
    m.noteVel = 0;
    m.lastPeriod = period;
    m.lastCycleVolume = 0;
    return;
  }

  // audible note:
  bool trigger = false;
  if (period != m.lastPeriod) {
    trigger = true;
  }
  m.lastPeriod = period;
  if (volume > m.lastCycleVolume) {
    trigger = true;
  }
  m.lastCycleVolume = volume;

  // prevent retriggering:
  if (!trigger) return;

  // trigger new note:
  m.noteNew = 1;
  m.lastTriggeredVolume = volume;

  // velocity (0..127):
  int v;
  //v = 1024.0 / (15.0 / (double)volume + 10.0);
  v = 16.0 + 384.0 * 95.88 / (8128.0 / (volume*2.0) + 100.0);

  // midi note:
  u8 kn;

  if (period >= 0xB) {
    // bass drum:
    kn = 35 | (period&1);
  } else if (period >= 0x6) {
    // snare drum:
    kn = 38 | ((period&1)<<1);
  } else if (period >= 0x4) {
    // crash:
    kn = (period&1) ? 49 : 57;
  } else if (period >= 0x2) {
    // closed hi-hit:
    kn = 42;
  } else {
    if (!envelope.useSpeedAsVolume) {
      // open triangle: (for Solstice)
      kn = 81;
    } else {
      // open hi-hat:
      kn = 46;
    }
  }

  // note on:
  m.noteOn = kn;
  m.noteChan = m.chans[0];
  m.noteVel = v;
}

auto APU::Noise::generateMidi(MIDIEmitter &emit) -> void {
  if ((m.noteOn != m.lastNoteOn) || (m.noteVel != m.lastVel) || (m.noteNew)) {
    if (m.lastNoteOn != 0) {
      // note off:
      emit(0x80 | m.noteChan, m.lastNoteOn, 0x00);
      m.lastNoteOn = 0;
    }
    if (m.noteOn != 0 && m.noteVel != 0 || m.noteNew) {
      // note on:
      emit(0x90 | m.noteChan, m.noteOn, m.noteVel);
      m.lastNoteOn = m.noteOn;
      m.lastVel = m.noteVel;
      m.noteNew = 0;
    }
  }
}
