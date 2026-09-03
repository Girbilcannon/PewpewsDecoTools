// Pewpew's Deco Tools - Interactive Layout Group Mover
// Imports decoration XML layouts, visualizes their bounds and decoration points
// in-game, supports interactive positioning, and exports relocated layouts.

#include "GroupMoverTab.h"

#include "../../Core/AppRuntime.h"
#include "../../Core/AppSettings.h"
#include "../../Core/DecorationDatabase.h"
#include "../../Core/GroupBackupDatabase.h"
#include "../../Core/Utf8Paths.h"
#include "../../Core/XmlFileUtils.h"
#include "../DecorationCounterWindow.h"
#include "../ManipulatorUtils.h"
#include "../XmlComboHelpers.h"
#include "../../imgui/imgui.h"
#include "../../imgui/imgui_internal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr float DecorationScale = 0.025400052f;
    constexpr float NearClip = 0.05f;
    constexpr float DefaultFovRadians = 0.872664626f;

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct DVec3
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    struct Mat3
    {
        double m[3][3] = {};
    };

    struct PropPosition
    {
        size_t tagStart = 0;
        size_t valueStart = 0;
        size_t valueLength = 0;
        size_t rotationValueStart = 0;
        size_t rotationValueLength = 0;
        size_t tagEnd = 0;
        bool hasRotationAttribute = false;
        bool modified = false;
        DVec3 position;
        DVec3 rotation;
        int id = -1;
        int groupIndex = -1;
        std::string name;
    };

    struct GroupInfo
    {
        std::string name;
        std::vector<size_t> propIndices;
        bool selected = false;
    };

    struct PropTransformState
    {
        size_t propIndex = 0;
        DVec3 position;
        DVec3 rotation;
    };

    struct TransformHistoryEntry
    {
        std::string label;
        std::vector<PropTransformState> before;
        std::vector<PropTransformState> after;
    };

    struct MapInfo
    {
        unsigned liveMapId;
        unsigned xmlMapId;
        const char* mapName;
        int type;
    };

    using XmlFileEntry = XmlFileUtils::Entry;

    struct Camera
    {
        Vec3 position;
        Vec3 forward;
        Vec3 up;
        Vec3 right;
        float fovRadians = DefaultFovRadians;

        bool Project(Vec3 world, ImVec2 viewport, ImVec2& screen) const;
    };

    float anchorPosition[3] = { 0.0f, 0.0f, 0.0f };

    std::string xmlSource;
    std::string importedPath;
    std::string importedFileName;
    std::string status = "No XML imported";
    std::vector<PropPosition> props;
    std::vector<GroupInfo> groups;
    std::vector<XmlFileEntry> availableXmlFiles;
    unsigned xmlMapId = 0;
    int xmlType = -1;
    int selectedFolderType = 0;
    int selectedXmlIndex = -1;
    bool fileListInitialized = false;
    bool listedSubFolders = false;

    int hoveredAxis = 0;
    int activeAxis = 0;
    bool inputCaptured = false;
    bool mouseDown = false;
    bool clickPending = false;
    bool pointClickPending = false;
    bool pointRightClickPending = false;
    bool pointRightClickCaptured = false;
    int hoveredPointGroup = -1;
    ImVec2 wndMousePosition(0.0f, 0.0f);
    ImVec2 dragStartMouse(0.0f, 0.0f);
    float dragStartAnchor[3] = { 0.0f, 0.0f, 0.0f };
    ImVec2 activeAxisDirection(0.0f, 0.0f);
    float activeDecoUnitsPerPixel = 0.0f;
    float activeWorldUnitsPerPixel = 0.0f;
    Vec3 dragViewRight;
    Vec3 dragViewUp;
    int manipulatorMode = 0;
    float groupRotationDegrees[3] = { 0.0f, 0.0f, 0.0f };
    Mat3 accumulatedGroupRotation =
    {
        {
            { 1.0, 0.0, 0.0 },
            { 0.0, 1.0, 0.0 },
            { 0.0, 0.0, 1.0 }
        }
    };
    DVec3 rotationDragPivot;
    Mat3 rotationDragStartGroupRotation;
    std::vector<DVec3> rotationDragStartPositions;
    std::vector<Mat3> rotationDragStartOrientations;
    std::vector<size_t> rotationDragPropIndices;
    constexpr size_t MaxTransformHistory = 100;
    std::vector<TransformHistoryEntry> undoHistory;
    std::vector<TransformHistoryEntry> redoHistory;
    std::vector<PropTransformState> pendingHistoryBefore;
    std::string pendingHistoryLabel;
    bool historyActionActive = false;

    Vec3 Add(Vec3 a, Vec3 b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    Vec3 Subtract(Vec3 a, Vec3 b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    Vec3 Multiply(Vec3 value, float scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    DVec3 Add(DVec3 a, DVec3 b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    DVec3 Subtract(DVec3 a, DVec3 b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    DVec3 Multiply(DVec3 value, double scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    float Dot(Vec3 a, Vec3 b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vec3 Cross(Vec3 a, Vec3 b)
    {
        return
        {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    float Length(Vec3 value)
    {
        return std::sqrt(Dot(value, value));
    }

    Vec3 Normalize(Vec3 value, Vec3 fallback)
    {
        const float length = Length(value);
        return length <= 0.000001f ? fallback : Multiply(value, 1.0f / length);
    }

    Vec3 FromMumble(const Mumble::Vector3& value)
    {
        return { value.X, value.Y, value.Z };
    }

    Vec3 DecorationToWorld(DVec3 decoration)
    {
        return
        {
            static_cast<float>(decoration.x * DecorationScale),
            static_cast<float>(-decoration.z * DecorationScale),
            static_cast<float>(decoration.y * DecorationScale)
        };
    }

    DVec3 WorldToDecoration(Vec3 world)
    {
        return
        {
            world.x / DecorationScale,
            world.z / DecorationScale,
            -world.y / DecorationScale
        };
    }

    Mat3 IdentityMatrix()
    {
        return
        {
            {
                { 1.0, 0.0, 0.0 },
                { 0.0, 1.0, 0.0 },
                { 0.0, 0.0, 1.0 }
            }
        };
    }

    Mat3 Multiply(const Mat3& a, const Mat3& b)
    {
        Mat3 result = {};
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                result.m[row][column] =
                    a.m[row][0] * b.m[0][column] +
                    a.m[row][1] * b.m[1][column] +
                    a.m[row][2] * b.m[2][column];
            }
        }
        return result;
    }

    DVec3 Multiply(const Mat3& matrix, DVec3 value)
    {
        return
        {
            matrix.m[0][0] * value.x + matrix.m[0][1] * value.y + matrix.m[0][2] * value.z,
            matrix.m[1][0] * value.x + matrix.m[1][1] * value.y + matrix.m[1][2] * value.z,
            matrix.m[2][0] * value.x + matrix.m[2][1] * value.y + matrix.m[2][2] * value.z
        };
    }

    Mat3 Transpose(const Mat3& matrix)
    {
        Mat3 result = {};
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                result.m[row][column] = matrix.m[column][row];
            }
        }
        return result;
    }

    bool IsIdentityMatrix(const Mat3& matrix)
    {
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                const double expected = row == column ? 1.0 : 0.0;
                if (std::fabs(matrix.m[row][column] - expected) > 0.000000000001)
                {
                    return false;
                }
            }
        }
        return true;
    }

    Mat3 RotationX(double radians)
    {
        Mat3 result = IdentityMatrix();
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        result.m[1][1] = cosine;
        result.m[1][2] = -sine;
        result.m[2][1] = sine;
        result.m[2][2] = cosine;
        return result;
    }

    Mat3 RotationY(double radians)
    {
        Mat3 result = IdentityMatrix();
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        result.m[0][0] = cosine;
        result.m[0][2] = sine;
        result.m[2][0] = -sine;
        result.m[2][2] = cosine;
        return result;
    }

    Mat3 RotationZ(double radians)
    {
        Mat3 result = IdentityMatrix();
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        result.m[0][0] = cosine;
        result.m[0][1] = -sine;
        result.m[1][0] = sine;
        result.m[1][1] = cosine;
        return result;
    }

    Mat3 AxisRotation(int axis, double radians)
    {
        if (axis == 0)
        {
            return RotationX(radians);
        }
        if (axis == 1)
        {
            return RotationY(radians);
        }
        return RotationZ(radians);
    }

    double NormalizeRadians(double radians)
    {
        constexpr double TwoPi = 6.28318530717958647692;
        radians = std::fmod(radians, TwoPi);
        if (radians < 0.0)
        {
            radians += TwoPi;
        }
        return radians;
    }

    Mat3 Gw2EulerToMatrix(DVec3 rotation)
    {
        return Multiply(
            RotationZ(rotation.z),
            Multiply(RotationX(rotation.x), RotationY(rotation.y))
        );
    }

    DVec3 Gw2MatrixToEuler(const Mat3& matrix)
    {
        constexpr double Epsilon = 1.0e-8;
        const double sinX = std::clamp(matrix.m[2][1], -1.0, 1.0);
        const double x = std::asin(sinX);
        const double cosX = std::cos(x);

        double y = 0.0;
        double z = 0.0;
        if (std::abs(cosX) > Epsilon)
        {
            y = std::atan2(-matrix.m[2][0], matrix.m[2][2]);
            z = std::atan2(-matrix.m[0][1], matrix.m[1][1]);
        }
        else
        {
            y = std::atan2(matrix.m[0][2], matrix.m[0][0]);
        }

        return
        {
            NormalizeRadians(x),
            NormalizeRadians(y),
            NormalizeRadians(z)
        };
    }

    Mat3 GroupEulerDegreesToMatrix(const float degrees[3])
    {
        constexpr double DegreesToRadians = 0.01745329251994329577;
        return Multiply(
            RotationZ(static_cast<double>(degrees[2]) * DegreesToRadians),
            Multiply(
                RotationY(static_cast<double>(degrees[1]) * DegreesToRadians),
                RotationX(static_cast<double>(degrees[0]) * DegreesToRadians)
            )
        );
    }

    void GroupMatrixToEulerDegrees(const Mat3& matrix, float degrees[3])
    {
        constexpr double Epsilon = 1.0e-8;
        constexpr double RadiansToDegrees = 57.2957795130823208768;
        const double sinY = std::clamp(-matrix.m[2][0], -1.0, 1.0);
        const double y = std::asin(sinY);
        const double cosY = std::cos(y);

        double x = 0.0;
        double z = 0.0;
        if (std::abs(cosY) > Epsilon)
        {
            x = std::atan2(matrix.m[2][1], matrix.m[2][2]);
            z = std::atan2(matrix.m[1][0], matrix.m[0][0]);
        }
        else
        {
            x = std::atan2(-matrix.m[1][2], matrix.m[1][1]);
        }

        degrees[0] = static_cast<float>(x * RadiansToDegrees);
        degrees[1] = static_cast<float>(y * RadiansToDegrees);
        degrees[2] = static_cast<float>(z * RadiansToDegrees);
    }

    Camera CameraFromMumble(const Mumble::Data& mumble)
    {
        Camera camera;
        camera.position = FromMumble(mumble.CameraPosition);
        camera.forward = Normalize(
            FromMumble(mumble.CameraFront),
            { 0.0f, 0.0f, 1.0f }
        );

        const Vec3 worldUp = { 0.0f, 1.0f, 0.0f };
        camera.right = Normalize(
            Cross(worldUp, camera.forward),
            { 1.0f, 0.0f, 0.0f }
        );
        camera.up = Normalize(
            Cross(camera.forward, camera.right),
            worldUp
        );

        const Mumble::Identity* identity = AppRuntime::GetMumbleIdentity();
        if (identity != nullptr && std::isfinite(identity->FOV) &&
            identity->FOV > 0.1f && identity->FOV < 3.0f)
        {
            camera.fovRadians = identity->FOV;
        }
        return camera;
    }

    bool Camera::Project(Vec3 world, ImVec2 viewport, ImVec2& screen) const
    {
        if (viewport.x <= 1.0f || viewport.y <= 1.0f)
        {
            return false;
        }

        const Vec3 relative = Subtract(world, position);
        const float cameraX = Dot(relative, right);
        const float cameraY = Dot(relative, up);
        const float cameraZ = Dot(relative, forward);

        if (cameraZ <= NearClip)
        {
            return false;
        }

        const float focalY =
            (viewport.y * 0.5f) / std::tan(fovRadians * 0.5f);

        screen.x = viewport.x * 0.5f + cameraX * focalY / cameraZ;
        screen.y = viewport.y * 0.5f - cameraY * focalY / cameraZ;
        return
            screen.x >= -4000.0f &&
            screen.x <= viewport.x + 4000.0f &&
            screen.y >= -4000.0f &&
            screen.y <= viewport.y + 4000.0f;
    }

    float WorldSizeForScreenPixels(
        const Camera& camera,
        ImVec2 viewport,
        Vec3 world,
        float pixels
    )
    {
        return ManipulatorUtils::WorldSizeForScreenPixels(
            Dot(Subtract(world, camera.position), camera.forward),
            NearClip, viewport.y, camera.fovRadians, pixels);
    }

    const MapInfo* FindMapInfo(unsigned mapId)
    {
        static constexpr MapInfo maps[] =
        {
            { 1558, 1558, "Hearth's Glow", 0 },
            { 1596, 1596, "Comosus Isle", 0 },

            { 1069, 1124, "Lost Precipice", 1 },
            { 1071, 1124, "Lost Precipice", 1 },
            { 1076, 1124, "Lost Precipice", 1 },
            { 1104, 1124, "Lost Precipice", 1 },
            { 1124, 1124, "Lost Precipice", 1 },
            { 1144, 1124, "Lost Precipice", 1 },

            { 1068, 1121, "Gilded Hollow", 1 },
            { 1101, 1121, "Gilded Hollow", 1 },
            { 1107, 1121, "Gilded Hollow", 1 },
            { 1108, 1121, "Gilded Hollow", 1 },
            { 1121, 1121, "Gilded Hollow", 1 },
            { 1125, 1121, "Gilded Hollow", 1 },

            { 1214, 1232, "Windswept Haven", 1 },
            { 1215, 1232, "Windswept Haven", 1 },
            { 1224, 1232, "Windswept Haven", 1 },
            { 1232, 1232, "Windswept Haven", 1 },
            { 1243, 1232, "Windswept Haven", 1 },
            { 1250, 1232, "Windswept Haven", 1 },

            { 1419, 1462, "Isle of Reflection", 1 },
            { 1426, 1462, "Isle of Reflection", 1 },
            { 1435, 1462, "Isle of Reflection", 1 },
            { 1444, 1462, "Isle of Reflection", 1 },
            { 1462, 1462, "Isle of Reflection", 1 }
        };

        for (const MapInfo& map : maps)
        {
            if (map.liveMapId == mapId)
            {
                return &map;
            }
        }

        return nullptr;
    }

    void RenderSectionHeading(const char* label)
    {
        ImGui::SetWindowFontScale(1.2f);
        ImGui::Text("%s", label);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
    }

    void RenderDisabledButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f))
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        ImGui::Button(label, size);
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }

    bool ParseFloat3(const std::string& value, DVec3& result)
    {
        std::istringstream stream(value);
        if (!(stream >> result.x >> result.y >> result.z))
        {
            return false;
        }

        std::string extra;
        return !(stream >> extra);
    }

    std::string Trim(std::string value)
    {
        while (!value.empty() &&
            std::isspace(static_cast<unsigned char>(value.front())) != 0)
        {
            value.erase(value.begin());
        }
        while (!value.empty() &&
            std::isspace(static_cast<unsigned char>(value.back())) != 0)
        {
            value.pop_back();
        }
        return value;
    }

    size_t FindTagEnd(const std::string& source, size_t tagStart)
    {
        char quote = '\0';

        for (size_t index = tagStart; index < source.size(); ++index)
        {
            const char character = source[index];
            if (quote != '\0')
            {
                if (character == quote)
                {
                    quote = '\0';
                }
                continue;
            }

            if (character == '"' || character == '\'')
            {
                quote = character;
            }
            else if (character == '>')
            {
                return index;
            }
        }

        return std::string::npos;
    }

    bool ReadAttribute(
        const std::string& source,
        size_t tagStart,
        size_t tagEnd,
        const char* attribute,
        std::string& value,
        size_t* valueStart = nullptr,
        size_t* valueLength = nullptr
    )
    {
        const std::string name(attribute);
        size_t position = tagStart;

        while (true)
        {
            position = source.find(name, position);
            if (position == std::string::npos || position >= tagEnd)
            {
                return false;
            }

            const bool validBefore =
                position == tagStart ||
                std::isspace(static_cast<unsigned char>(source[position - 1])) != 0;
            size_t equals = position + name.size();
            while (equals < tagEnd &&
                std::isspace(static_cast<unsigned char>(source[equals])) != 0)
            {
                ++equals;
            }

            if (!validBefore || equals >= tagEnd || source[equals] != '=')
            {
                position += name.size();
                continue;
            }

            ++equals;
            while (equals < tagEnd &&
                std::isspace(static_cast<unsigned char>(source[equals])) != 0)
            {
                ++equals;
            }

            if (equals >= tagEnd || (source[equals] != '"' && source[equals] != '\''))
            {
                return false;
            }

            const char quote = source[equals];
            const size_t start = equals + 1;
            const size_t end = source.find(quote, start);
            if (end == std::string::npos || end > tagEnd)
            {
                return false;
            }

            value = source.substr(start, end - start);
            if (valueStart != nullptr)
            {
                *valueStart = start;
            }
            if (valueLength != nullptr)
            {
                *valueLength = end - start;
            }
            return true;
        }
    }

    bool IsPropSelected(const PropPosition& prop)
    {
        return prop.groupIndex >= 0 &&
            prop.groupIndex < static_cast<int>(groups.size()) &&
            groups[static_cast<size_t>(prop.groupIndex)].selected;
    }

    size_t SelectedPropCount()
    {
        size_t count = 0;
        for (const PropPosition& prop : props)
        {
            if (IsPropSelected(prop)) ++count;
        }
        return count;
    }

    std::vector<PropTransformState> CaptureSelectedTransformState()
    {
        std::vector<PropTransformState> snapshot;
        snapshot.reserve(SelectedPropCount());
        for (size_t index = 0; index < props.size(); ++index)
        {
            const PropPosition& prop = props[index];
            if (!IsPropSelected(prop)) continue;
            snapshot.push_back({ index, prop.position, prop.rotation });
        }
        return snapshot;
    }

    std::vector<PropTransformState> CaptureMatchingTransformState(
        const std::vector<PropTransformState>& reference
    )
    {
        std::vector<PropTransformState> snapshot;
        snapshot.reserve(reference.size());
        for (const PropTransformState& state : reference)
        {
            if (state.propIndex >= props.size()) continue;
            const PropPosition& prop = props[state.propIndex];
            snapshot.push_back({ state.propIndex, prop.position, prop.rotation });
        }
        return snapshot;
    }

    bool TransformStatesMatch(
        const std::vector<PropTransformState>& left,
        const std::vector<PropTransformState>& right
    )
    {
        if (left.size() != right.size()) return false;
        constexpr double Epsilon = 0.000000001;
        for (size_t index = 0; index < left.size(); ++index)
        {
            const PropTransformState& a = left[index];
            const PropTransformState& b = right[index];
            if (a.propIndex != b.propIndex ||
                std::fabs(a.position.x - b.position.x) > Epsilon ||
                std::fabs(a.position.y - b.position.y) > Epsilon ||
                std::fabs(a.position.z - b.position.z) > Epsilon ||
                std::fabs(a.rotation.x - b.rotation.x) > Epsilon ||
                std::fabs(a.rotation.y - b.rotation.y) > Epsilon ||
                std::fabs(a.rotation.z - b.rotation.z) > Epsilon)
            {
                return false;
            }
        }
        return true;
    }

    void CancelPendingHistoryAction()
    {
        historyActionActive = false;
        pendingHistoryBefore.clear();
        pendingHistoryLabel.clear();
    }

    void BeginHistoryAction(const char* label)
    {
        CancelPendingHistoryAction();
        pendingHistoryBefore = CaptureSelectedTransformState();
        if (pendingHistoryBefore.empty()) return;
        pendingHistoryLabel = label;
        historyActionActive = true;
    }

    void CommitHistoryAction()
    {
        if (!historyActionActive) return;
        std::vector<PropTransformState> after =
            CaptureMatchingTransformState(pendingHistoryBefore);
        if (!TransformStatesMatch(pendingHistoryBefore, after))
        {
            undoHistory.push_back(
                { pendingHistoryLabel, std::move(pendingHistoryBefore), std::move(after) }
            );
            if (undoHistory.size() > MaxTransformHistory)
            {
                undoHistory.erase(undoHistory.begin());
            }
            redoHistory.clear();
        }
        CancelPendingHistoryAction();
    }

    void ClearTransformHistory()
    {
        undoHistory.clear();
        redoHistory.clear();
        CancelPendingHistoryAction();
    }

    void ResetManipulatorFrame()
    {
        CancelPendingHistoryAction();
        hoveredAxis = 0;
        activeAxis = 0;
        inputCaptured = false;
        mouseDown = false;
        clickPending = false;
        accumulatedGroupRotation = IdentityMatrix();
        groupRotationDegrees[0] = 0.0f;
        groupRotationDegrees[1] = 0.0f;
        groupRotationDegrees[2] = 0.0f;
        rotationDragStartPositions.clear();
        rotationDragStartOrientations.clear();
        rotationDragPropIndices.clear();
    }

    void ComputeAnchor()
    {
        const size_t selectedCount = SelectedPropCount();
        if (selectedCount == 0)
        {
            anchorPosition[0] = 0.0f;
            anchorPosition[1] = 0.0f;
            anchorPosition[2] = 0.0f;
            return;
        }

        double sumX = 0.0;
        double sumY = 0.0;
        double bottomZ = -std::numeric_limits<double>::infinity();

        for (const PropPosition& prop : props)
        {
            if (!IsPropSelected(prop)) continue;
            sumX += prop.position.x;
            sumY += prop.position.y;
            bottomZ = (std::max)(bottomZ, prop.position.z);
        }

        anchorPosition[0] = static_cast<float>(sumX / selectedCount);
        anchorPosition[1] = static_cast<float>(sumY / selectedCount);
        anchorPosition[2] = static_cast<float>(bottomZ);
    }

    void ApplyTransformState(const std::vector<PropTransformState>& snapshot)
    {
        for (const PropTransformState& state : snapshot)
        {
            if (state.propIndex >= props.size()) continue;
            PropPosition& prop = props[state.propIndex];
            prop.position = state.position;
            prop.rotation = state.rotation;
            prop.modified = true;
        }
        ResetManipulatorFrame();
        ComputeAnchor();
    }

    void UndoTransform()
    {
        CommitHistoryAction();
        if (undoHistory.empty()) return;
        TransformHistoryEntry entry = std::move(undoHistory.back());
        undoHistory.pop_back();
        ApplyTransformState(entry.before);
        status = "Undid " + entry.label +
            ". Apply to XML to save the restored state.";
        redoHistory.push_back(std::move(entry));
    }

    void RedoTransform()
    {
        CommitHistoryAction();
        if (redoHistory.empty()) return;
        TransformHistoryEntry entry = std::move(redoHistory.back());
        redoHistory.pop_back();
        ApplyTransformState(entry.after);
        status = "Redid " + entry.label +
            ". Apply to XML to save the restored state.";
        undoHistory.push_back(std::move(entry));
    }

    void SelectionChanged()
    {
        CommitHistoryAction();
        ResetManipulatorFrame();
        ComputeAnchor();
    }

    void MoveAnchorTo(const float target[3])
    {
        const DVec3 delta =
        {
            static_cast<double>(target[0] - anchorPosition[0]),
            static_cast<double>(target[1] - anchorPosition[1]),
            static_cast<double>(target[2] - anchorPosition[2])
        };

        if (std::fabs(delta.x) < 0.000000001 &&
            std::fabs(delta.y) < 0.000000001 &&
            std::fabs(delta.z) < 0.000000001)
        {
            return;
        }

        for (PropPosition& prop : props)
        {
            if (!IsPropSelected(prop)) continue;
            prop.position = Add(prop.position, delta);
            prop.modified = true;
        }

        anchorPosition[0] = target[0];
        anchorPosition[1] = target[1];
        anchorPosition[2] = target[2];
    }

    DVec3 CalculateAveragePivot()
    {
        DVec3 pivot;
        const size_t selectedCount = SelectedPropCount();
        if (selectedCount == 0)
        {
            return pivot;
        }

        for (const PropPosition& prop : props)
        {
            if (!IsPropSelected(prop)) continue;
            pivot = Add(pivot, prop.position);
        }
        return Multiply(pivot, 1.0 / static_cast<double>(selectedCount));
    }

    void CaptureRotationSnapshot()
    {
        rotationDragPivot = CalculateAveragePivot();
        rotationDragStartPositions.clear();
        rotationDragStartOrientations.clear();
        rotationDragPropIndices.clear();
        rotationDragStartPositions.reserve(SelectedPropCount());
        rotationDragStartOrientations.reserve(SelectedPropCount());
        rotationDragPropIndices.reserve(SelectedPropCount());

        for (size_t index = 0; index < props.size(); ++index)
        {
            const PropPosition& prop = props[index];
            if (!IsPropSelected(prop)) continue;
            rotationDragPropIndices.push_back(index);
            rotationDragStartPositions.push_back(prop.position);
            rotationDragStartOrientations.push_back(
                Gw2EulerToMatrix(prop.rotation)
            );
        }
    }

    void ApplyRigidGroupRotation(const Mat3& groupRotation)
    {
        if (rotationDragStartPositions.size() != rotationDragPropIndices.size() ||
            rotationDragStartOrientations.size() != rotationDragPropIndices.size())
        {
            return;
        }
        if (IsIdentityMatrix(groupRotation))
        {
            return;
        }

        const Mat3 orientationDelta = Transpose(groupRotation);
        for (size_t index = 0; index < rotationDragPropIndices.size(); ++index)
        {
            PropPosition& prop = props[rotationDragPropIndices[index]];
            const DVec3 relative = Subtract(
                rotationDragStartPositions[index],
                rotationDragPivot
            );
            prop.position = Add(
                rotationDragPivot,
                Multiply(groupRotation, relative)
            );
            prop.rotation = Gw2MatrixToEuler(
                Multiply(
                    rotationDragStartOrientations[index],
                    orientationDelta
                )
            );
            prop.modified = true;
        }

        ComputeAnchor();
    }

    void ApplyNumericGroupRotation(const float requestedDegrees[3])
    {
        CaptureRotationSnapshot();
        const Mat3 requested = GroupEulerDegreesToMatrix(requestedDegrees);
        const Mat3 delta = Multiply(
            requested,
            Transpose(accumulatedGroupRotation)
        );
        ApplyRigidGroupRotation(delta);
        accumulatedGroupRotation = requested;
        GroupMatrixToEulerDegrees(
            accumulatedGroupRotation,
            groupRotationDegrees
        );
    }

    std::string FormatFloat(double value)
    {
        if (std::fabs(value) < 0.0000005)
        {
            value = 0.0;
        }

        std::ostringstream stream;
        stream << std::fixed << std::setprecision(6) << value;
        std::string text = stream.str();

        while (text.size() > 1 && text.back() == '0')
        {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.')
        {
            text.pop_back();
        }
        return text;
    }

    std::string BuildUpdatedXml()
    {
        struct Replacement
        {
            size_t start = 0;
            size_t length = 0;
            std::string value;
        };

        std::vector<Replacement> replacements;
        replacements.reserve(props.size() * 2);
        for (const PropPosition& prop : props)
        {
            if (!prop.modified) continue;
            replacements.push_back(
                {
                    prop.valueStart,
                    prop.valueLength,
                    FormatFloat(prop.position.x) + " " +
                        FormatFloat(prop.position.y) + " " +
                        FormatFloat(prop.position.z)
                }
            );

            const std::string rotation =
                FormatFloat(prop.rotation.x) + " " +
                FormatFloat(prop.rotation.y) + " " +
                FormatFloat(prop.rotation.z);
            replacements.push_back(
                prop.hasRotationAttribute
                    ? Replacement
                    {
                        prop.rotationValueStart,
                        prop.rotationValueLength,
                        rotation
                    }
                    : Replacement
                    {
                        prop.tagEnd,
                        0,
                        " rot=\"" + rotation + "\""
                    }
            );
        }

        std::sort(
            replacements.begin(),
            replacements.end(),
            [](const Replacement& left, const Replacement& right)
            {
                return left.start > right.start;
            }
        );

        std::string output = xmlSource;
        for (const Replacement& replacement : replacements)
        {
            output.replace(
                replacement.start,
                replacement.length,
                replacement.value
            );
        }
        return output;
    }

    bool ReplaceRootAttribute(
        std::string& xml,
        const char* attribute,
        const std::string& value
    )
    {
        const size_t rootStart = xml.find("<Decorations");
        const size_t rootEnd = rootStart == std::string::npos
            ? std::string::npos
            : FindTagEnd(xml, rootStart);
        if (rootStart == std::string::npos || rootEnd == std::string::npos)
        {
            return false;
        }

        std::string oldValue;
        size_t valueStart = 0;
        size_t valueLength = 0;
        if (!ReadAttribute(
            xml,
            rootStart,
            rootEnd,
            attribute,
            oldValue,
            &valueStart,
            &valueLength
        ))
        {
            return false;
        }

        std::string encoded;
        const char quote = valueStart > 0 ? xml[valueStart - 1] : '"';
        for (const char character : value)
        {
            if (character == '&')
            {
                encoded += "&amp;";
            }
            else if (character == '<')
            {
                encoded += "&lt;";
            }
            else if (character == '"' && quote == '"')
            {
                encoded += "&quot;";
            }
            else if (character == '\'' && quote == '\'')
            {
                encoded += "&apos;";
            }
            else
            {
                encoded += character;
            }
        }

        xml.replace(valueStart, valueLength, encoded);
        return true;
    }

    std::string FolderForType(int type)
    {
        const AppSettings::Data& settings = AppSettings::Get();
        return type == 1
            ? settings.guildHallFolder.data()
            : settings.homesteadFolder.data();
    }

    void RefreshXmlList()
    {
        const std::string folder = FolderForType(selectedFolderType);
        const std::string previousPath =
            selectedXmlIndex >= 0 &&
            selectedXmlIndex < static_cast<int>(availableXmlFiles.size())
            ? availableXmlFiles[static_cast<size_t>(selectedXmlIndex)].path
            : std::string();

        availableXmlFiles.clear();
        selectedXmlIndex = -1;
        fileListInitialized = true;
        listedSubFolders = AppSettings::Get().showXmlsFromSubFolders;

        if (folder.empty())
        {
            status = "Set this XML folder path in Settings first.";
            return;
        }

        if (!XmlFileUtils::List(
            folder,
            AppSettings::Get().showXmlsFromSubFolders,
            availableXmlFiles
        ))
        {
            status = "XML folder not found or could not be read. Check the path in Settings.";
            return;
        }

        for (size_t index = 0; index < availableXmlFiles.size(); ++index)
        {
            if (availableXmlFiles[index].path == previousPath)
            {
                selectedXmlIndex = static_cast<int>(index);
                break;
            }
        }

        if (selectedXmlIndex < 0 && !availableXmlFiles.empty())
        {
            selectedXmlIndex = 0;
        }

        status = availableXmlFiles.empty()
            ? "No XML files found in this folder."
            : "Found " + std::to_string(availableXmlFiles.size()) +
                (availableXmlFiles.size() == 1 ? " XML file." : " XML files.");
    }

    void InitializeXmlList()
    {
        if (fileListInitialized &&
            listedSubFolders == AppSettings::Get().showXmlsFromSubFolders)
        {
            return;
        }

        RefreshXmlList();
    }

    bool ImportXml(const std::string& path, bool preserveTransformHistory = false)
    {
        const GroupBackupDatabase::ImportResult groupRestore =
            GroupBackupDatabase::PrepareImport(
            path,
            -1,
            AppSettings::Get().automaticGroupBackupRestore,
            AppSettings::Get().backupUngroupedXmls
        );
        if (groupRestore.action == GroupBackupDatabase::ImportAction::NeedsUserChoice ||
            groupRestore.action == GroupBackupDatabase::ImportAction::Error)
        {
            status = groupRestore.message;
            return false;
        }
        std::ifstream file(Utf8Paths::FromUtf8(path), std::ios::binary);
        if (!file.is_open())
        {
            status = "Could not open the selected XML file.";
            return false;
        }

        std::ostringstream contents;
        contents << file.rdbuf();
        std::string source = contents.str();

        const size_t rootStart = source.find("<Decorations");
        const size_t rootEnd = rootStart == std::string::npos
            ? std::string::npos
            : FindTagEnd(source, rootStart);
        if (rootStart == std::string::npos || rootEnd == std::string::npos)
        {
            status = "Invalid XML: missing the <Decorations> tag.";
            return false;
        }

        std::string typeText;
        if (!ReadAttribute(source, rootStart, rootEnd, "type", typeText) ||
            (typeText != "0" && typeText != "1"))
        {
            status = "Invalid XML: Decorations type must be 0 or 1.";
            return false;
        }

        std::string mapIdText;
        if (!ReadAttribute(source, rootStart, rootEnd, "mapId", mapIdText))
        {
            status = "Invalid XML: missing Decorations mapId.";
            return false;
        }

        unsigned parsedMapId = 0;
        try
        {
            parsedMapId = static_cast<unsigned>(std::stoul(mapIdText));
        }
        catch (...)
        {
            status = "Invalid XML: Decorations mapId is not a number.";
            return false;
        }

        std::vector<PropPosition> parsedProps;
        size_t searchPosition = rootEnd;
        while (true)
        {
            const size_t propStart = source.find("<prop", searchPosition);
            if (propStart == std::string::npos)
            {
                break;
            }

            const size_t propEnd = FindTagEnd(source, propStart);
            if (propEnd == std::string::npos)
            {
                status = "Invalid XML: an unfinished <prop> tag was found.";
                return false;
            }

            std::string positionText;
            std::string rotationText;
            std::string idText;
            PropPosition prop;
            prop.tagStart = propStart;
            prop.tagEnd =
                propEnd > propStart && source[propEnd - 1] == '/'
                    ? propEnd - 1
                    : propEnd;
            if (ReadAttribute(
                source,
                propStart,
                propEnd,
                "pos",
                positionText,
                &prop.valueStart,
                &prop.valueLength
            ))
            {
                if (!ParseFloat3(positionText, prop.position))
                {
                    status = "Invalid XML: a decoration has an invalid pos value.";
                    return false;
                }
                prop.hasRotationAttribute = ReadAttribute(
                    source,
                    propStart,
                    propEnd,
                    "rot",
                    rotationText,
                    &prop.rotationValueStart,
                    &prop.rotationValueLength
                );
                if (prop.hasRotationAttribute &&
                    !ParseFloat3(rotationText, prop.rotation))
                {
                    status = "Invalid XML: a decoration has an invalid rot value.";
                    return false;
                }
                if (ReadAttribute(source, propStart, propEnd, "id", idText))
                {
                    try { prop.id = std::stoi(idText); }
                    catch (...) { prop.id = -1; }
                }
                const char* databaseName =
                    DecorationDatabase::FindNameById(prop.id, typeText == "1" ? 1 : 0);
                prop.name = databaseName == nullptr
                    ? "Unknown Decoration"
                    : databaseName;
                parsedProps.push_back(prop);
            }

            searchPosition = propEnd + 1;
        }

        if (parsedProps.empty())
        {
            status = "Invalid XML: no positioned <prop> entries were found.";
            return false;
        }

        const size_t rootClose = source.rfind("</Decorations");
        if (rootClose == std::string::npos)
        {
            status = "Invalid XML: missing the closing Decorations tag.";
            return false;
        }

        std::vector<GroupInfo> parsedGroups;
        size_t comment = source.find("<!--", rootEnd + 1);
        while (comment != std::string::npos && comment < rootClose)
        {
            const size_t commentEnd = source.find("-->", comment + 4);
            if (commentEnd == std::string::npos || commentEnd >= rootClose)
            {
                break;
            }
            const size_t next = source.find("<!--", commentEnd + 3);
            const size_t groupEnd =
                next == std::string::npos || next >= rootClose
                    ? rootClose
                    : next;
            GroupInfo group;
            group.name = Trim(source.substr(
                comment + 4,
                commentEnd - comment - 4
            ));
            if (!group.name.empty())
            {
                const int groupIndex = static_cast<int>(parsedGroups.size());
                for (size_t propIndex = 0; propIndex < parsedProps.size(); ++propIndex)
                {
                    PropPosition& prop = parsedProps[propIndex];
                    if (prop.tagStart > commentEnd && prop.tagStart < groupEnd)
                    {
                        prop.groupIndex = groupIndex;
                        group.propIndices.push_back(propIndex);
                    }
                }
                if (!group.propIndices.empty())
                {
                    parsedGroups.push_back(std::move(group));
                }
            }
            comment = next;
        }

        if (parsedGroups.empty())
        {
            status = "This XML does not contain any named decoration groups.";
            return false;
        }

        xmlSource = std::move(source);
        props = std::move(parsedProps);
        groups = std::move(parsedGroups);
        importedPath = path;
        xmlMapId = parsedMapId;
        xmlType = typeText == "0" ? 0 : 1;
        importedFileName = Utf8Paths::ToUtf8(
            Utf8Paths::FromUtf8(path).filename()
        );
        if (!preserveTransformHistory)
        {
            ClearTransformHistory();
        }
        ResetManipulatorFrame();
        ComputeAnchor();

        std::map<int, DecorationCounterWindow::Requirement> countById;
        for (const PropPosition& prop : props)
        {
            auto& item = countById[prop.id];
            item.id = prop.id;
            item.name = prop.name;
            ++item.required;
        }
        std::vector<DecorationCounterWindow::Requirement> counterItems;
        for (const auto& [id, item] : countById)
        {
            static_cast<void>(id);
            counterItems.push_back(item);
        }
        DecorationCounterWindow::SetRequirements(
            importedFileName,
            xmlType,
            counterItems
        );

        size_t groupedPropCount = 0;
        for (const GroupInfo& group : groups)
        {
            groupedPropCount += group.propIndices.size();
        }
        status =
            "Loaded " + std::to_string(groups.size()) + " groups containing " +
            std::to_string(groupedPropCount) + " decorations (" +
            (xmlType == 0 ? "Homestead" : "Guild Hall") + ").";
        return true;
    }

    void MoveToCharacter()
    {
        CommitHistoryAction();
        Mumble::Data* mumble = AppRuntime::GetMumble();
        if (mumble == nullptr || mumble->Context.MapID == 0)
        {
            status = "Character position is unavailable. Load into a map first.";
            return;
        }

        const DVec3 target = WorldToDecoration(FromMumble(mumble->AvatarPosition));
        const float targetArray[3] =
        {
            static_cast<float>(target.x),
            static_cast<float>(target.y),
            static_cast<float>(target.z)
        };
        BeginHistoryAction("Move to Character");
        MoveAnchorTo(targetArray);
        CommitHistoryAction();
        status =
            "Moved the decoration group to the character on map " +
            std::to_string(mumble->Context.MapID) + ".";
    }

    void ApplyToXml()
    {
        CommitHistoryAction();
        if (importedPath.empty())
        {
            status = "Import a grouped XML file first.";
            return;
        }
        bool hasChanges = false;
        for (const PropPosition& prop : props)
        {
            if (prop.modified)
            {
                hasChanges = true;
                break;
            }
        }
        if (!hasChanges)
        {
            status = "No group movement has been made yet.";
            return;
        }

        std::vector<std::string> selectedNames;
        for (const GroupInfo& group : groups)
        {
            if (group.selected) selectedNames.push_back(group.name);
        }

        const std::filesystem::path outputPath =
            Utf8Paths::FromUtf8(importedPath);
        std::filesystem::path temporary = outputPath;
        temporary += L".decotools.tmp";
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);

        const std::string output = BuildUpdatedXml();
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            status = "Could not create the temporary XML update file.";
            return;
        }
        file.write(output.data(), static_cast<std::streamsize>(output.size()));
        file.close();
        if (!file.good())
        {
            std::filesystem::remove(temporary, cleanupError);
            status = "The XML update could not be written completely.";
            return;
        }
        if (!MoveFileExW(
            temporary.c_str(),
            outputPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        ))
        {
            std::filesystem::remove(temporary, cleanupError);
            status = "Could not replace the source XML. The original was left unchanged.";
            return;
        }

        if (AppSettings::Get().automaticGroupBackupRestore)
        {
            std::string backupStatus;
            GroupBackupDatabase::RecordFile(
                importedPath,
                xmlType,
                GroupBackupDatabase::RestorePointType::Auto,
                std::string(),
                backupStatus,
                AppSettings::Get().backupUngroupedXmls
            );
        }

        if (!ImportXml(importedPath, true))
        {
            status = "The XML was updated, but could not be reloaded.";
            return;
        }
        for (GroupInfo& group : groups)
        {
            group.selected = std::find(
                selectedNames.begin(),
                selectedNames.end(),
                group.name
            ) != selectedNames.end();
        }
        SelectionChanged();
        status = "Applied the selected group movement directly to " +
            importedFileName + ".";
    }

    float DistanceToSegment(ImVec2 point, ImVec2 start, ImVec2 end)
    {
        const float x = end.x - start.x;
        const float y = end.y - start.y;
        const float lengthSquared = x * x + y * y;
        if (lengthSquared <= 0.000001f)
        {
            const float dx = point.x - start.x;
            const float dy = point.y - start.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        float amount =
            ((point.x - start.x) * x + (point.y - start.y) * y) /
            lengthSquared;
        amount = (std::max)(0.0f, (std::min)(1.0f, amount));

        const float closestX = start.x + amount * x;
        const float closestY = start.y + amount * y;
        const float dx = point.x - closestX;
        const float dy = point.y - closestY;
        return std::sqrt(dx * dx + dy * dy);
    }

    void DrawArrow(
        ImDrawList* draw,
        ImVec2 start,
        ImVec2 end,
        ImU32 color,
        float thickness
    )
    {
        draw->AddLine(start, end, color, thickness);
        const float dx = end.x - start.x;
        const float dy = end.y - start.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 1.0f)
        {
            return;
        }

        const float unitX = dx / length;
        const float unitY = dy / length;
        const float sideX = -unitY;
        const float sideY = unitX;
        const float head = 9.0f;

        draw->AddTriangleFilled(
            end,
            ImVec2(end.x - unitX * head + sideX * head * 0.5f,
                end.y - unitY * head + sideY * head * 0.5f),
            ImVec2(end.x - unitX * head - sideX * head * 0.5f,
                end.y - unitY * head - sideY * head * 0.5f),
            color
        );
    }

    DVec3 RingPoint(
        DVec3 center,
        const Mat3& ringFrame,
        int axis,
        double radius,
        double angle
    )
    {
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        DVec3 local;
        if (axis == 0)
        {
            local = { 0.0, cosine * radius, sine * radius };
        }
        else if (axis == 1)
        {
            local = { cosine * radius, 0.0, sine * radius };
        }
        else
        {
            local = { cosine * radius, sine * radius, 0.0 };
        }
        return Add(center, Multiply(ringFrame, local));
    }

    void DrawRotationRing(
        ImDrawList* draw,
        const Camera& camera,
        ImVec2 viewport,
        DVec3 center,
        const Mat3& ringFrame,
        int axis,
        double radius,
        ImU32 color,
        float thickness
    )
    {
        constexpr int Segments = 96;
        constexpr double TwoPi = 6.28318530717958647692;
        ImVec2 previous;
        bool previousVisible = false;

        for (int index = 0; index <= Segments; ++index)
        {
            const double angle =
                static_cast<double>(index) / Segments * TwoPi;
            ImVec2 point;
            const bool visible = camera.Project(
                DecorationToWorld(RingPoint(
                    center,
                    ringFrame,
                    axis,
                    radius,
                    angle
                )),
                viewport,
                point
            );
            if (visible && previousVisible)
            {
                draw->AddLine(previous, point, color, thickness);
            }
            previous = point;
            previousVisible = visible;
        }
    }

    float DistanceToRotationRing(
        const Camera& camera,
        ImVec2 viewport,
        DVec3 center,
        const Mat3& ringFrame,
        int axis,
        double radius,
        ImVec2 mouse
    )
    {
        constexpr int Segments = 96;
        constexpr double TwoPi = 6.28318530717958647692;
        ImVec2 previous;
        bool previousVisible = false;
        float bestDistance = std::numeric_limits<float>::infinity();

        for (int index = 0; index <= Segments; ++index)
        {
            const double angle =
                static_cast<double>(index) / Segments * TwoPi;
            ImVec2 point;
            const bool visible = camera.Project(
                DecorationToWorld(RingPoint(
                    center,
                    ringFrame,
                    axis,
                    radius,
                    angle
                )),
                viewport,
                point
            );
            if (visible && previousVisible)
            {
                bestDistance = (std::min)(
                    bestDistance,
                    DistanceToSegment(mouse, previous, point)
                );
            }
            previous = point;
            previousVisible = visible;
        }
        return bestDistance;
    }

    void DrawPreview(const Camera& camera, ImVec2 viewport, ImDrawList* draw)
    {
        AppSettings::Data& settings = AppSettings::Get();
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float pointSize = (std::max)(2.0f, settings.pointSize);
        const float hitRadius = pointSize + 5.0f;
        const ImU32 orange = IM_COL32(255, 166, 36, 255);
        const ImU32 blue = IM_COL32(50, 150, 255, 255);
        hoveredPointGroup = -1;
        float closestPoint = std::numeric_limits<float>::infinity();

        DVec3 minimum =
        {
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity()
        };
        DVec3 maximum =
        {
            -std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity()
        };

        for (const PropPosition& prop : props)
        {
            if (IsPropSelected(prop))
            {
                minimum.x = (std::min)(minimum.x, prop.position.x);
                minimum.y = (std::min)(minimum.y, prop.position.y);
                minimum.z = (std::min)(minimum.z, prop.position.z);
                maximum.x = (std::max)(maximum.x, prop.position.x);
                maximum.y = (std::max)(maximum.y, prop.position.y);
                maximum.z = (std::max)(maximum.z, prop.position.z);
            }

            if (prop.groupIndex >= 0)
            {
                ImVec2 point;
                if (camera.Project(DecorationToWorld(prop.position), viewport, point))
                {
                    draw->AddCircleFilled(
                        point,
                        pointSize,
                        IsPropSelected(prop) ? blue : orange,
                        12
                    );
                    const float dx = mouse.x - point.x;
                    const float dy = mouse.y - point.y;
                    const float distance = std::sqrt(dx * dx + dy * dy);
                    if (distance <= hitRadius && distance < closestPoint)
                    {
                        closestPoint = distance;
                        hoveredPointGroup = prop.groupIndex;
                    }
                }
            }
        }

        const bool pointClicked = pointClickPending ||
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow));
        const bool pointRightClicked = pointRightClickPending ||
            (ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
                !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow));
        pointClickPending = false;
        pointRightClickPending = false;
        if (hoveredPointGroup >= 0 &&
            hoveredPointGroup < static_cast<int>(groups.size()))
        {
            const std::string& name =
                groups[static_cast<size_t>(hoveredPointGroup)].name;
            const ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
            const ImVec2 padding(7.0f, 5.0f);
            ImVec2 tooltipPosition(mouse.x + 18.0f, mouse.y + 8.0f);
            const ImVec2 tooltipSize(
                textSize.x + padding.x * 2.0f,
                textSize.y + padding.y * 2.0f
            );
            tooltipPosition.x = (std::min)(
                tooltipPosition.x,
                (std::max)(4.0f, viewport.x - tooltipSize.x - 4.0f)
            );
            tooltipPosition.y = (std::min)(
                tooltipPosition.y,
                (std::max)(4.0f, viewport.y - tooltipSize.y - 4.0f)
            );
            ImDrawList* foreground = ImGui::GetForegroundDrawList();
            foreground->AddRectFilled(
                tooltipPosition,
                ImVec2(
                    tooltipPosition.x + tooltipSize.x,
                    tooltipPosition.y + tooltipSize.y
                ),
                IM_COL32(24, 24, 28, 245),
                4.0f
            );
            foreground->AddText(
                ImVec2(
                    tooltipPosition.x + padding.x,
                    tooltipPosition.y + padding.y
                ),
                IM_COL32(255, 255, 255, 255),
                name.c_str()
            );
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::GetIO().WantCaptureMouse = true;
            if (activeAxis == 0)
            {
                GroupInfo& group = groups[static_cast<size_t>(hoveredPointGroup)];
                if (pointRightClicked && group.selected)
                {
                    group.selected = false;
                    SelectionChanged();
                    status = "Deselected group: " + group.name + ".";
                }
                else if (pointClicked && !group.selected)
                {
                    group.selected = true;
                    SelectionChanged();
                    status = "Selected group: " + group.name + ".";
                }
            }
        }

        if (SelectedPropCount() == 0)
        {
            return;
        }

        const DVec3 decorationCorners[8] =
        {
            { minimum.x, minimum.y, minimum.z },
            { maximum.x, minimum.y, minimum.z },
            { maximum.x, maximum.y, minimum.z },
            { minimum.x, maximum.y, minimum.z },
            { minimum.x, minimum.y, maximum.z },
            { maximum.x, minimum.y, maximum.z },
            { maximum.x, maximum.y, maximum.z },
            { minimum.x, maximum.y, maximum.z }
        };

        ImVec2 corners[8];
        bool visible[8] = {};
        for (int index = 0; index < 8; ++index)
        {
            visible[index] = camera.Project(
                DecorationToWorld(decorationCorners[index]),
                viewport,
                corners[index]
            );
        }

        const int faces[6][4] =
        {
            { 0, 1, 2, 3 },
            { 4, 7, 6, 5 },
            { 0, 4, 5, 1 },
            { 1, 5, 6, 2 },
            { 2, 6, 7, 3 },
            { 3, 7, 4, 0 }
        };

        if (settings.showSolidFaces)
        {
            const ImU32 faceColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
                settings.faceColor[0],
                settings.faceColor[1],
                settings.faceColor[2],
                settings.faceColor[3]
            ));

            for (const auto& face : faces)
            {
                if (visible[face[0]] && visible[face[1]] &&
                    visible[face[2]] && visible[face[3]])
                {
                    draw->AddTriangleFilled(
                        corners[face[0]], corners[face[1]], corners[face[2]], faceColor);
                    draw->AddTriangleFilled(
                        corners[face[0]], corners[face[2]], corners[face[3]], faceColor);
                }
            }
        }

        if (settings.showBoundingBox)
        {
            const ImU32 boxColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
                settings.boxColor[0],
                settings.boxColor[1],
                settings.boxColor[2],
                settings.boxColor[3]
            ));
            const int edges[12][2] =
            {
                { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
                { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
                { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
            };

            for (const auto& edge : edges)
            {
                if (visible[edge[0]] && visible[edge[1]])
                {
                    draw->AddLine(corners[edge[0]], corners[edge[1]], boxColor, 2.0f);
                }
            }
        }
    }

    void DrawManipulator(const Camera& camera, ImVec2 viewport, ImDrawList* draw)
    {
        const DVec3 originDecoration = manipulatorMode == 0
            ? DVec3
            {
                anchorPosition[0],
                anchorPosition[1],
                anchorPosition[2]
            }
            : CalculateAveragePivot();
        const Vec3 originWorld = DecorationToWorld(originDecoration);

        ImVec2 originScreen;
        if (!camera.Project(originWorld, viewport, originScreen))
        {
            hoveredAxis = 0;
            return;
        }

        ImVec2 mouse = inputCaptured || activeAxis != 0
            ? wndMousePosition
            : ImGui::GetIO().MousePos;
        hoveredAxis = 0;
        const ImU32 baseColors[3] =
        {
            ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.18f, 0.15f, 1.0f)),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.20f, 0.90f, 0.25f, 1.0f)),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.25f, 0.55f, 1.0f, 1.0f))
        };
        const ImU32 highlight =
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.95f, 1.0f, 1.0f));
        const bool clicked =
            hoveredPointGroup < 0 && (clickPending ||
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)));
        clickPending = false;

        if (manipulatorMode == 0)
        {
            const float axisWorldLength =
                WorldSizeForScreenPixels(camera, viewport, originWorld,
                    ManipulatorUtils::MoveAxisPixels);
            const Vec3 worldAxes[3] =
            {
                { axisWorldLength, 0.0f, 0.0f },
                { 0.0f, 0.0f, axisWorldLength },
                { 0.0f, axisWorldLength, 0.0f }
            };

            ImVec2 axisScreen[3];
            bool axisVisible[3] = {};
            for (int index = 0; index < 3; ++index)
            {
                axisVisible[index] = camera.Project(
                    Add(originWorld, worldAxes[index]),
                    viewport,
                    axisScreen[index]
                );
            }

            constexpr float centerHalfSize = ManipulatorUtils::CenterHalfSize;
            constexpr float centerHitHalfSize = ManipulatorUtils::CenterHitHalfSize;
            const bool centerHovered =
                mouse.x >= originScreen.x - centerHitHalfSize &&
                mouse.x <= originScreen.x + centerHitHalfSize &&
                mouse.y >= originScreen.y - centerHitHalfSize &&
                mouse.y <= originScreen.y + centerHitHalfSize;

            float bestDistance = 38.0f;
            if (activeAxis == 0 &&
                !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
            {
                if (centerHovered)
                {
                    hoveredAxis = 4;
                }
                else
                {
                    for (int index = 0; index < 3; ++index)
                    {
                        if (!axisVisible[index])
                        {
                            continue;
                        }
                        const float distance = DistanceToSegment(
                            mouse,
                            originScreen,
                            axisScreen[index]
                        );
                        if (distance < bestDistance)
                        {
                            bestDistance = distance;
                            hoveredAxis = index + 1;
                        }
                    }
                }
            }

            for (int index = 0; index < 3; ++index)
            {
                if (!axisVisible[index])
                {
                    continue;
                }
                const bool selected =
                    hoveredAxis == index + 1 || activeAxis == index + 1;
                DrawArrow(
                    draw,
                    originScreen,
                    axisScreen[index],
                    selected ? highlight : baseColors[index],
                    selected ? 5.0f : 3.0f
                );
            }

            const bool centerSelected =
                hoveredAxis == 4 || activeAxis == 4;
            const ImU32 centerColor = centerSelected
                ? highlight
                : ImGui::ColorConvertFloat4ToU32(
                    ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
            draw->AddRectFilled(
                ImVec2(originScreen.x - centerHalfSize,
                    originScreen.y - centerHalfSize),
                ImVec2(originScreen.x + centerHalfSize,
                    originScreen.y + centerHalfSize),
                centerColor,
                1.5f
            );
            draw->AddRect(
                ImVec2(originScreen.x - centerHalfSize,
                    originScreen.y - centerHalfSize),
                ImVec2(originScreen.x + centerHalfSize,
                    originScreen.y + centerHalfSize),
                ImGui::ColorConvertFloat4ToU32(
                    ImVec4(0.85f, 0.85f, 0.85f, 1.0f)),
                1.5f,
                0,
                centerSelected ? 2.0f : 1.0f
            );

            if (clicked && activeAxis == 0 && hoveredAxis != 0)
            {
                BeginHistoryAction("group move");
                activeAxis = hoveredAxis;
                inputCaptured = true;
                dragStartMouse = mouse;
                dragStartAnchor[0] = anchorPosition[0];
                dragStartAnchor[1] = anchorPosition[1];
                dragStartAnchor[2] = anchorPosition[2];

                if (activeAxis == 4)
                {
                    activeWorldUnitsPerPixel = WorldSizeForScreenPixels(
                        camera,
                        viewport,
                        originWorld,
                        1.0f
                    );
                    dragViewRight = camera.right;
                    dragViewUp = camera.up;
                }
                else
                {
                    const ImVec2 end = axisScreen[activeAxis - 1];
                    const float x = end.x - originScreen.x;
                    const float y = end.y - originScreen.y;
                    const float screenLength = std::sqrt(x * x + y * y);
                    if (screenLength > 1.0f)
                    {
                        activeAxisDirection = ImVec2(
                            x / screenLength,
                            y / screenLength
                        );
                        activeDecoUnitsPerPixel =
                            axisWorldLength / screenLength / DecorationScale;
                        if (activeAxis == 3)
                        {
                            activeDecoUnitsPerPixel = -activeDecoUnitsPerPixel;
                        }
                    }
                }
            }

            if (activeAxis != 0 && mouseDown)
            {
                const float mouseX = mouse.x - dragStartMouse.x;
                const float mouseY = mouse.y - dragStartMouse.y;
                float target[3] =
                {
                    dragStartAnchor[0],
                    dragStartAnchor[1],
                    dragStartAnchor[2]
                };
                if (activeAxis == 4)
                {
                    const Vec3 startWorld = DecorationToWorld(
                        DVec3{ target[0], target[1], target[2] }
                    );
                    const Vec3 worldDelta = Add(
                        Multiply(dragViewRight,
                            mouseX * activeWorldUnitsPerPixel),
                        Multiply(dragViewUp,
                            -mouseY * activeWorldUnitsPerPixel)
                    );
                    const DVec3 targetDecoration =
                        WorldToDecoration(Add(startWorld, worldDelta));
                    target[0] = static_cast<float>(targetDecoration.x);
                    target[1] = static_cast<float>(targetDecoration.y);
                    target[2] = static_cast<float>(targetDecoration.z);
                }
                else
                {
                    const float pixels =
                        mouseX * activeAxisDirection.x +
                        mouseY * activeAxisDirection.y;
                    target[activeAxis - 1] +=
                        pixels * activeDecoUnitsPerPixel;
                }
                MoveAnchorTo(target);
                status =
                    "Moved the decoration group with the scene manipulator.";
            }
        }
        else
        {
            const float ringWorldRadius =
                WorldSizeForScreenPixels(camera, viewport, originWorld,
                    ManipulatorUtils::RotationRingPixels);
            const double ringDecorationRadius =
                static_cast<double>(ringWorldRadius / DecorationScale);

            if (activeAxis == 0 &&
                !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
            {
                float bestDistance = 44.0f;
                for (int index = 0; index < 3; ++index)
                {
                    const float distance = DistanceToRotationRing(
                        camera,
                        viewport,
                        originDecoration,
                        accumulatedGroupRotation,
                        index,
                        ringDecorationRadius,
                        mouse
                    );
                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        hoveredAxis = index + 1;
                    }
                }
            }

            for (int index = 0; index < 3; ++index)
            {
                const bool selected =
                    hoveredAxis == index + 1 || activeAxis == index + 1;
                DrawRotationRing(
                    draw,
                    camera,
                    viewport,
                    originDecoration,
                    accumulatedGroupRotation,
                    index,
                    ringDecorationRadius,
                    selected ? highlight : baseColors[index],
                    selected ? 3.5f : 2.0f
                );
            }

            if (clicked && activeAxis == 0 && hoveredAxis != 0)
            {
                BeginHistoryAction("group rotation");
                activeAxis = hoveredAxis;
                inputCaptured = true;
                dragStartMouse = mouse;
                rotationDragStartGroupRotation =
                    accumulatedGroupRotation;
                CaptureRotationSnapshot();
            }

            if (activeAxis != 0 && mouseDown)
            {
                constexpr double DegreesToRadians =
                    0.01745329251994329577;
                const float mouseX = mouse.x - dragStartMouse.x;
                const float mouseY = mouse.y - dragStartMouse.y;
                const double deltaDegrees =
                    static_cast<double>(
                        (std::fabs(mouseX) >= std::fabs(mouseY)
                            ? mouseX
                            : -mouseY) * 0.5f
                    );
                const double direction = activeAxis == 3 ? 1.0 : -1.0;
                const Mat3 localDelta = AxisRotation(
                    activeAxis - 1,
                    deltaDegrees * direction * DegreesToRadians
                );
                const Mat3 delta = Multiply(
                    rotationDragStartGroupRotation,
                    Multiply(
                        localDelta,
                        Transpose(rotationDragStartGroupRotation)
                    )
                );
                ApplyRigidGroupRotation(delta);
                accumulatedGroupRotation = Multiply(
                    rotationDragStartGroupRotation,
                    localDelta
                );
                GroupMatrixToEulerDegrees(
                    accumulatedGroupRotation,
                    groupRotationDegrees
                );
                status =
                    "Rotated the decoration group with the scene manipulator.";
            }
        }

        if (hoveredAxis != 0 || activeAxis != 0)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::GetIO().WantCaptureMouse = true;
        }

        if (activeAxis != 0 && !mouseDown && inputCaptured)
        {
            CommitHistoryAction();
            activeAxis = 0;
            inputCaptured = false;
            rotationDragStartPositions.clear();
            rotationDragStartOrientations.clear();
        }
    }
}

