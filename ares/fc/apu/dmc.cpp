auto APU::DMC::start() -> void {
  if(lengthCounter == 0) {
    readAddress = 0x4000 + (addressLatch << 6);
    lengthCounter = (lengthLatch << 4) + 1;
    m.triggered = true;

    if (!dmaBufferValid)
      dmaDelayCounter = periodCounter & 1 ? 2 : 3;
  }
}

auto APU::DMC::stop() -> void {
  lengthCounter = 0;
}

auto APU::DMC::clock() -> n8 {
  n8 result = dacLatch;

  if(--periodCounter == 0) {
    if(sampleValid) {
      s32 delta = (((sample >> bitCounter) & 1) << 2) - 2;
      u32 data = dacLatch + delta;
      if((data & 0x80) == 0) dacLatch = data;
    }

    if(++bitCounter == 0) {
      if(dmaBufferValid) {
        sampleValid = true;
        sample = dmaBuffer;
        dmaBufferValid = false;

        if (lengthCounter > 0)
          cpu.dmcDMAPending();
      } else {
        sampleValid = false;
      }
    }

    periodCounter = Region::PAL() ? dmcPeriodTablePAL[period] : dmcPeriodTableNTSC[period];
  }

  if (dmaDelayCounter > 0 && --dmaDelayCounter == 0)
    cpu.dmcDMAPending();

  return result;
}

auto APU::DMC::power(bool reset) -> void {
  lengthCounter = 0;
  periodCounter = Region::PAL() ? dmcPeriodTablePAL[0] : dmcPeriodTableNTSC[0];
  irqPending = false;
  period = 0;
  irqEnable = 0;
  loopMode = 0;
  dacLatch = 0;
  addressLatch = 0;
  lengthLatch = 0;
  readAddress = 0;
  bitCounter = 0;
  dmaBufferValid = 0;
  dmaBuffer = 0;
  sampleValid = 0;
  sample = 0;

  // map sample hashes to functions that return MIDI data:
  sampleDescriptors.reset();
  samplesMissing.reset();

  // SMB3:
  {
    // kick drum:
    sampleDescriptors.insert(0x82A9954CEA45D76AULL, [](n4 period, u8& c, u8& n, u8& v){ c = 9; n = 36; v = 96; });
    // snare drum:
    sampleDescriptors.insert(0x5875F05F7B1D6EE7ULL, [](n4 period, u8& c, u8& n, u8& v){ c = 9; n = 38; v = 96; });
    // tom:
    sampleDescriptors.insert(0xF82E24ED29B23E6DULL, [](n4 period, u8& c, u8& n, u8& v){ c = 9; n = 47 + (period - 0xE); v = 96; });
    // high wood block:
    sampleDescriptors.insert(0x715DDEF53031FC1BULL, [](n4 period, u8& c, u8& n, u8& v){ c = 9; n = 77 - (period - 0xE); v = 96; });
    // timbale:
    sampleDescriptors.insert(0x14E85FA4D6211223ULL, [](n4 period, u8& c, u8& n, u8& v){ c = 9; n = 66 - (period - 0xE); v = 96; });

    // block explosion long
    sampleDescriptors.insert(0x530AD3FAABD3E6CDULL, [](n4 period, u8& c, u8& n, u8& v){ c = 9; n = 61; v = 96; });
    // block explosion short
    sampleDescriptors.insert(0x34EAE0720494FA43ULL, [](n4 period, u8& c, u8& n, u8& v){ c = 9; n = 61; v = 96; });

    // fortress timpani hits
    sampleDescriptors.insert(0x05B216622992F30DULL, [](n4 period, u8& c, u8& n, u8& v){ c = 9; n = 41 + (period - 0xE)*2; v = 96; });
  }

  // Super-C:
  {
    // kick drum:
    sampleDescriptors.insert(0x7CF96FBF8992F1B8ULL, [](n4 period, u8& c, u8& n, u8& v){ c = 9; n = 36; v = 96; });
    // snare drum:
    sampleDescriptors.insert(0x7CE12344BCC44AC8ULL, [](n4 period, u8& c, u8& n, u8& v){ c = 9; n = 38; v = 96; });

    // orchestra hits:
    //dmc: 0x1289EC66C4831269: a=71, l=2C, p=D NOT found
    //dmc: 0xB050AD75C62B7C7B: a=66, l=2C, p=D NOT found
    //dmc: 0x616ED59931F78D52: a=5B, l=2C, p=D NOT found
  }
}

auto APU::DMC::setDMABuffer(n8 data) -> void {
  dmaBuffer = data;
  dmaBufferValid = true;
  lengthCounter--;
  readAddress++;

  if (lengthCounter == 0) {
    if (loopMode) {
      readAddress = 0x4000 + (addressLatch << 6);
      lengthCounter = (lengthLatch << 4) + 1;
    } else if (irqEnable) {
      irqPending = true;
      apu.setIRQ();
    }
  }
}

