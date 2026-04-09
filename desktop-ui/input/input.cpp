#include "../desktop-ui.hpp"
#include "hotkeys.cpp"

VirtualPort virtualPorts[5];
InputManager inputManager;

auto InputMapping::bind() -> void {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  for(auto& binding : bindings) binding = {};

  for(u32 index : range(BindingLimit)) {
    auto& assignment = assignments[index];
    auto& binding = bindings[index];

    auto token = nall::split(assignment, "/");
    if(token.size() < 3) continue;  //ignore invalid mappings

    binding.deviceID = token[0].natural();
    binding.groupID = token[1].natural();
    binding.inputID = token[2].natural();
    binding.qualifier = Qualifier::None;
    if(token.size() > 3 && token[3] == "Lo") binding.qualifier = Qualifier::Lo;
    if(token.size() > 3 && token[3] == "Hi") binding.qualifier = Qualifier::Hi;
    if(token.size() > 3 && token[3] == "Rumble") binding.qualifier = Qualifier::Rumble;

    for(auto& device : inputManager.devices) {
      if(binding.deviceID == device->id()) {
        binding.device = device;
        break;
      }
    }
  }
}

auto InputMapping::bind(u32 binding, string assignment) -> void {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  if(binding >= BindingLimit) return;
  assignments[binding] = assignment;
  bind();
}

auto InputMapping::unbind() -> void {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  for(u32 binding : range(BindingLimit)) unbind(binding);
}

auto InputMapping::unbind(u32 binding) -> void {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  if(binding >= BindingLimit) return;
  bindings[binding] = {};
  assignments[binding] = {};
}

auto InputMapping::Binding::icon() -> multiFactorImage {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  if(!device && deviceID) return Icon::Device::Joypad;
  if(!device) return {};
  if(device->isKeyboard()) return Icon::Device::Keyboard;
  if(device->isMouse()) return Icon::Device::Mouse;
  if(device->isJoypad()) return Icon::Device::Joypad;
  return {};
}

auto InputMapping::Binding::text() -> string {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  if(!device && deviceID) return "(disconnected)";
  if(!device) return {};
  if(groupID >= device->size()) return {};
  if(inputID >= device->group(groupID).size()) return {};

  if(device->isKeyboard()) {
    return device->group(groupID).input(inputID).name();
  }

  if(device->isMouse()) {
    return device->group(groupID).input(inputID).name();
  }

  if(device->isJoypad()) {
    string name = device->name();
    if(name == "Joypad") {
      name.append(string{"{", Hash::CRC16(string{device->id()}).digest().upcase(), "}"});
    }
    name.append(" ", device->group(groupID).name());
    name.append(" ", device->group(groupID).input(inputID).name());
    if(qualifier == Qualifier::Lo) name.append(".Lo");
    if(qualifier == Qualifier::Hi) name.append(".Hi");
    if(qualifier == Qualifier::Rumble) name.append(".Rumble");
    return name;
  }

  return {};
}

//

auto InputDigital::bind(u32 binding, std::shared_ptr<HID::Device> device, u32 groupID, u32 inputID, s16 oldValue, s16 newValue) -> bool {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  string assignment = {"0x", hex(device->id()), "/", groupID, "/", inputID};

  if(device->isNull()) {
    return unbind(binding), true;
  }

  if(device->isKeyboard() && device->group(groupID).input(inputID).name() == "Escape") {
    return unbind(binding), true;
  }

  if(device->isKeyboard() && oldValue == 0 && newValue != 0) {
    return bind(binding, assignment), true;
  }

  if(device->isMouse() && oldValue == 0 && newValue != 0) {
    return bind(binding, assignment), true;
  }

  if(device->isJoypad() && groupID == HID::Joypad::GroupID::Button && oldValue == 0 && newValue != 0) {
    return bind(binding, assignment), true;
  }

  if(device->isJoypad() && groupID != HID::Joypad::GroupID::Button
  && oldValue >= -16384 && newValue < -16384
  ) {
    return bind(binding, {assignment, "/Lo"}), true;
  }

  if(device->isJoypad() && groupID != HID::Joypad::GroupID::Button
  && oldValue <= +16384 && newValue > +16384
  ) {
    return bind(binding, {assignment, "/Hi"}), true;
  }

  return false;
}

