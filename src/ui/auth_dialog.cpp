#include "ui/auth_dialog.hpp"
#include "config.hpp"
#include <miqutoolkit/core/config.hpp>
#include <algorithm>
#include <iostream>

namespace miqu {

AuthDialogManager* AuthDialogManager::instance() {
    static AuthDialogManager s_instance;
    return &s_instance;
}

void AuthDialogManager::show_dialog(
    const std::string& action_id,
    const std::string& message,
    const std::string& icon_name,
    const std::string& user_name,
    std::function<void(const std::string&)> on_authenticate,
    std::function<void()> on_cancel
) {
    dismiss_dialog();

    m_on_authenticate = std::move(on_authenticate);
    m_on_cancel = std::move(on_cancel);

    auto config = Config::get();
    const auto& polkit_cfg = PolkitConfig::get();

    // 1. Header: Icon + Title
    std::string resolved_icon = icon_name.empty() ? "dialog-password" : icon_name;
    auto iconView = ImageViewBuilder::create()
        ->source(resolved_icon)
        ->targetSize(polkit_cfg.icon_size)
        ->margin(0, 0, 10, 0)
        ->build();

    auto titleView = TextViewBuilder::create()
        ->text("Authentication Required")
        ->bold(true)
        ->textSize(15)
        ->textColor(config->colors.on_surface)
        ->build();

    auto headerLayout = LinearLayoutBuilder::create()
        ->orientation(Orientation::Horizontal)
        ->gravity(Gravity::CenterVertical)
        ->addView(iconView)
        ->addView(titleView, LayoutParams(static_cast<int>(LayoutDimension::MatchParent), static_cast<int>(LayoutDimension::WrapContent)))
        ->build();

    // 2. Action Message
    std::string display_msg = message.empty() ? "Authentication is required to perform an action." : message;
    int msg_lines = std::clamp(static_cast<int>(display_msg.length() / 42) + 1, 1, 4);
    int msg_h = std::max(24, msg_lines * 20);

    auto messageView = TextViewBuilder::create()
        ->text(display_msg)
        ->textSize(12)
        ->ellipsize(false)
        ->textColor(config->colors.on_surface_variant)
        ->build();

    // 3. User Identity Badge
    auto userIcon = ImageViewBuilder::create()
        ->source("avatar-default")
        ->targetSize(20)
        ->margin(0, 0, 8, 0)
        ->build();

    auto userLabel = TextViewBuilder::create()
        ->text("Authenticating as: " + user_name)
        ->bold(true)
        ->textSize(12)
        ->textColor(config->colors.primary)
        ->build();

    auto identityLayout = LinearLayoutBuilder::create()
        ->orientation(Orientation::Horizontal)
        ->gravity(Gravity::CenterVertical)
        ->backgroundColor(config->colors.surface_variant)
        ->cornerRadius(8)
        ->padding(10, 6)
        ->addView(userIcon)
        ->addView(userLabel)
        ->build();

    // 4. Status / Error Message Banner
    m_error_view = TextViewBuilder::create()
        ->text("")
        ->bold(true)
        ->textSize(11)
        ->textColor(Color::rgb(0.95f, 0.54f, 0.65f)) // #f38ba8 Catppuccin red
        ->build();

    // 5. Password Input Field
    m_password_input = EditTextBuilder::create()
        ->hint("Enter password...")
        ->passwordMode(true)
        ->focused(true)
        ->padding(14, 10)
        ->onSubmit([this](const std::string& password) {
            if (m_on_authenticate) {
                if (m_error_view) m_error_view->set_text("Authenticating...");
                if (m_window) m_window->schedule_redraw();
                m_on_authenticate(password);
            }
        })
        ->build();

    // 6. Buttons: Cancel and Authenticate
    auto cancelBtn = ButtonBuilder::create()
        ->text("Cancel")
        ->cornerRadius(8)
        ->padding(16, 8)
        ->onClick([this]() {
            cancel_current();
        })
        ->build();
    cancelBtn->set_custom_colors(config->colors.surface_variant, config->colors.on_surface);

    auto authBtn = ButtonBuilder::create()
        ->text("Authenticate")
        ->bold(true)
        ->cornerRadius(8)
        ->padding(18, 8)
        ->onClick([this]() {
            if (m_on_authenticate && m_password_input) {
                std::string password = m_password_input->get_text();
                if (m_error_view) m_error_view->set_text("Authenticating...");
                if (m_window) m_window->schedule_redraw();
                m_on_authenticate(password);
            }
        })
        ->build();
    authBtn->set_custom_colors(config->colors.primary, config->colors.background);

    auto buttonLayout = LinearLayoutBuilder::create()
        ->orientation(Orientation::Horizontal)
        ->gravity(Gravity::Right | Gravity::CenterVertical)
        ->spacing(10)
        ->addView(cancelBtn)
        ->addView(authBtn)
        ->build();

    // 7. Assemble Content Layout
    auto contentLayoutBuilder = LinearLayoutBuilder::create()
        ->orientation(Orientation::Vertical)
        ->spacing(12)
        ->addView(headerLayout, LayoutParams(static_cast<int>(LayoutDimension::MatchParent), static_cast<int>(LayoutDimension::WrapContent)))
        ->addView(messageView, LayoutParams(static_cast<int>(LayoutDimension::MatchParent), msg_h));

    if (polkit_cfg.show_user_identity) {
        contentLayoutBuilder->addView(identityLayout, LayoutParams(static_cast<int>(LayoutDimension::MatchParent), static_cast<int>(LayoutDimension::WrapContent)));
    }

    auto contentLayout = contentLayoutBuilder
        ->addView(m_error_view, LayoutParams(static_cast<int>(LayoutDimension::MatchParent), 18))
        ->addView(m_password_input, LayoutParams(static_cast<int>(LayoutDimension::MatchParent), 42))
        ->addView(buttonLayout, LayoutParams(static_cast<int>(LayoutDimension::MatchParent), 38))
        ->build();

    // 8. Outer Card Container
    int card_w = polkit_cfg.width;
    int identity_h = polkit_cfg.show_user_identity ? 44 : 0;
    int card_h = 110 + msg_h + identity_h + 140;

    auto rootCard = CardViewBuilder::create()
        ->backgroundColor(config->colors.surface)
        ->stroke(config->metrics.border_width > 0 ? config->metrics.border_width : 1, config->colors.outline)
        ->cornerRadius(config->metrics.corner_radius > 0 ? config->metrics.corner_radius : 14)
        ->padding(20)
        ->addView(contentLayout, LayoutParams(static_cast<int>(LayoutDimension::MatchParent), static_cast<int>(LayoutDimension::MatchParent)))
        ->build();

    rootCard->set_on_click_listener([this]() {
        if (m_password_input) {
            m_password_input->set_focused(true);
            if (m_window) m_window->schedule_redraw();
        }
    });

    // 9. Modal Window with Dim Backdrop
    m_window = WindowBuilder::create()
        ->role(WindowRole::LayerOverlay)
        ->layerNamespace("miqupolkit")
        ->appId("miqupolkit")
        ->title("Authentication Required")
        ->preferredSize(card_w, card_h)
        ->contentSize(card_w, card_h)
        ->anchors(0)
        ->dimBackdrop(polkit_cfg.dim_backdrop)
        ->keyboardInteractive(true)
        ->closeOnClickOutside(polkit_cfg.close_on_click_outside)
        ->closeOnEscape(true)
        ->contentView(rootCard)
        ->onClose([this]() {
            cancel_current();
        })
        ->onKey([this](const KeyPressEvent& event) {
            if (!event.pressed) return;
            if (event.keysym == XKB_KEY_Escape) {
                cancel_current();
                return;
            }
            if (m_password_input && !m_password_input->is_focused()) {
                m_password_input->set_focused(true);
                if (m_window) m_window->schedule_redraw();
            }
        })
        ->build();

    if (m_window) {
        m_window->schedule_redraw();
    }
}

void AuthDialogManager::show_error(const std::string& error_text) {
    if (m_error_view) {
        m_error_view->set_text(error_text);
    }
    if (m_password_input) {
        m_password_input->clear();
        m_password_input->set_focused(true);
    }
    if (m_window) {
        m_window->schedule_redraw();
    }
}

void AuthDialogManager::show_info(const std::string& info_text) {
    if (m_error_view) {
        m_error_view->set_text(info_text);
    }
    if (m_window) {
        m_window->schedule_redraw();
    }
}

void AuthDialogManager::cancel_current() {
    auto cancel_cb = std::move(m_on_cancel);
    m_on_cancel = nullptr;
    m_on_authenticate = nullptr;
    dismiss_dialog();
    if (cancel_cb) {
        cancel_cb();
    }
}

void AuthDialogManager::dismiss_dialog() {
    if (m_window) {
        auto win = m_window;
        m_window.reset();
        m_password_input.reset();
        m_error_view.reset();
        win->close();
    }
}

} // namespace miqu
