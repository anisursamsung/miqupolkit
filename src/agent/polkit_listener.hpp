#pragma once

#include <glib-object.h>
#include <polkitagent/polkitagent.h>
#include <memory>

namespace miqu {

class PolkitAgent {
public:
    PolkitAgent();
    ~PolkitAgent();

    bool start();
    void stop();

private:
    PolkitAgentListener* m_listener = nullptr;
    gpointer m_registration_handle = nullptr;
};

} // namespace miqu
