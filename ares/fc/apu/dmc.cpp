auto APU::DMC::start() -> void {
  if(lengthCounter == 0) {
    readAddress = 0x4000 + (addressLatch << 6);
    lengthCounter = (lengthLatch << 4) + 1;
    if (lengthCounter > 1) {
      m.triggered = true;
      clocksSinceStart = 1;
    }

    if (!dmaBufferValid)
      dmaDelayCounter = periodCounter & 1 ? 2 : 3;
  }
}

auto APU::DMC::stop() -> void {
  if (lengthCounter != 0) {
    m.triggered = 0;
    m.triggeredStop = 1;
  }
  lengthCounter = 0;
}

auto APU::DMC::clock() -> n8 {
  n8 result = dacLatch;

  if (clocksSinceStart > 0) clocksSinceStart++;

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

  printf("loading `dmc.bml` from `%s`\n", Path::resources().data());
  if(auto fp = file::open({Path::resources(), "dmc.bml"}, file::mode::read)) {
    string markup;
    markup.resize(fp.size());
    fp.read(markup);
    auto list = BML::unserialize(markup);
    for (auto node : list) {
      u64 h = node["fnv64a"].string().natural();

      u8 ch = node["channel"].natural(9);
      u8 cp = node["program"].natural(0);
      u8 cv = node["volume"].natural(0x60);

      u8 v =  node["velocity"].natural(96);

      double n[16];
      n[0x0] = node["p0"].real(0.0);
      n[0x1] = node["p1"].real(0.0);
      n[0x2] = node["p2"].real(0.0);
      n[0x3] = node["p3"].real(0.0);
      n[0x4] = node["p4"].real(0.0);
      n[0x5] = node["p5"].real(0.0);
      n[0x6] = node["p6"].real(0.0);
      n[0x7] = node["p7"].real(0.0);
      n[0x8] = node["p8"].real(0.0);
      n[0x9] = node["p9"].real(0.0);
      n[0xA] = node["pA"].real(0.0);
      n[0xB] = node["pB"].real(0.0);
      n[0xC] = node["pC"].real(0.0);
      n[0xD] = node["pD"].real(0.0);
      n[0xE] = node["pE"].real(0.0);
      n[0xF] = node["pF"].real(0.0);

      sampleDescriptors.insert(h, [=](n4 p, u8& ch_o, u8& cp_o, u8& cv_o, double& n_o, u8& v_o){
        ch_o=ch;
        cp_o=cp;
        cv_o=cv;

        n_o=n[p];
        v_o=v;
      });
    }
  } else {
    printf("unable to load `dmc.bml`\n");
  }

#if 0
  // SMB3:
  {
    // kick drum:
    sampleDescriptors.insert(0x82A9954CEA45D76AULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=9; n=36; v=96; });
    // snare drum:
    sampleDescriptors.insert(0x5875F05F7B1D6EE7ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=9; n=38; v=96; });
    // tom:
    sampleDescriptors.insert(0xF82E24ED29B23E6DULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=9; n=47 + (p - 0xE); v=96; });
    // high wood block:
    sampleDescriptors.insert(0x715DDEF53031FC1BULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=9; n=77 - (p - 0xE); v=96; });
    // timbale:
    sampleDescriptors.insert(0x14E85FA4D6211223ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=9; n=66 - (p - 0xE); v=96; });

    // block explosion long
    sampleDescriptors.insert(0x530AD3FAABD3E6CDULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=9; n=61; v=96; });
    // block explosion short
    sampleDescriptors.insert(0x34EAE0720494FA43ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=9; n=61; v=96; });

    // fortress timpani hits
    sampleDescriptors.insert(0x05B216622992F30DULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=9; n=41 + (p - 0xE)*2; v=112; });
  }

  // Super-C:
  {
    // kick drum:
    sampleDescriptors.insert(0x7CF96FBF8992F1B8ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=9; n=36; v=96; });
    // snare drum:
    sampleDescriptors.insert(0x7CE12344BCC44AC8ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=9; n=38; v=96; });

    // toms:
    sampleDescriptors.insert(0x621CFEA6F56DECB0ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=9; n=array<u8[3]>{48,47,45}[0xF-p]; v=112; });

    // orchestra hits:
    sampleDescriptors.insert(0x479ac6554ae6ff14ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=10; n=38+12; v=112; /* (D-2) p=0xD */ });
    sampleDescriptors.insert(0x82e05ddd4aaaff9aULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=10; n=39+12; v=112; /* (D#2) p=0xD */ });
    sampleDescriptors.insert(0x02e6bcfdd42e1039ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=10; n=40+12; v=112; /* (E-2) p=0xD */ });
    sampleDescriptors.insert(0xb96362a05fb2bae8ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=10; n=41+12; v=112; /* (F-2) p=0xD */ });
    sampleDescriptors.insert(0x64d735a09b5ea750ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=10; n=43+12; v=112; /* (G-2) p=0xD */ });
    sampleDescriptors.insert(0xbbfc22343c6e44b8ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=10; n=44+12; v=112; /* (G#2) p=0xD */ });
    sampleDescriptors.insert(0xe183be8c0e51c7c7ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=10; n=45+12; v=112; /* (A-2) p=0xD */ });
    sampleDescriptors.insert(0x616ed59931f78d52ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=10; n=46+12; v=112; /* (A#2) p=0xD */ });
    sampleDescriptors.insert(0xb050ad75c62b7c7bULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=10; n=47+12; v=112; /* (B-2) p=0xD */ });
    sampleDescriptors.insert(0x1289ec66c4831269ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=10; n=48+12; v=112; /* (C-3) p=0xD */ });
  }

  // Contra:
  {
    // kick drum:
    sampleDescriptors.insert(0xCDA8A7E459A39D2EULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=9; n=36; v=96; });
    // snare drum:
    sampleDescriptors.insert(0x2A62B26DD9F0BA73ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){ c=9; n=38; v=96; });
  }
  
  // Fester's Quest:
  {
    // slap bass:
    sampleDescriptors.insert(0x0c18c4a8aa28f924ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){
      c=11; v=112;
      switch (p) {
        case 0x3: n=29+0; break;
        case 0x4: n=31+0; break;
        case 0x7: n=36+0; break;
        case 0x8: n=38+0; break;
        case 0xA: n=43+0; break;
        case 0xC: n=48+0; break;
        // unknown high note to detect
        default: n = 72+12; break;
      }
    });
    sampleDescriptors.insert(0x35f1ba1464004391ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){
      c=11; v=112;
      switch (p) {
        case 0x4: n=30+0; break;
        case 0x5: n=32+0; break;
        case 0x6: n=34+0; break;
        case 0xD: n=51+0; break;
        // unknown high note to detect
        default: n = 72+12; break;
      }
    });
    sampleDescriptors.insert(0x130afdd8a57fb3e8ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){
      c=11; v=112;
      switch (p) {
        case 0x8: n=39+0; break;
        case 0xB: n=46+0; break;
        // unknown high note to detect
        default: n = 72+12; break;
      }
    });
    sampleDescriptors.insert(0xd27417903aa611b5ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){
      c=11; v=112;
      switch (p) {
        case 0xC: n=50+0; break;
        // unknown high note to detect
        default: n = 72+12; break;
      }
    });

    // orchestra hits:
    sampleDescriptors.insert(0x2d1074160f8eba58ULL, [](n4 p, u8& c, u8& g, double& n, u8& v){
      c=10; v=112;
      switch (p) {
        case 0x9: n=29+12; break;
        case 0xB: n=33+12; break;
        // unknown high note to detect
        default: n = 72+12; break;
      }
    });
    sampleDescriptors.insert(0xdee92674676cfbdeULL, [](n4 p, u8& c, u8& g, double& n, u8& v){
      c=10; v=112;
      switch (p) {
        case 0xC: n=34+12; break;
        // unknown high note to detect
        default: n=72+12; break;
      }
    });
  }

  // one-time function to convert above lambdas into BML output:
  {
    Markup::Node list;
    for (auto pair : sampleDescriptors) {
      Markup::Node node("sample");
      u64 h = pair.key;
      u8  nc = 0;
      u8  ng = 0;
      u8  nv = 96;
      double nn[16];

      for (int p = 0; p < 16; p++) {
        pair.value(p, nc, ng, nn[p], nv);
      }

      node.append(std::move(Markup::Node("fnv64a", {"0x", hex(h, 16, '0')})));
      node.append(std::move(Markup::Node("channel", {nc} )));
      node.append(std::move(Markup::Node("program", {ng} )));
      node.append(std::move(Markup::Node("velocity", {nv} )));

      for (int p = 0; p < 16; p++) {
        node.append(std::move(Markup::Node({"p", hex(p, 1, '0').upcase()}, {nn[p]})));
      }

      list.append(node);
    }
    print(BML::serialize(list));
  }
