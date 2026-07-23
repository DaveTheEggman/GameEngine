#include <doctest/doctest.h>

#include <cstring>

#include "Core/Debug/Assert.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

import draconic.core;

using namespace draconic::core;

// --- A small reflected hierarchy for the RTTI tests ------------------------
namespace
{
    class Animal : public Object
    {
        DRACONIC_OBJECT(Animal, Object)
    public:
        int legs = 4;

        int AddLegs(int n)
        {
            legs += n;
            return legs;
        }
        int GetLegs() const { return legs; }
        static int DefaultLegs() { return 4; }

        // Object-argument forms: pointer and owning RefPtr.
        int LegsOf(Animal* other) const { return other != nullptr ? other->legs : -1; }
        bool IsSame(RefPtr<Animal> other) const { return other.Get() == this; }
    };

    class Dog : public Animal
    {
        DRACONIC_OBJECT(Dog, Animal)
    public:
        const char* Speak() const { return "woof"; }
    };

    class Cat : public Animal
    {
        DRACONIC_OBJECT(Cat, Animal)
    };
}

DRACONIC_REFLECT(Animal, "draconic::test")
{
    builder.Property<&Animal::legs>("legs");
    builder.Method<&Animal::AddLegs>("AddLegs");
    builder.Method<&Animal::GetLegs>("GetLegs");
    builder.Method<&Animal::DefaultLegs>("DefaultLegs");
    builder.Method<&Animal::LegsOf>("LegsOf"); // takes Animal*
    builder.Method<&Animal::IsSame>("IsSame"); // takes RefPtr<Animal>
    builder.Attribute("scriptName", "Critter");
    builder.Attribute("maxLegs", 8);
    builder.Constructor(); // default ctor -> RefPtr<Animal> via MakeRef
}
DRACONIC_DEFINE_OBJECT(Dog, "draconic::test")
DRACONIC_DEFINE_OBJECT(Cat, "draconic::test")

enum class TestColor : int
{
    Red = 1,
    Green = 2,
    Blue = 4
};

DRACONIC_REFLECT_ENUM(TestColor, "draconic::test")
{
    builder.Value("Red", TestColor::Red);
    builder.Value("Green", TestColor::Green);
    builder.Value("Blue", TestColor::Blue);
}

// --- RTTI ------------------------------------------------------------------

TEST_CASE("rtti: stable type identity")
{
    CHECK(Dog::StaticType().id == Dog::StaticType().id);
    CHECK(Dog::StaticType().id != Cat::StaticType().id);
    CHECK(Dog::StaticType().base == &Animal::StaticType());
    CHECK(Animal::StaticType().base == &Object::StaticType());
    CHECK(Object::StaticType().base == nullptr);

    CHECK(std::strcmp(Dog::StaticType().name, "Dog") == 0);
    CHECK(std::strcmp(Dog::StaticType().namespaceName, "draconic::test") == 0);
}

TEST_CASE("rtti: GetType is virtual through a base pointer")
{
    RefPtr<Dog> dog = MakeRef<Dog>(DefaultAllocator());
    RefPtr<Object> asObject = dog; // upcast

    CHECK(asObject->GetType() == &Dog::StaticType());
    CHECK(dog->GetType()->id == Dog::StaticType().id);
}

