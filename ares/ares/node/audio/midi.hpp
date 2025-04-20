struct MIDI : Audio {
  DeclareClass(MIDI, "midi")
  using Audio::Audio;

  auto readShort() -> s32;
  auto writeShort(u8 cmd, u8 data1, u8 data2) -> void;
  auto delay() -> void;
protected:
  s32 _shortMsg;
};