#endif
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
  if (m.triggered && clocksSinceStart > 16) {
    // sample started:
    m.triggered = 0;

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
      double n;
      maybeDesc.get()(period, m.noteChan, m.chanProgram, m.chanVolume, n, m.noteVel);

      m.applyNoteWheel(n);

      //printf("dmc: 0x%016llX: a=%02X, l=%02X, p=%1X found\n", h, (u8)addressLatch, (u8)lengthLatch, (u8)period);
    } else {
      printf("dmc: 0x%016llX: a=%02X, l=%02X, p=%1X NOT found\n", h, (u8)addressLatch, (u8)lengthLatch, (u8)period);
    }

#if 1
    {
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
#endif

    m.lastAddressLatch = addressLatch;
    m.lastLengthLatch = lengthLatch;
    m.lastPeriod = period;
  }
  if (!loopMode && lengthCounter == 0 && m.lastLengthCounter != 0) {
    // sample naturally stopped (length exhausted):
    m.noteOn = 0;
    m.noteVel = 0;
    //printf("dmc: stop  a=%02X, l=%02X, p=%1X\n", (u8)m.lastAddressLatch, (u8)m.lastLengthLatch, (u8)m.lastPeriod);
  }
  if (m.triggeredStop) {
    // sample explicitly stopped:
    m.noteOn = 0;
    m.noteVel = 0;
    //printf("dmc: stop  a=%02X, l=%02X, p=%1X\n", (u8)m.lastAddressLatch, (u8)m.lastLengthLatch, (u8)m.lastPeriod);
  }

  m.triggeredStop = false;
  m.lastLengthCounter = lengthCounter;
}

auto APU::DMC::generateMidi(MIDIEmitter& emit) -> void {
  if (m.rateLimit()) return;

  if ((m.noteOn != m.lastNoteOn) || (m.noteVel != m.lastVel) || (m.noteNew)) {
    if (m.lastNoteOn != 0) {
      // note off:
      emit(0x80 | m.lastChan, m.lastNoteOn, 0x00);
      m.lastNoteOn = 0;
    }
    if (m.noteOn != 0 && m.noteVel != 0 || m.noteNew) {
      // emit will prevent redundant updates:
      emit(0xC0 | m.noteChan, m.chanProgram, 0);
      emit(0xB0 | m.noteChan, 0x07, m.chanVolume);

      // note on:
      emit(0x90 | m.noteChan, m.noteOn, m.noteVel);
      m.lastChan = m.noteChan;
      m.lastNoteOn = m.noteOn;
      m.lastVel = m.noteVel;
      m.noteNew = 0;
    }
  }

  m.rateControl();
}
