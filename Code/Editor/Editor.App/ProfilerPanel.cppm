// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::App - :profiler_panel partition.
//
// ProfilerPanel: the editor's Profiler tab, docked with the Console (Specs/profiler.md, P5). It
// records the editor process, every page, viewport preview and Play-in-editor run in one frame,
// and says so: a game's own numbers come from its player (the overlay, or a capture from it
// opened here). Record starts copying the store's frames into the panel's own recording and Stop
// ends it, so stopping, the selection and a longer history than the store keeps never touch the
// store another viewer reads (a PIE game's overlay). The frame strip shows every recorded frame
// (a click selects one, a drag a range; while recording it follows the newest until you choose),
// the timeline and the scope table show the selection, the counters their means over it. Save
// writes the recording as a capture (.aeprofile and its Chrome trace JSON) to <user-data>/profiles;
// Open shows a capture from there, a player's from a Deck or a phone copied in, and Live goes
// back to the editor's own.
module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:profiler_panel;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import foundation.profiler;
import foundation.profiler.ui;
import foundation.profiler.capture;
import :path_picker_dialog;

using namespace foundation::core;

export namespace editor::app
{
    namespace ui = foundation::ui;
    namespace profiler = foundation::profiler;
    namespace pui = foundation::profiler::ui;

    /// What the panel records and where its captures go: the process's store and recorder (a
    /// test's own), and the folder (made when missing).
    struct ProfilerPanelSeams
    {
        profiler::Profiler* store = nullptr;
        profiler::CaptureRecorder* recorder = nullptr; // the platform and build a saved capture names
        Function<String()> folder;
    };

    /// The panel's parts at their natural sizes: the toolbar, the frame strip, then the timeline
    /// beside a column of the scope table over the counters. It takes the pane's size, or its
    /// minimum when the pane is smaller, and the panel scrolls it then: resize the split for more.
    class ProfilerPanelBody final : public ui::ViewGroup
    {
    public:
        static constexpr f32 kMinWidth = 640.0f;
        static constexpr f32 kMinHeight = 360.0f;
        static constexpr f32 kSideMax = 380.0f; // the table and counters' column, at most
        static constexpr f32 kSideMin = 260.0f;
        static constexpr f32 kGap = 4.0f;

        /// The table and counters' column in a body `width` wide: 40%, within its bounds.
        [[nodiscard]] static f32 SideWidth(f32 width) noexcept { return Clamp(width * 0.4f, kSideMin, kSideMax); }

        /// The parts it places (borrowed: it holds them as children).
        ui::View* toolbar = nullptr;
        ui::View* strip = nullptr;
        ui::View* timeline = nullptr;
        ui::View* table = nullptr;
        ui::View* counters = nullptr;
        /// The pane it shows in (the panel sets it before each measure).
        Float2 viewport{kMinWidth, kMinHeight};

        void OnMeasure(ui::BoxConstraints constraints) override
        {
            (void)constraints; // unbounded along a scrolled axis: the pane's size decides instead
            const f32 width = Max(viewport.x, kMinWidth);
            const f32 height = Max(viewport.y, kMinHeight);
            toolbar->Measure(ui::BoxConstraints{width, width, 0.0f, kFloatMax});
            counters->Measure(ui::BoxConstraints{0.0f, SideWidth(width), 0.0f, kFloatMax});
            const ui::BoxConstraints loose{0.0f, width, 0.0f, height};
            strip->Measure(loose);
            timeline->Measure(loose);
            table->Measure(loose);
            MeasuredSize = Float2{width, height};
        }

        void OnLayout(f32 left, f32 top, f32 width, f32 height) override
        {
            (void)left;
            (void)top;
            const f32 bar = toolbar->MeasuredSize.y;
            toolbar->Layout(0, 0, width, bar);
            strip->Layout(0, bar, width, foundation::profiler::ui::FrameTimeGraph::kHeight);
            const f32 y = bar + foundation::profiler::ui::FrameTimeGraph::kHeight + kGap;
            const f32 rest = Max(height - y, 0.0f);
            const f32 side = SideWidth(width);
            timeline->Layout(0, y, width - side - kGap, rest);
            const f32 countersHeight = counters->MeasuredSize.y;
            const f32 tableHeight = Max(rest - countersHeight - kGap, 0.0f);
            table->Layout(width - side, y, side, tableHeight);
            counters->Layout(width - side, y + tableHeight + kGap, side, countersHeight);
        }
    };

