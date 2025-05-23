#include <fc/fc.hpp>
#include <math.h>

namespace ares::Famicom {

APU apu;
#include "length.cpp"
#include "envelope.cpp"
#include "sweep.cpp"
#include "pulse.cpp"
#include "triangle.cpp"
#include "noise.cpp"
#include "dmc.cpp"
#include "framecounter.cpp"
#include "serialization.cpp"

auto APU::load(Node::Object parent) -> void {
  node = parent->append<Node::Object>("APU");

  stream = node->append<Node::Audio::Stream>("PSG");
  stream->setChannels(1);
  stream->setFrequency(u32(system.frequency() + 0.5) / rate());
  stream->addHighPassFilter(   90.0, 1);
  stream->addHighPassFilter(  440.0, 1);
  stream->addLowPassFilter (14000.0, 1);

  for(u32 amp : range(32)) {
    if(amp == 0) {
      pulseDAC[amp] = 0;
    } else {
      pulseDAC[amp] = 32768.0 * 95.88 / (8128.0 / amp + 100.0);
    }
  }

  for(u32 dmcAmp : range(128)) {
    for(u32 triangleAmp : range(16)) {
      for(u32 noiseAmp : range(16)) {
        if(dmcAmp == 0 && triangleAmp == 0 && noiseAmp == 0) {
          dmcTriangleNoiseDAC[dmcAmp][triangleAmp][noiseAmp] = 0;
        } else {
          dmcTriangleNoiseDAC[dmcAmp][triangleAmp][noiseAmp]
          = 32768.0 * 159.79 / (100.0 + 1.0 / (triangleAmp / 8227.0 + noiseAmp / 12241.0 + dmcAmp / 22638.0));
        }
      }
    }
  }

  midi = node->append<Node::Audio::MIDI>("MIDI");
  midiEmitter = MIDIEmitter([&](u8 cmd, u8 d1, u8 d2){
    d1 &= 0x7F;
    d2 &= 0x7F;

    // prevent sending redundant updates:
    if ((cmd & 0xF0) == 0xB0) {
      if (chanCC[cmd & 0x0F][d1] == d2) {
        return;
      }

      chanCC[cmd & 0x0F][d1] = d2;
    } else if ((cmd & 0xF0) == 0xC0) {
      if (chanProgram[cmd & 0x0F] == d1) {
        return;
      }

      chanProgram[cmd & 0x0F] = d1;
    }

    midi->writeShort(cmd, d1, d2);

    midiMessages++;
    bpsMidiMessages++;
  });

  midiInit();
}

auto APU::unload() -> void {
  midiReset();

  midiEmitter.reset();
  node->remove(midi);

  node->remove(stream);
  stream.reset();
  node.reset();
}

auto APU::main() -> void {
  u32 pulseOutput = pulse1.clock() + pulse2.clock();
  u32 triangleOutput = triangle.clock();
  u32 noiseOutput = noise.clock();
  u32 dmcOutput = dmc.clock();
  frame.main();

  s32 output = 0;
  output += pulseDAC[pulseOutput];
  output += dmcTriangleNoiseDAC[dmcOutput][triangleOutput][noiseOutput];

  stream->frame(sclamp<16>(output) / 32768.0);

  generateMidi();
  if (bpsCycles++ >= 1'789'773UL) {
    // 30 bits per message (1 start bit, 8 data bit, 1 stop bit)
    printf("midi: %5lu bps\n", bpsMidiMessages * 30UL);
    bpsCycles = 0UL;
    bpsMidiMessages = 0UL;
  }

  tick();
}

auto APU::tick() -> void {
  Thread::step(rate());
  Thread::synchronize(cpu);
}

auto APU::setIRQ() -> void {
  cpu.apuLine(frame.irqPending | dmc.irqPending);
}

auto APU::power(bool reset) -> void {
  Thread::create(system.frequency(), {&APU::main, this});

  pulse1.power(reset);
  pulse2.power(reset);
  triangle.power(reset);
  noise.power(reset);
  dmc.power(reset);
  frame.power(reset);

  midiInit();

  setIRQ();
}

auto APU::readIO(n16 address) -> n8 {
  n8 data = cpu.io.openBus;

  switch(address) {

  case 0x4015: {
    data.bit(0) = (bool)pulse1.length.counter;
    data.bit(1) = (bool)pulse2.length.counter;
    data.bit(2) = (bool)triangle.length.counter;
    data.bit(3) = (bool)noise.length.counter;
    data.bit(4) = (bool)dmc.lengthCounter;
    data.bit(5) = 0;
    data.bit(6) = frame.irqPending;
    data.bit(7) = dmc.irqPending;
    frame.irqPending = false;
    setIRQ();
    return data;
  }

  }

  return data;
}

auto APU::writeIO(n16 address, n8 data) -> void {
  switch(address) {

  case 0x4000: {
    pulse1.envelope.speed = data.bit(0,3);
    pulse1.envelope.useSpeedAsVolume = data.bit(4);
    pulse1.envelope.loopMode = data.bit(5);
    pulse1.length.setHalt(frame.lengthClocking(), data.bit(5));
    pulse1.duty = data.bit(6,7);
    return;
  }

  case 0x4001: {
    pulse1.sweep.shift = data.bit(0,2);
    pulse1.sweep.decrement = data.bit(3);
    pulse1.sweep.period = data.bit(4,6);
    pulse1.sweep.enable = data.bit(7);
    pulse1.sweep.reload = true;
    return;
  }

  case 0x4002: {
    pulse1.period.bit(0,7) = data.bit(0,7);
    pulse1.sweep.pulsePeriod.bit(0,7) = data.bit(0,7);
    pulse1.m.periodWriteCountdown = 256;
    return;
  }

  case 0x4003: {
    pulse1.period.bit(8,10) = data.bit(0,2);
    pulse1.sweep.pulsePeriod.bit(8,10) = data.bit(0,2);
    pulse1.m.periodWriteCountdown = 256;

    pulse1.dutyCounter = 0;
    pulse1.envelope.reloadDecay = true;

    pulse1.length.setCounter(frame.lengthClocking(), data.bit(3,7));
    return;
  }

  case 0x4004: {
    pulse2.envelope.speed = data.bit(0,3);
    pulse2.envelope.useSpeedAsVolume = data.bit(4);
    pulse2.envelope.loopMode = data.bit(5);
    pulse2.length.setHalt(frame.lengthClocking(), data.bit(5));
    pulse2.duty = data.bit(6,7);
    return;
  }

  case 0x4005: {
    pulse2.sweep.shift = data.bit(0,2);
    pulse2.sweep.decrement = data.bit(3);
    pulse2.sweep.period = data.bit(4,6);
    pulse2.sweep.enable = data.bit(7);
    pulse2.sweep.reload = true;
    return;
  }

  case 0x4006: {
    pulse2.period.bit(0,7) = data.bit(0,7);
    pulse2.sweep.pulsePeriod.bit(0,7) = data.bit(0,7);
    pulse2.m.periodWriteCountdown = 256;
    return;
  }

  case 0x4007: {
    pulse2.period.bit(8,10) = data.bit(0,2);
    pulse2.sweep.pulsePeriod.bit(8,10) = data.bit(0,2);
    pulse2.m.periodWriteCountdown = 256;

    pulse2.dutyCounter = 0;
    pulse2.envelope.reloadDecay = true;

    pulse2.length.setCounter(frame.lengthClocking(), data.bit(3,7));
    return;
  }

  case 0x4008: {
    triangle.linearLength = data.bit(0,6);
    triangle.length.setHalt(frame.lengthClocking(), data.bit(7));
    return;
  }

  case 0x400a: {
    triangle.period.bit(0,7) = data.bit(0,7);
    triangle.m.periodWriteCountdown = 256;
    return;
  }

  case 0x400b: {
    triangle.period.bit(8,10) = data.bit(0,2);
    triangle.m.periodWriteCountdown = 256;

    triangle.reloadLinear = true;

    triangle.length.setCounter(frame.lengthClocking(), data.bit(3,7));
    return;
  }

  case 0x400c: {
    noise.envelope.speed = data.bit(0,3);
    noise.envelope.useSpeedAsVolume = data.bit(4);
    noise.length.setHalt(frame.lengthClocking(), data.bit(5));
    noise.envelope.loopMode = data.bit(5);
    noise.dump();
    return;
  }

  case 0x400e: {
    noise.period = data.bit(0,3);
    noise.shortMode = data.bit(7);
    noise.dump();
    return;
  }

  case 0x400f: {
    noise.envelope.reloadDecay = true;

    noise.length.setCounter(frame.lengthClocking(), data.bit(3, 7));
    noise.dump();
    return;
  }

  case 0x4010: {
    dmc.period = data.bit(0,3);
    dmc.loopMode = data.bit(6);
    dmc.irqEnable = data.bit(7);

    dmc.irqPending = dmc.irqPending && dmc.irqEnable && !dmc.loopMode;
    setIRQ();
    return;
  }

  case 0x4011: {
    dmc.dacLatch = data.bit(0,6);
    return;
  }

  case 0x4012: {
    dmc.addressLatch = data;
    return;
  }

  case 0x4013: {
    dmc.lengthLatch = data;
    return;
  }

  case 0x4015: {
    pulse1.length.setEnable(data.bit(0));
    pulse2.length.setEnable(data.bit(1));
    triangle.length.setEnable(data.bit(2));
    noise.length.setEnable(data.bit(3));
    noise.dump();
    if (data.bit(4) == 0) dmc.stop();
    if (data.bit(4) == 1) dmc.start();

    dmc.irqPending = false;
    setIRQ();
    return;
  }

  case 0x4017: return frame.write(data);
  }
}

auto APU::clockQuarterFrame() -> void {
  pulse1.envelope.clock();
  pulse2.envelope.clock();
  triangle.clockLinearLength();
  noise.envelope.clock();
}

auto APU::clockHalfFrame() -> void {
  pulse1.length.main();
  pulse1.sweep.clock(0);
  pulse2.length.main();
  pulse2.sweep.clock(1);
  triangle.length.main();
  noise.length.main();

  pulse1.envelope.clock();
  pulse2.envelope.clock();
  triangle.clockLinearLength();
  noise.envelope.clock();
}

const n16 APU::noisePeriodTableNTSC[16] = {
  4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068,
};

const n16 APU::noisePeriodTablePAL[16] = {
  4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708,  944, 1890, 3778,
};

const n16 APU::dmcPeriodTableNTSC[16] = {
  428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54,
};

const n16 APU::dmcPeriodTablePAL[16] = {
  398, 354, 316, 298, 276, 236, 210, 198, 176, 148, 132, 118,  98, 78, 66, 50,
};

auto APU::midiReset() -> void {
  // note off on all channels:
  if (pulse1.m.lastNoteOn) {
    midiEmitter(0x80 | pulse1.m.chans[0], pulse1.m.lastNoteOn, 0x00);
    midiEmitter(0x80 | pulse1.m.chans[1], pulse1.m.lastNoteOn, 0x00);
    midiEmitter(0x80 | pulse1.m.chans[2], pulse1.m.lastNoteOn, 0x00);
    midiEmitter(0x80 | pulse1.m.chans[3], pulse1.m.lastNoteOn, 0x00);
  }
  if (pulse2.m.lastNoteOn) {
    midiEmitter(0x80 | pulse2.m.chans[0], pulse2.m.lastNoteOn, 0x00);
    midiEmitter(0x80 | pulse2.m.chans[1], pulse2.m.lastNoteOn, 0x00);
    midiEmitter(0x80 | pulse2.m.chans[2], pulse2.m.lastNoteOn, 0x00);
    midiEmitter(0x80 | pulse2.m.chans[3], pulse2.m.lastNoteOn, 0x00);
  }
  if (triangle.m.lastNoteOn) {
    midiEmitter(0x80 | triangle.m.noteChan, triangle.m.lastNoteOn, 0x00);
  }
  if (noise.m.lastNoteOn) {
    midiEmitter(0x80 | noise.m.noteChan, noise.m.lastNoteOn, 0x00);
  }
  if (dmc.m.lastNoteOn) {
    midiEmitter(0x80 | dmc.m.lastChan, dmc.m.lastNoteOn, 0x00);
  }

  // all notes off:
  for (int i = 0; i < 16; i++) {
    midi->delay();
    midiEmitter(0xB0 | i, 123, 0x00);
    midi->delay();
  }

  // reset all controllers:
  for (int i = 0; i < 16; i++) {
    midi->delay();
    midiEmitter(0xB0 | i, 121, 0x00);
    midi->delay();
    midi->delay();
    midi->delay();
  }

  midiMessages = 0;
}

auto APU::midiInit() -> void {
  midiClocks = 0;
  midiMessages = 0;

  // reset MIDI:
  midiReset();

  // initialize MidiStates:
  pulse1.m = {};
  pulse2.m = {};
  triangle.m = {};
  noise.m = {};
  dmc.m = {};

  for (int c = 0; c < 16; c++) {
    chanProgram[c] = 255;
    for (int n = 0; n < 128; n++) {
      chanCC[c][n] = 255;
    }
  }

  u8 dutyPCs[4] = {
    81, // sawtooth lead
    63, // synth brass 2
    80, // square wave lead
    87  // bass lead
  };
  for (int n = 0; n < 4; n++) {
    pulse1.m.chans[n] = n;
    pulse2.m.chans[n] = n+4;

    midi->delay();
    midiProgram(pulse1.m.chans[n], dutyPCs[n]);
    midi->delay();
    midiCC(pulse1.m.chans[n], 0x07, 0); // vol
    midi->delay();
    midiCC(pulse1.m.chans[n], 0x0A, 0x28); // pan

    midi->delay();
    midiProgram(pulse2.m.chans[n], dutyPCs[n]);
    midi->delay();
    midiCC(pulse2.m.chans[n], 0x07, 0); // vol
    midi->delay();
    midiCC(pulse2.m.chans[n], 0x0A, 0x58); // pan
  }

  triangle.m.chans[0] = 8;
  midi->delay();
  midiProgram(triangle.m.chans[0], 33); // fingered bass
  midi->delay();
  midiCC(triangle.m.chans[0], 0x07, 0x48); // vol
  midi->delay();
  midiCC(triangle.m.chans[0], 0x0A, 0x40); // pan

  noise.m.chans[0] = 9;
  midi->delay();
  midiProgram(noise.m.chans[0], 0); // standard kit
  midi->delay();
  midiCC(noise.m.chans[0], 0x07, 0x60); // vol

#if 0
  // DMC orchestra hit
  midi->delay();
  midiEmitter(0xC0 | 10, 55, 0); // orchestra hit
  midi->delay();
  midiEmitter(0xB0 | 10, 0x07, 0x60); // vol

  // DMC slap bass
  midi->delay();
  midiEmitter(0xC0 | 11, 37, 0); // slap bass 2
  midi->delay();
  midiEmitter(0xB0 | 11, 0x07, 0x60); // vol
#endif

  midiMessages = 0;
}

auto APU::MidiState::rateLimit() -> bool {
  // MIDI message takes 0.000960000 sec (= 3 bytes / 3,125 bytes / sec)
  //    APU cycle takes 0.000000560 sec
  // means we can send one MIDI message every 1714.28571429 APU clock cycles:
  if (clocks++ < 1715*messages) {
    return true;
  }

  // start counting MIDI messages emitted:
  apu.midiMessages = 0;
  return false;
}

auto APU::MidiState::rateControl() -> void {
  // record how many MIDI messages were emitted:
  messages = apu.midiMessages;
  // start counting APU clock cycles until we can generate more MIDI messages:
  clocks = 0;
}

auto APU::MidiState::applyNoteWheel(double n) -> void {
  double k;

  if (noteOn != 0) {
    // continuing note:

    // try to maintain current note:
    k = (double)noteOn;
    if (fabs(n - noteFreq) >= 0.8) {
      // changed too much from last frequency, start a new note:
      k = round(n);
    } else if (fabs(n - k) >= 2.0) {
      // if we've strayed out of pitch bend range, start a new note:
      k = round(n);
    }
  } else {
    // start a new note:
    k = round(n);
  }

  // pitch difference in semitones (-2.0 <= b < +2.0):
  double b = (n - k);
  if (fabs(b) < 0.15) {
    b = 0.0;
  }

  // midi note:
  noteOn = (u8)(int)k;
  // pitch bend: (assuming +/- 2 semitone range)
  noteWheel = 8192 + (b * 4096.0);
  // remember actual frequency:
  noteFreq = n;
}

auto APU::midiProgram(u8 chan, u8 program) -> void {
  midiEmitter(0xC0 | (chan & 0x0F), program & 0x7F, 0);
}

auto APU::midiCC(u8 chan, u8 controller, u8 value) -> void {
  midiEmitter(0xB0 | (chan & 0x0F), controller & 0x7F, value & 0x7F);
}

auto APU::generateMidi() -> void {
  // always update latest desired midi state:
  pulse1.calculateMidi();
  pulse2.calculateMidi();
  triangle.calculateMidi();
  noise.calculateMidi();
  dmc.calculateMidi();

  // emit messages to transition to desired midi state:
  dmc.generateMidi( midiEmitter );
  noise.generateMidi( midiEmitter );
  triangle.generateMidi( midiEmitter );
  pulse1.generateMidi( midiEmitter );
  pulse2.generateMidi( midiEmitter );
}

}