auto APU::DMC::calculateMidi() -> void {
  if (m.triggered) {
    // sample started:

    // read the bytes of the sample and FNV-64a hash its contents:
    n16 a = (0x4000 + (addressLatch << 6));
    n16 l = (lengthLatch << 4) + 1;

    u64 h = 14695981039346656037ULL; // offset64 from fnv64a
    while (l-- != 0) {
      n8 b = cpu.readDebugger(0x8000 | a++);
      h ^= (u8)b;
      h *= 1099511628211ULL;
    }

    // look up sample mapping based on hash of its contents:
    auto maybeDesc = sampleDescriptors.find(h);
    if (maybeDesc) {
      maybeDesc.get()(period, m.noteChan, m.noteOn, m.noteVel);

      printf("dmc: 0x%016llX: a=%02X, l=%02X, p=%1X found\n", h, (u8)addressLatch, (u8)lengthLatch, (u8)period);
    } else {
      printf("dmc: 0x%016llX: a=%02X, l=%02X, p=%1X NOT found\n", h, (u8)addressLatch, (u8)lengthLatch, (u8)period);

      auto periodsMapMaybe = samplesMissing.find(h);
      if (!periodsMapMaybe) {
        samplesMissing.insert(h, {});
        periodsMapMaybe = samplesMissing.find(h);
      }
      if (!periodsMapMaybe.get().find(period)) {
        // convert DMC delta bits to PCM samples:

        string path = hex(h, 16, '0');
        path.append(".");
        path.append(hex(period, 1, '0'));
        path.append(".pcm");
        auto f = file_buffer(path, file::mode::write);

        n16 a = (0x4000 + (addressLatch << 6));
        n16 l = (lengthLatch << 4) + 1;
        n8 dacLatch = 0;
        while (l-- != 0) {
          n8 sample = cpu.readDebugger(0x8000 | a++);
          for (int i = 0; i < 8; i++) {
            s32 delta = (((sample >> i) & 1) << 2) - 2;
            u32 data = dacLatch + delta;
            if((data & 0x80) == 0) { dacLatch = data; }
            // convert to 16-bit PCM:
            u16 x = apu.dmcTriangleNoiseDAC[dacLatch][0][0];
            f.writel(x, 2);
          }
        }

        // also write a .rsrc.txt file for Reaper to import it:
        string rsrcPath = path;
        rsrcPath.append(".rsrc.txt");

        const double rates[16] = {
          4181.71, // 0x0
          4709.93, // 0x1
          5264.04, // 0x2
          5593.04, // 0x3
          6257.95, // 0x4
          7046.35, // 0x5
          7919.35, // 0x6
          8363.42, // 0x7
          9419.86, // 0x8
          11186.1, // 0x9
          12604.0, // 0xA
          13982.6, // 0xB
          16884.6, // 0xC
          21306.8, // 0xD
          24858.0, // 0xE
          33143.9, // 0xF
        };

        string rsrc;
        rsrc.append("SAMPLERATE ", (long)round(rates[period]), "\n");
        rsrc.append("CHANNELS 1\n");
        rsrc.append("BITSPERSAMPLE 16\n");
        rsrc.append("PREFPOS -1\n");
        rsrc.append("ENDIAN LITTLE\n");
        rsrc.append("BYTEOFFS 0\n");
        rsrc.append("BYTELEN 0\n");
        file::write(rsrcPath, rsrc);

        periodsMapMaybe.get().insert(period, true);
      }
    }

    m.lastAddressLatch = addressLatch;
    m.lastLengthLatch = lengthLatch;
    m.lastPeriod = period;
  } else if (!loopMode && lengthCounter == 0 && m.lastLengthCounter != 0) {
    // sample naturally stopped (length exhausted):
    m.noteOn = 0;
    m.noteVel = 0;
    //printf("dmc: stop  a=%02X, l=%02X, p=%1X\n", (u8)m.lastAddressLatch, (u8)m.lastLengthLatch, (u8)m.lastPeriod);
  } else if (m.triggeredStop) {
    // sample explicitly stopped:
    m.noteOn = 0;
    m.noteVel = 0;
    //printf("dmc: stop  a=%02X, l=%02X, p=%1X\n", (u8)m.lastAddressLatch, (u8)m.lastLengthLatch, (u8)m.lastPeriod);
  }

  m.triggered = false;
  m.triggeredStop = false;
  m.lastLengthCounter = lengthCounter;
}

auto APU::DMC::generateMidi(MIDIEmitter& emit) -> void {
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
