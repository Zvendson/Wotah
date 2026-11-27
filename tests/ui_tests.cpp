/**
 * @file ui_tests.cpp
 * The UI without a GPU: ImGui runs with its null renderer (backends/imgui_impl_null), which takes the font pictures
 * without drawing them, while made-up input events go through the UserInterface. Covers what the UI keeps and what
 * reaches the camera, the fixed cards, the control bar's buttons, the settings window, typing into a text field, the
 * sizes the UI lays out and draws in, and how readable the theme is.
 */
#include "input/camera_controller.hpp"
#include "input/input_dispatcher.hpp"
#include "input/input_event.hpp"
#include "platform/window.hpp"
#include "test_utils.hpp"
#include "ui/card.hpp"
#include "ui/control_bar.hpp"
#include "ui/hints_window.hpp"
#include "ui/settings_window.hpp"
#include "ui/stats_window.hpp"
#include "ui/theme.hpp"
#include "ui/user_interface.hpp"

#include <doctest/doctest.h>
#include <glm/vec2.hpp>
#include <imgui.h>
#include <imgui_impl_null.h>
#include <imgui_internal.h> // ImGui::FindWindowByName, to see where a card ended up

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <ostream> // doctest prints a failed string_view check through it, and MSVC needs it whole for that
#include <string_view>

using Wotah::Testing::IsNear;
using Wotah::Testing::MakeFocusLostEvent;
using Wotah::Testing::MakeKeyEvent;

namespace
{
    /** The window the UI is laid out in, in pixels: the program's at the start (app.cpp, MainWindowSize). */
    constexpr Wotah::WindowSize ScreenSize{1280, 720};

    /** One frame at 60 frames a second. */
    constexpr float FrameSeconds = 1.0f / 60.0f;

    /**
     * The frames run after each step. ImGui spreads input that comes within one frame over several (a key going down
     * and up is never seen in a single frame; imgui.h, ConfigInputTrickleEventQueue), and it knows what the mouse is
     * over only once a frame has run.
     */
    constexpr int SettleFrames = 4;

    /**
     * A spot in the stats window: in its left padding (the theme's 14 pixels), beside its rows below the header, so it
     * shows the bare panel.
     */
    constexpr glm::vec2 InsideStatsWindow{Wotah::StatsWindowPosition.x + 6.0f, Wotah::StatsWindowPosition.y + 50.0f};

    /** A spot at the top of the stats window, where a title bar would be (it has none). */
    constexpr glm::vec2 StatsWindowTop{Wotah::StatsWindowPosition.x + 40.0f, Wotah::StatsWindowPosition.y + 6.0f};

    /** A spot on the scene, clear of every UI window: the stats window, the control bar and the text field window. */
    constexpr glm::vec2 OverScene{500.0f, 400.0f};

    /** How far a card may sit off where it is asked to: ImGui places windows on whole pixels. */
    constexpr float PixelSlack = 1.0f;

    /** How far inside a button a click lands, from the button's outer edge. */
    constexpr float ButtonInset = 4.0f;

    /** How far apart clicks are when a test looks for a control down a column: well under a row's height. */
    constexpr float ScanStep = 4.0f;

    /** The text field window of the typing tests: its place and size, clear of the stats window. */
    constexpr ImVec2 FieldWindowPosition{300.0f, 300.0f};
    constexpr ImVec2 FieldWindowSize{260.0f, 80.0f};

    /** A spot inside the text field, which starts at the field window's padding (12) and is 25 pixels high. */
    constexpr glm::vec2 InsideTextField{FieldWindowPosition.x + 30.0f, FieldWindowPosition.y + 22.0f};

    /** The WCAG 2.2 contrast ratios: normal text (AA), enhanced (AAA), and marks that are not text (1.4.11). */
    constexpr double TextContrast         = 4.5;
    constexpr double EnhancedTextContrast = 7.0;
    constexpr double MarkContrast         = 3.0;

    /** The scenes a see-through panel may lie over: the darkest and the brightest. */
    constexpr ImVec4 Black{0.0f, 0.0f, 0.0f, 1.0f};
    constexpr ImVec4 White{1.0f, 1.0f, 1.0f, 1.0f};

