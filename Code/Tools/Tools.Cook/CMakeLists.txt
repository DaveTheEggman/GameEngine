# Tools.Cook - the command-line cooker (headless: no shell/GPU).
add_executable(Tools.Cook Main.cpp)
target_link_libraries(Tools.Cook PRIVATE
    Foundation::Core Foundation::Content Foundation::VFS
    Pipeline::Core Editor::Core Pipeline::Cook
    Pipeline::Registration) # the composition root brings every builder/type registration

