Portable Windows build — there is no Setup.exe installer.

Steam Deck (Proton)
--------------------
1. Copy this ENTIRE folder to the Deck (USB, SMB, etc.). ares.exe must stay next to
   all .dll files, Database/, and Shaders/ (if present).

2. Steam → Games → Add a Non-Steam Game to My Library → browse to ares.exe here.

3. Right‑click the shortcut → Properties → Compatibility → enable
   "Force the use of a specific Steam Play compatibility tool" → pick Proton.

4. If you still get a black screen: add launch option   --windowed
   or delete/rename settings.bml next to ares.exe so Deck defaults apply again.

5. If the game "won't start" or nothing happens after Proton setup:
   - Confirm you did not move only ares.exe without the DLLs.
   - Install the Visual C++ 2015–2022 x64 runtime into that game's Proton prefix, e.g.:
       protontricks <appid> vcrun2022
     (find appid in Steam → Properties for the shortcut), or run vc_redist.x64.exe
     once with Proton for that shortcut.

6. Launch options (optional):
   ARES_DISABLE_WINE_COMPAT=1 %command%
   (only if you need to bypass Wine-specific defaults)
   ARES_WINE_DECK_FORCE_SOFTWARE_RDP=1 %command%
   (if the window works but N64 picture is black — uses CPU VI instead of Vulkan RDP)

Developer note: if CMake ever errors about MSVC runtime library (/MD vs /MT) mismatch,
reconfigure without deleting the tree:
   cmake --fresh -S . -B build_msvc
(Use your actual build folder name after -B.)
