// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// GameUiSandbox - the game-UI-kit widgets on screen. A DefaultApplication (same host as
// ScriptPlayground / PhysicsPlayground: UISubsystem + rendering + input come for free) that pushes a
// game UIScreen onto the UISubsystem's screen-tier ScreenStack and fills it with the gamekit widgets:
//   - a MenuList whose items drive the demo (pad/arrow navigate, Enter/A activates),
//   - a Bar that drains/heals with a smooth animation,
//   - a Ticker that rolls the score up,
//   - a Toast on "Show toast" (via a ToastHost overlay),
//   - a ButtonPrompt resolving the "Deliver" action's binding through foundation.input.
// "Pause menu" pushes a second (Modal) screen to show ScreenStack push/pop. Esc exits.

#include "Core/Prelude.h"
#include "Runtime.Client/AppMain.h"

import foundation.core;
import foundation.runtime;
import foundation.runtime.client;
import foundation.runtime.desktop;
import foundation.shell;
import foundation.shell.desktop;
import foundation.graphics;
import foundation.graphics.gpu;
import foundation.scene;
import engine.scene;
import engine.render;
import engine.defaultapp;
import engine.ui;             // UISubsystem (screen tier: Screens() + ScreenRoot())
import foundation.ui;         // RootView / FlexLayout / Label / Thickness / SizeSpec
import foundation.ui.gamekit; // UIScreen / ScreenStack / MenuList / Bar / Ticker / ButtonPrompt / ToastHost
import foundation.input;      // InputMap (for ButtonPrompt)

namespace core = foundation::core;
namespace runtime = foundation::runtime;
namespace graphics = foundation::graphics;
namespace shell = foundation::shell;
namespace scene = foundation::scene;
namespace ui = foundation::ui;
namespace gamekit = foundation::ui::gamekit;
namespace input = foundation::input;

namespace
{
    // This binary's composition root: the ONE ambient-allocator decision here.
    [[nodiscard]] foundation::core::IAllocator& AppRoot() noexcept
    {
        return foundation::core::DefaultAllocator();
    }
}




using core::f32;
using core::i64;
using core::RefPtr;
using core::String;
using core::StringView;

namespace
{
    [[nodiscard]] RefPtr<ui::FlexLayout> Row(f32 spacing)
    {
        auto row = core::MakeRef<ui::FlexLayout>(AppRoot());
        row->Direction = ui::Orientation::Horizontal;
        row->AlignItems = ui::Align::Center;
        row->Spacing = spacing;
        return row;
    }

    [[nodiscard]] RefPtr<ui::Label> Text(StringView s, f32 size = 16.0f)
    {
        auto label = core::MakeRef<ui::Label>(AppRoot(), s);
        label->FontSize.SetValue(size);
        return label;
    }

    class GameUiSandbox final : public engine::runtime::DefaultApplication
    {
    public:
        void OnLaunch(runtime::IApplicationHost& host) override
        {
            if (host.Ctx().GetSubsystem<engine::scene::SceneSubsystem>() == nullptr)
            {
                return;
            }
            // A minimal scene + camera so a frame renders; the Opaque screen covers it as a backdrop.
            m_scene = PrimaryScenes().CreateScene(u8"gameui");
            m_camera = m_scene->CreateEntity(u8"camera");
            if (auto* cameras = m_scene->GetSystem<engine::render::CameraComponentManager>())
            {
                cameras->Add(m_camera);
            }
            m_scene->Start();

            m_ui = host.Ctx().GetSubsystem<engine::ui::UISubsystem>();
            if (m_ui == nullptr)
            {
                return;
            }

            // Toast overlay: a ToastHost on the screen-tier root, above the pushed screens.
            m_toasts = core::MakeRef<gamekit::ToastHost>(AppRoot());
            m_ui->ScreenRoot()->AddView(m_toasts.Get());

            BuildInputMap();
            m_ui->Screens().Push(BuildHudScreen());

            core::ConsoleWrite(u8"GameUiSandbox: Up/Down (or pad) move the menu, Enter/A activate. "
                               u8"Esc exits.\n");
        }