    /** The fills text is drawn on: window parts, fields, buttons, rows, tabs and selected text. */
    constexpr std::array<ImGuiCol, 21> TextFills{
        ImGuiCol_PopupBg,           ImGuiCol_TitleBg,       ImGuiCol_TitleBgActive,
        ImGuiCol_TitleBgCollapsed,  ImGuiCol_MenuBarBg,     ImGuiCol_FrameBg,
        ImGuiCol_FrameBgHovered,    ImGuiCol_FrameBgActive, ImGuiCol_Button,
        ImGuiCol_ButtonHovered,     ImGuiCol_ButtonActive,  ImGuiCol_Header,
        ImGuiCol_HeaderHovered,     ImGuiCol_HeaderActive,  ImGuiCol_Tab,
        ImGuiCol_TabHovered,        ImGuiCol_TabSelected,   ImGuiCol_TabDimmed,
        ImGuiCol_TabDimmedSelected, ImGuiCol_TableHeaderBg, ImGuiCol_TextSelectedBg,
    };

    /** The mouse moving to position. */
    Wotah::InputEvent MakeMouseMove(const glm::vec2& position)
    {
        Wotah::InputEvent event;
        event.Type           = Wotah::InputEventType::MouseMoved;
        event.CursorPosition = position;
        return event;
    }

    /** A mouse button going down or being let go at position. */
    Wotah::InputEvent MakeMouseButton(Wotah::MouseButton button, bool isPressed, const glm::vec2& position)
    {
        Wotah::InputEvent event;
        event.Type = isPressed ? Wotah::InputEventType::MouseButtonPressed : Wotah::InputEventType::MouseButtonReleased;
        event.Button         = button;
        event.CursorPosition = position;
        return event;
    }

    /** The wheel turning one step towards the screen at position. */
    Wotah::InputEvent MakeWheelStep(const glm::vec2& position)
    {
        Wotah::InputEvent event;
        event.Type           = Wotah::InputEventType::MouseScrolled;
        event.CursorPosition = position;
        event.Delta          = {0.0f, 1.0f};
        return event;
    }

    /** A character typed. */
    Wotah::InputEvent MakeTextEvent(char32_t character)
    {
        Wotah::InputEvent event;
        event.Type      = Wotah::InputEventType::TextEntered;
        event.Character = character;
        return event;
    }

    /** A color channel from its sRGB value to linear light (WCAG 2.2, "relative luminance"). */
    double ToLinear(float channel)
    {
        const double value = static_cast<double>(channel);
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    }

    /** How bright a color looks, 0 to 1 (WCAG 2.2, "relative luminance"). */
    double GetLuminance(const ImVec4& color)
    {
        return 0.2126 * ToLinear(color.x) + 0.7152 * ToLinear(color.y) + 0.0722 * ToLinear(color.z);
    }

    /** The WCAG 2.2 contrast ratio of two solid colors: 1 for none up to 21 for black on white. */
    double GetContrast(const ImVec4& first, const ImVec4& second)
    {
        const double lighter = std::max(GetLuminance(first), GetLuminance(second));
        const double darker  = std::min(GetLuminance(first), GetLuminance(second));
        return (lighter + 0.05) / (darker + 0.05);
    }

    /** A see-through color laid over a solid one, as the GPU blends ImGui's shapes. */
    ImVec4 LayOver(const ImVec4& top, const ImVec4& bottom)
    {
        const float opacity = top.w;
        return {top.x * opacity + bottom.x * (1.0f - opacity), top.y * opacity + bottom.y * (1.0f - opacity),
                top.z * opacity + bottom.z * (1.0f - opacity), 1.0f};
    }

    /**
     * The UI on ImGui's null renderer, with the control bar and a text field window when a test wants them. Its
     * renderer goes before the UI, as the UI asks (the destructor's body runs before the members are destroyed).
     */
    struct HeadlessUi
    {
        Wotah::UserInterface Ui;

        /** Whether the null renderer was attached, so it is taken away again. */
        bool HasRenderer = false;

        /** Whether the frames show the text field window, and what the field holds. */
        bool                 HasTextField = false;
        std::array<char, 32> FieldText{};

        /** Whether the frames show the hints window. */
        bool HasHints = false;

        /**
         * Whether the frames show the settings window, the settings it changes, their defaults, and whether it said a
         * setting changed.
         */
        bool                 HasSettings = false;
        Wotah::SceneSettings Settings;
        Wotah::SceneSettings Defaults;
        bool                 SettingsChanged = false;

