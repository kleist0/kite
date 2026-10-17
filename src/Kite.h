#pragma once

#include <Windows.h>

#include <Core/Log.h>

#include <string>

// Everything Kite reads from its own kite.ini, in one small struct.
//
// The file is expected next to gamemd.exe and is entirely optional: when it is
// missing, or a key is absent, the defaults below apply. A key that is present
// but invalid (LogLevel=banana) also falls back to the default, so a typo can
// never produce undefined behaviour.
//
//   [Kite]
//   Enabled=yes          ; master switch, "no" disables both features
//   LogLevel=info        ; off | error | warn | info | debug
//   DebugLog=no          ; write a timestamped kite.log next to the game
//
// The two FEATURES are configured in the game's rules file, not here:
// `[General] BuildCost` and `[<Country>] Sidebar.MixFileIndex` in rulesmd.ini.
// Keeping those keys where they belong is why this struct has only three
// fields: kite.ini is about the DLL, not about the mod.
namespace Kite
{
	struct Config
	{
		bool     Enabled = true;			// [Kite] Enabled
		bool     DebugLog = false;			// [Kite] DebugLog
		LogLevel Level = LogLevel::Info;	// [Kite] LogLevel
		bool     Loaded = false;			// false when kite.ini is absent
		std::wstring Path;					// full path that was looked up
	};

	// Fills `config` from <game directory>\kite.ini using the Win32 profile API,
	// so the project carries no INI parser of its own. A missing file, a missing
	// key and an unparsable value all leave the defaults in place.
	void LoadConfig(Config& config);
}

// The extension's main object.
//
// Lifecycle: DllMain(PROCESS_ATTACH) -> KiteExtension::OnProcessAttach() only
// records the module handle; the Syringe hook installed in dllmain.cpp then
// calls KiteExtension::OnGameStart() once gamemd.exe reaches its entry point,
// which is where the configuration is read and the logger is started.
// On PROCESS_DETACH, KiteExtension::OnProcessDetach() releases the logger.
//
// Add new features by creating a file under src\Ext and calling its Init() from
// KiteExtension::OnGameStart(). Files under src\Ext should stay
// dependency-free so they can be enabled or removed without touching the rest
// of the project.
//
// The class is deliberately not called `Kite`: the configuration and logging
// helpers live in a namespace of that name (namespace Kite), and a class and a
// namespace cannot share a name in the same scope.

class KiteExtension
{
public:
	// Process-wide state. Safe to touch from anywhere in the DLL.
	static const Kite::Config& GetConfig();

	// --- Lifecycle -------------------------------------------------------

	// Runs in DllMain. Never call loader-locking APIs such as LoadLibrary here.
	static void OnProcessAttach(HMODULE module);
	static void OnProcessDetach();

	// Runs from the Syringe hook, i.e. on the game's main thread once the
	// executable entry point is reached. This is the right place for any work
	// that touches engine classes, the file system, or the heap.
	static void OnGameStart();

	// --- Syringe entry point ---------------------------------------------

	// Handles the hook installed at gamemd.exe's entry point (0x7CD810).
	// The address and hook size live in the DEFINE_HOOK call in dllmain.cpp,
	// because the macro needs compile-time literals.
	static DWORD __cdecl OnEntryPointHook(void* registers);

	// Called once per frame by the main loop hook. Only asks the features to
	// report their state; it never touches an engine object.
	static void OnFrame();

private:
	// Loads kite.ini, starts the logger and reports what was found.
	// Called once from OnGameStart().
	static void ApplyConfig();

	static HMODULE ModuleHandle;
	static bool Attached;
	static bool GameStarted;
};