        void OnUpdate(runtime::IApplicationHost& host, f32 dt) override
        {
            engine::runtime::DefaultApplication::OnUpdate(host, dt); // ticks the UISubsystem (animations)
            if (m_toasts)
            {
                m_toasts->Update(dt); // ages timed toasts
            }
            if (m_quit)
            {
                host.RequestExit(0);
                return;
            }
            auto* in = host.Shell() != nullptr ? host.Shell()->Input() : nullptr;
            if (in == nullptr)
            {
                return;
            }
            if (in->Keyboard()->IsKeyPressed(shell::KeyCode::Escape))
            {
                // Escape backs out of the pause screen first, else exits.
                if (!m_ui->Screens().HandleBack())
                {
                    host.RequestExit(0);
                }
            }
            // The "Deliver" action the ButtonPrompt advertises: E fires it (a toast, for the demo).
            if (in->Keyboard()->IsKeyPressed(shell::KeyCode::E) && m_toasts)
            {
                gamekit::ToastRequest r;
                r.message = String(u8"Delivered!");
                r.durationSeconds = 1.5f;
                m_toasts->Show(core::Move(r));
            }
        }

    private:
        void BuildInputMap()
        {
            input::ActionSet set;
            set.name = String(u8"Gameplay");
            input::Action deliver;
            deliver.name = String(u8"Deliver");
            input::Binding key;
            key.source = input::BindingSource::Key;
            key.code = static_cast<core::u32>(shell::KeyCode::E);
            deliver.bindings.PushBack(key);
            input::Binding pad;
            pad.source = input::BindingSource::GamepadButton;
            pad.code = static_cast<core::u32>(shell::GamepadButton::South);
            deliver.bindings.PushBack(pad);
            set.actions.PushBack(core::Move(deliver));
            m_map.sets.PushBack(core::Move(set));
        }

        [[nodiscard]] RefPtr<gamekit::UIScreen> BuildHudScreen()
        {
            auto screen = core::MakeRef<gamekit::UIScreen>(AppRoot());
            screen->SetMode(gamekit::ScreenMode::Opaque);
            screen->SetTransition(gamekit::TransitionDesc{gamekit::TransitionKind::Fade, 0.2f});

            auto column = core::MakeRef<ui::FlexLayout>(AppRoot());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 16.0f;
            column->Padding = ui::Thickness{48.0f, 40.0f};

            column->AddView(Text(u8"Game UI Kit Sandbox", 28.0f).Get());

            auto prompt = core::MakeRef<gamekit::ButtonPrompt>(AppRoot());
            prompt->SetFromAction(m_map, u8"Deliver", u8"Deliver");
            column->AddView(prompt.Get());

            {
                auto row = Row(8.0f);
                row->AddView(Text(u8"Score").Get());
                auto ticker = core::MakeRef<gamekit::Ticker>(AppRoot());
                ticker->FontSize.SetValue(20.0f);
                m_score = ticker.Get();
                row->AddView(ticker.Get());
                column->AddView(row.Get());
            }
            {
                auto row = Row(8.0f);
                row->AddView(Text(u8"Health").Get());
                auto bar = core::MakeRef<gamekit::Bar>(AppRoot());
                bar->SetFill(1.0f);
                m_health = bar.Get();
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Fixed(ui::Unit::Px(220));
                row->AddView(bar.Get(), lp);
                column->AddView(row.Get());
            }

            auto menu = core::MakeRef<gamekit::MenuList>(AppRoot());
            GameUiSandbox* self = this;
            menu->AddItem(u8"Score +250",
                          [self]()
                          {
                              self->m_scoreValue += 250;
                              if (self->m_score)
                              {
                                  self->m_score->AnimateTo(self->m_scoreValue);
                              }
                          });
            menu->AddItem(u8"Take damage",
                          [self]()
                          {
                              self->m_healthValue = core::Max(0.0f, self->m_healthValue - 0.2f);
                              if (self->m_health)
                              {
                                  self->m_health->AnimateTo(self->m_healthValue);
                              }
                          });
            menu->AddItem(u8"Heal",
                          [self]()
                          {
                              self->m_healthValue = core::Min(1.0f, self->m_healthValue + 0.3f);
                              if (self->m_health)
                              {
                                  self->m_health->AnimateTo(self->m_healthValue);
                              }
                          });
            menu->AddItem(u8"Show toast",
                          [self]()
                          {
                              if (!self->m_toasts)
                              {
                                  return;
                              }
                              gamekit::ToastRequest r;
                              r.message = String(u8"Objective complete!");
                              r.severity = gamekit::ToastSeverity::Success;
                              r.durationSeconds = 3.0f;
                              self->m_toasts->Show(core::Move(r));
                          });
            menu->AddItem(u8"Pause menu", [self]() { self->m_ui->Screens().Push(self->BuildPauseScreen()); });
            menu->AddItem(u8"Quit", [self]() { self->m_quit = true; });
            column->AddView(menu.Get());

            screen->AddView(column.Get());
            return screen;
        }

