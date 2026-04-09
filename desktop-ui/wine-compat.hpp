#pragma once

#if defined(PLATFORM_WINDOWS)
#include <cstdlib>
#include <cstring>

// Windows build running under Wine / Steam Proton (Linux host, Steam Deck, etc.).
inline auto aresRunningUnderWineOrProton() -> bool {
  if(std::getenv("ARES_DISABLE_WINE_COMPAT")) return false;
  return std::getenv("WINEPREFIX") || std::getenv("STEAM_COMPAT_DATA_PATH")
      || std::getenv("STEAM_COMPAT_CLIENT_INSTALL_PATH");
}

// Steam injects these when the Windows game runs on Steam Deck hardware.
inline auto aresWineProtonLikelySteamDeck() -> bool {
  if(std::getenv("STEAM_DECK")) return true;
  if(const char* s = std::getenv("SteamDeck"); s && s[0] && std::strcmp(s, "0") != 0) return true;
  return false;
}

// If the game viewport stays black but the UI works, try: ARES_WINE_DECK_FORCE_SOFTWARE_RDP=1 %command%
inline auto aresWineDeckForceSoftwareN64Rdp() -> bool {
  return aresRunningUnderWineOrProton() && aresWineProtonLikelySteamDeck()
      && std::getenv("ARES_WINE_DECK_FORCE_SOFTWARE_RDP");
}
#endif
