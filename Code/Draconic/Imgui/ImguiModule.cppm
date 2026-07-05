/// Draconic::Imgui — `draconic.imgui`, the Dear ImGui debug-UI integration.
///
/// ImguiRenderer (RHI-based draw-data renderer) + ImguiSubsystem (context + per-frame NewFrame/Render
/// hooks an app drives). Dear ImGui itself is consumed via its own header (imgui.h, from the vendored
/// Draconic::ImGui lib this links PUBLIC) — apps include "imgui.h" and call ImGui:: directly; this
/// module only adds the engine integration. Depends on Runtime + Shell (input) + RuntimeGraphics
/// (FrameContext) + RHI + Shaders.

export module draconic.imgui;

export import :renderer;
export import :subsystem;
