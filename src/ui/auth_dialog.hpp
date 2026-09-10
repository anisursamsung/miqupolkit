#pragma once

#include <miqutoolkit/core/window.hpp>
#include <miqutoolkit/view/card_view.hpp>
#include <miqutoolkit/view/linear_layout.hpp>
#include <miqutoolkit/view/text_view.hpp>
#include <miqutoolkit/view/edit_text.hpp>
#include <miqutoolkit/view/button.hpp>
#include <miqutoolkit/view/image_view.hpp>
#include <string>
#include <functional>
#include <memory>

namespace miqu {

class AuthDialogManager {
public:
    static AuthDialogManager* instance();

    void show_dialog(
        const std::string& action_id,
        const std::string& message,
        const std::string& icon_name,
        const std::string& user_name,
        std::function<void(const std::string&)> on_authenticate,
        std::function<void()> on_cancel
    );

    void show_error(const std::string& error_text);
    void show_info(const std::string& info_text);
    void dismiss_dialog();
    void cancel_current();

    bool is_showing() const { return m_window != nullptr; }

private:
    AuthDialogManager() = default;

    std::shared_ptr<Window> m_window;
    std::shared_ptr<EditText> m_password_input;
    std::shared_ptr<TextView> m_error_view;

    std::function<void(const std::string&)> m_on_authenticate;
    std::function<void()> m_on_cancel;
};

} // namespace miqu
