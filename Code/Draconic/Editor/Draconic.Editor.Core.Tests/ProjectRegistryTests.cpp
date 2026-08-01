// Project-registry tests: the recent-projects Settings section (touch/dedupe/order/cap/remove +
// store round-trip), manifest probing, the engine-version relation, and the manifest backup -
// the headless core of the built-in project manager.

#include <doctest/doctest.h>

#include "Draconic.Core/Prelude.h"

import draconic.core;
import draconic.vfs;
import draconic.settings;
import draconic.xml.serialization;
import draconic.engine.project;
import draconic.editor.core;

using namespace draconic::core;
using namespace draconic::editor;
namespace settings = draconic::settings;
namespace project = draconic::project;

namespace
{
    void RemoveProjectTree(StringView root)
    {
        FileDelete(PathJoin(root, u8"Project.xml"));
        FileDelete(PathJoin(root, u8"Project.xml.0.1.0.bak"));
        RemoveDirectory(PathJoin(root, u8"Content"));
        RemoveDirectory(PathJoin(root, u8"Sources"));
        RemoveDirectory(PathJoin(root, u8"Cooked"));
        RemoveDirectory(PathJoin(root, u8"Editor"));
        RemoveDirectory(PathJoin(root, u8".cache"));
        RemoveDirectory(root);
    }
}

TEST_CASE("project-registry: touch inserts most-recent-first, dedupes, and caps")
{
    RegisterProjectRegistryTypes();
    settings::Settings store;

    TouchRecentProject(store, u8"/projects/a", u8"A", u8"0.1.0");
    TouchRecentProject(store, u8"/projects/b", u8"B", u8"0.1.0");
    const RecentProjectsSettings& reg = store.Section<RecentProjectsSettings>();
    REQUIRE(reg.entries.Size() == 2u);
    CHECK(reg.entries[0].path.AsView() == u8"/projects/b"); // most recent first

    // Re-touching A moves it to the front and refreshes its snapshot (no duplicate row).
    TouchRecentProject(store, u8"/projects/a", u8"A renamed", u8"0.2.0");
    REQUIRE(reg.entries.Size() == 2u);
    CHECK(reg.entries[0].path.AsView() == u8"/projects/a");
    CHECK(reg.entries[0].name.AsView() == u8"A renamed");
    CHECK(reg.entries[0].engineVersion.AsView() == u8"0.2.0");
    CHECK(reg.entries[1].path.AsView() == u8"/projects/b");

    // The cap drops the OLDEST entries.
    for (u32 i = 0; i < RecentProjectsSettings::kMaxEntries + 5; ++i)
    {
        const String p = Format(u8"/projects/p{}", i);
        TouchRecentProject(store, p.AsView(), u8"P", u8"0.1.0");
    }
    CHECK(reg.entries.Size() == RecentProjectsSettings::kMaxEntries);
    CHECK(reg.entries[0].path.AsView() ==
          Format(u8"/projects/p{}", RecentProjectsSettings::kMaxEntries + 4).AsView());
    CHECK(reg.Find(u8"/projects/a") == nullptr); // evicted
}

TEST_CASE("project-registry: remove deletes a row; the section round-trips the settings store")
{
    RegisterProjectRegistryTypes();
    settings::Settings store;
    TouchRecentProject(store, u8"/projects/keep", u8"Keep", u8"0.1.0");
    TouchRecentProject(store, u8"/projects/drop", u8"Drop", u8"0.1.0");

    CHECK(RemoveRecentProject(store, u8"/projects/drop"));
    CHECK_FALSE(RemoveRecentProject(store, u8"/projects/drop")); // already gone
    CHECK(store.Section<RecentProjectsSettings>().entries.Size() == 1u);

    // Round-trip through the fs-explicit editor-settings helpers (the store's real backend).
    const StringView dir = u8"draconic_registry_roundtrip_test";
    (void)CreateDirectory(dir);
    vfs::NativeFileSystem fs(dir);
    REQUIRE(SaveEditorSettings(*fs.AsWritable(), store).IsOk());

    settings::Settings loaded;
    REQUIRE(LoadEditorSettings(fs, loaded).IsOk());
    const RecentProjectsSettings& reg = loaded.Section<RecentProjectsSettings>();
    REQUIRE(reg.entries.Size() == 1u);
    CHECK(reg.entries[0].path.AsView() == u8"/projects/keep");
    CHECK(reg.entries[0].name.AsView() == u8"Keep");

    FileDelete(PathJoin(dir, kEditorSettingsFile));
    RemoveDirectory(dir);
}

