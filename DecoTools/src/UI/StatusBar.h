#pragma once

#include <string>

namespace StatusBar
{
    void Publish(const std::string& message);
    void PublishIfChanged(const void* source, const std::string& message);
    void Render();
}
