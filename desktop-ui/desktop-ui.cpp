#include "desktop-ui.hpp"
#include "wine-compat.hpp"
#if defined(PLATFORM_WINDOWS)
  #pragma comment(lib, "winmm.lib")
  extern "C" __declspec(dllimport) unsigned long __stdcall mciSendStringA(
    const char* lpstrCommand, char* lpstrReturnString, unsigned int uReturnLength, void* hwndCallback
  );
  extern "C" __declspec(dllimport) int __stdcall mciGetErrorStringA(
    unsigned long mcierr, char* pszText, unsigned int cchText
  );
#endif

namespace ruby {
  Video video;
  Audio audio;
  Input input;
}

auto locate(const string& name) -> string {
  // First check each path for the presence of the file we are looking for in the following order
  // allowing users to override the default resources if they wish to do so.

  // 1. The application directory
  string location = {Path::program(), name};
  if(inode::exists(location)) return location;

  // 2. The user data directory
  location = {Path::userData(), "ares/", name};
  if(inode::exists(location)) return location;

  // 3. The shared data directory
#if defined(PLATFORM_LINUX) || defined(PLATFORM_BSD)
  /// Unix-like systems have multiple notions of a 'shared data' directory. First, check for
  /// an install prefix, as would be used by package managers that do not use `/usr/share`.
  /// Secondly, look in `/usr/local/share` to cover software compiled by the user.
  /// Lastly, look in the 'global' shared data directory, `/usr/share`.
  location = {Path::prefixSharedData(), "ares/", name};
  if(inode::exists(location)) return location;
  
  location = {Path::localSharedData(), "ares/", name};
  if(inode::exists(location)) return location;
#endif

  location = {Path::sharedData(), "ares/", name};
  if(inode::exists(location)) return location;

  // 4. The application bundle resource directory (macOS only)
#if defined(PLATFORM_MACOS)
  location = {Path::resources(), name};
  if(inode::exists(location)) return location;
#endif

  // If the file was not found in any of the above locations, we may be intending to create it
#if defined(PLATFORM_WINDOWS)
  // We must return a path to a user writable directory; on Windows, this is the executable directory
  return {Path::program(), name};
#else
  // On other platforms, this is the "user data" directory
  directory::create({Path::userData(), "ares/"});
  return {Path::userData(), "ares/", name};
#endif

}

#if defined(PLATFORM_WINDOWS)
namespace {
auto mciRun(const string& command, bool verbose = true) -> unsigned long {
  auto rc = mciSendStringA(command, nullptr, 0, nullptr);
  if(rc != 0 && verbose) {
    char errorText[256] = {};
    mciGetErrorStringA(rc, errorText, sizeof(errorText));
    print("[BGM] MCI failed (", rc, "): ", command, " -> ", errorText, "\n");
  }
  return rc;
}

auto mciQuery(const string& command) -> string {
  char result[128] = {};
  if(mciSendStringA(command, result, sizeof(result), nullptr) != 0) return {};
  return result;
}

static bool bgmReady = false;

auto findBackgroundTracks() -> std::vector<string> {
  std::vector<string> candidates = {
    locate("resource/track1.wav"),
    locate("track1.wav"),
    {Path::active(), "desktop-ui/resource/track1.wav"},
    {Path::active(), "resource/track1.wav"},
    {Path::active(), "track1.wav"},
  };
  std::vector<string> tracks;
  for(auto& candidate : candidates) {
    if(inode::exists(candidate)) tracks.push_back(candidate);
  }
  return tracks;
}

auto startBackgroundMusic() -> void {
  bgmReady = false;
  auto tracks = findBackgroundTracks();
  if(tracks.empty()) {
    print("[BGM] track not found: track1.wav\n");
    return;
  }

  for(auto& track : tracks) {
    print("[BGM] track: ", track, "\n");
    mciRun("close aresbgm", false);
    auto openTyped = string{"open \"", track, "\" type waveaudio alias aresbgm"};
    auto openAuto = string{"open \"", track, "\" alias aresbgm"};
    if(mciRun(openTyped, false) != 0 && mciRun(openAuto, false) != 0) {
      continue;
    }
    mciRun("set aresbgm time format milliseconds", false);
    // MCI volume range is 0..1000; 500 ~= 50%.
    mciRun("setaudio aresbgm volume to 500", false);
    if(mciRun("play aresbgm from 0", true) != 0) continue;
    bgmReady = true;
    return;
  }

  print("[BGM] unable to play any track candidate.\n");
}

auto updateBackgroundMusic() -> void {
  if(!bgmReady) return;
  auto mode = mciQuery("status aresbgm mode");
  if(!mode) return;
  if(mode == "stopped") {
    mciRun("seek aresbgm to start", false);
    mciRun("play aresbgm from 0", false);
  }
}

auto stopBackgroundMusic() -> void {
  bgmReady = false;
  mciSendStringA("stop aresbgm", nullptr, 0, nullptr);
  mciSendStringA("close aresbgm", nullptr, 0, nullptr);
}
}
#endif

