#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MeasurementDatabase
{
    enum class MeasurementType
    {
        Decorations,
        Groups,
        FreeHand
    };

    enum class TickDivision
    {
        None = 1,
        Half = 2,
        Quarter = 4,
        Eighth = 8,
        Sixteenth = 16
    };

    enum class LineStyle
    {
        None,
        Single,
        Dashed,
        Ruler
    };

    struct Point
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        std::string source;
    };

    struct Measurement
    {
        std::uint64_t id = 0;
        std::string layoutKey;
        std::string layoutName;
        int layoutType = -1;
        std::string name;
        MeasurementType type = MeasurementType::Decorations;
        Point pointA;
        Point pointB;
        TickDivision ticks = TickDivision::None;
        LineStyle lineStyle = LineStyle::Single;
        bool visible = true;
        std::int64_t createdAtUnix = 0;
    };

    void Initialize(const std::string& addonDirectory);
    void Shutdown();

    std::vector<Measurement> LoadForLayout(const std::string& layoutKey);
    bool SaveForLayout(
        const std::string& layoutKey,
        const std::vector<Measurement>& measurements);
    std::uint64_t NextId();
}