auto InputDigital::value() -> s16 {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  s16 result = 0;

  for(auto& binding : bindings) {
    if(!binding.device) continue;  //unbound

    auto& device = binding.device;
    auto& groupID = binding.groupID;
    auto& inputID = binding.inputID;
    auto& qualifier = binding.qualifier;
    if (device->isKeyboard() && program.keyboardCaptured) continue;
    s16 value = device->group(groupID).input(inputID).value();
    s16 output = 0;

    if(device->isKeyboard() && groupID == HID::Keyboard::GroupID::Button) {
      output = value != 0;
    }

    if(device->isMouse() && groupID == HID::Mouse::GroupID::Button && ruby::input.acquired()) {
      output = value != 0;
    }

    if(device->isJoypad() && groupID == HID::Joypad::GroupID::Button) {
      output = value != 0;
    }

    if(device->isJoypad() && groupID != HID::Joypad::GroupID::Button) {
      if(qualifier == Qualifier::Lo) output = value < -16384;
      if(qualifier == Qualifier::Hi) output = value > +16384;
    }

    result |= output;
  }

  return result;
}

auto InputDigital::pressed() -> bool {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  return value() != 0;
}


auto InputHotkey::value() -> s16 {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  s16 result = 0;

  for(auto& binding : bindings) {
    if(!binding.device) continue;  //unbound

    auto& device = binding.device;
    auto& groupID = binding.groupID;
    auto& inputID = binding.inputID;
    auto& qualifier = binding.qualifier;

    s16 value = device->group(groupID).input(inputID).value();
    s16 output = 0;

    if(device->isKeyboard() && groupID == HID::Keyboard::GroupID::Button) {
      output = value != 0;
    }

    if(device->isMouse() && groupID == HID::Mouse::GroupID::Button && ruby::input.acquired()) {
      output = value != 0;
    }

    if(device->isJoypad() && groupID == HID::Joypad::GroupID::Button) {
      output = value != 0;
    }

    if(device->isJoypad() && groupID != HID::Joypad::GroupID::Button) {
      if(qualifier == Qualifier::Lo) output = value < -16384;
      if(qualifier == Qualifier::Hi) output = value > +16384;
    }

    result |= output;
  }

  return result;
}


//

auto InputAnalog::bind(u32 binding, std::shared_ptr<HID::Device> device, u32 groupID, u32 inputID, s16 oldValue, s16 newValue) -> bool {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  string assignment = {"0x", hex(device->id()), "/", groupID, "/", inputID};

  if(device->isNull()) {
    return unbind(binding), true;
  }

  if(device->isKeyboard() && device->group(groupID).input(inputID).name() == "Escape") {
    return unbind(binding), true;
  }

  if(device->isKeyboard() && oldValue == 0 && newValue != 0) {
    return bind(binding, assignment), true;
  }

  if(device->isJoypad() && groupID == HID::Joypad::GroupID::Button && oldValue == 0 && newValue != 0) {
    return bind(binding, assignment), true;
  }

  if(device->isJoypad() && groupID != HID::Joypad::GroupID::Button
  && oldValue >= -16384 && newValue < -16384
  ) {
    return bind(binding, {assignment, "/Lo"}), true;
  }

  if(device->isJoypad() && groupID != HID::Joypad::GroupID::Button
  && oldValue <= +16384 && newValue > +16384
  ) {
    return bind(binding, {assignment, "/Hi"}), true;
  }

  return false;
}

