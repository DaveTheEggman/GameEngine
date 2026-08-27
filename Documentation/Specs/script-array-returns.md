# Native array returns from script facades

> Status: **SHIPPED** (2026-08-26, Opus). Fable unavailable this session; built + self-reviewed, to be
> reviewed retroactively. Motivated by the `overlapSphere -> OverlapHits` workaround (physics shape
> queries): a facade that produces a SET had no way to hand script a real array, so it returned a
> bespoke `count()/entity(i)` wrapper handle. This track gives facades a first-class array return that
> each backend renders as its NATIVE array, and retires the wrapper. Element types wired + tested:
> numeric scalars, `String`, and reflected value/object handles (`Entity`, ...) - both backends, clang
> and gcc. `overlapSphere` migrated to `Array<Entity>`; `OverlapHits` deleted.

## The problem

The script-facade surface had no array type. A facade method could return a scalar, a `String`, an
`Entity`, a reflected value handle, or a `Variant` - but not a sequence. Query facades that produce a
SET (overlap-all, raycast-all, "find every entity with tag X") had two bad options:

1. Return only the nearest/first (loses information - the pushback that started this).
2. Wrap the set in a hand-rolled reflected type with `count()` + `at(i)` accessors (`OverlapHits`).

Option 2 works but is boilerplate per query, is not idiomatic (script authors expect `arr.length` /
`foreach` / index `[]`), and does not compose (cannot pass to a function that takes an array).

## Why a first-class array is feasible - the seam already exists

The whole facade boundary is `Variant`-based and backend-neutral. Crucially, the reflection layer
ALREADY models containers: `ContainerInfo` (`Reflection.cppm`) hangs off any `TypeInfo`
(`type.container`), giving `size()` / `getAt(i)` / `elementType`. `RegisterArrayType<T>()` patches
`TypeOf<Array<T>>().container`, so an `Array<T>` value carried in a `Variant` reports
`IsContainer(*value.Type()) == true` and can be walked element by element - with NO backend-specific
code in the facade.

The AngelScript backend already USES this for container *members* of a reflected type: it flattens an
`Array<T>` property into `x.items_count()` / `x.items_at(i)` on the owner (`RegisterContainerMethods`).
What was missing was applying the same reflection to a value a method *returns*, and rendering it as the
backend's native array rather than a flattened member. (Luau had no container support at all.)

## Design

A facade returns a plain engine `Array<T>` by value. Reflection carries it as a container `Variant`.
Each backend's RETURN marshaling detects `IsContainer` and materializes its native array:

- **AngelScript**: the vendored `scriptarray` add-on (`array<T>`, `CScriptArray`). The method's declared
  return type is spelled `array<Elem>@`; on return, a `CScriptArray` is built from the container and
  handed back as a handle. Script authors get `arr.length`, `arr[i]`, `foreach`.
- **Luau**: a plain 1-indexed Lua table. Script authors get `#arr` and `ipairs`.

Element types supported: whatever already marshals as a single return element - numeric scalars,
`String`, `Entity`, and any reflected value/object handle. That covers every query use case.

### Scope (this track)

- RETURN path only. Passing a script array INTO a facade to fill (`array<T>&out`) is a separate, larger
  change (arg direction + writing engine data back) and is deferred - see Follow-ups.
- Homogeneous element type (the `Array<T>` flavor). Polymorphic `Array<RefPtr<Base>>` returns are not a
  facade need today.

### The u64/Luau precision rule

A Lua table element is an `f64`; a packed entity word (`u64`, 32+32) exceeds 2^53 and would corrupt.
So a facade must NEVER return `Array<u64>` of packed handles - it returns `Array<Entity>` (resolved).
The element that crosses is the reflected `Entity`, which both backends box losslessly. This is why
`overlapSphere` builds `Array<Entity>`, not `Array<u64>`.

## Implementation

### 1. Vendor the add-on
`ThirdParty/angelscript/add_on/scriptarray/` (same SDK origin as the vendored `scriptstdstring` /
`scriptbuilder`, version 23900). `scriptarray.cpp` added to the `angelscript` static lib.

### 2. AngelScript backend (`AngelScriptScriptImpl.cpp`)
- `RegisterScriptArray(engine, true)` at engine bring-up (default-array so `T[]` also parses).
- `AppendDeclType`: a container type spells `array<Elem>@` (return) / `array<Elem>` (param placeholder);
  `Elem` reuses the element type's own spelling (`Entity@`, `int`, `string`, ...).
- `SetGenericReturn`: before the plain object-handle boxing, if the return slot is a handle and the
  `Variant` is a container, build a `CScriptArray` via `BuildScriptArray`.
- `BuildScriptArray(arrType, containerType, value)`: `ToInstance(value)` -> walk `ci.getAt(i)`; for a
  handle subtype each element is a `BoxedVariant*` set via `arr->SetValue(i, &box)` (SetValue AddRefs;
  the local ref is then released so the array owns the only ref); numeric subtypes write a width-matched
  temporary.

### 3. Luau backend (`LuauScript.cppm`)
- `PushVariant`: if `value.Type()` is a container, `lua_newtable` + recursively `PushVariant` each
  `getAt(i)` at 1-based index. GC-managed; no manual refcount.

### 4. Retire `OverlapHits`
- `ScenePhysics::overlapSphere` returns `Array<Entity>` (was `OverlapHits`). `RegisterArrayType<Entity>()`
  is called in the physics facade registrar; `Array<Entity>` need not be a script root (it renders as a
  native array, not a boxed handle). The `OverlapHits` type, its reflection, its surface registration,
  and the `RegisterExtraFacadeName(u8"OverlapHits")` are deleted. `kSubsystemFacadeNameCount` 34 -> 33.
- `nearestOverlap` (single closest) stays as the convenience.

## Tests

- Foundation script backends: array-return round-trips on BOTH backends (a facade/native method returning
  `Array<Entity>` iterated by `.length`/`[]` in AngelScript and `#`/index in Luau).
- `Engine.Physics.Tests`: `overlapSphere` returns `Array<Entity>`; count + element resolution + group
  filter + empty result.
- `Engine.Script.Tests`: an AngelScript behavior sweeps `overlapSphere` and acts on every element via
  `foreach`/index; a Luau behavior does the same via `ipairs` - both impulse every hit.
- `Engine.ScriptSurface.Tests`: count 33.
- Both toolchains (clang + gcc) green.

## Follow-ups (not this track)

- Array PARAMETER (fill-in / pass-a-set-to-a-facade).
- Polymorphic-element returns (`Array<RefPtr<Base>>`).
- Wider facade adoption (raycast-all, tag/type queries) now that the return shape exists.