void GroupMoverTab::RenderWorkspace()
{
        if (!groups.empty())
        {
            ImGui::Dummy(ImVec2(0.0f, 16.0f));
            RenderSectionHeading("Select Groups");
            ImGui::TextDisabled(
                "Select groups here, or click any orange group point in the scene."
            );
            ImGui::BeginChild("##GroupMoverGroupList", ImVec2(0.0f, 170.0f), true);
            for (size_t index = 0; index < groups.size(); ++index)
            {
                GroupInfo& group = groups[index];
                bool selected = group.selected;
                const std::string label = group.name + " (" +
                    std::to_string(group.propIndices.size()) + " decorations)##GroupMover" +
                    std::to_string(index);
                if (ImGui::Checkbox(label.c_str(), &selected))
                {
                    group.selected = selected;
                    SelectionChanged();
                }
            }
            ImGui::EndChild();
        }

        const bool hasSelectedGroups = SelectedPropCount() > 0;
        ImGui::Dummy(ImVec2(0.0f, 16.0f));
        RenderSectionHeading("Group Move");
        if (ImGui::RadioButton("Move##GroupMoverMode", &manipulatorMode, 0))
        {
            hoveredAxis = 0;
            activeAxis = 0;
            inputCaptured = false;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate##GroupMoverMode", &manipulatorMode, 1))
        {
            hoveredAxis = 0;
            activeAxis = 0;
            inputCaptured = false;
        }

        ImGui::Text("Move");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(360.0f);
        if (hasSelectedGroups)
        {
            float editedAnchor[3] =
            {
                anchorPosition[0], anchorPosition[1], anchorPosition[2]
            };
            const bool anchorEdited =
                ImGui::InputFloat3("##GroupMoverAnchor", editedAnchor, "%.3f");
            if (ImGui::IsItemActivated())
            {
                BeginHistoryAction("numeric group move");
            }
            if (anchorEdited)
            {
                MoveAnchorTo(editedAnchor);
                status = "Updated the selected group position.";
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                CommitHistoryAction();
            }
        }
        else
        {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            ImGui::InputFloat3("##GroupMoverAnchor", anchorPosition, "%.3f");
            ImGui::PopStyleVar();
            ImGui::PopItemFlag();
        }

        if (hasSelectedGroups)
        {
            if (ImGui::Button("Move to Character##GroupMover")) MoveToCharacter();
        }
        else
        {
            RenderDisabledButton("Move to Character##GroupMover");
        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGui::Text("Rotate");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(360.0f);
        if (hasSelectedGroups)
        {
            float editedRotation[3] =
            {
                groupRotationDegrees[0],
                groupRotationDegrees[1],
                groupRotationDegrees[2]
            };
            const bool rotationEdited =
                ImGui::InputFloat3("##GroupMoverRotation", editedRotation, "%.3f");
            if (ImGui::IsItemActivated())
            {
                BeginHistoryAction("numeric group rotation");
            }
            if (rotationEdited)
            {
                ApplyNumericGroupRotation(editedRotation);
                status = "Updated the selected group rotation in X, Y, Z order.";
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                CommitHistoryAction();
            }
        }
        else
        {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            ImGui::InputFloat3("##GroupMoverRotation", groupRotationDegrees, "%.3f");
            ImGui::PopStyleVar();
            ImGui::PopItemFlag();
        }

        ImGui::Dummy(ImVec2(0.0f, 16.0f));
        const float historyButtonWidth =
            (ImGui::GetContentRegionAvail().x -
                ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
        if (!undoHistory.empty())
        {
            if (ImGui::Button(
                "Undo##GroupMover",
                ImVec2(historyButtonWidth, 0.0f)
            ))
            {
                UndoTransform();
            }
        }
        else
        {
            RenderDisabledButton(
                "Undo##GroupMover",
                ImVec2(historyButtonWidth, 0.0f)
            );
        }
        ImGui::SameLine();
        if (!redoHistory.empty())
        {
            if (ImGui::Button(
                "Redo##GroupMover",
                ImVec2(historyButtonWidth, 0.0f)
            ))
            {
                RedoTransform();
            }
        }
        else
        {
            RenderDisabledButton(
                "Redo##GroupMover",
                ImVec2(historyButtonWidth, 0.0f)
            );
        }
        ImGui::SameLine();
        if (!props.empty())
        {
            if (ImGui::Button(
                "Apply to XML",
                ImVec2(historyButtonWidth, 0.0f)
            ))
            {
                ApplyToXml();
            }
        }
        else
        {
            RenderDisabledButton(
                "Apply to XML",
                ImVec2(historyButtonWidth, 0.0f)
            );
        }
        ImGui::Spacing();
        ImGui::TextDisabled("%s", status.c_str());
}

bool GroupMoverTab::ImportPath(const std::string& path)
{
    return ImportXml(path);
}

void GroupMoverTab::Render()
{
    RenderWorkspace();
}
void GroupMoverTab::RenderOverlay()
{
    if (props.empty())
    {
        hoveredAxis = 0;
        activeAxis = 0;
        inputCaptured = false;
        return;
    }

    Mumble::Data* mumble = AppRuntime::GetMumble();
    if (mumble == nullptr || mumble->Context.MapID == 0)
    {
        hoveredAxis = 0;
        activeAxis = 0;
        inputCaptured = false;
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    if (!inputCaptured && io.MousePos.x >= 0.0f && io.MousePos.y >= 0.0f)
    {
        wndMousePosition = io.MousePos;
        mouseDown = io.MouseDown[0];
    }

    const ImVec2 viewport = io.DisplaySize;
    const Camera camera = CameraFromMumble(*mumble);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    DrawPreview(camera, viewport, draw);
    if (SelectedPropCount() > 0)
    {
        DrawManipulator(camera, viewport, draw);
    }
    else
    {
        hoveredAxis = 0;
        activeAxis = 0;
    }
}

void GroupMoverTab::ClearImportedData()
{
    DecorationCounterWindow::Clear();
    std::string().swap(xmlSource);
    std::string().swap(importedPath);
    std::string().swap(importedFileName);
    std::vector<PropPosition>().swap(props);
    std::vector<GroupInfo>().swap(groups);

    anchorPosition[0] = 0.0f;
    anchorPosition[1] = 0.0f;
    anchorPosition[2] = 0.0f;
    xmlMapId = 0;
    xmlType = -1;
    selectedXmlIndex = -1;
    status = "No XML imported";

    hoveredAxis = 0;
    activeAxis = 0;
    inputCaptured = false;
    mouseDown = false;
    clickPending = false;
    pointClickPending = false;
    pointRightClickPending = false;
    pointRightClickCaptured = false;
    hoveredPointGroup = -1;
    activeDecoUnitsPerPixel = 0.0f;
    activeWorldUnitsPerPixel = 0.0f;
    manipulatorMode = 0;
    groupRotationDegrees[0] = 0.0f;
    groupRotationDegrees[1] = 0.0f;
    groupRotationDegrees[2] = 0.0f;
    accumulatedGroupRotation = IdentityMatrix();
    rotationDragStartPositions.clear();
    rotationDragStartOrientations.clear();
    rotationDragPropIndices.clear();
    ClearTransformHistory();
}

UINT GroupMoverTab::WndProc(HWND, UINT message, WPARAM, LPARAM lParam)
{
    switch (message)
    {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
        wndMousePosition = ImVec2(
            static_cast<float>(static_cast<short>(LOWORD(lParam))),
            static_cast<float>(static_cast<short>(HIWORD(lParam)))
        );
        break;
    default:
        return 1;
    }

    const bool canClaim =
        inputCaptured ||
        activeAxis != 0 ||
        hoveredAxis != 0 ||
        hoveredPointGroup >= 0;

    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK)
    {
        if (canClaim)
        {
            if (hoveredPointGroup >= 0 && activeAxis == 0 && hoveredAxis == 0)
            {
                pointClickPending = true;
            }
            else
            {
                inputCaptured = true;
                mouseDown = true;
                clickPending = true;
            }
            return 0;
        }
    }
    else if (message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK)
    {
        if (hoveredPointGroup >= 0 && activeAxis == 0 && hoveredAxis == 0)
        {
            pointRightClickPending = true;
            pointRightClickCaptured = true;
            return 0;
        }
    }
    else if (message == WM_LBUTTONUP)
    {
        mouseDown = false;
        if (inputCaptured || activeAxis != 0)
        {
            CommitHistoryAction();
            inputCaptured = false;
            activeAxis = 0;
            rotationDragStartPositions.clear();
            rotationDragStartOrientations.clear();
            rotationDragPropIndices.clear();
            return 0;
        }
    }
    else if (message == WM_RBUTTONUP)
    {
        if (pointRightClickCaptured)
        {
            pointRightClickCaptured = false;
            return 0;
        }
    }
    else if (message == WM_MOUSEMOVE &&
        (inputCaptured || activeAxis != 0 || pointRightClickCaptured))
    {
        return 0;
    }

    return 1;
}
