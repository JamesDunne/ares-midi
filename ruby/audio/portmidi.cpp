extern "C" {
#include <portmidi.h>
#include <porttime.h>
}

namespace MIDI {

PortMidiStream* stream = nullptr;

auto init() -> void {
  static bool inited = false;

  PmError err;

  if (inited) {
    Pm_Terminate();
    inited = false;
  }

  err = Pm_Initialize();
  if (err != pmNoError) {
    NSLog(@"portmidi: Pm_Initialize(): %s", Pm_GetErrorText(err));
    return;
  }
  NSLog(@"portmidi: Pm_Initialize()");

  int devCount = Pm_CountDevices();
  for (int i = 0; i < devCount; i++) {
    auto d = Pm_GetDeviceInfo(i);
    NSLog(@"portmidi: [%d]: in=%d, out=%d, interf='%s', name='%s'",
      i,
      d->input,
      d->output,
      d->interf,
      d->name
    );
  }

  // prefer USB Midi:
  PmDeviceID devOut = pmNoDevice;
  for (int i = 0; i < devCount; i++) {
    auto d = Pm_GetDeviceInfo(i);
    if (d->output) {
      if (strncmp("USB Midi", d->name, 8) == 0) {
        devOut = i;
      }
    }
  }

  // fallback to IAC:
  if (devOut == pmNoDevice) {
    for (int i = 0; i < devCount; i++) {
      auto d = Pm_GetDeviceInfo(i);
      if (d->output) {
        if (strncmp("IAC Driver Bus 1", d->name, 16) == 0) {
          devOut = i;
        }
      }
    }
  }

  //PmDeviceID devOut = Pm_GetDefaultOutputDeviceID();

  err = Pm_OpenOutput(
    &stream,
    devOut,
    nullptr, // void *outputDriverInfo
    2000,    // bufferSize
    nullptr, // time_proc
    nullptr, // time_info
    0        // latency
  );
  if (err != pmNoError) {
    NSLog(@"portmidi: Pm_OpenOutput(%d): %s", devOut, Pm_GetErrorText(err));
    return;
  }

  NSLog(@"portmidi: Pm_OpenOutput(%d)", devOut);

  inited = true;
}

auto writeShort(s32 msg) -> void {
  static PtTimestamp lastTs = 0;
  const char *spaces = "                                                                                                                ";

  if (!stream) {
    return;
  }

  PmError err;
  int retry = 100;

  PtTimestamp ts;
  if (msg == 0xFFFFFFFF) {
//    NSLog(@"midi: delay");
    PtTimestamp lastTs = Pt_Time();
    do {
      ts = Pt_Time();
    } while ((ts - lastTs) <= 2);
    lastTs = ts;
    return;
  }

  ts = Pt_Time();

//  NSLog(@"[%9u] %.*s%02X%02X%02X", ts, (msg&0x0F) * 7, spaces, msg&0xFF, (msg>>8)&0xFF, (msg>>16)&0xFF);

  do {
    err = Pm_WriteShort(stream, 0, msg);
    if (err != pmNoError) {
      NSLog(@"portmidi: Pm_WriteShort err=%d: '%s'", err, Pm_GetErrorText(err));
    }
    if (retry-- <= 0) break;
  } while (err != pmNoError);

  if (err != pmNoError) {
    NSLog(@"portmidi: Pm_WriteShort err=%d: '%s'; no more retries", err, Pm_GetErrorText(err));
  }

  lastTs = ts;
}

}
