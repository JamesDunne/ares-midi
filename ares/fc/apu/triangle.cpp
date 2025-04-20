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

auto APU::Triangle::generateMidi(MIDIEmitter &emit) -> void {
  if (length.counter == 0 || linearLengthCounter == 0) {
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

  // audible note:

  // velocity (0..127):
  int v = 96;

  double f = 1'789'773.0 / (32.0 * ((double)period + 1.0));
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
    m.noteChan = m.chans[0];
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
    m.noteChan = m.chans[0];
    m.noteVel = v;
    emit(0x90 | m.noteChan, m.noteOn, m.noteVel);
  }
}
