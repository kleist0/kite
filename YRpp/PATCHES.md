# YRpp patches applied to this copy

This folder is a **complete, unmodified-in-scope copy** of the YRpp shipped with
the Karst project, which in turn is a copy of the YRpp headers that come with the
`October Revolution` project. It is not an upstream YRpp checkout and it is not a
git submodule here.

It is kept **complete** (every engine class header, `YRPP.h`, `StaticInits.cpp`,
`Examples\`, `Blitters\`, `FileFormats\`, about 1.07 MB) on purpose: a feature
that needs an engine class can just `#include <BuildingTypeClass.h>` without
first hunting the header down. Nothing in this folder has been trimmed for Kite.

That original copy predates Visual Studio 2022 17.10 / VS 2026 and does not
compile with the toolset installed on this machine. Three minimal, mechanical
patches were applied so it builds. **Keep this list up to date**: if you refresh
this folder from another YRpp source, re-apply them.

## 1. `ArrayClasses.h` - bare injected-class-names in template bases

Modern MSVC rejects naming a base class template without its argument list when
the base is a template (diagnostic C2955 / C2352 / C2670). Each call below
already intended a non-virtual or explicit call to the base implementation, so
adding the arguments changes no behaviour.

| Line (approx.) | Before | After |
| --- | --- | --- |
| `DynamicVectorClass::SetCapacity` | `VectorClass::SetCapacity(capacity, pMem)` | `VectorClass<T>::SetCapacity(capacity, pMem)` |
| `DynamicVectorClass::Clear` | `VectorClass::Clear()` | `VectorClass<T>::Clear()` |
| `DynamicVectorClass::Purge` | `VectorClass::Purge()` | `VectorClass<T>::Purge()` |
| `DynamicVectorClass::Swap` | `VectorClass::Swap(other)` | `VectorClass<T>::Swap(other)` |
| `TypeList::operator=` | `DynamicVectorClass::operator=(other)` | `DynamicVectorClass<T>::operator=(other)` |
| `TypeList::Swap` | `DynamicVectorClass::Swap(other)` | `DynamicVectorClass<T>::Swap(other)` |
| `CounterClass::Swap` | `VectorClass::Swap(other)` | `VectorClass<int>::Swap(other)` |

`CounterClass` derives from `VectorClass<int>`, hence the concrete argument.

The `VectorClass::Clear()` inside `VectorClass`'s own destructor is valid and was
left alone.

## 2. `Memory.h` - missing standard includes

`Memory.h` uses `std::memset`, `std::allocator` and `std::allocator_traits` but
only included `<exception>` and `<type_traits>`. Older standard libraries pulled
those in transitively; the current ones do not. Added:

```cpp
#include <cstring>
#include <memory>
```

## 3. `Syringe.h` - `declhook` prepends `0x`

**Not a patch** - a property of this copy to be aware of. The `declhook` macro
in this `Syringe.h` already prefixes the address and size with `0x` before token
pasting:

```cpp
#define declhook(hook, funcname, size) \
    ... = { 0x ## hook, 0x ## size, #funcname }; ...
```

Therefore `DEFINE_HOOK` arguments must be written **without** a `0x` prefix:

```cpp
DEFINE_HOOK(7CD810, Kite_EntryPoint, 9)    // correct for this copy
DEFINE_HOOK(0x7CD810, Kite_EntryPoint, 9)  // expands to 0x0x7CD810 - will not compile
```

Phobos and Kratos use a newer `Syringe.h` whose macro does **not** prepend `0x`,
which is why their hook lines look different. If you replace this `Syringe.h`,
re-check every `DEFINE_HOOK` in `src\dllmain.cpp`.

The original upstream note mentions a `Syringe.h.bak` backup next to it; **no such
file is present in this copy** (there is none in `..\Karst\YRpp` either), so
`Syringe.h` is the only variant here and it is the one `Helpers\Macro.h` includes.
