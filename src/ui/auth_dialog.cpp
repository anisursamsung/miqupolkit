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

    // 1. Prominent Header Icon
    std::string resolved_icon = icon_name.empty() ? "dialog-password" : icon_name;
    int icon_sz = std::max(40, polkit_cfg.icon_size);
    auto iconView = ImageViewBuilder::create()
        ->source(resolved_icon)
        ->targetSize(icon_sz)
        ->build();

    // 2. Centered Title
    auto titleView = TextViewBuilder::create()
        ->text("Authentication Required")
        ->bold(true)
        ->textSize(15)
        ->textAlignment(TextAlignment::Center)
        ->textColor(config->colors.on_surface)
        ->build();

    // 3. Centered Action Message
    std::string display_msg = message.empty() ? "Authentication is required to perform an action." : message;
    int msg_lines = std::clamp(static_cast<int>(display_msg.length() / 36) + 1, 1, 4);
    int msg_h = std::max(20, msg_lines * 18);

    auto messageView = TextViewBuilder::create()
        ->text(display_msg)
        ->textSize(12)
        ->multiline(true)
        ->wrap(true)
        ->ellipsize(false)
        ->textAlignment(TextAlignment::Center)
        ->textColor(config->colors.on_surface_variant)
        ->build();

    // 4. User Identity Badge (Centered Material Chip)
    auto userIcon = ImageViewBuilder::create()
        ->source("avatar-default")
        ->targetSize(16)
        ->margin(0, 0, 6, 0)
        ->build();

    auto userLabel = TextViewBuilder::create()
        ->text("Authenticating as " + user_name)
        ->bold(true)
        ->textSize(11)
        ->textColor(config->colors.primary)
        ->build();

    auto identityLayout = LinearLayoutBuilder::create()
        ->orientation(Orientation::Horizontal)
        ->gravity(Gravity::Center)
        ->backgroundColor(config->colors.surface_variant)
        ->cornerRadius(14)
        ->padding(12, 5)
        ->addView(userIcon)
        ->addView(userLabel)
        ->build();

    // 5. Status / Error Message Banner
    m_error_view = TextViewBuilder::create()
        ->text("")
        ->bold(true)
        ->textSize(11)
        ->textAlignment(TextAlignment::Center)
        ->textColor(Color::rgb(0.95f, 0.54f, 0.65f)) // #f38ba8 Catppuccin red
        ->build();

    // 6. Password Input Field
    m_password_input = EditTextBuilder::create()
        ->hint("Enter password...")
        ->passwordMode(true)
        ->focused(true)
        ->padding(18, 11)
        ->margin(8, 0)
        ->onSubmit([this](const std::string& password) {
            if (m_on_authenticate) {
                if (m_error_view) m_error_view->set_text("Authenticating...");
                if (m_window) m_window->schedule_redraw();
                m_on_authenticate(password);
            }
        })
        ->build();

    // 7. Buttons: Cancel and Authenticate (Centered & Wrap Content)
    auto cancelBtn = ButtonBuilder::create()
        ->text("Cancel")
        ->cornerRadius(10)
        ->padding(20, 9)
        ->onClick([this]() {
            cancel_current();
        })
        ->build();
    cancelBtn->set_custom_colors(config->colors.surface_variant, config->colors.on_surface);

    auto authBtn = ButtonBuilder::create()
        ->text("Authenticate")
        ->bold(true)
        ->cornerRadius(10)
        ->padding(24, 9)
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
        ->gravity(Gravity::Center)
        ->spacing(12)
        ->addView(cancelBtn, LayoutParams(static_cast<int>(LayoutDimension::WrapContent), 38))
        ->addView(authBtn, LayoutParams(static_cast<int>(LayoutDimension::WrapContent), 38))
        ->build();

    // 8. Assemble Content Layout
    auto contentLayoutBuilder = LinearLayoutBuilder::create()
        ->orientation(Orientation::Vertical)
        ->gravity(Gravity::CenterHorizontal)
        ->spacing(10)
        ->addView(iconView, LayoutParams(icon_sz, icon_sz, Gravity::CenterHorizontal))
        ->addView(titleView, LayoutParams(static_cast<int>(LayoutDimension::MatchParent), 22, Gravity::CenterHorizontal))
        ->addView(messageView, LayoutParams(static_cast<int>(LayoutDimension::MatchParent), msg_h, Gravity::CenterHorizontal));

    if (polkit_cfg.show_user_identity) {
        contentLayoutBuilder->addView(identityLayout, LayoutParams(static_cast<int>(LayoutDimension::WrapContent), static_cast<int>(LayoutDimension::WrapContent), Gravity::CenterHorizontal));
    }

    auto contentLayout = contentLayoutBuilder
        ->addView(m_error_view, LayoutParams(static_cast<int>(LayoutDimension::MatchParent), 16, Gravity::CenterHorizontal))
        ->addView(m_password_input, LayoutParams(static_cast<int>(LayoutDimension::MatchParent), 40))
        ->addView(buttonLayout, LayoutParams(static_cast<int>(LayoutDimension::WrapContent), 38, Gravity::CenterHorizontal))
        ->build();

    // 9. Outer Card Container with dynamic wrap-content geometry and generous bottom padding
    int card_w = polkit_cfg.width > 0 ? polkit_cfg.width : 420;
    int pad_h = 24;
    int pad_t = 22;
    int pad_b = 32;
    int identity_h = polkit_cfg.show_user_identity ? (26 + 10) : 0;
    int card_h = pad_t + pad_b + icon_sz + 10 + 22 + 10 + msg_h + identity_h + 10 + 16 + 10 + 40 + 10 + 38;

    auto rootCard = CardViewBuilder::create()
        ->backgroundColor(config->colors.background)
        ->stroke(config->metrics.border_width, config->colors.outline)
        ->cornerRadius(config->metrics.corner_radius > 0 ? config->metrics.corner_radius : 16)
        ->padding(pad_h, pad_t, pad_h, pad_b)
        ->addView(contentLayout, LayoutParams(static_cast<int>(LayoutDimension::MatchParent), static_cast<int>(LayoutDimension::MatchParent)))
        ->build();

    rootCard->set_on_click_listener([this]() {
        if (m_password_input) {
            m_password_input->set_focused(true);
            if (m_window) m_window->schedule_redraw();
        }
    });

    // 9. Layer overlay Window (matching miqulauncher)
    auto builder = WindowBuilder::create()
        ->role(WindowRole::LayerOverlay)
        ->appId("miqupolkit")
        ->keyboardInteractive(true)
        ->preferredSize(card_w, card_h)
        ->contentSize(card_w, card_h)
        ->closeOnClickOutside(polkit_cfg.close_on_click_outside)
        ->closeOnEscape(true)
        ->contentView(rootCard)
        ->onClose([this]() {
            cancel_current();
        });

    if (!polkit_cfg.dim_backdrop) {
        builder->anchors(0)->dimBackdrop(false);
    } else {
        builder->dimBackdrop(true);
    }

    m_window = builder
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
