// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The model-loader registry storage: one instance per process (loader libraries
// register, Model.IO dispatches - shared-libraries.md rendezvous rule).

module;
#include "Core/Prelude.h"

module foundation.model.io;

using namespace foundation::core;

namespace foundation::model::io::detail
{
    Array<ModelLoader*>& loaders()
    {
        static Array<ModelLoader*> s;
        return s;
    }
} // namespace foundation::model::io::detail