auto InputAnalog::value() -> s16 {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  s32 result = 0;

  for(auto& binding : bindings) {
    if(!binding.device) continue;  //unbound

    auto& device = binding.device;
    auto& groupID = binding.groupID;
    auto& inputID = binding.inputID;
    auto& qualifier = binding.qualifier;
    if (device->isKeyboard() && program.keyboardCaptured) continue;    
    s16 value = device->group(groupID).input(inputID).value();

    if(device->isKeyboard() && groupID == HID::Keyboard::GroupID::Button) {
      result += value != 0 ? 32767 : 0;
    }

    if(device->isJoypad() && groupID == HID::Joypad::GroupID::Button) {
      result += value != 0 ? 32767 : 0;
    }

    if(device->isJoypad() && groupID != HID::Joypad::GroupID::Button) {
      if(qualifier == Qualifier::Lo && value < 0) result += abs(value);
      if(qualifier == Qualifier::Hi && value > 0) result += abs(value);
    }
  }

  return sclamp<16>(result);
}

auto InputAnalog::pressed() -> bool {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  return value() > 16384;
}

//

auto InputAbsolute::bind(u32 binding, std::shared_ptr<HID::Device> device, u32 groupID, u32 inputID, s16 oldValue, s16 newValue) -> bool {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  string assignment = {"0x", hex(device->id()), "/", groupID, "/", inputID};

  if(device->isNull()) {
    return unbind(binding), true;
  }

  if(device->isKeyboard() && device->group(groupID).input(inputID).name() == "Escape") {
    return unbind(binding), true;
  }

  if(device->isMouse() && groupID == HID::Mouse::GroupID::Axis) {
    return bind(binding, assignment), true;
  }

  if(device->isJoypad() && groupID == HID::Joypad::GroupID::Axis
  && oldValue >= -16384 && newValue < -16384
  ) {
    return bind(binding, assignment), true;
  }

  if(device->isJoypad() && groupID == HID::Joypad::GroupID::Axis
  && oldValue <= +16384 && newValue > +16384
  ) {
    return bind(binding, assignment), true;
  }

  return false;
}

auto InputAbsolute::value() -> s16 {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  s32 result = 0;

  for(auto& binding : bindings) {
    if(!binding.device) continue;  //unbound

    auto& device = binding.device;
    auto& groupID = binding.groupID;
    auto& inputID = binding.inputID;
    auto& qualifier = binding.qualifier;
    if (device->isKeyboard() && program.keyboardCaptured) continue;
    s16 value = device->group(groupID).input(inputID).value();

    if(device->isMouse() && groupID == HID::Joypad::GroupID::Axis && ruby::input.acquired()) {
      result += value;
    }

    if(device->isJoypad() && groupID == HID::Joypad::GroupID::Axis) {
      result += value;
    }
  }

  return sclamp<16>(result);
}

//

auto InputRelative::bind(u32 binding, std::shared_ptr<HID::Device> device, u32 groupID, u32 inputID, s16 oldValue, s16 newValue) -> bool {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  string assignment = {"0x", hex(device->id()), "/", groupID, "/", inputID};

  if(device->isNull()) {
    return unbind(binding), true;
  }

  if(device->isKeyboard() && device->group(groupID).input(inputID).name() == "Escape") {
    return unbind(binding), true;
  }

  if(device->isMouse() && groupID == HID::Mouse::GroupID::Axis) {
    return bind(binding, assignment), true;
  }

  if(device->isJoypad() && groupID == HID::Joypad::GroupID::Axis
  && oldValue >= -16384 && newValue < -16384
  ) {
    return bind(binding, assignment), true;
  }

  if(device->isJoypad() && groupID == HID::Joypad::GroupID::Axis
  && oldValue <= +16384 && newValue > +16384
  ) {
    return bind(binding, assignment), true;
  }

  return false;
}

auto InputRelative::value() -> s16 {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  s32 result = 0;

  for(auto& binding : bindings) {
    if(!binding.device) continue;  //unbound

    auto& device = binding.device;
    auto& groupID = binding.groupID;
    auto& inputID = binding.inputID;
    auto& qualifier = binding.qualifier;
    if (device->isKeyboard() && program.keyboardCaptured) continue;
    s16 value = device->group(groupID).input(inputID).value();

    if(device->isMouse() && groupID == HID::Joypad::GroupID::Axis && ruby::input.acquired()) {
      result += value;
    }

    if(device->isJoypad() && groupID == HID::Joypad::GroupID::Axis) {
      result += value;
    }
  }

  return sclamp<16>(result);
}

//

