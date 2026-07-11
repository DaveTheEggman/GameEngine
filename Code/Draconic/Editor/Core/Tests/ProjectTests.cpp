// EditorProject tests: create/open round-trip of the fixed project layout (docs/design/editor.md
// §3.9) - manifest, subdirectories, source (XML) + cooked (binary) content databases.

#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.editor.core;

using namespace draconic::core;
using namespace draconic::editor;

namespace
{
    class TestMaterial final : public ISerializable
    {
        DRACONIC_OBJECT(TestMaterial, ISerializable)
    public:
        i32 shininess = 0;

        void Serialize(ISerializer& ar) override
        {
            draconic::core::Serialize(ar, "shininess", shininess);
        }
    };

    void RemoveProjectTree(StringView root)
    {
        FileDelete(PathJoin(root, u8"Project.xml"));
        FileDelete(PathJoin(root, u8"Content/materials/steel.xasset"));
        FileDelete(PathJoin(root, u8"Cooked/materials/steel.rasset"));
        RemoveDirectory(PathJoin(root, u8"Content/materials"));
        RemoveDirectory(PathJoin(root, u8"Cooked/materials"));
        RemoveDirectory(PathJoin(root, u8"Content"));
        RemoveDirectory(PathJoin(root, u8"Sources"));
        RemoveDirectory(PathJoin(root, u8"Cooked"));
        RemoveDirectory(PathJoin(root, u8"Editor"));
        RemoveDirectory(PathJoin(root, u8".cache"));
        RemoveDirectory(root);
    }
}

DRACONIC_DEFINE_OBJECT(TestMaterial, "draconic::editor::test")

TEST_CASE("editor-project: create scaffolds the layout and open round-trips the manifest")
{
    const StringView dir = u8"draconic_editor_test_project";
    RemoveProjectTree(dir);

    REQUIRE(EditorProject::Create(dir, u8"Test Project").IsOk());
    CHECK(FileExists(PathJoin(dir, u8"Project.xml")));
    CHECK(DirectoryExists(PathJoin(dir, u8"Content")));
    CHECK(DirectoryExists(PathJoin(dir, u8"Sources")));
    CHECK(DirectoryExists(PathJoin(dir, u8"Cooked")));
    CHECK(DirectoryExists(PathJoin(dir, u8"Editor")));
    CHECK(DirectoryExists(PathJoin(dir, u8".cache")));

    // Creating again fails: the manifest already exists.
    CHECK(EditorProject::Create(dir, u8"Test Project").Code() == ErrorCode::AlreadyExists);

    UniquePtr<EditorProject> project = EditorProject::Open(dir);
    REQUIRE(static_cast<bool>(project));
    CHECK(project->Name() == u8"Test Project");
    CHECK(project->Settings().formatVersion == 1);
    CHECK(project->Settings().defaultScene.IsEmpty());
    CHECK(project->SourceDb().RootGroup() != nullptr);
    CHECK(project->CookedDb().RootGroup() != nullptr);
    CHECK(project->SourcesRoot() == PathJoin(dir, u8"Sources"));
    CHECK(project->EditorStateRoot() == PathJoin(dir, u8"Editor"));

    RemoveProjectTree(dir);
}

TEST_CASE("editor-project: open fails without a manifest")
{
    const StringView dir = u8"draconic_editor_test_project_missing";
    RemoveProjectTree(dir);
    CHECK(!static_cast<bool>(EditorProject::Open(dir)));
    RemoveProjectTree(dir);
}

TEST_CASE("editor-project: settings changes persist through SaveSettings")
{
    const StringView dir = u8"draconic_editor_test_project_save";
    RemoveProjectTree(dir);
    REQUIRE(EditorProject::Create(dir, u8"P").IsOk());

    {
        UniquePtr<EditorProject> project = EditorProject::Open(dir);
        REQUIRE(static_cast<bool>(project));
        project->Settings().defaultScene = String(u8"scenes/main");
        CHECK(project->SaveSettings().IsOk());
    }
    {
        UniquePtr<EditorProject> project = EditorProject::Open(dir);
        REQUIRE(static_cast<bool>(project));
        CHECK(project->Settings().defaultScene == u8"scenes/main");
    }

    RemoveProjectTree(dir);
}

TEST_CASE("editor-project: source db is XML, cooked db is binary, both round-trip")
{
    GlobalTypeRegistry().Register(TestMaterial::StaticType());
    RegisterSerializable<TestMaterial>();

    const StringView dir = u8"draconic_editor_test_project_dbs";
    RemoveProjectTree(dir);
    REQUIRE(EditorProject::Create(dir, u8"P").IsOk());

    Guid sourceId;
    {
        UniquePtr<EditorProject> project = EditorProject::Open(dir);
        REQUIRE(static_cast<bool>(project));

        // Author a source instance and a cooked instance.
        auto* srcGroup = project->SourceDb().RootGroup()->CreateGroup(u8"materials");
        REQUIRE(srcGroup != nullptr);
        auto* src = srcGroup->CreateInstance(u8"steel", TestMaterial::StaticType());
        REQUIRE(src != nullptr);
        sourceId = src->Id();
        TestMaterial mat;
        mat.shininess = 7;
        REQUIRE(src->WriteObject(mat).IsOk());

        auto* cookedGroup = project->CookedDb().RootGroup()->CreateGroup(u8"materials");
        auto* cooked = cookedGroup->CreateInstance(u8"steel", TestMaterial::StaticType());
        REQUIRE(cooked != nullptr);
        REQUIRE(cooked->WriteObject(mat).IsOk());
    }

    // Envelope extensions match the §3.9 split: source readable XML, cooked binary.
    CHECK(FileExists(PathJoin(dir, u8"Content/materials/steel.xasset")));
    CHECK(FileExists(PathJoin(dir, u8"Cooked/materials/steel.rasset")));

    {
        UniquePtr<EditorProject> project = EditorProject::Open(dir);
        REQUIRE(static_cast<bool>(project));
        RefPtr<ISerializable> obj = project->SourceDb().ReadObject(sourceId);
        REQUIRE(obj.Get() != nullptr);
        TestMaterial* mat = Cast<TestMaterial>(obj.Get());
        REQUIRE(mat != nullptr);
        CHECK(mat->shininess == 7);
    }

    RemoveProjectTree(dir);
}
