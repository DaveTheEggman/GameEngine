// Pipeline::Importer - implementation unit.
//
// The file-import seam (asset-pipeline design §7): an OS file (drag-dropped onto the editor)
// becomes a SOURCE - the raw bytes copied into the project's Sources/ tree - plus a typed Asset
// instance in the content DB whose import settings point at it. Cooking then owns the
// source -> product path like any other asset (the imported file's content is part of the
// recipe hash).
//
// IFileImporter implementations live with their asset modules (texture/image/...) and are
// registered by the executable; routing is by lowercase extension. Several importers may claim
// one extension - v1 takes the FIRST match (the Sedulous-style chooser dialog is a later
// nicety; the registry API already exposes all matches).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module pipeline.importer;

import foundation.core;
import foundation.content;

using namespace foundation::core;

namespace pipeline
{
    RefPtr<Object> IFileImporter::PrepareOnWorker(StringView /*sourcePath*/) { return {}; }
    void ImporterRegistry::Register(UniquePtr<IFileImporter> importer)
    {
        if (importer)
        {
            m_importers.PushBack(Move(importer));
        }
    }

    IFileImporter* ImporterRegistry::FindFor(StringView extension) const
    {
        for (const UniquePtr<IFileImporter>& importer : m_importers)
        {
            if (importer->Accepts(extension))
            {
                return importer.Get();
            }
        }
        return nullptr;
    }

    Array<IFileImporter*> ImporterRegistry::FindAllFor(StringView extension) const
    {
        Array<IFileImporter*> matches;
        for (const UniquePtr<IFileImporter>& importer : m_importers)
        {
            if (importer->Accepts(extension))
            {
                matches.PushBack(importer.Get());
            }
        }
        return matches;
    }
}