#include <nall/main.hpp>
auto nall::main(Arguments arguments) -> void {
  //force early allocation for better proximity to executable code
  ares::Memory::FixedAllocator::get();

#if defined(PLATFORM_WINDOWS)
  bool createTerminal = arguments.take("--terminal");
  terminal::redirectStdioToTerminal(createTerminal);
#endif

  Application::setName("ares");
  Application::setScreenSaver(false);

  mia::setHomeLocation([]() -> string {
    if(auto location = settings.paths.home) return location;
    return locate("Systems/");
  });

  mia::setSaveLocation([]() -> string {
    return settings.paths.saves;
  });

  bool displayModeFromArgs = false;
  if(arguments.take("--windowed")) {
    program.startFullScreen = false;
    displayModeFromArgs = true;
  }
  if(arguments.take("--fullscreen")) {
    program.startFullScreen = true;
    displayModeFromArgs = true;
  } else if(arguments.take("--pseudofullscreen")) {
    program.startPseudoFullScreen = true;
    program.startFullScreen = false;
    displayModeFromArgs = true;
  }

  if(arguments.take("--kiosk")) {
    program.kiosk = true;
    program.noFilePrompt = true;
  }

  if(string system; arguments.take("--system", system)) {
    program.startSystem = system;
  }

  if(string shader; arguments.take("--shader", shader)) {
    program.startShader = shader;
  }

  if(arguments.take("--no-file-prompt")) {
    program.noFilePrompt = true;
  }

  settings.filePath = locate("settings.bml");
  if(string settingsFile; arguments.take("--settings-file", settingsFile)) {
    settings.filePath = settingsFile;
  }

  if(string savestate; arguments.take("--save-state", savestate)) {
    if(savestate.length() == 1 && savestate[0] >= '1' && savestate[0] <= '9') {
      program.startSaveStateSlot = savestate;
    }
  }

  inputManager.create();
  Emulator::construct();

  settings.load();

#if defined(PLATFORM_WINDOWS)
  if(aresRunningUnderWineOrProton() && aresWineProtonLikelySteamDeck() && !displayModeFromArgs) {
    program.startFullScreen = false;
  }
#endif

  if(arguments.find("--setting")) {
    string settingValue;
    while(arguments.take("--setting", settingValue)) {
      auto kv = nall::split(settingValue, "=", 1L);
      if(kv.size() == 2) {
        auto node = settings[kv[0]];
        if(node) {
          node.setValue(kv[1]);
        } else {
          print("Invalid setting: ", settingValue, "\n");
          return;
        }
      } else {
        print("Invalid setting: ", settingValue, "\n");
        return;
      }
    }
    settings.process(true);
  }

  if(program.noFilePrompt) settings.general.noFilePrompt = true;

  if(arguments.take("--help")) {
    print("\n Usage: ares [OPTIONS]... game(s)\n\n");
    print("Options:\n");
    print("  --help                Displays available options and exit\n");
    print("  --version             Displays the version string of the application\n");
#if defined(PLATFORM_WINDOWS)
    print("  --terminal            Create new terminal window\n");
#endif
    print("  --fullscreen          Start in full screen mode (default unless --windowed)\n");
    print("  --windowed            Start in a window\n");
    print("  --pseudofullscreen    Start in psuedo full screen mode\n");
    print("  --kiosk               Start in minimal UI mode (implies --no-file-prompt)\n");
    print("  --system name         Specify the system name\n");
    print("  --shader name         Specify the name of the shader to use\n");
    print("  --setting name=value  Specify a value for a setting\n");
    print("  --dump-all-settings   Show a list of all existing settings and exit\n");
    print("  --no-file-prompt      Do not prompt to load (optional) additional roms (eg: 64DD)\n");
    print("  --settings-file path  Specify a settings file override (settings.bml)\n");
    print("  --save-state slot     Specify a save state slot to load (1-9)\n");
#if defined(PLATFORM_WINDOWS)
    print("\n");
    print("Steam Deck / Proton: defaults to windowed + OpenGL (not D3D9), safer video flags.\n");
    print("Override with --fullscreen or --windowed. Env: ARES_DISABLE_WINE_COMPAT=1 skips this.\n");
#endif
    print("\n");
    print("If no game path is given, ares looks for game.z64 next to the executable,\n");
    print("then in the current working directory, and loads it when found.\n");
    print("\n");
    print("Available Systems:\n");
    print("  ");
    for(auto& emulator : emulators) {
      print(emulator->name, ", ");
    }
    print("\n\nares version ", ares::Version, "\n");
    return;
  }

  if(arguments.take("--version")) {
    print("\n", ares::Version, "\n");
    return;
  }

  if(arguments.take("--dump-all-settings")) {
    std::function<void(const Markup::Node&, string)> dump;
    dump = [&](const Markup::Node& node, string prefix) -> void {
      for(const auto& setting : node) {
        print(prefix, setting.name(), "\n");
        dump(setting, string(prefix, setting.name(), "/"));
      }
    };
    dump(settings, "");
    return;
  }

  program.startGameLoad.clear();
  std::vector<string> invalidKioskPaths;
  for(auto argument : arguments) {
    if(file::exists(argument) || directory::exists(argument)) {
      program.startGameLoad.push_back(argument);
    } else if(program.kiosk) {
      invalidKioskPaths.push_back(argument);
    }
  }

  // Keep convenient auto-load of game.z64, but it now loads on a deferred
  // main-loop tick so window startup stays responsive.
  if(program.startGameLoad.empty()) {
    string autoRom = {Path::program(), "game.z64"};
    if(!inode::exists(autoRom)) autoRom = {Path::active(), "game.z64"};
    if(inode::exists(autoRom)) {
      program.startGameLoad.push_back(autoRom);
    }
  }

  if(program.kiosk) {
    if(!invalidKioskPaths.empty()) {
      program.error({"path does not exist: ", invalidKioskPaths.front()});
      return;
    }
    if(program.startGameLoad.empty()) {
      program.error("provide a valid game file or directory.");
      return;
    }
  }

  if(program.startSystem && !program.startGameLoad.empty()) {
    bool foundSystem = false;
    for(auto& emulator : emulators) {
      if(emulator->name == program.startSystem) {
        foundSystem = true;
        break;
      }
    }
    if(!foundSystem) {
      auto text = string{"Unrecognized argument for --system: ", program.startSystem, "\n"
                         "Use --help to list all valid systems supported by ares."};
      program.error(text);
      if(program.kiosk) return;
    }
  }

  Instances::presentation.construct();

  presentation.setVisible();
  program.create();
  Application::onMain([&] {
    program.main();

    static bool firstTickCompleted = false;
    if(!firstTickCompleted) {
      firstTickCompleted = true;
      return;
    }

    static bool startupGameTried = false;
    if(!startupGameTried && !program.startGameLoad.empty()) {
      startupGameTried = true;
      Program::Guard guard;
      auto gameToLoad = program.startGameLoad.front();
      program.startGameLoad.erase(program.startGameLoad.begin());
      if(program.startSystem) {
        for(auto& emu : emulators) {
          if(emu->name == program.startSystem) {
            program.load(emu, gameToLoad);
            break;
          }
        }
      } else if(auto emu = program.identify(gameToLoad)) {
        program.load(emu, gameToLoad);
      }
    }

    // Stagger expensive startup work so first frame appears quickly.
    static u32 deferredStage = 0;
    if(program.kiosk) return;
    if(deferredStage == 0) {
      Instances::settingsWindow.construct();
      program.settingsWindowConstructed = true;
      deferredStage = 1;
      return;
    }
    if(deferredStage == 1) {
      Instances::gameBrowserWindow.construct();
      program.gameBrowserWindowConstructed = true;
      deferredStage = 2;
      return;
    }
    if(deferredStage == 2) {
      Instances::toolsWindow.construct();
      program.toolsWindowConstructed = true;
      deferredStage = 3;
      return;
    }
    if(deferredStage == 3) {
      presentation.loadShaders();
      deferredStage = 4;
      return;
    }
#if defined(PLATFORM_WINDOWS)
    if(deferredStage == 4) {
      startBackgroundMusic();
      deferredStage = 5;
    }
    if(deferredStage >= 5) {
      updateBackgroundMusic();
    }
#endif
  });
  Application::run();
#if defined(PLATFORM_WINDOWS)
  stopBackgroundMusic();
#endif

  settings.save();

  Instances::presentation.destruct();
  if(program.settingsWindowConstructed) Instances::settingsWindow.destruct();
  if(program.toolsWindowConstructed) Instances::toolsWindow.destruct();
  if(program.gameBrowserWindowConstructed) Instances::gameBrowserWindow.destruct();
}

#if defined(PLATFORM_WINDOWS) && defined(ARCHITECTURE_AMD64) && !defined(BUILD_LOCAL)

#include <nall/windows/windows.hpp>
#include <intrin.h>

//this code must run before C++ global initializers
//it works with any valid combination of GCC, Clang, or MSVC and MingW or MSVCRT
//ref: https://learn.microsoft.com/en-us/cpp/c-runtime-library/crt-initialization

auto preCppInitializer() -> int {
  int data[4] = {};
  __cpuid(data, 1);
  bool sse42 = data[2] & 1 << 20;
  if(!sse42) FatalAppExitA(0, "This build of ares requires a CPU that supports SSE4.2.");
  return 0;
}

extern "C" {
#if defined(_MSC_VER)
  #pragma comment(linker, "/include:preCppInitializerEntry")
  #pragma section(".CRT$XCT", read)
  __declspec(allocate(".CRT$XCT"))
#else
  __attribute__((section (".CRT$XCT"), used))
#endif
  decltype(&preCppInitializer) preCppInitializerEntry = preCppInitializer;
}

#endif
