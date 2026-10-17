#include <Kite.h>

#include <Core/Log.h>
#include <Ext/SidebarMix/SidebarMix.h>
#include <Ext/BuildCost/BuildCost.h>
#include <Version.h>

#include <cwchar>

HMODULE KiteExtension::ModuleHandle = nullptr;
bool KiteExtension::Attached = false;
bool KiteExtension::GameStarted = false;

namespace
{
	// The one Config instance. Function-local so construction order across
	// translation units cannot matter.
	Kite::Config& MutableConfig()
	{
		static Kite::Config instance;
		return instance;
	}

	// <game directory>\kite.ini - the folder gamemd.exe sits in, which is also
	// where Kite.dll was loaded from.
	bool BuildIniPath(std::wstring& path)
	{
		wchar_t module[MAX_PATH] = { 0 };

		if (::GetModuleFileNameW(nullptr, module, MAX_PATH) == 0)
			return false;

		wchar_t* lastSeparator = ::wcsrchr(module, L'\\');

		if (!lastSeparator)
			return false;

		*(lastSeparator + 1) = L'\0';

		path = module;
		path += L"kite.ini";
		return true;
	}

	// One [Kite] key, parsed as a switch. Accepts 1/0, yes/no, true/false,
	// on/off (case-insensitive) - the spellings the INI template and Karst both
	// used. An absent key, an empty value and an unknown word all yield
	// `fallback`.
	bool ReadIniBool(const std::wstring& ini, const wchar_t* key, bool fallback)
	{
		wchar_t buffer[32] = { 0 };

		::GetPrivateProfileStringW(L"Kite", key, L"", buffer, _countof(buffer), ini.c_str());

		if (!buffer[0])
			return fallback;

		if (::_wcsicmp(buffer, L"yes") == 0 || ::_wcsicmp(buffer, L"true") == 0
			|| ::_wcsicmp(buffer, L"on") == 0 || ::_wcsicmp(buffer, L"1") == 0)
		{
			return true;
		}

		if (::_wcsicmp(buffer, L"no") == 0 || ::_wcsicmp(buffer, L"false") == 0
			|| ::_wcsicmp(buffer, L"off") == 0 || ::_wcsicmp(buffer, L"0") == 0)
		{
			return false;
		}

		return fallback;
	}
}

void Kite::LoadConfig(Config& config)
{
	config = Config();

	std::wstring ini;

	if (!BuildIniPath(ini))
		return;

	config.Path = ini;
	config.Loaded = (::GetFileAttributesW(ini.c_str()) != INVALID_FILE_ATTRIBUTES);

	config.Enabled = ReadIniBool(ini, L"Enabled", true);
	config.DebugLog = ReadIniBool(ini, L"DebugLog", false);

	wchar_t level[32] = { 0 };
	::GetPrivateProfileStringW(L"Kite", L"LogLevel", L"info", level, _countof(level), ini.c_str());
	config.Level = Log::ParseLevel(level);
}

const Kite::Config& KiteExtension::GetConfig()
{
	return MutableConfig();
}

void KiteExtension::OnProcessAttach(HMODULE module)
{
	KiteExtension::ModuleHandle = module;
	KiteExtension::Attached = true;

	// At this point the loader lock is held: only touch memory and primitives.
	// File I/O and the logger are set up in OnGameStart().
}

void KiteExtension::OnProcessDetach()
{
	if (!KiteExtension::Attached)
		return;

	Kite::Log::Shutdown();

	KiteExtension::Attached = false;
	KiteExtension::GameStarted = false;
	KiteExtension::ModuleHandle = nullptr;
}