auto InputRumble::bind(u32 binding, std::shared_ptr<HID::Device> device, u32 groupID, u32 inputID, s16 oldValue, s16 newValue) -> bool {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  string assignment = {"0x", hex(device->id()), "/", groupID, "/", inputID};

  if(device->isNull()) {
    return unbind(binding), true;
  }

  if(device->isKeyboard() && device->group(groupID).input(inputID).name() == "Escape") {
    return unbind(binding), true;
  }

  if(device->isJoypad() && groupID == HID::Joypad::GroupID::Button && oldValue == 0 && newValue == 1) {
    return bind(binding, assignment), true;
  }

  return false;
}

auto InputRumble::value() -> s16 {
  return 0;
}

auto InputRumble::rumble(u16 strong, u16 weak) -> void {
  for(auto& binding : bindings) {
    if(!binding.device) continue;
    ruby::input.rumble(binding.deviceID, strong, weak);
  }
}

//

VirtualPad::VirtualPad() {
  InputDevice::name = "Virtual Gamepad";
  InputDevice::digital("Pad Up",          up);
  InputDevice::digital("Pad Down",        down);
  InputDevice::digital("Pad Left",        left);
  InputDevice::digital("Pad Right",       right);
  InputDevice::digital("Select",          select);
  InputDevice::digital("Start",           start);
  InputDevice::digital("A (South)",       south);
  InputDevice::digital("B (East)",        east);
  InputDevice::digital("X (West)",        west);
  InputDevice::digital("Y (North)",       north);
  InputDevice::digital("L-Bumper",        l_bumper);
  InputDevice::digital("R-Bumper",        r_bumper);
  InputDevice::digital("L-Trigger",       l_trigger);
  InputDevice::digital("R-Trigger",       r_trigger);
  InputDevice::digital("L-Stick (Click)", lstick_click);
  InputDevice::digital("R-Stick (Click)", rstick_click);
  InputDevice::analog ("L-Up",            lstick_up);
  InputDevice::analog ("L-Down",          lstick_down);
  InputDevice::analog ("L-Left",          lstick_left);
  InputDevice::analog ("L-Right",         lstick_right);
  InputDevice::analog ("R-Up",            rstick_up);
  InputDevice::analog ("R-Down",          rstick_down);
  InputDevice::analog ("R-Left",          rstick_left);
  InputDevice::analog ("R-Right",         rstick_right);
  InputDevice::rumble ("Rumble",          rumble);
}

//

VirtualMouse::VirtualMouse() {
  InputDevice::name = "Mouse";
  InputDevice::relative("X",      x);
  InputDevice::relative("Y",      y);
  InputDevice::digital ("Left",   left);
  InputDevice::digital ("Middle", middle);
  InputDevice::digital ("Right",  right);
  InputDevice::digital ("Extra",  extra);
}

//

