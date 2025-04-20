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

auto APU::Noise::generateMidi(MIDIEmitter &emit) -> void {
  u32 volume = envelope.volume();

  if (volume == 0 || length.counter == 0 || period == 0) {
    // silence:

    if (m.noteOn) {
      // note off:
      emit(0x80 | m.noteChan, m.noteOn, 0x00);
      m.noteOn = 0;
      m.noteVel = 0;
    }

    m.lastPeriod = period;
    return;
  }

  // audible note:

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
  } else {
    // closed hi-hit:
    kn = 42;
  }

  if (!m.noteOn) {
    // note on:
    m.noteOn = kn;
    m.noteChan = m.chans[0];
    m.noteVel = v;
    emit(0x90 | m.noteChan, m.noteOn, m.noteVel);
  } else if (m.noteOn != kn || v > m.noteVel) {
    if (period >= m.lastPeriod) {
      // change note or repeat note if increased velocity:
      if (m.noteOn != 0) {
        // note off:
        emit(0x80 | m.noteChan, m.noteOn, 0x00);
      }

      // note on:
      m.noteOn = kn;
      m.noteChan = m.chans[0];
      m.noteVel = v;
      emit(0x90 | m.noteChan, m.noteOn, m.noteVel);
    }
  }

  m.noteVel = v;
  m.lastPeriod = period;
}
