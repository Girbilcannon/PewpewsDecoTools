#include "MeasureToolTab.h"

#include "../../Core/AppRuntime.h"
#include "../../Core/AppSettings.h"
#include "../../Core/GroupBackupDatabase.h"
#include "../../Core/MeasurementDatabase.h"
#include "../../Core/SharedXmlWorkspace.h"
#include "../../Core/Utf8Paths.h"
#include "../PrimaryActionButton.h"
#include "../StatusBar.h"
#include "../../imgui/imgui.h"
#include "../../imgui/imgui_internal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    using Measurement = MeasurementDatabase::Measurement;
    using MeasurementType = MeasurementDatabase::MeasurementType;
    using TickDivision = MeasurementDatabase::TickDivision;
    using LineStyle = MeasurementDatabase::LineStyle;
    using Point = MeasurementDatabase::Point;

    constexpr float DecorationScale = 0.025400052f;
    constexpr float NearClip = 0.05f;
    constexpr float DefaultFovRadians = 0.872664626f;
    constexpr size_t MaximumMeasurements = 100;
    constexpr size_t MaximumHistory = 100;

    struct Vec3 { float x = 0.0f; float y = 0.0f; float z = 0.0f; };
    struct Candidate
    {
        Point point;
        std::string label;
    };
    struct Prop
    {
        Point point;
        std::string name;
        size_t tagStart = 0;
        int groupIndex = -1;
    };
    struct Group
    {
        std::string name;
        std::vector<size_t> props;
    };
    struct Camera
    {
        Vec3 position;
        Vec3 forward;
        Vec3 up;
        Vec3 right;
        float fovRadians = DefaultFovRadians;
    };
    struct History
    {
        std::vector<std::vector<Measurement>> undo;
        std::vector<std::vector<Measurement>> redo;
    };

    bool activeTab = false;
    bool hudVisible = false;
    std::uint64_t importedRevision = 0;
    std::string layoutPath;
    std::string layoutName;
    std::string layoutKey;
    int layoutType = -1;
    std::vector<Prop> props;
    std::vector<Group> groups;
    std::vector<Measurement> measurements;
    std::set<std::uint64_t> selectedIds;
    std::uint64_t selectionAnchor = 0;
    std::unordered_map<std::string, History> histories;
    std::string status = "Import a Layout to begin measuring.";

    bool draftActive = false;
    bool draftHasA = false;
    bool draftHasB = false;
    Measurement draft;
    std::uint64_t draftStoredId = 0;
    int hoveredCandidate = -1;
    int hoveredEndpoint = -1;
    int hoveredTick = -1;
    int selectedTick = -1;
    ImVec2 windowMouse(-10000.0f, -10000.0f);
    bool pointLeftPending = false;
    bool pointRightPending = false;

    std::uint64_t renameTarget = 0;
    std::array<char, 128> renameBuffer = {};
    bool openRenamePopup = false;
    bool openRemoveSelectedPopup = false;
    bool openRemoveAllPopup = false;

    Vec3 Add(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    Vec3 Subtract(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    Vec3 Multiply(Vec3 value, float scale)
    { return { value.x * scale, value.y * scale, value.z * scale }; }
    float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    Vec3 Cross(Vec3 a, Vec3 b)
    {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x };
    }
    float Length(Vec3 value) { return std::sqrt(Dot(value, value)); }
    Vec3 Normalize(Vec3 value, Vec3 fallback)
    {
        const float length = Length(value);
        return length <= 0.000001f ? fallback : Multiply(value, 1.0f / length);
    }
    Vec3 FromMumble(const Mumble::Vector3& value)
    { return { value.X, value.Y, value.Z }; }
    Vec3 DecorationToWorld(const Point& point)
    {
        return { static_cast<float>(point.x * DecorationScale),
            static_cast<float>(-point.z * DecorationScale),
            static_cast<float>(point.y * DecorationScale) };
    }
    Point WorldToDecoration(Vec3 world)
    {
        Point point;
        point.x = world.x / DecorationScale;
        point.y = world.z / DecorationScale;
        point.z = -world.y / DecorationScale;
        return point;
    }
    Point Interpolate(const Point& a, const Point& b, double value)
    {
        Point result;
        result.x = a.x + (b.x - a.x) * value;
        result.y = a.y + (b.y - a.y) * value;
        result.z = a.z + (b.z - a.z) * value;
        return result;
    }
    double Distance(const Point& a, const Point& b)
    {
        const double x = b.x - a.x;
        const double y = b.y - a.y;
        const double z = b.z - a.z;
        return std::sqrt(x * x + y * y + z * z);
    }

    Camera CameraFromMumble(const Mumble::Data& mumble)
    {
        Camera camera;
        camera.position = FromMumble(mumble.CameraPosition);
        camera.forward = Normalize(FromMumble(mumble.CameraFront), { 0.0f, 0.0f, 1.0f });
        const Vec3 worldUp = { 0.0f, 1.0f, 0.0f };
        camera.right = Normalize(Cross(worldUp, camera.forward), { 1.0f, 0.0f, 0.0f });
        camera.up = Normalize(Cross(camera.forward, camera.right), worldUp);
        const Mumble::Identity* identity = AppRuntime::GetMumbleIdentity();
        if (identity != nullptr && std::isfinite(identity->FOV) &&
            identity->FOV > 0.1f && identity->FOV < 3.0f)
            camera.fovRadians = identity->FOV;
        return camera;
    }

    bool Project(const Camera& camera, Vec3 world, ImVec2 viewport, ImVec2& screen)
    {
        if (viewport.x <= 1.0f || viewport.y <= 1.0f) return false;
        const Vec3 relative = Subtract(world, camera.position);
        const float cameraX = Dot(relative, camera.right);
        const float cameraY = Dot(relative, camera.up);
        const float cameraZ = Dot(relative, camera.forward);
        if (cameraZ <= NearClip) return false;
        const float focalY = (viewport.y * 0.5f) /
            std::tan(camera.fovRadians * 0.5f);
        screen.x = viewport.x * 0.5f + cameraX * focalY / cameraZ;
        screen.y = viewport.y * 0.5f - cameraY * focalY / cameraZ;
        return screen.x >= -4000.0f && screen.x <= viewport.x + 4000.0f &&
            screen.y >= -4000.0f && screen.y <= viewport.y + 4000.0f;
    }

    size_t FindTagEnd(const std::string& source, size_t start)
    {
        char quote = '\0';
        for (size_t index = start; index < source.size(); ++index)
        {
            const char character = source[index];
            if (quote != '\0')
            {
                if (character == quote) quote = '\0';
            }
            else if (character == '"' || character == '\'') quote = character;
            else if (character == '>') return index;
        }
        return std::string::npos;
    }

    bool ReadAttribute(
        const std::string& source,
        size_t start,
        size_t end,
        const char* attribute,
        std::string& value)
    {
        const std::string token = std::string(attribute) + "=";
        size_t position = source.find(token, start);
        if (position == std::string::npos || position >= end) return false;
        position += token.size();
        while (position < end && std::isspace(
            static_cast<unsigned char>(source[position])) != 0) ++position;
        if (position >= end || (source[position] != '"' && source[position] != '\''))
            return false;
        const char quote = source[position++];
        const size_t close = source.find(quote, position);
        if (close == std::string::npos || close > end) return false;
        value = source.substr(position, close - position);
        return true;
    }

    bool ParsePoint(const std::string& value, Point& point)
    {
        std::istringstream stream(value);
        return static_cast<bool>(stream >> point.x >> point.y >> point.z);
    }

    std::string Trim(std::string value)
    {
        while (!value.empty() && std::isspace(
            static_cast<unsigned char>(value.front())) != 0) value.erase(value.begin());
        while (!value.empty() && std::isspace(
            static_cast<unsigned char>(value.back())) != 0) value.pop_back();
        return value;
    }

    bool ParseLayout(const std::string& path)
    {
        std::ifstream file(Utf8Paths::FromUtf8(path), std::ios::binary);
        if (!file.is_open()) return false;
        std::ostringstream contents;
        contents << file.rdbuf();
        const std::string source = contents.str();
        const size_t rootStart = source.find("<Decorations");
        const size_t rootEnd = rootStart == std::string::npos
            ? std::string::npos : FindTagEnd(source, rootStart);
        const size_t rootClose = source.rfind("</Decorations");
        if (rootStart == std::string::npos || rootEnd == std::string::npos ||
            rootClose == std::string::npos) return false;

        std::vector<Prop> parsedProps;
        size_t search = rootEnd + 1;
        while (true)
        {
            const size_t start = source.find("<prop", search);
            if (start == std::string::npos || start >= rootClose) break;
            const size_t end = FindTagEnd(source, start);
            if (end == std::string::npos) return false;
            std::string positionText;
            if (ReadAttribute(source, start, end, "pos", positionText))
            {
                Prop prop;
                prop.tagStart = start;
                if (!ParsePoint(positionText, prop.point)) return false;
                if (!ReadAttribute(source, start, end, "name", prop.name))
                    prop.name = "Decoration";
                parsedProps.push_back(std::move(prop));
            }
            search = end + 1;
        }

        std::vector<Group> parsedGroups;
        size_t comment = source.find("<!--", rootEnd + 1);
        while (comment != std::string::npos && comment < rootClose)
        {
            const size_t commentEnd = source.find("-->", comment + 4);
            if (commentEnd == std::string::npos) break;
            const size_t next = source.find("<!--", commentEnd + 3);
            const size_t groupEnd = next == std::string::npos || next >= rootClose
                ? rootClose : next;
            Group group;
            group.name = Trim(source.substr(comment + 4, commentEnd - comment - 4));
            if (!group.name.empty())
            {
                const int groupIndex = static_cast<int>(parsedGroups.size());
                for (size_t index = 0; index < parsedProps.size(); ++index)
                {
                    if (parsedProps[index].tagStart > commentEnd &&
                        parsedProps[index].tagStart < groupEnd)
                    {
                        parsedProps[index].groupIndex = groupIndex;
                        group.props.push_back(index);
                    }
                }
                if (!group.props.empty()) parsedGroups.push_back(std::move(group));
            }
            comment = next;
        }
        props = std::move(parsedProps);
        groups = std::move(parsedGroups);
        return !props.empty();
    }

    std::vector<Candidate> CurrentCandidates()
    {
        std::vector<Candidate> output;
        if (draft.type == MeasurementType::Decorations)
        {
            output.reserve(props.size());
            for (const Prop& prop : props)
                output.push_back({ prop.point, prop.name });
        }
        else if (draft.type == MeasurementType::Groups)
        {
            output.reserve(groups.size());
            for (const Group& group : groups)
            {
                Point center;
                for (const size_t index : group.props)
                {
                    center.x += props[index].point.x;
                    center.y += props[index].point.y;
                    center.z += props[index].point.z;
                }
                const double count = static_cast<double>(group.props.size());
                center.x /= count;
                center.y /= count;
                center.z /= count;
                output.push_back({ center, group.name });
            }
        }
        return output;
    }

    History& CurrentHistory() { return histories[layoutKey]; }

    void PushHistory()
    {
        History& history = CurrentHistory();
        history.undo.push_back(measurements);
        if (history.undo.size() > MaximumHistory) history.undo.erase(history.undo.begin());
        history.redo.clear();
    }

    void Persist()
    {
        if (!layoutKey.empty() && !MeasurementDatabase::SaveForLayout(layoutKey, measurements))
            status = "Could not save the measurement database.";
    }

    void SynchronizeStoredDraft()
    {
        if (draftStoredId == 0) return;
        const auto found = std::find_if(
            measurements.begin(), measurements.end(),
            [&](const Measurement& item) { return item.id == draftStoredId; });
        if (found != measurements.end()) draft = *found;
        else
        {
            draftActive = false;
            draftStoredId = 0;
            draftHasA = false;
            draftHasB = false;
            selectedTick = -1;
        }
    }

    void Undo()
    {
        History& history = CurrentHistory();
        if (history.undo.empty()) return;
        history.redo.push_back(measurements);
        measurements = std::move(history.undo.back());
        history.undo.pop_back();
        SynchronizeStoredDraft();
        selectedIds.clear();
        Persist();
        status = "Undid the last measurement change.";
    }

    void Redo()
    {
        History& history = CurrentHistory();
        if (history.redo.empty()) return;
        history.undo.push_back(measurements);
        measurements = std::move(history.redo.back());
        history.redo.pop_back();
        SynchronizeStoredDraft();
        selectedIds.clear();
        Persist();
        status = "Redid the measurement change.";
    }

    std::string NextName()
    {
        for (int number = 1; number <= static_cast<int>(MaximumMeasurements); ++number)
        {
            std::ostringstream name;
            name << "Length " << std::setw(3) << std::setfill('0') << number;
            bool used = false;
            for (const Measurement& item : measurements)
                if (item.name == name.str()) { used = true; break; }
            if (!used) return name.str();
        }
        return "Length 100";
    }

    void BeginNewMeasurement()
    {
        if (measurements.size() >= MaximumMeasurements)
        {
            status = "This Layout already has the maximum of 100 measurements.";
            return;
        }
        draft = {};
        draft.id = MeasurementDatabase::NextId();
        draft.layoutKey = layoutKey;
        draft.layoutName = layoutName;
        draft.layoutType = layoutType;
        draft.name = NextName();
        draft.type = MeasurementType::Decorations;
        draft.ticks = TickDivision::None;
        draft.lineStyle = LineStyle::Single;
        draft.visible = true;
        draft.createdAtUnix = static_cast<std::int64_t>(std::time(nullptr));
        draftActive = true;
        draftHasA = false;
        draftHasB = false;
        draftStoredId = 0;
        hoveredCandidate = -1;
        hoveredEndpoint = -1;
        hoveredTick = -1;
        pointLeftPending = false;
        pointRightPending = false;
        selectedTick = -1;
        status = "New measurement started. Choose Point A.";
    }

    void SetAllVisibility(bool visible)
    {
        const bool changed = std::any_of(
            measurements.begin(), measurements.end(),
            [visible](const Measurement& item) { return item.visible != visible; });
        if (!changed) return;

        PushHistory();
        for (Measurement& item : measurements) item.visible = visible;
        if (draftStoredId != 0) draft.visible = visible;
        Persist();
        status = visible
            ? "All recorded rulers are now visible throughout Deco Tools."
            : "All recorded rulers are now hidden.";
    }

    void StoreDraft()
    {
        if (!draftHasA || !draftHasB) return;
        auto found = std::find_if(
            measurements.begin(), measurements.end(),
            [&](const Measurement& item) { return item.id == draft.id; });
        PushHistory();
        if (found == measurements.end()) measurements.push_back(draft);
        else *found = draft;
        draftStoredId = draft.id;
        selectedIds.clear();
        selectedIds.insert(draft.id);
        selectionAnchor = draft.id;
        Persist();
        status = "Recorded " + draft.name + ".";
    }

    void RemoveStoredDraftForEndpointEdit()
    {
        if (draftStoredId == 0) return;
        PushHistory();
        measurements.erase(std::remove_if(
            measurements.begin(), measurements.end(),
            [&](const Measurement& item) { return item.id == draftStoredId; }),
            measurements.end());
        selectedIds.erase(draftStoredId);
        draftStoredId = 0;
        Persist();
    }

    Point CharacterPoint()
    {
        Mumble::Data* mumble = AppRuntime::GetMumble();
        Point point;
        if (mumble != nullptr)
        {
            point = WorldToDecoration(FromMumble(mumble->AvatarPosition));
            point.source = "Character Position";
        }
        return point;
    }

    void AssignCandidate(const Candidate& candidate)
    {
        if (!draftHasA)
        {
            draft.pointA = candidate.point;
            draft.pointA.source = candidate.label;
            draftHasA = true;
            if (draftHasB) StoreDraft();
            else status = "Point A selected. Choose Point B.";
        }
        else
        {
            if (draftHasB) RemoveStoredDraftForEndpointEdit();
            draft.pointB = candidate.point;
            draft.pointB.source = candidate.label;
            draftHasB = true;
            StoreDraft();
        }
    }

    void RemoveMeasurements(const std::set<std::uint64_t>& ids)
    {
        if (ids.empty()) return;
        PushHistory();
        measurements.erase(std::remove_if(
            measurements.begin(), measurements.end(),
            [&](const Measurement& item) { return ids.contains(item.id); }),
            measurements.end());
        if (draftStoredId != 0 && ids.contains(draftStoredId))
        {
            draftActive = false;
            draftStoredId = 0;
        }
        selectedIds.clear();
        selectionAnchor = 0;
        Persist();
    }

    std::string PointText(const Point& point)
    {
        std::ostringstream output;
        output << std::fixed << std::setprecision(2) << "(" << point.x << ", "
            << point.y << ", " << point.z << ")";
        return output.str();
    }

    void SelectRow(std::uint64_t id, size_t rowIndex)
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyShift && selectionAnchor != 0)
        {
            size_t anchorIndex = rowIndex;
            for (size_t index = 0; index < measurements.size(); ++index)
                if (measurements[index].id == selectionAnchor) anchorIndex = index;
            const size_t first = (std::min)(anchorIndex, rowIndex);
            const size_t last = (std::max)(anchorIndex, rowIndex);
            if (!io.KeyCtrl) selectedIds.clear();
            for (size_t index = first; index <= last; ++index)
                selectedIds.insert(measurements[index].id);
        }
        else if (io.KeyCtrl)
        {
            if (selectedIds.contains(id)) selectedIds.erase(id);
            else selectedIds.insert(id);
            selectionAnchor = id;
        }
        else
        {
            selectedIds.clear();
            selectedIds.insert(id);
            selectionAnchor = id;
            const auto found = std::find_if(
                measurements.begin(), measurements.end(),
                [&](const Measurement& item) { return item.id == id; });
            if (found != measurements.end())
            {
                draft = *found;
                draftActive = true;
                draftHasA = true;
                draftHasB = true;
                draftStoredId = found->id;
                selectedTick = -1;
            }
        }
    }

    std::string CsvQuote(const std::string& value)
    {
        std::string output = "\"";
        for (const char character : value)
        {
            if (character == '"') output += "\"\"";
            else output += character;
        }
        output += '"';
        return output;
    }

    const char* TypeName(MeasurementType type)
    {
        if (type == MeasurementType::Groups) return "Groups";
        if (type == MeasurementType::FreeHand) return "Free Hand";
        return "Decorations";
    }

    const char* TickName(TickDivision ticks)
    {
        if (ticks == TickDivision::Half) return "1/2";
        if (ticks == TickDivision::Quarter) return "1/4";
        if (ticks == TickDivision::Eighth) return "1/8";
        if (ticks == TickDivision::Sixteenth) return "1/16";
        return "No Ticks";
    }

    const char* LineName(LineStyle style)
    {
        if (style == LineStyle::Single) return "Single Line";
        if (style == LineStyle::Dashed) return "Dashed Line";
        if (style == LineStyle::Ruler) return "Ruler";
        return "No Line";
    }

    void ExportCsv()
    {
        if (layoutPath.empty() || measurements.empty()) return;
        const std::filesystem::path source = Utf8Paths::FromUtf8(layoutPath);
        const std::string configured = layoutType == 1
            ? AppSettings::Get().guildHallFolder.data()
            : AppSettings::Get().homesteadFolder.data();
        std::filesystem::path folder = configured.empty()
            ? source.parent_path() : Utf8Paths::FromUtf8(configured);
        std::filesystem::path output = folder /
            (source.stem().wstring() + L"_Measurements.csv");
        for (int index = 1; std::filesystem::exists(output); ++index)
            output = folder / (source.stem().wstring() + L"_Measurements" +
                std::to_wstring(index) + L".csv");
        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            status = "Could not create the measurement CSV.";
            return;
        }
        file << "\xEF\xBB\xBF";
        file << "Name,Measurement Type,Length,A X,A Y,A Z,B X,B Y,B Z,"
            "Mid X,Mid Y,Mid Z,Delta X,Delta Y,Delta Z,Ticks,Line Style,"
            "Layout Name,Layout Type,Created At Unix\r\n";
        file << std::setprecision(15);
        for (const Measurement& item : measurements)
        {
            const Point midpoint = Interpolate(item.pointA, item.pointB, 0.5);
            file << CsvQuote(item.name) << ',' << CsvQuote(TypeName(item.type)) << ','
                << Distance(item.pointA, item.pointB) << ','
                << item.pointA.x << ',' << item.pointA.y << ',' << item.pointA.z << ','
                << item.pointB.x << ',' << item.pointB.y << ',' << item.pointB.z << ','
                << midpoint.x << ',' << midpoint.y << ',' << midpoint.z << ','
                << item.pointB.x - item.pointA.x << ','
                << item.pointB.y - item.pointA.y << ','
                << item.pointB.z - item.pointA.z << ','
                << CsvQuote(TickName(item.ticks)) << ','
                << CsvQuote(LineName(item.lineStyle)) << ','
                << CsvQuote(item.layoutName) << ','
                << CsvQuote(item.layoutType == 1 ? "Guild Hall" : "Homestead") << ','
                << item.createdAtUnix << "\r\n";
        }
        status = "Exported " + std::to_string(measurements.size()) +
            " measurements to " + Utf8Paths::ToUtf8(output.filename()) + ".";
    }

    void DrawDashedLine(ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 color, float thickness)
    {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.1f) return;
        const float dash = 9.0f;
        const float gap = 6.0f;
        for (float start = 0.0f; start < length; start += dash + gap)
        {
            const float finish = (std::min)(start + dash, length);
            draw->AddLine(
                ImVec2(a.x + dx * start / length, a.y + dy * start / length),
                ImVec2(a.x + dx * finish / length, a.y + dy * finish / length),
                color, thickness);
        }
    }

    void DrawMeasurement(
        const Measurement& item,
        bool interactive,
        bool complete,
        const Camera& camera,
        ImVec2 viewport,
        ImDrawList* draw)
    {
        ImVec2 a;
        ImVec2 b;
        if (!Project(camera, DecorationToWorld(item.pointA), viewport, a) ||
            !Project(camera, DecorationToWorld(item.pointB), viewport, b)) return;
        const ImU32 lineColor = IM_COL32(105, 190, 255, 230);
        const ImU32 pointColor = IM_COL32(55, 150, 255, 255);
        if (item.lineStyle == LineStyle::Single || item.lineStyle == LineStyle::Ruler)
            draw->AddLine(a, b, lineColor, 2.0f);
        else if (item.lineStyle == LineStyle::Dashed)
            DrawDashedLine(draw, a, b, lineColor, 2.0f);

        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float screenLength = std::sqrt(dx * dx + dy * dy);
        const ImVec2 perpendicular = screenLength > 0.1f
            ? ImVec2(-dy / screenLength, dx / screenLength) : ImVec2(0.0f, 1.0f);

        if (item.lineStyle == LineStyle::Ruler && screenLength > 20.0f)
        {
            for (int division = 1; division < 16; ++division)
            {
                if (screenLength / 16.0f < 8.0f && division % 2 != 0) continue;
                if (screenLength / 8.0f < 8.0f && division % 4 != 0) continue;
                const float fraction = static_cast<float>(division) / 16.0f;
                const Point worldMark = Interpolate(
                    item.pointA, item.pointB, static_cast<double>(fraction));
                ImVec2 center;
                if (!Project(camera, DecorationToWorld(worldMark), viewport, center))
                    continue;
                float mark = 4.0f;
                if (division == 8) mark = 12.0f;
                else if (division % 4 == 0) mark = 9.0f;
                else if (division % 2 == 0) mark = 6.0f;
                draw->AddLine(center,
                    ImVec2(center.x + perpendicular.x * mark,
                        center.y + perpendicular.y * mark),
                    lineColor, 1.5f);
            }
        }

        draw->AddCircleFilled(a, 5.0f, pointColor, 20);
        draw->AddCircleFilled(
            b,
            complete ? 5.0f : 4.0f,
            complete ? pointColor : IM_COL32(210, 214, 222, 235),
            20);
        if (interactive)
        {
            const float da = std::hypot(windowMouse.x - a.x, windowMouse.y - a.y);
            const float db = std::hypot(windowMouse.x - b.x, windowMouse.y - b.y);
            if (da <= 9.0f) hoveredEndpoint = 0;
            else if (complete && db <= 9.0f) hoveredEndpoint = 1;
        }

        const int divisions = complete ? static_cast<int>(item.ticks) : 1;
        for (int tick = 1; tick < divisions; ++tick)
        {
            const float fraction = static_cast<float>(tick) /
                static_cast<float>(divisions);
            const Point worldTick = Interpolate(
                item.pointA, item.pointB, static_cast<double>(fraction));
            ImVec2 location;
            if (!Project(camera, DecorationToWorld(worldTick), viewport, location))
                continue;
            const bool chosen = interactive && selectedTick == tick;
            draw->AddCircleFilled(location, chosen ? 5.0f : 3.5f,
                chosen ? pointColor : IM_COL32(170, 175, 185, 240), 16);
            std::ostringstream label;
            label << std::fixed << std::setprecision(2)
                << Distance(item.pointA, item.pointB) * fraction;
            draw->AddText(
                ImVec2(
                    location.x + perpendicular.x * 12.0f + 4.0f,
                    location.y + perpendicular.y * 12.0f - 7.0f),
                IM_COL32(205, 208, 215, 235), label.str().c_str());
            if (interactive && std::hypot(
                windowMouse.x - location.x, windowMouse.y - location.y) <= 8.0f)
                hoveredTick = tick;
        }

        ImVec2 labelPoint = b;
        if (complete)
        {
            const Point worldMidpoint = Interpolate(item.pointA, item.pointB, 0.5);
            Project(camera, DecorationToWorld(worldMidpoint), viewport, labelPoint);
        }
        std::ostringstream lengthText;
        lengthText << "Length: " << std::fixed << std::setprecision(3)
            << Distance(item.pointA, item.pointB);
        const float lengthOffset = complete ? 30.0f : 9.0f;
        draw->AddText(
            ImVec2(
                labelPoint.x + perpendicular.x * lengthOffset + 7.0f,
                labelPoint.y + perpendicular.y * lengthOffset + 5.0f),
            IM_COL32(235, 238, 245, 255), lengthText.str().c_str());
    }

    void RenderRenamePopup()
    {
        if (openRenamePopup)
        {
            ImGui::OpenPopup("Rename Measurement");
            openRenamePopup = false;
        }
        if (ImGui::BeginPopupModal("Rename Measurement", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Enter a new measurement name:");
            ImGui::SetNextItemWidth(300.0f);
            ImGui::InputText("##MeasurementName", renameBuffer.data(), renameBuffer.size());
            if (PrimaryActionButton::Draw("Rename", ImVec2(145.0f, 0.0f)))
            {
                const std::string name = Trim(renameBuffer.data());
                if (!name.empty())
                {
                    auto found = std::find_if(measurements.begin(), measurements.end(),
                        [&](const Measurement& item) { return item.id == renameTarget; });
                    if (found != measurements.end())
                    {
                        PushHistory();
                        found->name = name;
                        if (draft.id == found->id) draft.name = name;
                        Persist();
                        status = "Renamed measurement to " + name + ".";
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(145.0f, 0.0f)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void DisabledButton(const char* label, ImVec2 size)
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        ImGui::Button(label, size);
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }

    bool DestructiveButton(const char* label, ImVec2 size)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.48f, 0.07f, 0.07f, 1.0f));
        const bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(3);
        return pressed;
    }

    void BeginDisabledCompat(bool disabled)
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, disabled);
        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            disabled ? ImGui::GetStyle().Alpha * 0.5f : ImGui::GetStyle().Alpha);
    }

    void EndDisabledCompat()
    {
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }
}