namespace {

enum class XboxStyleLayout : u32 { None, XInput, SdlMicrosoft, SdlSteamDeck };

static auto joyAssignment(u64 deviceId, u32 group, u32 input, const char* qualifier = nullptr) -> string {
  string s = {"0x", hex(deviceId), "/", group, "/", input};
  if(qualifier) s.append("/", qualifier);
  return s;
}

static auto assignmentDeviceConnected(const string& assignment) -> bool {
  if(!assignment) return false;
  auto token = nall::split(assignment, "/");
  if(token.size() < 3) return false;
  u64 want = token[0].natural();
  for(auto& d : inputManager.devices) {
    if(d->id() == want) return true;
  }
  return false;
}

static auto port0GamepadAutoMapNeeded() -> bool {
  return !assignmentDeviceConnected(virtualPorts[0].pad.south.assignments[0]);
}

static auto detectXboxStyleLayout(const std::shared_ptr<HID::Joypad>& j) -> XboxStyleLayout {
  const u32 ax = j->axes().size();
  const u32 ht = j->hats().size();
  const u32 tr = j->triggers().size();
  const u32 bt = j->buttons().size();
  const u16 vid = j->vendorID();

  if(vid == HID::Joypad::GenericVendorID) return XboxStyleLayout::None;
  if(vid == 0x054c || vid == 0x057e) return XboxStyleLayout::None;

  if(tr >= 2 && ax >= 4 && ht >= 2 && bt >= 11) return XboxStyleLayout::XInput;

  string name = j->name();
  if(((bool)name.ifind("Steam Deck")) || vid == 0x28de) {
    if(ax >= 8 && bt >= 14) return XboxStyleLayout::SdlSteamDeck;
  }

  const bool xboxName =
    (bool)name.ifind("Xbox") || (bool)name.ifind("X360") || (bool)name.ifind("X-Box");
  if(ax >= 6 && ht >= 2 && bt >= 10) {
    if(vid == 0x045e || xboxName) return XboxStyleLayout::SdlMicrosoft;
  }

  return XboxStyleLayout::None;
}

static auto clearVirtualPort0Pad() -> void {
  auto& pad = virtualPorts[0].pad;
  for(auto& input : pad.inputs) input.mapping->unbind();
  for(auto& pair : pad.pairs) {
    pair.mappingLo->unbind();
    pair.mappingHi->unbind();
  }
}

static auto bindHatDpad(VirtualPad& p, u64 id) -> void {
  p.up.bind(0, joyAssignment(id, HID::Joypad::GroupID::Hat, 1, "Lo"));
  p.down.bind(0, joyAssignment(id, HID::Joypad::GroupID::Hat, 1, "Hi"));
  p.left.bind(0, joyAssignment(id, HID::Joypad::GroupID::Hat, 0, "Lo"));
  p.right.bind(0, joyAssignment(id, HID::Joypad::GroupID::Hat, 0, "Hi"));
}

static auto bindLeftStickXY(VirtualPad& p, u64 id, u32 axisX, u32 axisY) -> void {
  p.lstick_left.bind(0, joyAssignment(id, HID::Joypad::GroupID::Axis, axisX, "Lo"));
  p.lstick_right.bind(0, joyAssignment(id, HID::Joypad::GroupID::Axis, axisX, "Hi"));
  p.lstick_up.bind(0, joyAssignment(id, HID::Joypad::GroupID::Axis, axisY, "Lo"));
  p.lstick_down.bind(0, joyAssignment(id, HID::Joypad::GroupID::Axis, axisY, "Hi"));
}

static auto bindRightStickXY(VirtualPad& p, u64 id, u32 axisX, u32 axisY) -> void {
  p.rstick_left.bind(0, joyAssignment(id, HID::Joypad::GroupID::Axis, axisX, "Lo"));
  p.rstick_right.bind(0, joyAssignment(id, HID::Joypad::GroupID::Axis, axisX, "Hi"));
  p.rstick_up.bind(0, joyAssignment(id, HID::Joypad::GroupID::Axis, axisY, "Lo"));
  p.rstick_down.bind(0, joyAssignment(id, HID::Joypad::GroupID::Axis, axisY, "Hi"));
}

static auto applyXInputPreset(u64 id) -> void {
  auto& p = virtualPorts[0].pad;
  bindHatDpad(p, id);
  bindLeftStickXY(p, id, 0, 1);
  bindRightStickXY(p, id, 2, 3);
  p.south.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 0));
  p.east.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 1));
  p.west.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 2));
  p.north.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 3));
  p.start.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 5));
  p.select.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 4));
  p.l_bumper.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 6));
  p.r_bumper.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 7));
  p.lstick_click.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 8));
  p.rstick_click.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 9));
  p.l_trigger.bind(0, joyAssignment(id, HID::Joypad::GroupID::Trigger, 0, "Hi"));
  p.r_trigger.bind(0, joyAssignment(id, HID::Joypad::GroupID::Trigger, 1, "Hi"));
  p.rumble.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 0));
}

