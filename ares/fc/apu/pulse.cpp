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
  if (envelope.volume() == 0) {
    // silence:

    if (m.noteOn) {
      // aftertouch off:
      emit(0xA0 | m.noteChan, m.noteOn, 0x00);
      // note off:
      emit(0x80 | m.noteChan, m.noteOn, 0x00);
      m.noteOn = 0;
      m.noteVel = 0;
    }
    return;
  }
  if (sweep.pulsePeriod < 8) {
    // silence:

    if (m.noteOn) {
      // aftertouch off:
      emit(0xA0 | m.noteChan, m.noteOn, 0x00);
      // note off:
      emit(0x80 | m.noteChan, m.noteOn, 0x00);
      m.noteOn = 0;
      m.noteVel = 0;
    }
    return;
  }

//  if(!sweep.checkPeriod()) return 0;
//  if(length.counter == 0) return 0;

  // audible note:

  // velocity (0..127):
  int v = 1024.0/(15.0 / (double)envelope.volume() + 10.0);
//double v = 24449.4 / (8128.0 / (envelope.volume()*2.0) + 100.0);

  double f = 1'789'773.0 / (16.0 * ((double)sweep.pulsePeriod + 1.0));
  // A0 = midi note 21; 27.5Hz
  double n = 12.0 * log2(f / 27.5) + 21.0;
  if (n > 127) {
    return;
  }

  // integer part:
  double k;
  // fractional part:
  double b = modf(n, &k);

  u8 kn = (u8)(int)k;
  if (!m.noteOn) {
    // note on:
    m.noteOn = kn;
    m.noteChan = m.chans[duty];
    m.noteVel = v;
    emit(0x90 | m.noteChan, m.noteOn, m.noteVel);
  } else if (m.noteOn != kn) {
    if (m.noteOn != 0) {
      // aftertouch off:
      emit(0xA0 | m.noteChan, m.noteOn, 0x00);
      // note off:
      emit(0x80 | m.noteChan, m.noteOn, 0x00);
    }

    // note on:
    m.noteOn = kn;
    m.noteChan = m.chans[duty];
    m.noteVel = v;
    emit(0x90 | m.noteChan, m.noteOn, m.noteVel);
  } else if (m.noteVel != v) {
    if (v <= 1) {
      // aftertouch off:
      emit(0xA0 | m.noteChan, m.noteOn, 0x00);
      // note off:
      emit(0x80 | m.noteChan, m.noteOn, 0x00);
      m.noteVel = 0;
      m.noteOn = 0;
    } else {
      // aftertouch:
      emit(0xA0 | m.noteChan, m.noteOn, v);
      m.noteVel = v;
    }
  }
}
