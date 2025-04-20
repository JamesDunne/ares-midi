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

  // integer part:
  double k = round(n);
  // fractional part (-0.5 <= n < +0.5):
  double b = (n - k);

  // midi note:
  u8 kn = (u8)(int)k;
  // pitch bend: (assuming +/- 2 semitone range)
  n14 wheel = 8192 + (b * 4096.0);

  // set desired midi state:
  m.noteOn = kn;
  m.noteChan = m.chans[0];
  m.noteVel = v;
  m.noteWheel = wheel;
}

auto APU::Triangle::generateMidi(MIDIEmitter &emit) -> void {
  // audible note:
  if (m.noteOn != m.lastNoteOn) {
    // note off:
    emit(0x80 | m.noteChan, m.lastNoteOn, 0x00);
  }

  if (m.noteOn == 0) {
    goto done;
  }
  if (m.noteOn != m.lastNoteOn) {
    emit(0x90 | m.noteChan, m.noteOn, 96);
  }

  // adjust pitch bend:
  if (m.noteWheel != m.lastWheel) {
    emit(0xE0 | m.noteChan, m.noteWheel & 0x7F, (m.noteWheel >> 7) & 0x7F);
  }

done:
  m.lastNoteOn = m.noteOn;
  m.lastWheel = m.noteWheel;
}