        [[nodiscard]] RefPtr<gamekit::UIScreen> BuildPauseScreen()
        {
            auto screen = core::MakeRef<gamekit::UIScreen>(AppRoot());
            screen->SetMode(gamekit::ScreenMode::Modal); // shields the HUD below, keeps it visible
            screen->SetTransition(gamekit::TransitionDesc{gamekit::TransitionKind::Scale, 0.18f});

            // Scrim: a full-screen semi-transparent black behind the card, dimming the frozen HUD.
            // UIScreen lays out each child to fill, so a bare ColorView covers the whole screen; it is
            // added FIRST so it draws behind the centered card.
            auto scrim = core::MakeRef<ui::ColorView>(
                AppRoot(), core::Color{0.0f, 0.0f, 0.0f, 0.55f}, 0.0f, 0.0f);
            screen->AddView(scrim.Get());

            // Center a solid CARD over the dimmed HUD so the menu reads clearly instead of
            // jumbling over the text beneath it.
            auto center = core::MakeRef<ui::FlexLayout>(AppRoot());
            center->Direction = ui::Orientation::Vertical;
            center->JustifyContent = ui::Justify::Center;
            center->AlignItems = ui::Align::Center;

            auto card = core::MakeRef<ui::Panel>(AppRoot());
            card->SetStyle(ui::StyleProperty::Background,
                           core::RefPtr<ui::Drawable>(core::MakeRef<ui::ColorDrawable>(
                               AppRoot(), core::Color{0.12f, 0.13f, 0.17f, 0.98f})));

            // Padding lives on the inner FlexLayout (it honours Padding); the Panel wraps it + paints
            // the background behind it.
            auto content = core::MakeRef<ui::FlexLayout>(AppRoot());
            content->Direction = ui::Orientation::Vertical;
            content->AlignItems = ui::Align::Center;
            content->Spacing = 16.0f;
            content->Padding = ui::Thickness{40.0f, 28.0f};

            content->AddView(Text(u8"Paused", 26.0f).Get());
            auto menu = core::MakeRef<gamekit::MenuList>(AppRoot());
            GameUiSandbox* self = this;
            menu->AddItem(u8"Resume", [self]() { self->m_ui->Screens().Pop(); });
            menu->AddItem(u8"Quit", [self]() { self->m_quit = true; });
            content->AddView(menu.Get());

            card->AddView(content.Get());
            center->AddView(card.Get());
            screen->AddView(center.Get());
            return screen;
        }

        scene::Scene* m_scene = nullptr;
        scene::EntityHandle m_camera{};
        engine::ui::UISubsystem* m_ui = nullptr;
        RefPtr<gamekit::ToastHost> m_toasts;
        gamekit::Bar* m_health = nullptr;
        gamekit::Ticker* m_score = nullptr;
        input::InputMap m_map;
        i64 m_scoreValue = 0;
        f32 m_healthValue = 1.0f;
        bool m_quit = false;
    };
}

APP_MAIN(GameUiSandbox)
