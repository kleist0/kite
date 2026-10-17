#include <Ext/SidebarMix/SidebarMix.h>

#include <Core/Log.h>
#include <Core/MemorySafety.h>
#include <Kite.h>

// The country list (`HouseTypeClass::Array`), the session that holds the
// player's pick (`SessionClass::Instance`) and the rules INI the key is read
// from (`CCINIClass::INI_Rules`).
//
// ArrayClasses.h must come FIRST: SessionClass declares
// DynamicVectorClass<NodeNameType*> members, and that template does not declare
// itself, so including SessionClass.h on its own fails with C7568.
#include <ArrayClasses.h>
#include <SessionClass.h>
#include <HouseTypeClass.h>
#include <CCINIClass.h>

namespace Kite
{
	namespace Ext
	{
		namespace SidebarMix
		{
			namespace
			{
				const char* const kMixFileIndexKey = "Sidebar.MixFileIndex";

				// Reads the country index the player selected in the game-setup
				// screen. Returns -1 when there is no session yet (main menu,
				// loading screen) or when the session holds no start spot.
				//
				// WHY THE SESSION AND NOT `[Basic] Player=`
				//
				// In a skirmish `[Basic] Player=` holds the player's NAME, so
				// resolving it through the country list returns -1 and the engine
				// takes its "unknown" branch. The country the player actually
				// picked lives in the session's first start spot - which is also
				// what Ares reads for its own per-side UI work.
				//
				// SessionClass::Instance is a value fixed at compile time (the
				// object itself sits at 0xA8B238), so reading the symbol cannot
				// fault; the member reads are guarded because the object is only
				// valid once a session exists.
				int SelectedCountryIndex()
				{
					__try
					{
						SessionClass* session = SessionClass::Instance;

						if (!session || session->StartSpots.Count <= 0)
							return -1;

						NodeNameType* spot = session->StartSpots.Items[0];

						return spot ? spot->Country : -1;
					}
					__except (EXCEPTION_EXECUTE_HANDLER)
					{
						return -1;
					}
				}

				// Country index -> the ID of that country, which is exactly the
				// rulesmd.ini section the key is written in.
				//
				// Returns nullptr when the country list does not exist yet or the
				// index is out of range. Both pointer hops are guarded: the list is
				// an engine global bound to a fixed address.
				const char* CountrySectionName(int countryIndex)
				{
					__try
					{
						if (!HouseTypeClass::Array
							|| countryIndex < 0
							|| countryIndex >= HouseTypeClass::Array->Count)
						{
							return nullptr;
						}

						HouseTypeClass* houseType =
							HouseTypeClass::Array->Items[countryIndex];

						if (!houseType || !houseType->ID
							|| !Kite::MemorySafety::IsReadable(houseType->ID, 1))
						{
							return nullptr;
						}

						return houseType->ID;
					}
					__except (EXCEPTION_EXECUTE_HANDLER)
					{
						return nullptr;
					}
				}
			}

			void Init()
			{
				// Nothing to prepare. The country is only known once a match is
				// being set up, and the rules INI only exists after the engine has
				// loaded it, so the value can only be read from the hook at
				// 0x5350D3 - the moment the engine itself decides which
				// SIDECnnMD.MIX to name. This line exists so the log states that
				// the feature is installed; a user who sees no line here would
				// otherwise have to guess whether the DLL is even the one they
				// built.
				Kite::LogInfo(L"SidebarMix: per-country [Sidebar.MixFileIndex] is installed.");
			}

			bool CountryMixIndex(int& mixIndex)
			{
				// Enabled=no in kite.ini means "load the DLL and do nothing".
				if (!KiteExtension::GetConfig().Enabled)
					return false;

				const int countryIndex = SelectedCountryIndex();

				if (countryIndex < 0)
					return false;

				// Country index -> ID, which is the INI section to read.
				const char* section = CountrySectionName(countryIndex);

				if (!section)
					return false;

				// Read through the safe slot reader rather than dereferencing the
				// global directly: CCINIClass::INI_Rules is a reference bound to a
				// fixed address, so a plain read touches that address. This way the
				// address is proven readable first and a bad slot yields nullptr
				// instead of a fault inside the game loop.
				CCINIClass* ini = Kite::MemorySafety::SafeReadSlot<CCINIClass*>(
					&CCINIClass::INI_Rules);

				if (!ini)
					return false;

				// 1-based, same as Ares: 1 == SIDEC01MD.MIX.
				const int value = ini->ReadInteger(section, kMixFileIndexKey, -1);

				// A country without the key (or with 0 / a negative value) is left
				// completely alone, so this cannot change anything for mods that do
				// not use it.
				if (value < 1)
					return false;

				mixIndex = value;

				Kite::LogInfo(L"SidebarMix[mixcountry] '%hs' (index %d) -> "
					L"Sidebar.MixFileIndex=%d", section, countryIndex, value);

				return true;
			}
		}
	}
}