    class ProfilerPanel : public ui::ViewGroup
    {
        RTTI_OBJECT(ProfilerPanel, ui::ViewGroup)
    public:
        explicit ProfilerPanel(ProfilerPanelSeams seams)
            : m_seams(Move(seams)), m_live(*m_seams.store), m_recording(MemoryAllocator())
        {
            IAllocator& allocator = MemoryAllocator();

            m_toolbar = MakeRef<ui::toolkit::Toolbar>(allocator);
            m_record = m_toolbar->AddToggle(u8"Record");
            m_record->TooltipText = String(u8"Record the editor process (every page, preview and Play-in-editor run)");
            m_record->OnCheckedChanged.Add([this](ui::toolkit::ToolbarToggle*, bool on) { on ? Start() : Stop(); });
            ui::toolkit::ToolbarButton* clear = m_toolbar->AddButton(u8"Clear");
            clear->TooltipText = String(u8"Drop the recorded frames");
            clear->OnClick.Add([this](ui::toolkit::ToolbarButton*) { Clear(); });
            m_toolbar->AddSeparator();
            ui::toolkit::ToolbarButton* open = m_toolbar->AddButton(u8"Open...");
            open->TooltipText = String(u8"Show a capture from <user-data>/profiles (a player's F9 or --profile-capture)");
            open->OnClick.Add([this](ui::toolkit::ToolbarButton*) { PickCapture(); });
            m_save = m_toolbar->AddButton(u8"Save");
            m_save->TooltipText = String(u8"Write the recording to <user-data>/profiles (.aeprofile, and .json for Perfetto)");
            m_save->OnClick.Add([this](ui::toolkit::ToolbarButton*) { (void)SaveRecording(); });
            m_liveButton = m_toolbar->AddButton(u8"Live");
            m_liveButton->TooltipText = String(u8"Back to the editor's own recording");
            m_liveButton->OnClick.Add([this](ui::toolkit::ToolbarButton*) { ShowLive(); });
            m_toolbar->AddSeparator();
            m_status = m_toolbar->AddLabel(u8"");
            m_status->TooltipText = String(u8"The editor process: every page, viewport preview and Play-in-editor run "
                                           u8"in one frame. A game's own numbers come from its player: the overlay "
                                           u8"(F3), or a capture (F9, --profile-capture) opened here.");
            m_body = MakeRef<ProfilerPanelBody>(allocator);
            m_body->AddView(m_toolbar.Get());

            m_strip = MakeRef<pui::FrameTimeGraph>(allocator, allocator);
            m_strip->SetFitToSource(true);
            m_strip->SetInteractive(true);
            m_strip->onSelectionChanged = [this](pui::FrameRange range)
            {
                m_follow = false; // the user chose: stop following the newest
                ShowFrames(range);
            };
            m_body->AddView(m_strip.Get());

            m_timeline = MakeRef<pui::Timeline>(allocator, allocator);
            m_table = MakeRef<pui::ScopeTable>(allocator, allocator);
            m_table->onSelect = [this](const pui::ScopeTable::Row* row)
            {
                if (row != nullptr)
                {
                    m_timeline->SetHighlight(row->name, row->gpu);
                }
                else
                {
                    m_timeline->ClearHighlight();
                }
            };
            m_counters = MakeRef<pui::CounterStrip>(allocator, allocator);
            m_body->AddView(m_timeline.Get());
            m_body->AddView(m_table.Get());
            m_body->AddView(m_counters.Get());
            m_body->toolbar = m_toolbar.Get();
            m_body->strip = m_strip.Get();
            m_body->timeline = m_timeline.Get();
            m_body->table = m_table.Get();
            m_body->counters = m_counters.Get();
            // A pane smaller than the body's minimum scrolls it; the user resizes the split for more.
            m_scroll = MakeRef<ui::ScrollView>(allocator);
            m_scroll->VScrollBarPolicy.SetValue(ui::ScrollBarPolicy::Auto);
            m_scroll->HScrollBarPolicy.SetValue(ui::ScrollBarPolicy::Auto);
            m_scroll->AddView(m_body.Get());
            AddView(m_scroll.Get());
            ShowLive();
        }

        ~ProfilerPanel() override
        {
            if (m_recordingNow)
            {
                m_seams.store->Disarm();
            }
        }

        /// Start recording the editor process afresh (arming the store, counted).
        void Start()
        {
            if (m_recordingNow)
            {
                return;
            }
            ShowLive();
            m_recording.Begin(m_live);
            m_seams.store->Arm();
            m_recordingNow = true;
            m_follow = true;
            SyncRecordToggle();
            RefreshViews();
        }

        /// Stop: the frames recorded stay to read and save; the store is let go.
        void Stop()
        {
            if (!m_recordingNow)
            {
                return;
            }
            m_recording.Pull(m_live, 0); // the rest, as they are
            m_seams.store->Disarm();
            m_recordingNow = false;
            SyncRecordToggle();
            RefreshViews();
        }