        /** Whether the frames show the control bar, whether it shows the world paused, and what was clicked in it. */
        bool                    HasControlBar = false;
        bool                    IsPaused      = false;
        Wotah::ControlBarClicks Clicked;

        HeadlessUi() = default;

        ~HeadlessUi()
        {
            if (HasRenderer)
            {
                ImGui_ImplNullRender_Shutdown();
            }
        }

        HeadlessUi(const HeadlessUi&)            = delete;
        HeadlessUi& operator=(const HeadlessUi&) = delete;

        [[nodiscard]] bool Create()
        {
            HasRenderer = Ui.Create() && ImGui_ImplNullRender_Init();
            return HasRenderer;
        }

        /** Runs UI frames: the stats window and, when wanted, the hints, settings, control bar and text field. */
        void RunFrames(int count = SettleFrames)
        {
            const Wotah::FrameStats stats;
            for (int frame = 0; frame < count; ++frame)
            {
                Ui.BeginFrame(ScreenSize, ScreenSize, FrameSeconds);
                Wotah::ShowStatsWindow(stats);
                if (HasHints)
                {
                    Wotah::ShowHintsWindow();
                }
                if (HasSettings)
                {
                    const bool isChanged = Wotah::ShowSettingsWindow(Settings, Defaults);
                    SettingsChanged      = SettingsChanged || isChanged;
                }
                if (HasControlBar)
                {
                    NoteClicks(Wotah::ShowControlBar(IsPaused));
                }
                if (HasTextField)
                {
                    ShowTextField();
                }
                Ui.EndFrame();
                ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
            }
        }

        /** Adds a frame's control bar clicks to Clicked. */
        void NoteClicks(const Wotah::ControlBarClicks& clicks)
        {
            Clicked.ResetScene  = Clicked.ResetScene || clicks.ResetScene;
            Clicked.TogglePause = Clicked.TogglePause || clicks.TogglePause;
            Clicked.ResetCamera = Clicked.ResetCamera || clicks.ResetCamera;
        }

        /** Moves the mouse to position and clicks the left button there. */
        void Click(const glm::vec2& position)
        {
            (void)Ui.OnInput(MakeMouseMove(position)); // what the UI kept is not the question here
            RunFrames();
            (void)Ui.OnInput(MakeMouseButton(Wotah::MouseButton::Left, true, position));
            RunFrames();
            (void)Ui.OnInput(MakeMouseButton(Wotah::MouseButton::Left, false, position));
            RunFrames();
        }

        /** Presses and lets go of a key. */
        void PressKey(Wotah::KeyCode key)
        {
            (void)Ui.OnInput(MakeKeyEvent(key, true));
            RunFrames();
            (void)Ui.OnInput(MakeKeyEvent(key, false));
            RunFrames();
        }

        /** Types text, one character at a time. */
        void Type(std::u32string_view text)
        {
            for (const char32_t character : text)
            {
                (void)Ui.OnInput(MakeTextEvent(character));
                RunFrames();
            }
        }

        /** What the text field holds. */
        [[nodiscard]] std::string_view GetFieldText() const { return FieldText.data(); }

