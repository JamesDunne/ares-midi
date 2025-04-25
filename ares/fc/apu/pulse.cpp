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

auto APU::Pulse::calculateMidi() -> void {
  u32 volume = envelope.volume();

  if (!sweep.checkPeriod() || length.counter == 0 || sweep.pulsePeriod < 0x008) {
    // silence:
    m.noteOn = 0;
    m.clocksSinceDutyChange = 0;
    return;
  }

  // audible note:
  double f = 1'789'773.0 / (16.0 * ((double)sweep.pulsePeriod + 1.0));
  // A0 = midi note 21; 27.5Hz
  double n = 12.0 * log2(f / 27.5) + 21.0;
  if (n > 127) {
    return;
  }

  // velocity (0..127):
  int v;
  if (volume == 0) {
    v = 0;
  } else {
    //v = 1024.0 / (15.0 / (double)volume + 10.0);
    v = 24.0 + 384.0 * 95.88 / (8128.0 / (volume*2.0) + 100.0);
  }

  // rate-limit duty changes:
  if (m.clocksSinceDutyChange > 0) m.clocksSinceDutyChange++;
  if (duty != m.lastDuty && m.noteOn != 0) {
    if (m.clocksSinceDutyChange < 65000) {
      // maintain note on existing channel
    } else {
      m.noteChan = m.chans[duty];
    }
    m.clocksSinceDutyChange = 1;
  } else {
    m.noteChan = m.chans[duty];
  }
  m.lastDuty = duty;

  // set desired midi state:
  m.applyNoteWheel(n);
  m.noteVel = v;
}

auto APU::Pulse::generateMidi(MIDIEmitter &emit) -> void {
  if (m.rateLimit()) return;

  u8 lastChan = m.lastChan;

  if (m.noteOn != m.lastNoteOn || m.noteChan != m.lastChan) {
    if (m.lastNoteOn != 0) {
      // note off:
      emit(0x80 | m.lastChan, m.lastNoteOn, 0x00);
      m.lastNoteOn = 0;
      m.lastFreq = 0;
    }
    if (m.noteOn != 0) {
      // note on:
      emit(0x90 | m.noteChan, m.noteOn, 96);
      m.lastNoteOn = m.noteOn;
      m.lastChan = m.noteChan;
      m.lastFreq = m.noteFreq;
    }
  }

  if (m.noteOn != 0) {
    // adjust pitch bend:
    if (m.noteWheel != m.lastWheel || m.noteChan != lastChan) {
      emit(0xE0 | m.noteChan, m.noteWheel & 0x7F, (m.noteWheel >> 7) & 0x7F);
      m.lastWheel = m.noteWheel;
      m.lastFreq = m.noteFreq;
    }
    
    // adjust channel volumes:
    if (m.noteVel != m.lastVel || m.noteChan != lastChan) {
      // channel volume:
      emit(0xB0 | m.noteChan, 0x07, m.noteVel);
      m.lastVel = m.noteVel;
    }
  }

  m.rateControl();
}