        [[nodiscard]] bool IsRecording() const noexcept { return m_recordingNow; }

        /// Drop the recorded frames (recording carries on from now if it is on).
        void Clear()
        {
            m_recording.Begin(m_live);
            m_follow = true;
            ShowFrames(pui::FrameRange{});
            RefreshViews();
        }

        /// Each frame: take the store's newly settled frames while recording, and redraw.
        void Update()
        {
            if (!m_recordingNow)
            {
                return;
            }
            const u32 before = m_recording.FrameCount();
            const u64 newest = m_recording.NewestFrame();
            m_recording.Pull(m_live);
            if (m_recording.FrameCount() != before || m_recording.NewestFrame() != newest)
            {
                RefreshViews();
            }
        }

        /// Write the recording as a capture; answers the .aeprofile's path (empty: nothing to
        /// save, or it could not be written, which the log says).
        String SaveRecording()
        {
            IAllocator& allocator = MemoryAllocator();
            if (m_capture.Get() != nullptr || !m_recording.HasFrames())
            {
                SetStatus(u8"Nothing to save: record first");
                return String(allocator);
            }
            const String folder = m_seams.folder();
            (void)CreateDirectories(folder.AsView());
            String base(allocator);
            AppendFormat(base, u8"{}/Editor-{}", folder.AsView(), Clock::Now().Ticks());
            profiler::StreamHeader header;
            header.tickFrequency = m_recording.TickFrequency();
            header.application = String(u8"Editor", allocator);
            if (m_seams.recorder != nullptr)
            {
                header.platform = String(m_seams.recorder->Platform(), allocator);
                header.build = String(m_seams.recorder->Build(), allocator);
            }
            String path = profiler::WriteCaptureFiles(allocator, m_recording, m_recording.OldestFrame(),
                                                      m_recording.NewestFrame(), base.AsView(), header);
            String status(allocator);
            if (path.IsEmpty())
            {
                AppendFormat(status, u8"Could not save to {} (see the Console)", folder.AsView());
            }
            else
            {
                AppendFormat(status, u8"Saved {} frames to {}", m_recording.FrameCount(), path.AsView());
                LOG_INFO(u8"Profiler", u8"saved the editor's recording to {}", path.AsView());
            }
            SetStatus(status.AsView());
            return path;
        }

        /// Show the capture file at `path` (recording stops). False when it cannot be read.
        bool OpenCapture(StringView path)
        {
            IAllocator& allocator = MemoryAllocator();
            FileStream file(path, FileMode::Read);
            auto capture = MakeUnique<profiler::ProfileCapture>(allocator, allocator);
            if (!file.IsValid() || !capture->Read(file))
            {
                String status(allocator);
                AppendFormat(status, u8"Could not read {}", path);
                SetStatus(status.AsView());
                return false;
            }
            Stop();
            m_capture = Move(capture);
            m_capturePath = String(path, allocator);
            SetSource(m_capture.Get());
            m_follow = false;
            // The newest frame selected, as a recording shows it.
            pui::FrameRange newest;
            if (m_capture->HasFrames())
            {
                newest.first = newest.last = m_capture->NewestFrame();
                newest.empty = false;
            }
            ShowFrames(newest);
            return true;
        }

        /// Back to the editor's own recording.
        void ShowLive()
        {
            m_capture = nullptr;
            m_capturePath = String(MemoryAllocator());
            SetSource(&m_recording);
        }

        [[nodiscard]] bool ViewingCapture() const noexcept { return m_capture.Get() != nullptr; }
        [[nodiscard]] const profiler::IProfileSource* Source() const noexcept { return m_source; }
        [[nodiscard]] const profiler::ProfileRecording& Recording() const noexcept { return m_recording; }
        [[nodiscard]] pui::FrameRange Selection() const noexcept { return m_strip->Selection(); }

        /// Select frames as a click or drag on the strip does.
        void Select(pui::FrameRange range)
        {
            m_follow = false;
            ShowFrames(range);
        }

        [[nodiscard]] pui::FrameTimeGraph& Strip() noexcept { return *m_strip; }
        [[nodiscard]] pui::Timeline& TimelineView() noexcept { return *m_timeline; }
        [[nodiscard]] pui::ScopeTable& Table() noexcept { return *m_table; }
        [[nodiscard]] pui::CounterStrip& Counters() noexcept { return *m_counters; }
        [[nodiscard]] StringView StatusText() const noexcept { return m_statusText.AsView(); }

        [[nodiscard]] ProfilerPanelBody& Body() noexcept { return *m_body; }
        [[nodiscard]] ui::ScrollView& Scroller() noexcept { return *m_scroll; }