void MeasureToolTab::Initialize() {}

void MeasureToolTab::Shutdown()
{
    props.clear();
    groups.clear();
    measurements.clear();
    selectedIds.clear();
    histories.clear();
}

void MeasureToolTab::SynchronizeSharedWorkspace()
{
    if (!SharedXmlWorkspace::HasImport()) return;
    const std::uint64_t revision = SharedXmlWorkspace::Revision();
    if (revision == importedRevision) return;
    importedRevision = revision;
    layoutPath = SharedXmlWorkspace::Path();
    layoutName = SharedXmlWorkspace::FileName();
    layoutType = SharedXmlWorkspace::XmlType();
    layoutKey = std::to_string(layoutType) + ":" +
        GroupBackupDatabase::NormalizeLineage(layoutName);
    if (!ParseLayout(layoutPath))
    {
        props.clear();
        groups.clear();
        measurements.clear();
        status = "The imported Layout could not be prepared for measurement.";
        return;
    }
    measurements = MeasurementDatabase::LoadForLayout(layoutKey);
    selectedIds.clear();
    selectionAnchor = 0;
    draftActive = false;
    draftStoredId = 0;
    status = "Loaded " + std::to_string(measurements.size()) +
        (measurements.size() == 1 ? " recorded measurement for " :
            " recorded measurements for ") + layoutName + ".";
}