TEST_CASE("rtti: object arguments marshal through reflected methods")
{
    RefPtr<Animal> self = MakeRef<Animal>(DefaultAllocator());
    self->legs = 6;
    RefPtr<Animal> other = MakeRef<Animal>(DefaultAllocator());
    other->legs = 4;
    Instance inst = Instance::From(self.Get());

    // U* parameter: pass an object-mode Variant.
    const MethodInfo* legsOf = FindMethod(Animal::StaticType(), "LegsOf");
    REQUIRE(legsOf != nullptr);
    CHECK(ParamAt(*legsOf, 0).type() == &Animal::StaticType()); // object's static type
    Variant otherArg[] = {Variant::From(other)};
    CHECK(InvokeMethod(*legsOf, inst, Span<Variant>{otherArg, 1}).Value().Get<int>() == 4);

    // Polymorphic: a Dog is accepted where Animal* is expected.
    RefPtr<Dog> dog = MakeRef<Dog>(DefaultAllocator()); // legs == 4
    Variant dogArg[] = {Variant::From(dog)};
    CHECK(InvokeMethod(*legsOf, inst, Span<Variant>{dogArg, 1}).Value().Get<int>() == 4);

    // RefPtr<U> parameter (owning): IsSame(self) -> true.
    Variant selfArg[] = {Variant::From(self)};
    CHECK(InvokeMethod(*FindMethod(Animal::StaticType(), "IsSame"), inst, Span<Variant>{selfArg, 1})
              .Value()
              .Get<bool>());

    // A non-object (value) argument is rejected.
    Variant valueArg[] = {Variant::From(5)};
    CHECK_FALSE(InvokeMethod(*legsOf, inst, Span<Variant>{valueArg, 1}).HasValue());

    // No leak from the call: self is held only by `self` and `selfArg` (== 2);
    // the RefPtr<Animal> param copy made during the call was released (else 3).
    CHECK(self->RefCount() == 2u);
}

TEST_CASE("rtti: Construct an Object-derived type via reflection")
{
    Result<Variant> created = Construct(Animal::StaticType(), Span<Variant>{});
    REQUIRE(created.HasValue());
    Variant& v = created.Value();
    CHECK(v.IsObject()); // object mode (RefPtr<Animal>)
    CHECK(v.Type() == &Animal::StaticType());
    Animal* animal = v.AsObject<Animal>();
    REQUIRE(animal != nullptr);
    CHECK(animal->legs == 4);
    CHECK(animal->RefCount() == 1u); // the Variant owns the only ref
}

TEST_CASE("variant: object mode owns a ref and reports the dynamic type")
{
    RefPtr<Dog> dog = MakeRef<Dog>(DefaultAllocator());
    CHECK(dog->RefCount() == 1u);

    {
        // From a RefPtr<Dog> -> object mode (auto-detected).
        Variant v = Variant::From(dog);
        CHECK(v.IsObject());
        CHECK(dog->RefCount() == 2u);          // Variant owns a strong ref
        CHECK(v.Type() == &Dog::StaticType()); // dynamic type, not RefPtr<Object>

        // Borrowed access, with down/up-cast.
        CHECK(v.AsObject() != nullptr);
        CHECK(v.AsObject<Animal>() != nullptr); // upcast
        CHECK(v.AsObject<Dog>() != nullptr);
        CHECK(v.AsObject<Cat>() == nullptr); // wrong branch

        // Copy shares ownership; move transfers it.
        Variant copy = v;
        CHECK(dog->RefCount() == 3u);
        CHECK(copy.Type() == &Dog::StaticType());
        Variant moved = Move(copy);
        CHECK(dog->RefCount() == 3u); // moved, not added
        CHECK(moved.AsObject<Dog>() != nullptr);
    }
    CHECK(dog->RefCount() == 1u); // all Variants released
}

TEST_CASE("variant: a null object ref falls back to the static type")
{
    Variant v = Variant::From(RefPtr<Dog>{});
    CHECK(v.IsObject());
    CHECK(v.Type() == &Dog::StaticType());
    CHECK(v.AsObject() == nullptr);
    CHECK(v.AsObject<Dog>() == nullptr);
}

TEST_CASE("rtti: Cast and IsA walk the inheritance chain")
{
    RefPtr<Dog> dog = MakeRef<Dog>(DefaultAllocator());
    Object* obj = dog.Get();

    CHECK(IsA<Dog>(obj));
    CHECK(IsA<Animal>(obj)); // up the chain
    CHECK(IsA<Object>(obj));
    CHECK_FALSE(IsA<Cat>(obj));

    // Down/cross casts
    REQUIRE(Cast<Animal>(obj) != nullptr);
    REQUIRE(Cast<Dog>(obj) != nullptr);
    CHECK(Cast<Cat>(obj) == nullptr);

    // Casting preserves the object and lets us call derived API.
    Dog* backToDog = Cast<Dog>(Cast<Animal>(obj));
    REQUIRE(backToDog != nullptr);
    CHECK(std::strcmp(backToDog->Speak(), "woof") == 0);

    CHECK_FALSE(IsA<Dog>(static_cast<Object*>(nullptr)));
}