        /** The text field window: fixed in place, no title bar. */
        void ShowTextField()
        {
            ImGui::SetNextWindowPos(FieldWindowPosition);
            ImGui::SetNextWindowSize(FieldWindowSize);
            ImGui::Begin("field", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
            ImGui::InputText("##text", FieldText.data(), FieldText.size());
            ImGui::End();
        }
    };
} // namespace

TEST_SUITE("ui")
{
    TEST_CASE("the UI starts once, stops safely, and lets input through while it is not running")
    {
        Wotah::UserInterface ui;
        CHECK_FALSE(ui.OnInput(MakeMouseButton(Wotah::MouseButton::Left, true, InsideStatsWindow)));
        ui.BeginFrame(ScreenSize, ScreenSize, FrameSeconds); // not running: nothing happens
        ui.EndFrame();

        REQUIRE(ui.Create());
        CHECK(ImGui::GetIO().IniFilename == nullptr); // nothing written beside the executable
        CHECK(ImGui::GetIO().LogFilename == nullptr);
        CHECK(IsNear(ImGui::GetStyle().FontSizeBase, Wotah::ThemeFontSize));

        Wotah::UserInterface second;
        CHECK_FALSE(second.Create()); // ImGui runs already

        ui.Destroy();
        ui.Destroy(); // twice is safe
        CHECK(ImGui::GetCurrentContext() == nullptr);
        CHECK(second.Create()); // free again
    }

    TEST_CASE("the UI keeps the mouse over its windows and lets the rest through to the camera")
    {
        HeadlessUi headless;
        REQUIRE(headless.Create());
        Wotah::CameraController camera;
        Wotah::InputDispatcher  dispatcher;
        dispatcher.AddHandler(headless.Ui);
        dispatcher.AddHandler(camera);
        headless.RunFrames();

        // Over the stats window: the UI keeps the wheel, and the right button does not turn the camera.
        dispatcher.Dispatch(MakeMouseMove(InsideStatsWindow));
        headless.RunFrames();
        CHECK(headless.Ui.OnInput(MakeWheelStep(InsideStatsWindow)));
        dispatcher.Dispatch(MakeMouseButton(Wotah::MouseButton::Right, true, InsideStatsWindow));
        CHECK_FALSE(camera.IsTurning());
        dispatcher.Dispatch(MakeMouseButton(Wotah::MouseButton::Right, false, InsideStatsWindow));
        headless.RunFrames();

        // Over the scene: the camera gets them.
        dispatcher.Dispatch(MakeMouseMove(OverScene));
        headless.RunFrames();
        CHECK_FALSE(headless.Ui.OnInput(MakeWheelStep(OverScene)));
        dispatcher.Dispatch(MakeMouseButton(Wotah::MouseButton::Right, true, OverScene));
        CHECK(camera.IsTurning());
    }

    TEST_CASE("a drag stays with whoever it started on: the camera over UI windows, the UI over the scene")
    {
        HeadlessUi headless;
        REQUIRE(headless.Create());
        headless.RunFrames();

        // Turning the camera, started over the scene, goes on while the mouse crosses the stats window.
        (void)headless.Ui.OnInput(MakeMouseMove(OverScene));
        headless.RunFrames();
        CHECK_FALSE(headless.Ui.OnInput(MakeMouseButton(Wotah::MouseButton::Right, true, OverScene)));
        headless.RunFrames();
        (void)headless.Ui.OnInput(MakeMouseMove(InsideStatsWindow));
        headless.RunFrames();
        CHECK_FALSE(headless.Ui.OnInput(MakeMouseMove(InsideStatsWindow)));
        (void)headless.Ui.OnInput(MakeMouseButton(Wotah::MouseButton::Right, false, InsideStatsWindow));
        headless.RunFrames();

        // A press on the stats window stays with the UI while it is held, also out over the scene.
        CHECK(headless.Ui.OnInput(MakeMouseButton(Wotah::MouseButton::Left, true, InsideStatsWindow)));
        headless.RunFrames();
        (void)headless.Ui.OnInput(MakeMouseMove(OverScene));
        headless.RunFrames();
        CHECK(headless.Ui.OnInput(MakeMouseMove(OverScene)));
    }

    TEST_CASE("the stats window stays as it is: a double-click does not fold it, a drag does not move it")
    {
        HeadlessUi headless;
        REQUIRE(headless.Create());
        headless.RunFrames();

        // A double-click at its top, where a title bar would fold it. The clicks come a frame apart, well inside
        // ImGui's double-click time (ImGuiIO::MouseDoubleClickTime, 0.3 s).
        (void)headless.Ui.OnInput(MakeMouseMove(StatsWindowTop));
        headless.RunFrames();
        for (int click = 0; click < 2; ++click)
        {
            (void)headless.Ui.OnInput(MakeMouseButton(Wotah::MouseButton::Left, true, StatsWindowTop));
            headless.RunFrames(1);
            (void)headless.Ui.OnInput(MakeMouseButton(Wotah::MouseButton::Left, false, StatsWindowTop));
            headless.RunFrames(1);
        }
        headless.RunFrames();

        // A drag from its middle out over the scene.
        (void)headless.Ui.OnInput(MakeMouseMove(InsideStatsWindow));
        headless.RunFrames();
        (void)headless.Ui.OnInput(MakeMouseButton(Wotah::MouseButton::Left, true, InsideStatsWindow));
        headless.RunFrames();
        (void)headless.Ui.OnInput(MakeMouseMove(OverScene));
        headless.RunFrames();
        (void)headless.Ui.OnInput(MakeMouseButton(Wotah::MouseButton::Left, false, OverScene));
        headless.RunFrames();

        // The scene is still free, and the whole window still where it was.
        CHECK_FALSE(headless.Ui.OnInput(MakeWheelStep(OverScene)));
        (void)headless.Ui.OnInput(MakeMouseMove(InsideStatsWindow));
        headless.RunFrames();
        CHECK(headless.Ui.OnInput(MakeWheelStep(InsideStatsWindow)));
    }

    TEST_CASE("the control bar sits at the bottom middle, and each of its buttons reports its own click")
    {
        HeadlessUi headless;
        REQUIRE(headless.Create());
        headless.HasControlBar = true;
        headless.RunFrames();

        const ImGuiWindow* bar = ImGui::FindWindowByName(Wotah::ControlBarName);
        REQUIRE(bar != nullptr);
        const float width  = static_cast<float>(ScreenSize.Width);
        const float height = static_cast<float>(ScreenSize.Height);
        CHECK(std::abs(bar->Pos.x + bar->Size.x / 2.0f - width / 2.0f) <= PixelSlack);
        CHECK(std::abs(bar->Pos.y + bar->Size.y - (height - Wotah::CardMargin)) <= PixelSlack);

        // Left to right: Reset scene, Pause, Reset camera. The outer ones are hit just inside the bar's padding.
        const ImVec2&   padding = ImGui::GetStyle().WindowPadding;
        const float     middleY = bar->Pos.y + bar->Size.y / 2.0f;
        const glm::vec2 left{bar->Pos.x + padding.x + ButtonInset, middleY};
        const glm::vec2 middle{bar->Pos.x + bar->Size.x / 2.0f, middleY};
        const glm::vec2 right{bar->Pos.x + bar->Size.x - padding.x - ButtonInset, middleY};

        headless.Click(left);
        CHECK(headless.Clicked.ResetScene);
        CHECK_FALSE(headless.Clicked.TogglePause);
        CHECK_FALSE(headless.Clicked.ResetCamera);

        headless.Clicked = {};
        headless.Click(middle);
        CHECK_FALSE(headless.Clicked.ResetScene);
        CHECK(headless.Clicked.TogglePause);
        CHECK_FALSE(headless.Clicked.ResetCamera);

        headless.Clicked = {};
        headless.Click(right);
        CHECK_FALSE(headless.Clicked.ResetScene);
        CHECK_FALSE(headless.Clicked.TogglePause);
        CHECK(headless.Clicked.ResetCamera);

        // The bar keeps the mouse, so clicks on it never reach the camera.
        CHECK(headless.Ui.OnInput(MakeWheelStep(middle)));
    }

    TEST_CASE("the program's cards fit its window: stats, hints, settings and the bar, none overlapping")
    {
        HeadlessUi headless;
        REQUIRE(headless.Create());
        headless.HasHints      = true;
        headless.HasSettings   = true;
        headless.HasControlBar = true;
        headless.RunFrames();

        const ImGuiWindow* stats    = ImGui::FindWindowByName(Wotah::StatsWindowTitle);
        const ImGuiWindow* hints    = ImGui::FindWindowByName(Wotah::HintsWindowTitle);
        const ImGuiWindow* settings = ImGui::FindWindowByName(Wotah::SettingsWindowTitle);
        const ImGuiWindow* bar      = ImGui::FindWindowByName(Wotah::ControlBarName);
        REQUIRE(stats != nullptr);
        REQUIRE(hints != nullptr);
        REQUIRE(settings != nullptr);
        REQUIRE(bar != nullptr);

        // The hints sit in the top-right corner, a card's margin in.
        const float width = static_cast<float>(ScreenSize.Width);
        CHECK(std::abs(hints->Pos.x + hints->Size.x - (width - Wotah::CardMargin)) <= PixelSlack);
        CHECK(std::abs(hints->Pos.y - Wotah::CardMargin) <= PixelSlack);

        // Side by side at the top with room between them, and both well above the bar.
        CHECK(stats->Pos.x + stats->Size.x + Wotah::CardMargin <= hints->Pos.x);
        CHECK(stats->Pos.y + stats->Size.y + Wotah::CardMargin <= bar->Pos.y);
        CHECK(hints->Pos.y + hints->Size.y + Wotah::CardMargin <= bar->Pos.y);

        // The settings sit in the bottom-left corner, below the stats and left of the bar.
        const float height = static_cast<float>(ScreenSize.Height);
        CHECK(std::abs(settings->Pos.x - Wotah::CardMargin) <= PixelSlack);
        CHECK(std::abs(settings->Pos.y + settings->Size.y - (height - Wotah::CardMargin)) <= PixelSlack);
        CHECK(stats->Pos.y + stats->Size.y + Wotah::CardMargin <= settings->Pos.y);
        CHECK(settings->Pos.x + settings->Size.x + Wotah::CardMargin <= bar->Pos.x);
    }

    TEST_CASE(
        "the settings window: a click on a slider changes that setting and says so; Reset puts every setting back")
    {
        HeadlessUi headless;
        REQUIRE(headless.Create());
        headless.HasSettings                   = true;
        headless.Defaults.TimeOfDayHours       = 9.0f;
        headless.Defaults.SunNoonHeightDegrees = 60.0f;
        headless.Defaults.Gravity              = 9.8f;
        headless.Defaults.BallBounciness       = 0.6f;
        headless.Settings                      = headless.Defaults;
        headless.RunFrames();
        CHECK_FALSE(headless.SettingsChanged); // only shown, nothing touched

        const ImGuiWindow* window = ImGui::FindWindowByName(Wotah::SettingsWindowTitle);
        REQUIRE(window != nullptr);
        const ImVec2& padding = ImGui::GetStyle().WindowPadding;

        // Down the sliders' column, just inside its right end: the first click that lands on a slider is the time of
        // day (the sky's tab is open first), which jumps to nearly midnight. Nothing else changes.
        const float sliderX = window->Pos.x + window->Size.x - padding.x - ButtonInset;
        const float bottom  = window->Pos.y + window->Size.y;
        for (float y = window->Pos.y + padding.y; y < bottom && !headless.SettingsChanged; y += ScanStep)
        {
            headless.Click({sliderX, y});
        }
        REQUIRE(headless.SettingsChanged);
        CHECK(headless.Settings.TimeOfDayHours > 22.0f);
        CHECK(IsNear(headless.Settings.SunNoonHeightDegrees, headless.Defaults.SunNoonHeightDegrees));
        CHECK(IsNear(headless.Settings.Gravity, headless.Defaults.Gravity));

        // Reset to defaults, the button in the bottom-left corner, puts it back.
        headless.SettingsChanged = false;
        headless.Click({window->Pos.x + padding.x + ButtonInset, bottom - padding.y - ButtonInset});
        CHECK(headless.SettingsChanged);
        CHECK(IsNear(headless.Settings.TimeOfDayHours, headless.Defaults.TimeOfDayHours));

        // The window keeps the mouse, so dragging a slider never turns the camera.
        CHECK(headless.Ui.OnInput(MakeWheelStep({sliderX, bottom - padding.y - ButtonInset})));
    }

    TEST_CASE("the settings window's tabs: Sea shows the waves' slider, the window staying in its corner")
    {
        HeadlessUi headless;
        REQUIRE(headless.Create());
        headless.HasSettings               = true;
        headless.Defaults.TimeOfDayHours   = 9.0f;
        headless.Defaults.Gravity          = 9.8f;
        headless.Defaults.DayLengthMinutes = 10.0f;
        headless.Settings                  = headless.Defaults;
        headless.RunFrames();

        const ImGuiWindow* window = ImGui::FindWindowByName(Wotah::SettingsWindowTitle);
        REQUIRE(window != nullptr);
        const ImVec2& padding   = ImGui::GetStyle().WindowPadding;
        const float   skyHeight = window->Size.y;

        // Row by row from the top, left to right: the header and its line take no clicks, the Sky tab is open already,
        // so the first click that changes the window is on the Sea tab, which has fewer rows. No setting changes.
        bool isSwitched = false;
        for (float y = window->Pos.y + padding.y; y < window->Pos.y + skyHeight && !isSwitched; y += ScanStep)
        {
            for (float x = window->Pos.x + padding.x; x < window->Pos.x + window->Size.x && !isSwitched; x += ScanStep)
            {
                headless.Click({x, y});
                isSwitched = window->Size.y != skyHeight || headless.SettingsChanged;
            }
        }
        REQUIRE(isSwitched);
        CHECK_FALSE(headless.SettingsChanged);
        CHECK(window->Size.y < skyHeight);

        // Still in the bottom-left corner: it shrank downwards.
        const float height = static_cast<float>(ScreenSize.Height);
        CHECK(std::abs(window->Pos.y + window->Size.y - (height - Wotah::CardMargin)) <= PixelSlack);

        // Its slider is the waves' height: a click at its right end sets nearly the most (3 m); the others stay.
        const float sliderX = window->Pos.x + window->Size.x - padding.x - ButtonInset;
        for (float y = window->Pos.y + padding.y; y < window->Pos.y + window->Size.y && !headless.SettingsChanged;
             y += ScanStep)
        {
            headless.Click({sliderX, y});
        }
        REQUIRE(headless.SettingsChanged);
        CHECK(headless.Settings.WaveHeight > 2.5f);
        CHECK(IsNear(headless.Settings.Gravity, headless.Defaults.Gravity));
        CHECK(IsNear(headless.Settings.TimeOfDayHours, headless.Defaults.TimeOfDayHours));
    }

    TEST_CASE("the time of day reads as a 24-hour clock, wrapping past midnight")
    {
        CHECK(std::string_view(Wotah::MakeClockText(6.5f).data()) == "06:30");
        CHECK(std::string_view(Wotah::MakeClockText(0.0f).data()) == "00:00");
        CHECK(std::string_view(Wotah::MakeClockText(12.0f).data()) == "12:00");
        CHECK(std::string_view(Wotah::MakeClockText(23.999f).data()) == "23:59");
        CHECK(std::string_view(Wotah::MakeClockText(24.0f).data()) == "00:00");
        CHECK(std::string_view(Wotah::MakeClockText(25.25f).data()) == "01:15");
        CHECK(std::string_view(Wotah::MakeClockText(-1.0f).data()) == "23:00");
        CHECK(std::string_view(Wotah::MakeClockText(std::numeric_limits<float>::quiet_NaN()).data()) == "00:00");
        CHECK(std::string_view(Wotah::MakeClockText(std::numeric_limits<float>::infinity()).data()) == "00:00");
    }

    TEST_CASE("the pause button reads Resume while paused, keeps its width so the bar does not jump, and still works")
    {
        HeadlessUi headless;
        REQUIRE(headless.Create());
        headless.HasControlBar = true;
        headless.RunFrames();
        const ImGuiWindow* bar = ImGui::FindWindowByName(Wotah::ControlBarName);
        REQUIRE(bar != nullptr);
        const float runningWidth = bar->Size.x;

        headless.IsPaused = true;
        headless.RunFrames();
        CHECK(IsNear(bar->Size.x, runningWidth));

        // The same button under its other label: a click on it still asks to resume.
        const glm::vec2 middle{bar->Pos.x + bar->Size.x / 2.0f, bar->Pos.y + bar->Size.y / 2.0f};
        headless.Click(middle);
        CHECK(headless.Clicked.TogglePause);
    }

    TEST_CASE("typing goes into a focused UI text field, not to the camera; without one, keys reach the camera")
    {
        HeadlessUi headless;
        REQUIRE(headless.Create());
        headless.HasTextField = true;
        headless.RunFrames();

        // No field has the keyboard yet.
        CHECK_FALSE(headless.Ui.OnInput(MakeKeyEvent(Wotah::KeyCode::W, true)));
        CHECK_FALSE(headless.Ui.OnInput(MakeKeyEvent(Wotah::KeyCode::W, false)));

        headless.Click(InsideTextField);
        CHECK(headless.Ui.OnInput(MakeKeyEvent(Wotah::KeyCode::W, true)));
        CHECK(headless.Ui.OnInput(MakeKeyEvent(Wotah::KeyCode::W, false)));
        headless.RunFrames();

        headless.Type(U"hi");
        CHECK(headless.GetFieldText() == "hi");
        headless.PressKey(Wotah::KeyCode::Backspace);
        CHECK(headless.GetFieldText() == "h");
        headless.Type(U"ey");
        headless.PressKey(Wotah::KeyCode::Home);
        headless.PressKey(Wotah::KeyCode::Delete);
        CHECK(headless.GetFieldText() == "ey");

        // Ctrl+A picks everything, and Backspace takes it away: the modifier reaches ImGui.
        (void)headless.Ui.OnInput(MakeKeyEvent(Wotah::KeyCode::LeftControl, true));
        headless.PressKey(Wotah::KeyCode::A);
        (void)headless.Ui.OnInput(MakeKeyEvent(Wotah::KeyCode::LeftControl, false));
        headless.RunFrames();
        headless.PressKey(Wotah::KeyCode::Backspace);
        CHECK(headless.GetFieldText().empty());

        // A click on the scene ends the typing: keys reach the camera again.
        headless.Click(OverScene);
        CHECK_FALSE(headless.Ui.OnInput(MakeKeyEvent(Wotah::KeyCode::W, true)));
    }

    TEST_CASE("losing the window's focus lets go of held modifiers, so typing afterwards is no shortcut")
    {
        HeadlessUi headless;
        REQUIRE(headless.Create());
        headless.HasTextField = true;
        headless.RunFrames();
        headless.Click(InsideTextField);
        headless.Type(U"hi");

        // Ctrl goes down, then another program takes the focus: the window never sees Ctrl let go.
        (void)headless.Ui.OnInput(MakeKeyEvent(Wotah::KeyCode::LeftControl, true));
        headless.RunFrames();
        CHECK_FALSE(headless.Ui.OnInput(MakeFocusLostEvent()));
        headless.RunFrames();

        // Back in the window, X types an x; with Ctrl still counted as held, it would be Ctrl+X and cut the whole text.
        (void)headless.Ui.OnInput(MakeKeyEvent(Wotah::KeyCode::X, true));
        (void)headless.Ui.OnInput(MakeTextEvent(U'x'));
        headless.RunFrames();
        (void)headless.Ui.OnInput(MakeKeyEvent(Wotah::KeyCode::X, false));
        headless.RunFrames();
        CHECK(headless.GetFieldText() == "hix");
    }

    TEST_CASE("the UI lays out in the cursor's units and draws at the picture's pixels")
    {
        HeadlessUi headless;
        REQUIRE(headless.Create());
        const ImGuiIO& io = ImGui::GetIO();

        // A screen the system scales to 200 %: the window counts half the picture's pixels.
        constexpr Wotah::WindowSize ScaledWindow{400, 300};
        constexpr Wotah::WindowSize Picture{800, 600};
        headless.Ui.BeginFrame(ScaledWindow, Picture, FrameSeconds);
        headless.Ui.EndFrame();
        CHECK(IsNear(io.DisplaySize.x, 400.0f));
        CHECK(IsNear(io.DisplaySize.y, 300.0f));
        CHECK(IsNear(io.DisplayFramebufferScale.x, 2.0f));
        CHECK(IsNear(io.DisplayFramebufferScale.y, 2.0f));

        // A window without area has no ratio: the picture's size counts for both. A frame time of 0 is made positive,
        // which ImGui needs (it would stop the program in a debug build).
        constexpr Wotah::WindowSize NoArea{0, 0};
        headless.Ui.BeginFrame(NoArea, Picture, 0.0f);
        CHECK(io.DeltaTime > 0.0f);
        headless.Ui.EndFrame();
        CHECK(IsNear(io.DisplaySize.x, 800.0f));
        CHECK(IsNear(io.DisplaySize.y, 600.0f));
        CHECK(IsNear(io.DisplayFramebufferScale.x, 1.0f));
        CHECK(IsNear(io.DisplayFramebufferScale.y, 1.0f));
    }

    TEST_CASE("the theme keeps text readable on every fill and makes the accent stand out on fields")
    {
        ImGuiStyle style;
        Wotah::ApplyTheme(style);
        const ImVec4* colors = style.Colors;
        CHECK(IsNear(style.FontSizeBase, Wotah::ThemeFontSize));

        // Panels are slightly see-through, so they are checked over the darkest and the brightest scene.
        for (const ImVec4& scene : {Black, White})
        {
            CAPTURE(scene.x);
            const ImVec4 panel = LayOver(colors[ImGuiCol_WindowBg], scene);
            CHECK(GetContrast(colors[ImGuiCol_Text], panel) >= EnhancedTextContrast);
            CHECK(GetContrast(colors[ImGuiCol_TextDisabled], panel) >= TextContrast);
            CHECK(GetContrast(colors[ImGuiCol_TextLink], panel) >= TextContrast);

            for (const ImGuiCol fill : TextFills)
            {
                CAPTURE(fill);
                CHECK(GetContrast(colors[ImGuiCol_Text], LayOver(colors[fill], panel)) >= TextContrast);
            }

            const ImVec4 field = LayOver(colors[ImGuiCol_FrameBg], panel);
            CHECK(GetContrast(colors[ImGuiCol_CheckMark], field) >= MarkContrast);
            CHECK(GetContrast(colors[ImGuiCol_SliderGrab], field) >= MarkContrast);
        }
    }
}
