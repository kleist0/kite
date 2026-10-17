// Kite - Yuri's Revenge engine extension.
//
// This file owns the DLL entry point and every Syringe hook the project
// installs. Keep the hooks here and put the actual behaviour in Kite.cpp or
// under src\Ext\, so the list of patched addresses stays readable in one place.

#include <Windows.h>

#include <Kite.h>
#include <Core/Log.h>
#include <Ext/SidebarMix/SidebarMix.h>
#include <Ext/BuildCost/BuildCost.h>
#include <Version.h>

#include <Helpers/Macro.h>

// ---------------------------------------------------------------------------
// HOOK INVENTORY  (4 hooks - keep this list in sync with the DEFINE_HOOKs)
// ---------------------------------------------------------------------------
//
//   0x7CD810  9  Kite_EntryPoint        gamemd.exe entry point -> OnGameStart
//   0x55D360  5  Kite_MainLoop          once per frame       -> OnFrame
//   0x5350D3  7  Kite_SidebarMix_...    Ext\SidebarMix  (the feature)
//   0x7146B3  6  Kite_BuildCost_...     Ext\BuildCost   (the feature)
//
// Two of the four exist only to start and to tick this DLL; they are shared by
// every feature. Each feature adds exactly one hook, at a point where the
// engine has already handed it a live object.
//
// RULES FOR ADDING ONE (both were learned the hard way - see the notes at each
// hook below):
//
//   * size >= 5, because Syringe writes a 5-byte `jmp`;
//   * the size must cover WHOLE instructions, otherwise the resumed code is
//     garbage;
//   * check the address against Ares / Phobos / Kratos, which hook the same
//     executable.

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		// Do not call LoadLibrary, create threads or touch the heap here: the
		// loader lock is held. KiteExtension::OnGameStart() does the real setup.
		::DisableThreadLibraryCalls(hModule);
		KiteExtension::OnProcessAttach(hModule);
		break;

	case DLL_PROCESS_DETACH:
		KiteExtension::OnProcessDetach();
		break;

	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	default:
		break;
	}

	return TRUE;
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------
//
// NOTE ON THE ADDRESS SYNTAX
// The declhook() macro inside YRpp\Syringe.h already prepends "0x" to the
// address and the size before pasting the tokens together, so the arguments
// below must be written WITHOUT a 0x prefix: writing 0x7CD810 would expand to
// the invalid literal 0x0x7CD810. (Phobos and Kratos use a newer Syringe.h that
// does not prepend 0x, which is why their DEFINE_HOOK lines look different.)
// If you ever update the YRpp copy in this project, re-check this.

// gamemd.exe entry point. By the time this runs, Syringe has already loaded
// every other extension DLL, so the engine is as ready as it will ever be.
DEFINE_HOOK(7CD810, Kite_EntryPoint, 9)
{
	return KiteExtension::OnEntryPointHook(R);
}

// Main loop, executed once per frame. Used here to report feature state; it
// deliberately touches no engine object (see KiteExtension::OnFrame).
DEFINE_HOOK(55D360, Kite_MainLoop, 5)
{
	KiteExtension::OnFrame();

	return 0;
}

// ---------------------------------------------------------------------------
// Per-COUNTRY sidebar MIX  (0x5350D3)  --  Ext\SidebarMix
// ---------------------------------------------------------------------------
//
// 0x5350D3 is where the engine finally turns a side index into a file name:
//
//   0x005350D3  lea  esi, [ebx + 1]    ; side index + 1
//   0x005350D6  lea  eax, [esp+0x14]
//   0x005350DA  push esi
//   0x005350DB  push 0x827e0c          ; "SIDEC%02dMD.MIX"
//   0x005350E1  call 0x7c8ef4          ; sprintf
//
// Ares hooks 0x534FB1 and rewrites ESI from its own per-SIDE setting. Kite hooks
// here instead, far away from Ares' 5 bytes, and rewrites EBX when the player's
// COUNTRY declares `Sidebar.MixFileIndex`. Because `lea esi,[ebx+1]` runs with the
// substituted EBX, the engine builds the country's MIX name itself.
//
// Only the name is changed: loading the MIX, building the sidebar and drawing the
// cameos all remain the engine's own work.
//
// SIZE MUST COVER 7 BYTES (3 + 4), not 3.
//
// Declaring 3 (just `lea esi,[ebx+1]`) let Syringe's 5-byte jmp eat the first two
// bytes of the following `lea eax,[esp+0x14]`; resuming at 0x5350D6 then ran a
// corrupted instruction and sprintf got garbage stack arguments - C0000005 at
// 0x7CE9F6.
DEFINE_HOOK(5350D3, Kite_SidebarMix_CountryMix, 7)
{
	int mixIndex = 0;

	if (Kite::Ext::SidebarMix::CountryMixIndex(mixIndex))
	{
		// The engine adds 1 back (`lea esi,[ebx+1]`), so subtract it here: a rule
		// of 2 must produce SIDEC02MD.MIX.
		R->EBX(mixIndex - 1);
	}

	return 0;
}

// ---------------------------------------------------------------------------
// Global build cost  (0x7146B3)  --  Ext\BuildCost
// ---------------------------------------------------------------------------
//
// THE ENGINE'S PATH (measured in gamemd 1.001)
//
// A TechnoTypeClass' cost is read in exactly ONE place in the whole binary:
// the string "Cost" (0x825470) has a single reference that stores into a
// type's cost field. That reader is shared by every type class, which is what
// makes one hook cover infantry, units, aircraft and buildings alike:
//
//   0x0071469F  mov  ecx, [ebp + 0x610]   ; the type's current Cost (default)
//   0x007146A5  push ecx
//   0x007146A6  push 0x825470             ; "Cost"
//   0x007146AB  push ebx                  ; section name = the type's ID
//   0x007146AC  mov  ecx, esi             ; CCINIClass* (the rules INI)
//   0x007146AE  call 0x5276D0             ; CCINIClass::ReadInteger
//   0x007146B3  mov  [ebp + 0x610], eax   ; <- hook here
//
// WHY THIS IS THE RIGHT POINT
//
// The hook runs BEFORE the engine stores the value, so putting our number back
// into EAX makes the engine's own instruction write it. Nothing has to be
// copied afterwards and no sweep over the type arrays is needed.
//
// The value comes from the same CCINIClass the engine is reading (ESI), section
// [General].
//
// SIZE IS EXACTLY 6, and it covers one whole instruction:
//
//   0x7146B3  mov dword ptr [ebp+0x610], eax    89 85 10 06 00 00
//   0x7146B9  mov edx, dword ptr [ebp+0x614]    <- the next instruction
//
// WHAT THE FEATURE DOES, AND WHAT IT DRAGS ALONG
//
// `[General] BuildCost=<int>` replaces every type's Cost. Because Cost is the
// engine's single source of truth for price, the sidebar price, the money
// actually paid, the refund and the AI's estimates all follow it - and so does
// the build TIME, because 0x711EE0 computes `(int)(Cost * BuildSpeed * 0.9)`.
// That coupling is the engine's own arithmetic, not something this DLL does;
// see src\Ext\BuildCost\BuildCost.h and README.md.
//
// No key, an unreadable INI or a negative value leave the engine's answer
// completely untouched.
DEFINE_HOOK(7146B3, Kite_BuildCost_CostOverride, 6)
{
	int cost = 0;

	if (Kite::Ext::BuildCost::QueryOverride(
		reinterpret_cast<void*>(R->ESI()),
		reinterpret_cast<const char*>(R->EBX()),
		cost))
	{
		R->EAX(cost);
	}

	return 0;
}
