# Standalone UWP DLC Unlocker for Call of Duty: WWII (MS Store)

This standalone module is designed to bypass the UWP/Microsoft Store license checks for DLC "activator" packages without modifying any game code or memory. It relies purely on hooking the `XPackage` and `XStore` COM interfaces natively within `xgameruntime.dll`.

## How it works
By avoiding any modifications to the game's executable memory (`.text` section), this unlocker completely evades detection by anti-tamper systems like Arxan.
1. The DLL is injected into the game.
2. Upon injection, it manually loads `xgameruntime.dll` and locates the `QueryApiImpl` function.
3. It calls `QueryApiImpl` to retrieve the `IStoreImpl1` and `XPackage` interfaces.
4. It hooks the virtual methods (vtables) of these interfaces.
5. When the game queries the OS for DLC licenses, our hooks intercept the requests, fake a successful "Mount" operation, and return the base game directory as the location for the DLC assets.
6. The game internally validates the fake response and unlocks the DLCs.

## Usage Instructions (Late Injection)
To successfully inject this module without crashing the game or triggering anti-cheat mechanisms, follow these steps:
1. Launch the clean, unmodified MS Store version of Call of Duty: WWII.
2. Wait for the game window to appear and the intro sequences to begin playing (e.g., the "Activision" and "Sledgehammer Games" logo screens).
3. **During these intro sequences**, use a standard DLL injector (like **System Informer**) to inject `s2_dlc_unlocker.dll` into the game process.
4. The DLL will silently initialize its hooks in the background.
5. Once you reach the Main Menu, the DLC content should be fully unlocked and available.

**Note:** If you inject too early (e.g., via boot-time injection), the game's initial initialization sequence might conflict with the manual COM interface loading. If you inject too late, the game may have already failed its initial DLC checks . The optimal time is during right before the publisher logos (before the "reading disk" warning/ XUI Sync).