TEST_CASE("rtti: explicit registration and lookup")
{
    TypeRegistry& registry = GlobalTypeRegistry();
    const usize before = registry.Count();

    registry.Register(Object::StaticType());
    registry.Register(Animal::StaticType());
    registry.Register(Dog::StaticType());
    registry.Register(Cat::StaticType());

    CHECK(registry.Count() >= before + 4);

    CHECK(registry.FindById(Dog::StaticType().id) == &Dog::StaticType());
    CHECK(registry.FindByName("draconic::test", "Cat") == &Cat::StaticType());
    CHECK(registry.FindByName("draconic::test", "Missing") == nullptr);

    // Idempotent: re-registering does not duplicate.
    const usize count = registry.Count();
    registry.Register(Dog::StaticType());
    CHECK(registry.Count() == count);
}

// --- RTTI: Variant ---------------------------------------------------------

TEST_CASE("variant: holds small values inline")
{
    Variant v = Variant::From(42);
    CHECK_FALSE(v.IsEmpty());
    CHECK(v.Is<int>());
    CHECK_FALSE(v.Is<float>());

    REQUIRE(v.TryGet<int>() != nullptr);
    CHECK(*v.TryGet<int>() == 42);
    CHECK(v.TryGet<float>() == nullptr);
    CHECK(v.Get<int>() == 42);

    CHECK(v.Type() == &TypeOf<int>());
}

TEST_CASE("variant: holds a Float3 and a large (heap) value")
{
    Variant small = Variant::From(Float3{1.0f, 2.0f, 3.0f});
    REQUIRE(small.Is<Float3>());
    CHECK(*small.TryGet<Float3>() == Float3{1.0f, 2.0f, 3.0f});

    Variant large = Variant::From(Float4x4::Translation(Float3{5.0f, 0.0f, 0.0f}));
    REQUIRE(large.Is<Float4x4>());
    CHECK(large.TryGet<Float4x4>()->m[3][0] == 5.0f);
}

TEST_CASE("variant: copy and move are independent")
{
    Variant a = Variant::From(7);
    Variant b = a; // copy
    *b.TryGet<int>() = 99;
    CHECK(*a.TryGet<int>() == 7);
    CHECK(*b.TryGet<int>() == 99);

    Variant c = Move(b); // move
    CHECK(*c.TryGet<int>() == 99);
    CHECK(b.IsEmpty());

    a.Reset();
    CHECK(a.IsEmpty());
}

TEST_CASE("variant: manages non-trivial payload lifetimes")
{
    struct Tracked
    {
        static int& Live()
        {
            static int n = 0;
            return n;
        }
        int value;
        explicit Tracked(int v = 0) : value(v) { ++Live(); }
        Tracked(const Tracked& o) : value(o.value) { ++Live(); }
        Tracked(Tracked&& o) noexcept : value(o.value) { ++Live(); }
        ~Tracked() { --Live(); }
    };

    Tracked::Live() = 0;
    {
        Variant v = Variant::From(Tracked{5});
        CHECK(Tracked::Live() == 1);
        Variant copy = v;
        CHECK(Tracked::Live() == 2);
        CHECK(copy.TryGet<Tracked>()->value == 5);
    }
    CHECK(Tracked::Live() == 0);
}

TEST_CASE("variant: Instance borrows without owning")
{
    int x = 17;
    Instance inst = Instance::From(&x);
    CHECK_FALSE(inst.IsEmpty());
    CHECK(inst.Type() == &TypeOf<int>());

    REQUIRE(inst.TryGet<int>() != nullptr);
    CHECK(*inst.TryGet<int>() == 17);
    *inst.TryGet<int>() = 23;
    CHECK(x == 23); // writes through to the borrowed object

    CHECK(inst.TryGet<float>() == nullptr); // wrong type
}

// --- RTTI: properties ------------------------------------------------------

