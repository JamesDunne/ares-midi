#if defined(Hiro_MenuSeparator)

namespace hiro {

auto pMenuSeparator::construct() -> void {
  cocoaSeparator = [NSMenuItem separatorItem];
  cocoaAction = (NSMenuItem<CocoaMenuProtocol> *)cocoaSeparator;
  pAction::construct();
}

auto pMenuSeparator::destruct() -> void {
}

}

#endif
