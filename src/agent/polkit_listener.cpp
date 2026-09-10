#include "agent/polkit_listener.hpp"
#include "ui/auth_dialog.hpp"
#include <miqutoolkit/core/app_engine.hpp>

#include <polkitagent/polkitagent.h>
#include <polkit/polkit.h>
#include <gio/gio.h>
#include <glib.h>
#include <pwd.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <mutex>
#include <memory>

G_BEGIN_DECLS

#define MIQU_TYPE_POLKIT_LISTENER (miqu_polkit_listener_get_type())
G_DECLARE_FINAL_TYPE(MiquPolkitListener, miqu_polkit_listener, MIQU, POLKIT_LISTENER, PolkitAgentListener)

struct _MiquPolkitListener {
    PolkitAgentListener parent_instance;
};

G_END_DECLS

struct AuthSessionContext {
    PolkitAgentListener* listener = nullptr;
    GTask* task = nullptr;
    PolkitAgentSession* session = nullptr;
    GCancellable* cancellable = nullptr;
    gulong cancellable_handler_id = 0;

    std::string action_id;
    std::string message;
    std::string icon_name;
    std::string user_name;
    std::string cookie;
    bool completed = false;
    std::mutex mutex;
};

static void miqu_polkit_listener_initiate_authentication(
    PolkitAgentListener* listener,
    const gchar* action_id,
    const gchar* message,
    const gchar* icon_name,
    PolkitDetails* details,
    const gchar* cookie,
    GList* identities,
    GCancellable* cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data
);

static gboolean miqu_polkit_listener_initiate_authentication_finish(
    PolkitAgentListener* listener,
    GAsyncResult* res,
    GError** error
);

G_DEFINE_TYPE(MiquPolkitListener, miqu_polkit_listener, POLKIT_AGENT_TYPE_LISTENER)

static void miqu_polkit_listener_init(MiquPolkitListener* self) {
    (void)self;
}

static void miqu_polkit_listener_class_init(MiquPolkitListenerClass* klass) {
    PolkitAgentListenerClass* listener_class = POLKIT_AGENT_LISTENER_CLASS(klass);
    listener_class->initiate_authentication = miqu_polkit_listener_initiate_authentication;
    listener_class->initiate_authentication_finish = miqu_polkit_listener_initiate_authentication_finish;
}

static void on_session_request(
    PolkitAgentSession* s,
    gchar* request,
    gboolean echo_on,
    gpointer user_data
) {
    (void)s;
    auto ctx = *static_cast<std::shared_ptr<AuthSessionContext>*>(user_data);

    miqu::AppEngine::instance()->post([ctx, echo_on]() {
        miqu::AuthDialogManager::instance()->show_dialog(
            ctx->action_id,
            ctx->message,
            ctx->icon_name,
            ctx->user_name,
            [ctx](const std::string& password) {
                std::lock_guard<std::mutex> lock(ctx->mutex);
                if (ctx->session && !ctx->completed) {
                    polkit_agent_session_response(ctx->session, password.c_str());
                }
            },
            [ctx]() {
                std::lock_guard<std::mutex> lock(ctx->mutex);
                if (ctx->session && !ctx->completed) {
                    polkit_agent_session_cancel(ctx->session);
                }
            }
        );
    });
}

static void on_session_show_error(
    PolkitAgentSession* s,
    gchar* text,
    gpointer user_data
) {
    (void)s;
    auto ctx = *static_cast<std::shared_ptr<AuthSessionContext>*>(user_data);
    std::string err_msg = text ? text : "Authentication failed.";

    miqu::AppEngine::instance()->post([err_msg]() {
        miqu::AuthDialogManager::instance()->show_error(err_msg);
    });
}

static void on_session_show_info(
    PolkitAgentSession* s,
    gchar* text,
    gpointer user_data
) {
    (void)s;
    auto ctx = *static_cast<std::shared_ptr<AuthSessionContext>*>(user_data);
    std::string info_msg = text ? text : "";

    miqu::AppEngine::instance()->post([info_msg]() {
        miqu::AuthDialogManager::instance()->show_info(info_msg);
    });
}

static void on_session_completed(
    PolkitAgentSession* s,
    gboolean gained_authorization,
    gpointer user_data
) {
    (void)s;
    auto* ctx_ptr = static_cast<std::shared_ptr<AuthSessionContext>*>(user_data);
    auto ctx = *ctx_ptr;

    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        if (ctx->completed) return;
        ctx->completed = true;

        if (ctx->cancellable && ctx->cancellable_handler_id > 0) {
            g_cancellable_disconnect(ctx->cancellable, ctx->cancellable_handler_id);
            ctx->cancellable_handler_id = 0;
        }

        if (ctx->task) {
            if (gained_authorization) {
                g_task_return_boolean(ctx->task, TRUE);
            } else {
                g_task_return_new_error(
                    ctx->task,
                    POLKIT_ERROR,
                    POLKIT_ERROR_FAILED,
                    "Authentication failed or was cancelled."
                );
            }
            g_object_unref(ctx->task);
            ctx->task = nullptr;
        }

        if (ctx->session) {
            g_object_unref(ctx->session);
            ctx->session = nullptr;
        }
    }

    miqu::AppEngine::instance()->post([]() {
        miqu::AuthDialogManager::instance()->dismiss_dialog();
    });

    delete ctx_ptr;
}

static void on_cancellable_cancelled(GCancellable* cancellable, gpointer user_data) {
    (void)cancellable;
    auto* ctx_ptr = static_cast<std::shared_ptr<AuthSessionContext>*>(user_data);
    auto ctx = *ctx_ptr;
    std::lock_guard<std::mutex> lock(ctx->mutex);
    if (ctx->session && !ctx->completed) {
        polkit_agent_session_cancel(ctx->session);
    }
}