TEST_CASE("rtti: reflected property is discoverable")
{
    CHECK(Properties(Animal::StaticType()).Size() == 1u);

    const PropertyInfo* legs = FindProperty(Animal::StaticType(), "legs");
    REQUIRE(legs != nullptr);
    CHECK(std::strcmp(legs->name, "legs") == 0);
    CHECK(legs->type == &TypeOf<int>());

    CHECK(FindProperty(Animal::StaticType(), "missing") == nullptr);
}

TEST_CASE("rtti: property get/set through an Instance")
{
    RefPtr<Animal> animal = MakeRef<Animal>(DefaultAllocator());
    const PropertyInfo* legs = FindProperty(Animal::StaticType(), "legs");
    REQUIRE(legs != nullptr);

    Instance inst = Instance::From(animal.Get());

    Variant got = GetProperty(*legs, inst);
    REQUIRE(got.Is<int>());
    CHECK(got.Get<int>() == 4); // default

    CHECK(SetProperty(*legs, inst, Variant::From(6)).IsOk());
    CHECK(animal->legs == 6); // mutated the real object
    CHECK(GetProperty(*legs, inst).Get<int>() == 6);

    // Wrong-typed value -> error, object unchanged.
    Status bad = SetProperty(*legs, inst, Variant::From(3.5f));
    CHECK_FALSE(bad.IsOk());
    CHECK(bad.Code() == ErrorCode::InvalidArgument);
    CHECK(animal->legs == 6);
}

TEST_CASE("rtti: inherited property is found through the base chain")
{
    // Dog declares no properties of its own but inherits 'legs' from Animal.
    CHECK(Properties(Dog::StaticType()).Size() == 0u);

    const PropertyInfo* legs = FindProperty(Dog::StaticType(), "legs");
    REQUIRE(legs != nullptr);

    RefPtr<Dog> dog = MakeRef<Dog>(DefaultAllocator());
    Instance inst = Instance::From(dog.Get());
    CHECK(SetProperty(*legs, inst, Variant::From(3)).IsOk());
    CHECK(dog->legs == 3);
}

// --- RTTI: methods ---------------------------------------------------------

TEST_CASE("rtti: instance method invoke with an argument and a return value")
{
    RefPtr<Animal> animal = MakeRef<Animal>(DefaultAllocator()); // legs = 4
    Instance inst = Instance::From(animal.Get());

    const MethodInfo* add = FindMethod(Animal::StaticType(), "AddLegs");
    REQUIRE(add != nullptr);
    CHECK_FALSE(add->isStatic);
    CHECK_FALSE(add->isConst);
    CHECK(add->returnType() == &TypeOf<int>());
    CHECK(add->paramCount == 1u);
    CHECK(add->params[0].type() == &TypeOf<int>());

    Variant args[] = {Variant::From(3)};
    Result<Variant> r = InvokeMethod(*add, inst, Span<Variant>{args, 1});
    REQUIRE(r.HasValue());
    CHECK(r.Value().Get<int>() == 7);
    CHECK(animal->legs == 7); // mutated the real object
}

TEST_CASE("rtti: const method and zero-arg invoke")
{
    RefPtr<Animal> animal = MakeRef<Animal>(DefaultAllocator());
    Instance inst = Instance::From(animal.Get());

    const MethodInfo* get = FindMethod(Animal::StaticType(), "GetLegs");
    REQUIRE(get != nullptr);
    CHECK(get->isConst);
    CHECK(get->paramCount == 0u);

    Result<Variant> r = InvokeMethod(*get, inst, Span<Variant>{});
    REQUIRE(r.HasValue());
    CHECK(r.Value().Get<int>() == 4);
}

TEST_CASE("rtti: static method invoke needs no instance")
{
    const MethodInfo* def = FindMethod(Animal::StaticType(), "DefaultLegs");
    REQUIRE(def != nullptr);
    CHECK(def->isStatic);

    Result<Variant> r = InvokeStatic(*def, Span<Variant>{});
    REQUIRE(r.HasValue());
    CHECK(r.Value().Get<int>() == 4);
}

