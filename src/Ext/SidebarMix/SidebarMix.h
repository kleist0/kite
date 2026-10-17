#pragma once

// "Sidebar.MixFileIndex" - let **each country** use its own sidebar MIX.
//
//     [Americans]
//     Sidebar.MixFileIndex=2      ; 1-based: 2 -> SIDEC02MD.MIX
//
//     [Russians]
//     Sidebar.MixFileIndex=1
//
// The key is written in the COUNTRY's own section of rulesmd.ini. A country
// that does not declare it is left completely untouched, so a mod that makes no
// use of this feature behaves exactly as if Kite were not loaded.
//
// WHAT CHANGES
//
// Only the FILE NAME the engine builds. Everything downstream - loading the
// MIX, laying out the sidebar, drawing the cameos, the radar frame, the credits
// strip - stays the engine's own work, unchanged. This module draws nothing and
// copies nothing.
//
// DIFFERENCE FROM ARES (the reason this module exists)
//
// Ares' `Sidebar.MixFileIndex` is per SIDE (faction), because the engine only
// ever hands the SIDE index to the function that builds the name. Kite reads
// the same key from the COUNTRY's section instead, so two countries inside one
// side can use different sidebars. The two can be used together: Ares decides
// by side, Kite overrides by country when the country has a rule.
//
// THE TWO ENGINE FACTS THIS MODULE IS BUILT ON
//
// 1. WHERE THE COUNTRY IS
//
//    The country the player picked in the game-setup screen lives in the
//    session's first start spot:
//
//        SessionClass::Instance (0xA8B238)
//            -> StartSpots (DynamicVectorClass<NodeNameType*>)
//                -> Items[0]->Country          <- country index
//
//    NOT `[Basic] Player=`: in a skirmish that key holds the player's NAME, so
//    resolving it through the country list returns -1. (Hooking that branch was
//    tried and never fired - that is how this was found.) The country index is
//    turned into a section name through HouseTypeClass::Array[index]->ID.
//
// 2. WHERE THE NAME IS BUILT
//
//        0x0067E74A  mov  eax,[0xa8b230]      ; ScenarioClass::Instance
//        0x0067E75B  mov  ecx,[eax+0x34B8]    ; PlayerSideIndex - a SIDE, not a country
//        0x0067E761  call 0x534FA0            ; -> "SIDEC%02dMD.MIX"
//
//        0x00534FA0  sub  esp,0x11c           ; entry
//        0x00534FAA  mov  esi,ecx             ; esi = side index
//        0x00534FB1  cmp  esi,2               ; <- Ares hooks HERE (5 bytes)
//        0x00534FB4  jne  0x534FBB
//        0x00534FB6  mov  esi,1               ; stock: Yuri counts as 1
//        0x00534FBB  mov  ebx,esi
//        ...
//        0x005350D3  lea  esi,[ebx+1]         ; ★ Kite hooks here (7 bytes)
//        0x005350D6  lea  eax,[esp+0x14]
//        0x005350DA  push esi
//        0x005350DB  push 0x827e0c            ; "SIDEC%02dMD.MIX"
//        0x005350E1  call 0x7c8ef4            ; sprintf -> the actual file name
//
//    0x5350D3 is the convergence point where the side index finally becomes the
//    file name. It is far from Ares' 5-byte site at 0x534FB1, so the two hooks
//    do not overlap and can be installed at the same time.
//
//    The engine adds 1 back (`lea esi,[ebx+1]`), so the hook writes
//    `MixFileIndex - 1` into EBX: a rule of 2 produces SIDEC02MD.MIX.
//
//    THE HOOK MUST COVER 7 BYTES (3 + 4), not 3. Declaring only
//    `lea esi,[ebx+1]` lets Syringe's 5-byte `jmp` swallow the first two bytes
//    of the following `lea eax,[esp+0x14]`; resuming at 0x5350D6 then runs a
//    corrupted instruction and the sprintf arguments are garbage (measured:
//    C0000005 at 0x7CE9F6). Every hook in this project must also be at least 5
//    bytes long, because that is the size of the `jmp` Syringe writes.

namespace Kite
{
	namespace Ext
	{
		namespace SidebarMix
		{
			// Called once from KiteExtension::OnGameStart().
			//
			// Does no work of its own: the country is only known, and the rules
			// INI only loaded, long after the executable entry point. This call
			// exists so the log states that the feature is installed.
			void Init();

			// Called from the hook at 0x5350D3, immediately before the engine
			// turns the side index into "SIDEC%02dMD.MIX".
			//
			// Returns true and fills `mixIndex` (1-based) only when the country
			// the local player picked declares `Sidebar.MixFileIndex` with a
			// value >= 1. A country without the key, an unreadable session, an
			// unreadable INI and values below 1 all return false, which leaves
			// the engine's own answer untouched.
			bool CountryMixIndex(int& mixIndex);
		}
	}
}
