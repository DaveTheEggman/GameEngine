// draconic.settings: typed sections round-trip through the serializer abstraction (binary factory
// here; the editor exercises the XML factory). Also covers defaults, change notification, and the
// core UserDataDir / GetEnvironmentVariable helpers the store's storage location builds on.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import draconic.core;
import draconic.settings;

using namespace draconic::core;
namespace settings = draconic::settings;

namespace
{
    // A test settings section (v1): plain String fields so it round-trips on any backend.
    class GameSettings : public ISerializable
    {
        DRACONIC_OBJECT(GameSettings, ISerializable)
    public:
        String profile = String(u8"default");
        String locale  = String(u8"en");

        void Serialize(ISerializer& ar) override
        {
            draconic::core::Serialize(ar, "profile", profile);
            draconic::core::Serialize(ar, "locale", locale);
        }
    };
    DRACONIC_DEFINE_OBJECT_VERSIONED(GameSettings, "draconic::test", 1)

    // Load needs the type resolvable by name + constructible by id (idempotent to call repeatedly).
    void EnsureRegistered()
    {
        GlobalTypeRegistry().Register(GameSettings::StaticType());
        RegisterSerializable<GameSettings>();
    }
}

TEST_CASE("settings: a fresh section reads its struct defaults")
{
    settings::Settings s;
    CHECK(s.SectionCount() == 0u);
    CHECK(s.Find<GameSettings>() == nullptr);

    GameSettings& g = s.Section<GameSettings>();   // lazily created
    CHECK(g.profile == u8"default");
    CHECK(g.locale == u8"en");
    CHECK(s.SectionCount() == 1u);
    CHECK(s.Find<GameSettings>() != nullptr);
}

TEST_CASE("settings: sections round-trip through the (binary) serializer factory")
{
    EnsureRegistered();

    MemoryStream stream;
    {
        settings::Settings s;
        GameSettings& g = s.Section<GameSettings>();
        g.profile = String(u8"hardcore");
        g.locale  = String(u8"fr");
        REQUIRE(s.Save(stream, BinarySerializerFactory()).IsOk());
    }

    REQUIRE(stream.Seek(0, SeekOrigin::Begin) == 0);
    {
        settings::Settings s;
        REQUIRE(s.Load(stream, BinarySerializerFactory()).IsOk());
        const GameSettings* g = s.Find<GameSettings>();
        REQUIRE(g != nullptr);
        CHECK(g->profile == u8"hardcore");
        CHECK(g->locale == u8"fr");
    }
}

TEST_CASE("settings: MarkChanged fires OnChanged with the section type name")
{
    settings::Settings s;
    String changed;
    s.OnChanged([&](StringView name) { changed = String(name); });

    s.Section<GameSettings>().profile = String(u8"x");
    s.MarkChanged<GameSettings>();
    CHECK(changed == u8"GameSettings");
}

TEST_CASE("core/system: GetEnvironmentVariable + UserDataDir")
{
    // PATH is defined on every platform we target; a bogus name is absent.
    CHECK(GetEnvironmentVariable(u8"PATH").HasValue());
    CHECK_FALSE(GetEnvironmentVariable(u8"DRACONIC_DEFINITELY_NOT_SET_XYZ_123").HasValue());
    CHECK(GetUserDataDirectory(u8"draconic").Size() > 0u);

    // The running test executable resolves, and its directory is a prefix of the full path.
    const String exePath = GetExecutablePath();
    const String exeDir = GetExecutableDirectory();
    CHECK(exePath.Size() > 0u);
    CHECK(exeDir.Size() > 0u);
    CHECK(exeDir.Size() < exePath.Size());
    CHECK(exePath.AsView().StartsWith(exeDir.AsView()));
}
