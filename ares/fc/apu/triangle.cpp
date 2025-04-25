auto APU::Triangle::clockLinearLength() -> void {
  if(reloadLinear) {
    linearLengthCounter = linearLength;
  } else if(linearLengthCounter) {
    linearLengthCounter--;
  }

  if(length.halt == 0) reloadLinear = false;
}

auto APU::Triangle::clock() -> n8 {
  n8 result = stepCounter & 0x0f;
  if((stepCounter & 0x10) == 0) result ^= 0x0f;
  if(length.counter == 0 || linearLengthCounter == 0) return result;

  if(--periodCounter == 0) {
    stepCounter++;
    periodCounter = period + 1;
  }

  return result;
}

auto APU::Triangle::power(bool reset) -> void {
  length.power(reset, true);

  periodCounter = 1;
  linearLength = 0;
  period = 0;
  stepCounter = 16;
  linearLengthCounter = 0;
  reloadLinear = 0;
}

auto APU::Triangle::calculateMidi() -> void {
  // don't sample the period too early when writes are spread across two registers and multiple clocks between:
  if (m.periodWriteCountdown > 0) {
    m.periodWriteCountdown--;
    return;
  }

  // velocity (0..127):
  int v = 96;

  if (length.counter == 0 || linearLengthCounter == 0 || period <= 1) {
    // silence:
    m.noteOn = 0;
    m.noteChan = m.chans[0];
    m.noteVel = v;
    return;
  }

  // audible note:

  double f = 1'789'773.0 / (32.0 * ((double)period + 1.0));
  // A0 = midi note 21; 27.5Hz
  double n = 12.0 * log2(f / 27.5) + 21.0;
  if (n > 127) {
    return;
  }

  // set desired midi state:
  m.applyNoteWheel(n);
  m.noteChan = m.chans[0];
  m.noteVel = v;
}

auto APU::Triangle::generateMidi(MIDIEmitter &emit) -> void {
  if (m.rateLimit()) return;

  if (m.noteOn != m.lastNoteOn) {
    if (m.lastNoteOn != 0) {
      // note off:
      emit(0x80 | m.noteChan, m.lastNoteOn, 0x00);
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
    if (m.noteWheel != m.lastWheel) {
      emit(0xE0 | m.noteChan, m.noteWheel & 0x7F, (m.noteWheel >> 7) & 0x7F);
      m.lastWheel = m.noteWheel;
      m.lastFreq = m.noteFreq;
    }
  }

  m.rateControl();
}