TEST_CASE("project-registry: probe reads the manifest without opening; NotFound for non-projects")
{
    const StringView dir = u8"draconic_registry_probe_test";
    RemoveProjectTree(dir);

    project::ProjectSettings probed;
    CHECK(ProbeProject(dir, probed).Code() == ErrorCode::NotFound); // no dir at all

    REQUIRE(EditorProject::Create(dir, u8"Probe Me").IsOk());
    REQUIRE(ProbeProject(dir, probed).IsOk());
    CHECK(probed.name.AsView() == u8"Probe Me");
    CHECK(probed.engineVersion.AsView() == project::kEngineVersionString);

    RemoveProjectTree(dir);
}

TEST_CASE("project-registry: engine-version relation")
{
    // This engine is kEngineVersionString; build relative stamps from the constants so the
    // test stays true when the engine version bumps.
    const String same = String(project::kEngineVersionString);
    const String older = Format(u8"{}.{}.{}", project::kEngineVersionMajor,
                                project::kEngineVersionMinor, project::kEngineVersionPatch);
    CHECK(CompareProjectEngineVersion(same.AsView()) == EngineVersionRelation::Same);
    CHECK(CompareProjectEngineVersion(older.AsView()) == EngineVersionRelation::Same);

    const String newer = Format(u8"{}.{}.{}", project::kEngineVersionMajor + 1, 0, 0);
    CHECK(CompareProjectEngineVersion(newer.AsView()) == EngineVersionRelation::ProjectNewer);
    const String newerPatch =
        Format(u8"{}.{}.{}", project::kEngineVersionMajor, project::kEngineVersionMinor,
               project::kEngineVersionPatch + 1);
    CHECK(CompareProjectEngineVersion(newerPatch.AsView()) ==
          EngineVersionRelation::ProjectNewer);

    // Anything below the current version is older; 0.0.x is always below (version starts 0.1.0).
    CHECK(CompareProjectEngineVersion(u8"0.0.9") == EngineVersionRelation::ProjectOlder);

    CHECK(CompareProjectEngineVersion(u8"") == EngineVersionRelation::Unstamped);
    CHECK(CompareProjectEngineVersion(u8"abc") == EngineVersionRelation::Unstamped);
    CHECK(CompareProjectEngineVersion(u8"1.2") == EngineVersionRelation::Unstamped);
    CHECK(CompareProjectEngineVersion(u8"1..2") == EngineVersionRelation::Unstamped);
    CHECK(CompareProjectEngineVersion(u8"1.2.3.4") == EngineVersionRelation::Unstamped);
}

TEST_CASE("project-registry: manifest backup copies Project.xml beside itself")
{
    const StringView dir = u8"draconic_registry_backup_test";
    RemoveProjectTree(dir);
    REQUIRE(EditorProject::Create(dir, u8"Backup Me").IsOk());

    Result<String> backup = BackupProjectManifest(dir);
    REQUIRE(backup.HasValue());
    CHECK(FileExists(backup.Value().AsView()));
    // The original manifest is untouched and still probes.
    project::ProjectSettings probed;
    CHECK(ProbeProject(dir, probed).IsOk());

    FileDelete(backup.Value().AsView());
    RemoveProjectTree(dir);

    CHECK_FALSE(BackupProjectManifest(u8"draconic_registry_no_such_dir").HasValue());
}