void MeasureToolTab::SetActive(bool active) { activeTab = active; }

void MeasureToolTab::Render()
{
    SynchronizeSharedWorkspace();
    const float fullWidth = ImGui::GetContentRegionAvail().x;
    if (PrimaryActionButton::Draw(
        hudVisible ? "Hide Ruler HUD" : "Show Ruler HUD", ImVec2(fullWidth, 0.0f)))
        hudVisible = !hudVisible;

    ImGui::Spacing();
    ImGui::SetWindowFontScale(1.2f);
    ImGui::TextUnformatted("Recorded Measurements");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();

    const float visibilitySpacing = ImGui::GetStyle().ItemSpacing.x;
    const float visibilityButtonWidth =
        (ImGui::GetContentRegionAvail().x - visibilitySpacing) * 0.5f;
    if (measurements.empty())
        DisabledButton("Show All", ImVec2(visibilityButtonWidth, 0.0f));
    else if (ImGui::Button("Show All", ImVec2(visibilityButtonWidth, 0.0f)))
        SetAllVisibility(true);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show every recorded ruler and enable all of their snap points.");
    ImGui::SameLine();
    if (measurements.empty())
        DisabledButton("Hide All", ImVec2(visibilityButtonWidth, 0.0f));
    else if (ImGui::Button("Hide All", ImVec2(visibilityButtonWidth, 0.0f)))
        SetAllVisibility(false);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Hide every recorded ruler and disable all of their snap points.");

    const float footerHeight = ImGui::GetFrameHeightWithSpacing() + 8.0f;
    const float tableHeight = (std::max)(80.0f,
        ImGui::GetContentRegionAvail().y - footerHeight);
    const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##Measurements", 6, flags, ImVec2(0.0f, tableHeight)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("Show", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.85f);
        ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 85.0f);
        ImGui::TableSetupColumn("Point A", ImGuiTableColumnFlags_WidthStretch, 1.15f);
        ImGui::TableSetupColumn("Point B", ImGuiTableColumnFlags_WidthStretch, 1.15f);
        ImGui::TableHeadersRow();

        for (size_t row = 0; row < measurements.size(); ++row)
        {
            Measurement& item = measurements[row];
            ImGui::PushID(static_cast<int>(item.id));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.12f, 0.12f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.20f, 0.20f, 1.0f));
            if (ImGui::SmallButton("X"))
            {
                const std::set<std::uint64_t> one = { item.id };
                RemoveMeasurements(one);
                ImGui::PopStyleColor(2);
                ImGui::PopID();
                break;
            }
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Remove this measurement. Undo remains available this session.");

            ImGui::TableSetColumnIndex(1);
            bool newVisibility = item.visible;
            if (ImGui::Checkbox("##Visible", &newVisibility))
            {
                PushHistory();
                item.visible = newVisibility;
                if (draftStoredId == item.id) draft.visible = newVisibility;
                Persist();
                status = item.visible
                    ? "Ruler is now visible throughout Deco Tools."
                    : "Ruler hidden.";
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Keep this ruler visible in every tool and enable its endpoints and ticks as snap targets.");

            ImGui::TableSetColumnIndex(2);
            const bool selected = selectedIds.contains(item.id);
            if (ImGui::Selectable(item.name.c_str(), selected,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap))
                SelectRow(item.id, row);
            if (ImGui::BeginPopupContextItem("MeasurementContext"))
            {
                if (ImGui::MenuItem("Rename"))
                {
                    renameTarget = item.id;
                    std::snprintf(renameBuffer.data(), renameBuffer.size(),
                        "%s", item.name.c_str());
                    openRenamePopup = true;
                }
                ImGui::EndPopup();
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.3f", Distance(item.pointA, item.pointB));
            ImGui::TableSetColumnIndex(4);
            const std::string pointA = PointText(item.pointA);
            ImGui::TextUnformatted(pointA.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\nSource: %s", pointA.c_str(), item.pointA.source.c_str());
            ImGui::TableSetColumnIndex(5);
            const std::string pointB = PointText(item.pointB);
            ImGui::TextUnformatted(pointB.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\nSource: %s", pointB.c_str(), item.pointB.source.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    History& history = CurrentHistory();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttonWidth = (fullWidth - spacing * 4.0f) / 5.0f;
    if (history.undo.empty()) DisabledButton("Undo", ImVec2(buttonWidth, 0.0f));
    else if (ImGui::Button("Undo", ImVec2(buttonWidth, 0.0f))) Undo();
    ImGui::SameLine();
    if (history.redo.empty()) DisabledButton("Redo", ImVec2(buttonWidth, 0.0f));
    else if (ImGui::Button("Redo", ImVec2(buttonWidth, 0.0f))) Redo();
    ImGui::SameLine();
    if (selectedIds.empty()) DisabledButton("Remove", ImVec2(buttonWidth, 0.0f));
    else if (DestructiveButton("Remove", ImVec2(buttonWidth, 0.0f)))
    {
        if (selectedIds.size() > 1) openRemoveSelectedPopup = true;
        else RemoveMeasurements(selectedIds);
    }
    ImGui::SameLine();
    if (measurements.empty()) DisabledButton("Remove All", ImVec2(buttonWidth, 0.0f));
    else if (DestructiveButton("Remove All", ImVec2(buttonWidth, 0.0f)))
        openRemoveAllPopup = true;
    ImGui::SameLine();
    if (measurements.empty()) DisabledButton("Export CSV", ImVec2(buttonWidth, 0.0f));
    else if (PrimaryActionButton::Draw("Export CSV", ImVec2(buttonWidth, 0.0f)))
        ExportCsv();

    if (openRemoveSelectedPopup)
    {
        ImGui::OpenPopup("Remove Selected Measurements?");
        openRemoveSelectedPopup = false;
    }
    if (ImGui::BeginPopupModal("Remove Selected Measurements?", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("Remove the %zu selected measurements from this Layout? You can undo this during the current session.", selectedIds.size());
        if (DestructiveButton("Remove", ImVec2(140.0f, 0.0f)))
        {
            RemoveMeasurements(selectedIds);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(140.0f, 0.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (openRemoveAllPopup)
    {
        ImGui::OpenPopup("Remove All Measurements?");
        openRemoveAllPopup = false;
    }
    if (ImGui::BeginPopupModal("Remove All Measurements?", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("This will remove all recorded measurements for the currently loaded Layout. You can undo this action during the current Deco Tools session.");
        if (DestructiveButton("Remove All", ImVec2(140.0f, 0.0f)))
        {
            std::set<std::uint64_t> all;
            for (const Measurement& item : measurements) all.insert(item.id);
            RemoveMeasurements(all);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(140.0f, 0.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    RenderRenamePopup();
    StatusBar::PublishIfChanged(&status, status);
}

void MeasureToolTab::RenderHud()
{
    if (!hudVisible) return;
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.82f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 13.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 7.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.060f, 0.070f, 0.82f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.025f, 0.028f, 0.034f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.035f, 0.040f, 0.050f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.65f, 0.68f, 0.74f, 0.36f));
    if (ImGui::Begin("Ruler HUD", &hudVisible,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (!SharedXmlWorkspace::HasImport())
        {
            ImGui::TextDisabled("Import a Layout in Deco Tools first.");
        }
        else
        {
            const float newMeasurementWidth = ImGui::GetContentRegionAvail().x;
            if (PrimaryActionButton::Draw(
                "+ New Measurement##RulerHud", ImVec2(newMeasurementWidth, 0.0f)))
            {
                BeginNewMeasurement();
            }
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 5.0f));
            BeginDisabledCompat(!draftActive);
            ImGui::TextUnformatted("Measurement Type");
            ImGui::SetNextItemWidth(-1.0f);
            const char* typePreview = TypeName(draft.type);
            if (ImGui::BeginCombo("##MeasurementType", typePreview))
            {
                const MeasurementType types[] = { MeasurementType::Decorations,
                    MeasurementType::Groups, MeasurementType::FreeHand };
                for (const MeasurementType type : types)
                {
                    if (ImGui::Selectable(TypeName(type), draft.type == type))
                    {
                        if (draftStoredId != 0) RemoveStoredDraftForEndpointEdit();
                        draft.type = type;
                        draftHasA = false;
                        draftHasB = false;
                        selectedTick = -1;
                    }
                }
                ImGui::EndCombo();
            }

            if (draft.type == MeasurementType::FreeHand)
            {
                const float width = (ImGui::GetContentRegionAvail().x -
                    ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                if (ImGui::Button(draftHasA ? "Reposition Start" : "+ Start Point",
                    ImVec2(width, 0.0f)))
                {
                    if (draftStoredId != 0) RemoveStoredDraftForEndpointEdit();
                    draft.pointA = CharacterPoint();
                    draftHasA = true;
                    if (draftHasB) StoreDraft();
                }
                ImGui::SameLine();
                BeginDisabledCompat(!draftHasA);
                if (ImGui::Button(draftHasB ? "Reposition End" : "+ End Point",
                    ImVec2(width, 0.0f)))
                {
                    if (draftStoredId != 0) RemoveStoredDraftForEndpointEdit();
                    draft.pointB = CharacterPoint();
                    draftHasB = true;
                    StoreDraft();
                }
                EndDisabledCompat();
            }

            ImGui::Dummy(ImVec2(0.0f, 5.0f));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 5.0f));
            ImGui::TextUnformatted("Ticks");
            const TickDivision ticks[] = { TickDivision::None, TickDivision::Half,
                TickDivision::Quarter, TickDivision::Eighth, TickDivision::Sixteenth };
            for (size_t tickIndex = 0; tickIndex < std::size(ticks); ++tickIndex)
            {
                const TickDivision value = ticks[tickIndex];
                if (tickIndex > 0) ImGui::SameLine();
                if (ImGui::RadioButton(TickName(value), draft.ticks == value))
                {
                    draft.ticks = value;
                    selectedTick = -1;
                    if (draftStoredId != 0) StoreDraft();
                }
            }

            ImGui::Dummy(ImVec2(0.0f, 5.0f));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 5.0f));
            ImGui::TextUnformatted("Line Draw");
            ImGui::SetNextItemWidth(-1.0f);
            const char* linePreview = LineName(draft.lineStyle);
            if (ImGui::BeginCombo("##LineDraw", linePreview))
            {
                const LineStyle styles[] = { LineStyle::None, LineStyle::Single,
                    LineStyle::Dashed, LineStyle::Ruler };
                for (const LineStyle value : styles)
                    if (ImGui::Selectable(LineName(value), draft.lineStyle == value))
                    {
                        draft.lineStyle = value;
                        if (draftStoredId != 0) StoreDraft();
                    }
                ImGui::EndCombo();
            }

            if (draftHasA)
            {
                Point previewB = draftHasB ? draft.pointB : CharacterPoint();
                ImGui::Dummy(ImVec2(0.0f, 5.0f));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 5.0f));
                ImGui::Text("Length: %.3f", Distance(draft.pointA, previewB));
                ImGui::TextDisabled("A  %.3f   %.3f   %.3f",
                    draft.pointA.x, draft.pointA.y, draft.pointA.z);
                ImGui::TextDisabled("B  %.3f   %.3f   %.3f",
                    previewB.x, previewB.y, previewB.z);
                if (selectedTick > 0 && draftHasB)
                {
                    const Point tick = Interpolate(draft.pointA, draft.pointB,
                        static_cast<double>(selectedTick) /
                        static_cast<double>(static_cast<int>(draft.ticks)));
                    ImGui::TextColored(ImVec4(0.32f, 0.70f, 1.0f, 1.0f),
                        "Selected Tick  %.3f   %.3f   %.3f", tick.x, tick.y, tick.z);
                }
            }
            EndDisabledCompat();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(5);
}

void MeasureToolTab::RenderOverlay()
{
    if (!SharedXmlWorkspace::HasImport()) return;
    Mumble::Data* mumble = AppRuntime::GetMumble();
    if (mumble == nullptr || mumble->Context.MapID == 0) return;
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 viewport = io.DisplaySize;
    const Camera camera = CameraFromMumble(*mumble);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    hoveredCandidate = -1;
    hoveredEndpoint = -1;
    hoveredTick = -1;

    const bool editingAvailable = activeTab || hudVisible;
    for (const Measurement& item : measurements)
        if (item.visible && (!editingAvailable || !draftActive || item.id != draftStoredId))
            DrawMeasurement(item, false, true, camera, viewport, draw);

    if (!editingAvailable) return;
    if (!draftActive) return;
    if (draft.type != MeasurementType::FreeHand)
    {
        const std::vector<Candidate> candidates = CurrentCandidates();
        float nearest = 11.0f;
        for (size_t index = 0; index < candidates.size(); ++index)
        {
            ImVec2 screen;
            if (!Project(camera, DecorationToWorld(candidates[index].point), viewport, screen))
                continue;
            const bool isA = draftHasA && Distance(draft.pointA, candidates[index].point) < 0.0001;
            const bool isB = draftHasB && Distance(draft.pointB, candidates[index].point) < 0.0001;
            draw->AddCircleFilled(screen, isA || isB ? 5.0f : 3.5f,
                isA || isB ? IM_COL32(55, 150, 255, 255) :
                    IM_COL32(255, 156, 45, 245), 16);
            const float distance = std::hypot(windowMouse.x - screen.x, windowMouse.y - screen.y);
            if (distance < nearest)
            {
                nearest = distance;
                hoveredCandidate = static_cast<int>(index);
            }
        }
    }

    if (draftHasA && draft.visible)
    {
        Measurement display = draft;
        if (!draftHasB) display.pointB = CharacterPoint();
        DrawMeasurement(display, true, draftHasB, camera, viewport, draw);
    }

    if (pointLeftPending)
    {
        pointLeftPending = false;
        if (hoveredTick > 0) selectedTick = hoveredTick;
        else if (hoveredCandidate >= 0)
        {
            const std::vector<Candidate> candidates = CurrentCandidates();
            if (hoveredCandidate < static_cast<int>(candidates.size()))
                AssignCandidate(candidates[static_cast<size_t>(hoveredCandidate)]);
        }
    }
    if (pointRightPending)
    {
        pointRightPending = false;
        if (hoveredTick > 0)
        {
            selectedTick = -1;
            status = "Tick selection cleared.";
        }
        else if (hoveredEndpoint == 0)
        {
            RemoveStoredDraftForEndpointEdit();
            draftHasA = false;
            selectedTick = -1;
            status = "Point A removed. Choose a new Point A.";
        }
        else if (hoveredEndpoint == 1)
        {
            RemoveStoredDraftForEndpointEdit();
            draftHasB = false;
            selectedTick = -1;
            status = "Point B removed. Choose a new Point B.";
        }
    }
}

void MeasureToolTab::GetVisibleSnapPoints(std::vector<SnapPoint>& output)
{
    output.clear();
    for (const Measurement& item : measurements)
    {
        if (!item.visible) continue;
        output.push_back({ item.pointA.x, item.pointA.y, item.pointA.z });
        const int divisions = static_cast<int>(item.ticks);
        for (int tick = 1; tick < divisions; ++tick)
        {
            const double fraction = static_cast<double>(tick) /
                static_cast<double>(divisions);
            const Point point = Interpolate(item.pointA, item.pointB, fraction);
            output.push_back({ point.x, point.y, point.z });
        }
        output.push_back({ item.pointB.x, item.pointB.y, item.pointB.z });
    }
}

UINT MeasureToolTab::WndProc(HWND, UINT message, WPARAM, LPARAM lParam)
{
    switch (message)
    {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        windowMouse = ImVec2(
            static_cast<float>(static_cast<short>(LOWORD(lParam))),
            static_cast<float>(static_cast<short>(HIWORD(lParam))));
        break;
    default:
        return 1;
    }
    if (!draftActive || ImGui::GetIO().WantCaptureMouse) return 1;
    if ((message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) &&
        (hoveredCandidate >= 0 || hoveredTick > 0))
    {
        pointLeftPending = true;
        return 0;
    }
    if ((message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK) &&
        (hoveredEndpoint >= 0 || hoveredTick > 0))
    {
        pointRightPending = true;
        return 0;
    }
    return 1;
}
