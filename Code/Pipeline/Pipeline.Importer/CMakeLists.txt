# Pipeline::Importer - `pipeline.importer` (the file-import framework: IFileImporter +
# ImportOptions + ImporterRegistry + the deferred-write/copy helpers). Tooling only,
# headless-drivable: importers land sources + typed Assets through an injected
# ImportContext - the editor's project model never appears here.

util_add_engine_library(Pipeline.Importer ALIAS Pipeline::Importer)
target_link_libraries(Pipeline.Importer PUBLIC Foundation::Core Foundation::Content Foundation::VFS Foundation::Policy Pipeline::Core)

target_sources(Pipeline.Importer
    PRIVATE
        ImporterImpl.cpp
    PUBLIC
        FILE_SET pipeline_modules TYPE CXX_MODULES FILES
            Importer.cppm)