        void OnMeasure(ui::BoxConstraints constraints) override
        {
            m_body->viewport = Float2{constraints.MaxWidth, constraints.MaxHeight};
            m_scroll->Measure(constraints);
            MeasuredSize = Float2{constraints.MaxWidth, constraints.MaxHeight};
        }

        void OnLayout(f32 left, f32 top, f32 width, f32 height) override
        {
            (void)left;
            (void)top;
            m_scroll->Layout(0, 0, width, height);
        }

    private:
        void SetSource(const profiler::IProfileSource* source)
        {
            m_source = source;
            m_strip->SetSource(source);
            m_timeline->SetSource(source);
            m_table->SetSource(source);
            m_counters->SetSource(source);
            m_liveButton->Visibility = m_capture.Get() != nullptr ? ui::VisibilityValue::Visible : ui::VisibilityValue::Gone;
            m_save->IsEnabled = m_capture.Get() == nullptr;
            ShowFrames(pui::FrameRange{});
            UpdateStatus();
        }

        // The selection, to every view that shows it.
        void ShowFrames(pui::FrameRange range)
        {
            m_strip->SetSelection(range);
            m_timeline->SetFrames(range);
            m_table->SetFrames(range);
            m_counters->SetRange(range);
        }

        // New frames: the strip grows, and while following, the newest is shown.
        void RefreshViews()
        {
            m_strip->Refresh();
            if (m_follow && m_recording.HasFrames())
            {
                pui::FrameRange newest;
                newest.first = newest.last = m_recording.NewestFrame();
                newest.empty = false;
                ShowFrames(newest);
            }
            UpdateStatus();
        }

        void SyncRecordToggle()
        {
            if (m_record->IsChecked() != m_recordingNow)
            {
                m_record->SetIsChecked(m_recordingNow); // fires Start/Stop, which return at once
            }
        }

        void UpdateStatus()
        {
            IAllocator& allocator = MemoryAllocator();
            String text(allocator);
            if (m_capture.Get() != nullptr)
            {
                const profiler::StreamHeader& header = m_capture->Header();
                AppendFormat(text, u8"{}: {} frames of {} ({} {})", m_capturePath.AsView(), m_capture->FrameCount(),
                             header.application.AsView(), header.platform.AsView(), header.build.AsView());
            }
            else if (m_recordingNow)
            {
                AppendFormat(text, u8"Recording the editor process: {} frames", m_recording.FrameCount());
            }
            else if (m_recording.HasFrames())
            {
                AppendFormat(text, u8"Editor process: {} frames", m_recording.FrameCount());
            }
            else
            {
                text.Append(u8"Editor process (every page, preview and Play-in-editor run): Record to start");
            }
            SetStatus(text.AsView());
        }

        void SetStatus(StringView text)
        {
            m_statusText = String(text, MemoryAllocator());
            m_status->Text.SetValue(m_statusText);
        }

        void PickCapture()
        {
            if (Context == nullptr)
            {
                return;
            }
            const String folder = m_seams.folder();
            (void)CreateDirectories(folder.AsView());
            Array<String> extensions(MemoryAllocator());
            extensions.PushBack(String(u8".aeprofile"));
            auto dialog = MakeRef<PathPickerDialog>(MemoryAllocator(), u8"Open a capture", folder.AsView(), Move(extensions));
            dialog->OnPicked = [this, folder](StringView picked)
            { (void)OpenCapture(PathJoin(folder.AsView(), picked).AsView()); };
            dialog->Show(Context);
        }

        ProfilerPanelSeams m_seams;
        profiler::LiveProfileSource m_live;
        profiler::ProfileRecording m_recording;
        UniquePtr<profiler::ProfileCapture> m_capture; // shown instead of the recording, when open
        String m_capturePath;
        const profiler::IProfileSource* m_source = nullptr;
        bool m_recordingNow = false;
        bool m_follow = true; // while recording, show the newest frame until the user selects

        RefPtr<ui::ScrollView> m_scroll;
        RefPtr<ProfilerPanelBody> m_body;
        RefPtr<ui::toolkit::Toolbar> m_toolbar;
        ui::toolkit::ToolbarToggle* m_record = nullptr; // borrowed from the toolbar
        ui::toolkit::ToolbarButton* m_save = nullptr;
        ui::toolkit::ToolbarButton* m_liveButton = nullptr;
        ui::Label* m_status = nullptr;
        String m_statusText;
        RefPtr<pui::FrameTimeGraph> m_strip;
        RefPtr<pui::Timeline> m_timeline;
        RefPtr<pui::ScopeTable> m_table;
        RefPtr<pui::CounterStrip> m_counters;
    };

    RTTI_DEFINE_OBJECT(ProfilerPanel, "rtti::editor::editor::app")
}