static void miqu_polkit_listener_initiate_authentication(
    PolkitAgentListener* listener,
    const gchar* action_id,
    const gchar* message,
    const gchar* icon_name,
    PolkitDetails* details,
    const gchar* cookie,
    GList* identities,
    GCancellable* cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data
) {
    GTask* task = g_task_new(G_OBJECT(listener), cancellable, callback, user_data);

    if (miqu::AuthDialogManager::instance()->is_showing()) {
        g_task_return_new_error(
            task,
            POLKIT_ERROR,
            POLKIT_ERROR_FAILED,
            "Another authentication request is currently active."
        );
        g_object_unref(task);
        return;
    }

    auto ctx = std::make_shared<AuthSessionContext>();
    ctx->listener = listener;
    ctx->task = task;
    ctx->action_id = action_id ? action_id : "";
    ctx->message = message ? message : "";
    ctx->icon_name = icon_name ? icon_name : "";
    ctx->cookie = cookie ? cookie : "";
    ctx->cancellable = cancellable;

    if (ctx->message.empty() && details) {
        const gchar* det_msg = polkit_details_lookup(details, "polkit.message");
        if (det_msg) {
            ctx->message = det_msg;
        } else {
            const gchar* prog = polkit_details_lookup(details, "program");
            if (prog) {
                ctx->message = std::string("Authentication is required to run '") + prog + "'.";
            }
        }
    }
    if (ctx->message.empty()) {
        ctx->message = "Authentication is required to perform an action.";
    }

    if (ctx->icon_name.empty() && details) {
        const gchar* det_icon = polkit_details_lookup(details, "polkit.icon_name");
        if (det_icon) ctx->icon_name = det_icon;
    }
    if (ctx->icon_name.empty()) {
        ctx->icon_name = "dialog-password";
    }

    // Pick identity
    PolkitIdentity* identity = nullptr;
    if (identities && identities->data) {
        identity = POLKIT_IDENTITY(identities->data);
    }

    if (identity && POLKIT_IS_UNIX_USER(identity)) {
        const char* name = polkit_unix_user_get_name(POLKIT_UNIX_USER(identity));
        if (name) {
            ctx->user_name = name;
        } else {
            int uid = polkit_unix_user_get_uid(POLKIT_UNIX_USER(identity));
            struct passwd* pw = getpwuid(uid);
            if (pw && pw->pw_name) ctx->user_name = pw->pw_name;
            else ctx->user_name = std::to_string(uid);
        }
    }
    if (ctx->user_name.empty()) {
        struct passwd* pw = getpwuid(getuid());
        if (pw && pw->pw_name) ctx->user_name = pw->pw_name;
        else ctx->user_name = "root";
    }

    // Create session
    ctx->session = polkit_agent_session_new(identity, ctx->cookie.c_str());
    if (!ctx->session) {
        g_task_return_new_error(
            task,
            POLKIT_ERROR,
            POLKIT_ERROR_FAILED,
            "Failed to create PolkitAgentSession."
        );
        g_object_unref(task);
        return;
    }

    auto* ctx_closure = new std::shared_ptr<AuthSessionContext>(ctx);

    g_signal_connect(ctx->session, "request", G_CALLBACK(on_session_request), ctx_closure);
    g_signal_connect(ctx->session, "show-error", G_CALLBACK(on_session_show_error), ctx_closure);
    g_signal_connect(ctx->session, "show-info", G_CALLBACK(on_session_show_info), ctx_closure);
    g_signal_connect(ctx->session, "completed", G_CALLBACK(on_session_completed), ctx_closure);

    if (cancellable) {
        ctx->cancellable_handler_id = g_cancellable_connect(
            cancellable,
            G_CALLBACK(on_cancellable_cancelled),
            ctx_closure,
            nullptr
        );
    }

    polkit_agent_session_initiate(ctx->session);
}

static gboolean miqu_polkit_listener_initiate_authentication_finish(
    PolkitAgentListener* listener,
    GAsyncResult* res,
    GError** error
) {
    (void)listener;
    return g_task_propagate_boolean(G_TASK(res), error);
}

namespace miqu {

PolkitAgent::PolkitAgent() = default;

PolkitAgent::~PolkitAgent() {
    stop();
}

bool PolkitAgent::start() {
    m_listener = POLKIT_AGENT_LISTENER(g_object_new(MIQU_TYPE_POLKIT_LISTENER, nullptr));

    GError* error = nullptr;
    PolkitSubject* subject = polkit_unix_session_new_for_process_sync(getpid(), nullptr, &error);
    if (!subject) {
        if (error) {
            std::cerr << "[miqupolkit] Warning: Session lookup failed: " << error->message
                      << ", falling back to unix process subject" << std::endl;
            g_clear_error(&error);
        }
        subject = polkit_unix_process_new_for_owner(getpid(), 0, getuid());
    }

    m_registration_handle = polkit_agent_listener_register(
        m_listener,
        POLKIT_AGENT_REGISTER_FLAGS_RUN_IN_THREAD,
        subject,
        nullptr,
        nullptr,
        &error
    );

    g_object_unref(subject);

    if (!m_registration_handle) {
        if (error) {
            std::cerr << "[miqupolkit] Failed to register agent listener: " << error->message << std::endl;
            g_clear_error(&error);
        }
        g_object_unref(m_listener);
        m_listener = nullptr;
        return false;
    }

    return true;
}

void PolkitAgent::stop() {
    if (m_registration_handle) {
        polkit_agent_listener_unregister(m_registration_handle);
        m_registration_handle = nullptr;
    }
    if (m_listener) {
        g_object_unref(m_listener);
        m_listener = nullptr;
    }
}

} // namespace miqu
