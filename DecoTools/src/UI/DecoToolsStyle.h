// Pewpew's Deco Tools - Scoped Interface Style
// Keeps Deco Tools visually stable without changing Nexus or other addons.

#pragma once

namespace DecoToolsStyle
{
    void Initialize(void* addonModule);
    void Shutdown();

    class Scope
    {
    public:
        Scope();
        ~Scope();

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        bool active = false;
        bool fontPushed = false;
    };
}
