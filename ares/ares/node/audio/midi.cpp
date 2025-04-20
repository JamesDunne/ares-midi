auto MIDI::writeShort(u8 cmd, u8 data1, u8 data2) -> void {
  _shortMsg = (((s32)data2 << 16) & 0xFF0000) |
              (((s32)data1 <<  8) &   0xFF00) |
               ((s32)cmd & 0xFF);

  platform->midi(shared());
}

auto MIDI::delay() -> void {
  _shortMsg = 0xFFFFFFFF;
  platform->midi(shared());
}

auto MIDI::readShort() -> s32 {
  return _shortMsg;
}
