#include "agent/polkit_listener.hpp"
#include "config.hpp"
#include <miqutoolkit/core/app_engine.hpp>
#include <iostream>
#include <csignal>

static void sig_handler(int sig) {
    std::cout << "\n[miqupolkit] Caught signal " << sig << ", shutting down..." << std::endl;
    if (miqu::AppEngine::instance()) {
        miqu::AppEngine::instance()->quit(0);
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, sig_handler);
    std::signal(SIGTERM, sig_handler);

    // Initialize configuration and theme
    std::string custom_config = (argc > 1) ? argv[1] : "";
    miqu::PolkitConfig::get().load(custom_config);

    std::cout << "[miqupolkit] Starting MiquLand PolicyKit Authentication Agent..." << std::endl;

    auto engine = miqu::AppEngine::create();
    if (!engine) {
        std::cerr << "[miqupolkit] Error: Failed to connect to Wayland display. Is a Wayland compositor running?" << std::endl;
        return 1;
    }
    engine->set_quit_on_last_window_closed(false);

    auto agent = std::make_unique<miqu::PolkitAgent>();
    if (!agent->start()) {
        std::cerr << "[miqupolkit] Error: Failed to register Polkit authentication agent." << std::endl;
        return 1;
    }

    std::cout << "[miqupolkit] PolicyKit Agent successfully registered. Running in background." << std::endl;

    int exit_code = engine->enter_loop();

    std::cout << "[miqupolkit] Stopping agent..." << std::endl;
    agent->stop();

    return exit_code;
}