TEST_CASE("rtti: method invoke rejects wrong arity and arg types")
{
    RefPtr<Animal> animal = MakeRef<Animal>(DefaultAllocator());
    Instance inst = Instance::From(animal.Get());
    const MethodInfo* add = FindMethod(Animal::StaticType(), "AddLegs");
    REQUIRE(add != nullptr);

    // Wrong arity.
    Result<Variant> noArgs = InvokeMethod(*add, inst, Span<Variant>{});
    CHECK_FALSE(noArgs.HasValue());
    CHECK(noArgs.Error() == ErrorCode::InvalidArgument);

    // Wrong argument type.
    Variant wrong[] = {Variant::From(2.5f)};
    Result<Variant> badType = InvokeMethod(*add, inst, Span<Variant>{wrong, 1});
    CHECK_FALSE(badType.HasValue());
    CHECK(badType.Error() == ErrorCode::InvalidArgument);

    CHECK(animal->legs == 4); // unchanged after failed calls
}

// --- RTTI: enums -----------------------------------------------------------

TEST_CASE("rtti: enum reflection exposes named values")
{
    DraconicRegisterEnum_TestColor();

    const TypeInfo& type = TypeOf<TestColor>();
    CHECK(IsEnum(type));
    CHECK(Enumerators(type).Size() == 3u);
    CHECK(std::strcmp(type.name, "TestColor") == 0);

    CHECK(std::strcmp(EnumValueName(type, static_cast<i64>(TestColor::Green)), "Green") == 0);
    CHECK(EnumValueName(type, 999) == nullptr);

    i64 value = 0;
    CHECK(EnumValueByName(type, "Blue", value));
    CHECK(value == static_cast<i64>(TestColor::Blue));
    CHECK_FALSE(EnumValueByName(type, "Purple", value));
}

TEST_CASE("rtti: enum values round-trip through a Variant")
{
    DraconicRegisterEnum_TestColor();

    Variant v = Variant::From(TestColor::Green);
    REQUIRE(v.Is<TestColor>());
    CHECK(v.Get<TestColor>() == TestColor::Green);
    CHECK(v.Type() == &TypeOf<TestColor>());
    CHECK(IsEnum(*v.Type()));
}

// --- RTTI: attributes ------------------------------------------------------

TEST_CASE("rtti: type attributes are queryable")
{
    const TypeInfo& type = Animal::StaticType();
    CHECK(Attributes(type).Size() == 2u);

    const Variant* scriptName = FindAttribute(type, "scriptName");
    REQUIRE(scriptName != nullptr);
    REQUIRE(scriptName->Is<const char*>());
    CHECK(std::strcmp(scriptName->Get<const char*>(), "Critter") == 0);

    const Variant* maxLegs = FindAttribute(type, "maxLegs");
    REQUIRE(maxLegs != nullptr);
    CHECK(maxLegs->Get<int>() == 8);

    CHECK(FindAttribute(type, "missing") == nullptr);
}

// --- RTTI: container reflection --------------------------------------------

TEST_CASE("rtti: Array reflected as a container, iterated generically")
{
    RegisterArrayType<int>();

    const TypeInfo& type = TypeOf<Array<int>>();
    REQUIRE(IsContainer(type));
    const ContainerInfo* container = type.container;
    REQUIRE(container != nullptr);
    CHECK(container->elementType == &TypeOf<int>());

    Array<int> values;
    values.PushBack(10);
    values.PushBack(20);
    values.PushBack(30);

    Instance inst = Instance::From(&values);

    CHECK(ContainerSize(*container, inst) == 3u);
    CHECK(ContainerGetAt(*container, inst, 1).Get<int>() == 20);

    // Generic sum without knowing the element type at the call site.
    i64 sum = 0;
    for (usize i = 0; i < ContainerSize(*container, inst); ++i)
    {
        sum += ContainerGetAt(*container, inst, i).Get<int>();
    }
    CHECK(sum == 60);

    // Generic write-back.
    CHECK(ContainerSetAt(*container, inst, 0, Variant::From(99)).IsOk());
    CHECK(values[0] == 99);

    // Type-checked: wrong element type rejected.
    CHECK(ContainerSetAt(*container, inst, 0, Variant::From(1.5f)).Code() ==
          ErrorCode::InvalidArgument);
}
