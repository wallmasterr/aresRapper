#include "../desktop-ui.hpp"
#include "platform.cpp"
#include "load.cpp"
#include "states.cpp"
#include "rewind.cpp"
#include "status.cpp"
#include "utility.cpp"
#include "drivers.cpp"

Program program;
thread worker;

namespace {

auto fastForwardSpeedMultiplier() -> f64 {
  const string& s = settings.general.fastForwardSpeed;
  if(s == "1.5x") return 1.5;
  if(s == "2x") return 2.0;
  if(s == "3x") return 3.0;
  return 0.0;
}

}  // namespace

auto Program::create() -> void {
  ares::platform = this;

  videoDriverUpdate();
  audioDriverUpdate();
  inputDriverUpdate();

  if(startFullScreen) videoFullScreenToggle();
  if(startPseudoFullScreen) videoPseudoFullScreenToggle();

  _isRunning = true;
  worker = thread::create(std::bind_front(&Program::emulatorRunLoop, this));
  program.rewindReset();

  if(!startGameLoad.empty()) {
    Program::Guard guard;
    auto gameToLoad = startGameLoad.front();
    startGameLoad.erase(startGameLoad.begin());
    if(startSystem) {
      for(auto &emulator: emulators) {
        if(emulator->name == startSystem) {
          load(emulator, gameToLoad);
          return;
        }
      }
      return;
    }

    if(auto emulator = identify(gameToLoad)) {
      load(emulator, gameToLoad);
    }
  }
}

auto Program::waitForInterrupts() -> void {
  std::unique_lock<std::mutex> lock(_programMutex);
  _interruptWorking = true;
  _programConditionVariable.notify_one();
  _programConditionVariable.wait(lock, [this] { return !_interruptWorking || _quitting; });
}

auto Program::emulatorRunLoop(uintptr_t) -> void {
  thread::setName("dev.ares.worker");
  _programThread = true;
  while(!_quitting) {
    // Allow other threads to carry out tasks between emulator run loop iterations
    if(_interruptWaiting) {
      waitForInterrupts();
      continue;
    }
    if(!emulator) {
      usleep(20 * 1000);
      continue;
    }

    if(emulator && nall::GDB::server.isHalted()) {
      ruby::audio.clear();
      nall::GDB::server.updateLoop(); // sleeps internally
      continue;
    }

    bool defocused = settings.input.defocus == "Pause" && !ruby::video.fullScreen() && !presentation.focused();

    if(!emulator || (paused && !program.requestFrameAdvance) || defocused) {
      ruby::audio.clear();
      nall::GDB::server.updateLoop();
      usleep(20 * 1000);
      continue;
    }

    rewindRun();

    nall::GDB::server.updateLoop();

    program.requestFrameAdvance = false;

    if(fastForwarding) {
      if(f64 mult = fastForwardSpeedMultiplier()) {
        f64 hz = emulatedRefreshRate;
        if(hz < 1.0) hz = 60.0;
        const u64 frameNs = (u64)(1'000'000'000.0 / (hz * mult));
        u64 now = chrono::nanosecond();
        if(fastForwardPaceNextNs == 0) fastForwardPaceNextNs = now;
        if(now < fastForwardPaceNextNs) {
          u64 sleepNs = fastForwardPaceNextNs - now;
          if(sleepNs > frameNs * 10) sleepNs = frameNs;
          u64 sleepUs = sleepNs / 1000;
          if(sleepUs > 2'000'000) sleepUs = 2'000'000;
          if(sleepUs > 0) usleep((unsigned)sleepUs);
        }
        now = chrono::nanosecond();
        fastForwardPaceNextNs += frameNs;
        if(now > fastForwardPaceNextNs + frameNs * 4) fastForwardPaceNextNs = now + frameNs;
      } else {
        fastForwardPaceNextNs = 0;
      }
    } else {
      fastForwardPaceNextNs = 0;
    }

    if(!runAhead || fastForwarding || rewinding) {
      emulator->root->run();
    } else {
      ares::setRunAhead(true);
      emulator->root->run();
      auto state = emulator->root->serialize(false);
      ares::setRunAhead(false);
      emulator->root->run();
      state.setReading();
      emulator->root->unserialize(state);
    }

    nall::GDB::server.updateLoop();

    if(settings.general.autoSaveMemory) {
      static u64 previousTime = chrono::timestamp();
      u64 currentTime = chrono::timestamp();
      if(currentTime - previousTime >= 30) {
        previousTime = currentTime;
        emulator->save();
      }
    }

    if(emulator->latch.changed) {
      emulator->latch.changed = false;
      _needsResize = true;
    }
  }
}

auto Program::main() -> void {
  if(Application::modal()) {
    ruby::audio.clear();
    return;
  }

  if(pendingKioskExit) {
    pendingKioskExit = false;
    quit();
    return;
  }
  
  inputManager.poll();
  inputManager.pollHotkeys();

  updateMessage();
  presentation.updateFpsOverlay();

  //If Platform::video() changed the screen resolution, resize the presentation window here.
  //Window operations must be performed from the main thread.
  
  if(_needsResize) {
    if(settings.video.adaptiveSizing && !startPseudoFullScreen) presentation.resizeWindow();
    _needsResize = false;
  }

  if(toolsWindowConstructed) {
    memoryEditor.liveRefresh();
    graphicsViewer.liveRefresh();
    propertiesViewer.liveRefresh();
    tapeViewer.liveRefresh();
  }
  if (_quitRequested) {
    quit();
  }
}

auto Program::quit() -> void {
  if (_programThread) {
    _quitRequested = true;
    return;
  }
  Program::Guard guard;
  _quitRequested = false;
  _quitting = true;
  if(lock.owns_lock()) {
    lock.unlock();
  }
  _programConditionVariable.notify_all();
  worker.join();
  program._isRunning = false;
  unload();
  Application::processEvents();
  Application::quit();

  ruby::video.reset();
  ruby::audio.reset();
  ruby::input.reset();
}