// Linux/SDL Xbox 360 class mapping: sticks on 0/1 and 3/4, LT axis 2, RT axis 5.
static auto applySdlMicrosoftXboxPreset(u64 id) -> void {
  auto& p = virtualPorts[0].pad;
  bindHatDpad(p, id);
  bindLeftStickXY(p, id, 0, 1);
  bindRightStickXY(p, id, 3, 4);
  p.south.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 0));
  p.east.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 1));
  p.west.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 2));
  p.north.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 3));
  p.start.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 7));
  p.select.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 6));
  p.l_bumper.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 4));
  p.r_bumper.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 5));
  p.lstick_click.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 8));
  p.rstick_click.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 9));
  p.l_trigger.bind(0, joyAssignment(id, HID::Joypad::GroupID::Axis, 2, "Hi"));
  p.r_trigger.bind(0, joyAssignment(id, HID::Joypad::GroupID::Axis, 5, "Hi"));
  p.rumble.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 0));
}

// SDL indices aligned with common Steam Deck / gamecontrollerdb joystick layouts.
static auto applySdlSteamDeckPreset(u64 id) -> void {
  auto& p = virtualPorts[0].pad;
  p.up.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 11));
  p.down.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 12));
  p.left.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 13));
  p.right.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 14));
  bindLeftStickXY(p, id, 0, 1);
  bindRightStickXY(p, id, 2, 3);
  p.south.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 3));
  p.east.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 4));
  p.west.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 2));
  p.north.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 5));
  p.start.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 6));
  p.select.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 15));
  p.l_bumper.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 9));
  p.r_bumper.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 10));
  p.lstick_click.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 7));
  p.rstick_click.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 8));
  p.l_trigger.bind(0, joyAssignment(id, HID::Joypad::GroupID::Axis, 8, "Hi"));
  p.r_trigger.bind(0, joyAssignment(id, HID::Joypad::GroupID::Axis, 9, "Hi"));
  p.rumble.bind(0, joyAssignment(id, HID::Joypad::GroupID::Button, 3));
}

static auto tryApplyStandardGamepadDefaults() -> bool {
  if(!port0GamepadAutoMapNeeded()) return false;
  for(auto& dev : inputManager.devices) {
    if(!dev->isJoypad()) continue;
    auto joy = std::static_pointer_cast<HID::Joypad>(dev);
    const auto layout = detectXboxStyleLayout(joy);
    if(layout == XboxStyleLayout::None) continue;

    clearVirtualPort0Pad();
    const u64 id = joy->id();
    if(layout == XboxStyleLayout::XInput) applyXInputPreset(id);
    else if(layout == XboxStyleLayout::SdlMicrosoft) applySdlMicrosoftXboxPreset(id);
    else if(layout == XboxStyleLayout::SdlSteamDeck) applySdlSteamDeckPreset(id);

    settings.save();
    return true;
  }
  return false;
}

}  // namespace

auto InputManager::create() -> void {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  createHotkeys();
}

auto InputManager::bind() -> void {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  for(auto& port : virtualPorts) {
    for(auto& input : port.pad.inputs) input.mapping->bind();
    for(auto& input : port.mouse.inputs) input.mapping->bind();
  }
  for(auto& mapping : hotkeys) mapping.bind();
}

auto InputManager::poll(bool force) -> void {
  //polling actual hardware is very time-consuming; skip call if poll was called too recently
  auto thisPoll = chrono::millisecond();
  if(thisPoll - lastPoll < pollFrequency && !force) return;
  lastPoll = thisPoll;

  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  auto devices = ruby::input.poll();
  bool listChanged = devices.size() != this->devices.size();
  if(!listChanged) {
    for(u32 index : range(devices.size())) {
      listChanged = devices[index] != this->devices[index];
      if(listChanged) break;
    }
  }
  this->devices = devices;
  const bool remapped = tryApplyStandardGamepadDefaults();
  if(listChanged || remapped) {
    bind();
    if(settingsWindow.initialized) {
      inputSettings.refresh();
      hotkeySettings.refresh();
    }
  }
}

auto InputManager::eventInput(std::shared_ptr<HID::Device> device, u32 groupID, u32 inputID, s16 oldValue, s16 newValue) -> void {
  lock_guard<recursive_mutex> inputLock(program.inputMutex);
  inputSettings.eventInput(device, groupID, inputID, oldValue, newValue);
  hotkeySettings.eventInput(device, groupID, inputID, oldValue, newValue);
}