void KiteExtension::OnGameStart()
{
	if (KiteExtension::GameStarted)
		return;

	KiteExtension::GameStarted = true;

	KiteExtension::ApplyConfig();

	Kite::LogInfo(L"%s started (build %s).", KITE_NAME_WSTR, KITE_VERSION_FULL_WSTR);

	// Feature 1 (Ext\SidebarMix) needs no setup: it reads the player's country
	// and that country's rulesmd.ini section inside its own hook at 0x5350D3, at
	// the exact moment the engine turns a side index into "SIDECnnMD.MIX". This
	// call only states in the log that the feature is installed - it deliberately
	// does not resolve the index here, because at the executable entry point no
	// session exists yet and therefore no country can be known.
	Kite::Ext::SidebarMix::Init();

	// Feature 2 (Ext\BuildCost). Nothing to prepare here either: its value can
	// only be read from the rules INI while the engine reads that INI, which is
	// what its hook at 0x7146B3 does. This call only logs that it is installed.
	Kite::Ext::BuildCost::Init();

	// Register your own features here, e.g.
	//   Kite::Ext::MyFeature::Init();
	// Each feature should check KiteExtension::GetConfig().Enabled before acting.
	//
	// Note: features that need the engine's INI data cannot run yet. This hook
	// fires at the executable entry point, before the type lists and the ART INI
	// are loaded. Such features must read their data from a hook that fires late
	// enough - see Ext\SidebarMix for an example.
}

void KiteExtension::ApplyConfig()
{
	Kite::Config& config = MutableConfig();
	Kite::LoadConfig(config);

	Kite::Log::Init(config.Level, config.DebugLog);

	if (!config.Loaded)
	{
		Kite::LogInfo(L"kite.ini not found, using defaults.");
	}
	else
	{
		Kite::LogInfo(L"Loaded %ls", config.Path.c_str());
	}

	if (!config.Enabled)
	{
		// Enabled=no. Keep the DLL resident but do nothing: every feature checks
		// this flag, and the log level has already been applied so the reason
		// stays visible in the debug output.
		Kite::LogInfo(L"Kite is disabled by kite.ini (Enabled=no).");
	}

	Kite::LogDebug(L"Config: Enabled=%d LogLevel=%hs DebugLog=%d",
		config.Enabled ? 1 : 0,
		Kite::Log::LevelName(config.Level),
		config.DebugLog ? 1 : 0);
}

DWORD __cdecl KiteExtension::OnEntryPointHook(void* /*registers*/)
{
	// The hook fires on the game's main thread, so this is the safe moment to
	// touch the file system, the CRT heap and the logger.
	KiteExtension::OnGameStart();

	// 0 means "continue": Syringe restores the original instructions and the
	// game proceeds normally. Return a non-zero value only if you intend to
	// replace the hooked code entirely.
	return 0;
}

void KiteExtension::OnFrame()
{
	// Runs every frame from the main loop hook in dllmain.cpp.
	//
	// This deliberately does NOT touch any engine object here.
	//
	// The reason is worth knowing before you add one: YRpp globals such as
	// SessionClass::Instance are bound to fixed addresses, and "the address is
	// readable" is NOT the same as "the pointer is a live engine object". A
	// fixed address can hold unrelated mapped memory, so VirtualQuery says
	// readable, the pointer survives validation, and the first call through it
	// crashes. Kite::MemorySafety filters the obvious cases; it cannot make a
	// stale pointer safe.
	//
	// Both of this project's features are therefore driven entirely by their own
	// hooks, at points where the engine has already handed them a live object:
	// Ext\SidebarMix at 0x5350D3 and Ext\BuildCost at 0x7146B3. Keep per-frame
	// work limited to the DLL's own state.
	static bool announced = false;
	if (!announced)
	{
		announced = true;

		Kite::LogDebug(L"First frame reached (main loop hook is live, enabled=%d).",
			KiteExtension::GetConfig().Enabled ? 1 : 0);
	}

	// A SEPARATE feature (Ext\BuildCost). Its work is done inside the hook at
	// 0x7146B3; this only reports, once per change, how many type sections were
	// priced - a few integer comparisons per frame the rest of the time.
	Kite::Ext::BuildCost::OnFrame();
}
