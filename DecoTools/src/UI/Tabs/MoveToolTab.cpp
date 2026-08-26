// Pewpew's Deco Tools - Interactive Layout Move Tool
// Imports decoration XML layouts, visualizes their bounds and decoration points
// in-game, supports interactive positioning, and exports relocated layouts.

#include "MoveToolTab.h"

#include "../../Core/AppRuntime.h"
#include "../../Core/AppSettings.h"
#include "../../Core/DecorationDatabase.h"
#include "../../Core/Utf8Paths.h"
#include "../../Core/XmlFileUtils.h"
#include "../DecorationCounterWindow.h"
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
        size_t valueStart = 0;
        size_t valueLength = 0;
        size_t rotationValueStart = 0;
        size_t rotationValueLength = 0;
        size_t tagEnd = 0;
        bool hasRotationAttribute = false;
        DVec3 position;
        DVec3 rotation;
        int id = -1;
        std::string name;
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
    std::string importedFileName;
    std::string status = "No XML imported";
    std::vector<PropPosition> props;
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
    ImVec2 wndMousePosition(0.0f, 0.0f);
    ImVec2 dragStartMouse(0.0f, 0.0f);
    float dragStartAnchor[3] = { 0.0f, 0.0f, 0.0f };
    ImVec2 activeAxisDirection(0.0f, 0.0f);
    float activeDecoUnitsPerPixel = 0.0f;
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

    void ComputeAnchor()
    {
        if (props.empty())
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
            sumX += prop.position.x;
            sumY += prop.position.y;
            bottomZ = (std::max)(bottomZ, prop.position.z);
        }

        anchorPosition[0] = static_cast<float>(sumX / props.size());
        anchorPosition[1] = static_cast<float>(sumY / props.size());
        anchorPosition[2] = static_cast<float>(bottomZ);
    }

    void MoveAnchorTo(const float target[3])
    {
        const DVec3 delta =
        {
            static_cast<double>(target[0] - anchorPosition[0]),
            static_cast<double>(target[1] - anchorPosition[1]),
            static_cast<double>(target[2] - anchorPosition[2])
        };

        for (PropPosition& prop : props)
        {
            prop.position = Add(prop.position, delta);
        }

        anchorPosition[0] = target[0];
        anchorPosition[1] = target[1];
        anchorPosition[2] = target[2];
    }

    DVec3 CalculateAveragePivot()
    {
        DVec3 pivot;
        if (props.empty())
        {
            return pivot;
        }

        for (const PropPosition& prop : props)
        {
            pivot = Add(pivot, prop.position);
        }
        return Multiply(pivot, 1.0 / static_cast<double>(props.size()));
    }

    void CaptureRotationSnapshot()
    {
        rotationDragPivot = CalculateAveragePivot();
        rotationDragStartPositions.clear();
        rotationDragStartOrientations.clear();
        rotationDragStartPositions.reserve(props.size());
        rotationDragStartOrientations.reserve(props.size());

        for (const PropPosition& prop : props)
        {
            rotationDragStartPositions.push_back(prop.position);
            rotationDragStartOrientations.push_back(
                Gw2EulerToMatrix(prop.rotation)
            );
        }
    }

    void ApplyRigidGroupRotation(const Mat3& groupRotation)
    {
        if (rotationDragStartPositions.size() != props.size() ||
            rotationDragStartOrientations.size() != props.size())
        {
            return;
        }

        const Mat3 orientationDelta = Transpose(groupRotation);
        for (size_t index = 0; index < props.size(); ++index)
        {
            const DVec3 relative = Subtract(
                rotationDragStartPositions[index],
                rotationDragPivot
            );
            props[index].position = Add(
                rotationDragPivot,
                Multiply(groupRotation, relative)
            );
            props[index].rotation = Gw2MatrixToEuler(
                Multiply(
                    rotationDragStartOrientations[index],
                    orientationDelta
                )
            );
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

    bool ImportXml(const std::string& path)
    {
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

        xmlSource = std::move(source);
        props = std::move(parsedProps);
        xmlMapId = parsedMapId;
        xmlType = typeText == "0" ? 0 : 1;
        importedFileName = Utf8Paths::ToUtf8(
            Utf8Paths::FromUtf8(path).filename()
        );
        accumulatedGroupRotation = IdentityMatrix();
        groupRotationDegrees[0] = 0.0f;
        groupRotationDegrees[1] = 0.0f;
        groupRotationDegrees[2] = 0.0f;
        rotationDragStartPositions.clear();
        rotationDragStartOrientations.clear();
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

        status =
            "Loaded " + std::to_string(props.size()) + " decorations (" +
            (xmlType == 0 ? "Homestead" : "Guild Hall") + ").";
        return true;
    }

    void MoveToCharacter()
    {
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
        MoveAnchorTo(targetArray);
        status =
            "Moved the decoration group to the character on map " +
            std::to_string(mumble->Context.MapID) + ".";
    }

    void ExportXml()
    {
        Mumble::Data* mumble = AppRuntime::GetMumble();
        if (mumble == nullptr || mumble->Context.MapID == 0)
        {
            status = "Load into the destination map before exporting.";
            return;
        }

        const MapInfo* destinationMap = FindMapInfo(mumble->Context.MapID);
        if (destinationMap == nullptr)
        {
            status =
                "The current map is not a supported Homestead or Guild Hall.";
            return;
        }

        const std::string baseName = Utf8Paths::ToUtf8(
            Utf8Paths::FromUtf8(importedFileName).stem()
        );

        std::string output = BuildUpdatedXml();
        const bool headerUpdated =
            ReplaceRootAttribute(
                output,
                "mapId",
                std::to_string(destinationMap->xmlMapId)
            ) &&
            ReplaceRootAttribute(
                output,
                "mapName",
                destinationMap->mapName
            ) &&
            ReplaceRootAttribute(
                output,
                "type",
                std::to_string(destinationMap->type)
            );

        if (!headerUpdated)
        {
            status = "Could not update the destination map information.";
            return;
        }

        const std::string destinationFolder =
            FolderForType(destinationMap->type);
        if (destinationFolder.empty())
        {
            status = "Set the destination XML folder path in Settings first.";
            return;
        }

        std::error_code error;
        std::filesystem::create_directories(destinationFolder, error);
        if (error)
        {
            status = "The destination XML folder could not be created.";
            return;
        }

        const std::filesystem::path selectedFile =
            XmlFileUtils::IndexedOperationPath(
                std::filesystem::path(destinationFolder),
                baseName,
                "_MOVED"
            );
        if (selectedFile.empty())
        {
            status = "The destination XML folder could not be checked.";
            return;
        }

        std::ofstream file(selectedFile, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            status = "Could not create the exported XML file.";
            return;
        }

        file.write(output.data(), static_cast<std::streamsize>(output.size()));
        if (!file.good())
        {
            status = "The exported XML could not be written completely.";
            return;
        }

        if (selectedFolderType == destinationMap->type)
        {
            RefreshXmlList();
        }

        status =
            "Exported " +
            Utf8Paths::ToUtf8(selectedFile.filename()) +
            " for " + destinationMap->mapName + ".";
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
            minimum.x = (std::min)(minimum.x, prop.position.x);
            minimum.y = (std::min)(minimum.y, prop.position.y);
            minimum.z = (std::min)(minimum.z, prop.position.z);
            maximum.x = (std::max)(maximum.x, prop.position.x);
            maximum.y = (std::max)(maximum.y, prop.position.y);
            maximum.z = (std::max)(maximum.z, prop.position.z);

            if (settings.showDecorationPoints)
            {
                ImVec2 point;
                if (camera.Project(DecorationToWorld(prop.position), viewport, point))
                {
                    draw->AddCircleFilled(
                        point,
                        settings.pointSize,
                        ImGui::ColorConvertFloat4ToU32(ImVec4(
                            settings.pointColor[0],
                            settings.pointColor[1],
                            settings.pointColor[2],
                            settings.pointColor[3]
                        )),
                        12
                    );
                }
            }
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

        const float distanceToCamera = Length(Subtract(originWorld, camera.position));
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
            clickPending ||
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow));
        clickPending = false;

        if (manipulatorMode == 0)
        {
            const float axisWorldLength =
                (std::max)(1.5f, distanceToCamera * 0.035f);
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

            float bestDistance = 38.0f;
            if (activeAxis == 0 &&
                !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
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

            draw->AddCircleFilled(
                originScreen,
                5.0f,
                ImGui::ColorConvertFloat4ToU32(
                    ImVec4(1.0f, 1.0f, 1.0f, 0.95f)
                ),
                12
            );

            if (clicked && activeAxis == 0 && hoveredAxis != 0)
            {
                activeAxis = hoveredAxis;
                inputCaptured = true;
                dragStartMouse = mouse;
                dragStartAnchor[0] = anchorPosition[0];
                dragStartAnchor[1] = anchorPosition[1];
                dragStartAnchor[2] = anchorPosition[2];

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

            if (activeAxis != 0 && mouseDown)
            {
                const float mouseX = mouse.x - dragStartMouse.x;
                const float mouseY = mouse.y - dragStartMouse.y;
                const float pixels =
                    mouseX * activeAxisDirection.x +
                    mouseY * activeAxisDirection.y;

                float target[3] =
                {
                    dragStartAnchor[0],
                    dragStartAnchor[1],
                    dragStartAnchor[2]
                };
                target[activeAxis - 1] +=
                    pixels * activeDecoUnitsPerPixel;
                MoveAnchorTo(target);
                status =
                    "Moved the decoration group with the scene manipulator.";
            }
        }
        else
        {
            const float ringWorldRadius =
                (std::max)(1.0f, distanceToCamera * 0.025f);
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
            activeAxis = 0;
            inputCaptured = false;
            rotationDragStartPositions.clear();
            rotationDragStartOrientations.clear();
        }
    }
}

void MoveToolTab::Render()
{
    AppSettings::Data& settings = AppSettings::Get();
    const bool hasXml = !props.empty();
    InitializeXmlList();

    RenderSectionHeading("Import");

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::Text("Import Decoration XML");

    if (ImGui::RadioButton("Homestead", &selectedFolderType, 0))
    {
        RefreshXmlList();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Guild Hall", &selectedFolderType, 1))
    {
        RefreshXmlList();
    }

    const bool hasSelection =
        selectedXmlIndex >= 0 &&
        selectedXmlIndex < static_cast<int>(availableXmlFiles.size());
    const char* selectedName = hasSelection
        ? availableXmlFiles[static_cast<size_t>(selectedXmlIndex)].name.c_str()
        : "No XML files available";

    ImGui::SetNextItemWidth(-1.0f);
    XmlComboHelpers::SetPopupWidth(availableXmlFiles);
    if (ImGui::BeginCombo("##XmlFileList", selectedName))
    {
        for (size_t index = 0; index < availableXmlFiles.size(); ++index)
        {
            const bool selected =
                selectedXmlIndex == static_cast<int>(index);
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(
                availableXmlFiles[index].name.c_str(),
                selected
            ))
            {
                selectedXmlIndex = static_cast<int>(index);
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    const float actionWidth =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) *
        0.5f;
    if (ImGui::Button("Refresh List", ImVec2(actionWidth, 0.0f)))
    {
        RefreshXmlList();
    }
    ImGui::SameLine();
    if (hasSelection)
    {
        if (ImGui::Button("Import Selected", ImVec2(actionWidth, 0.0f)))
        {
            ImportXml(
                availableXmlFiles[static_cast<size_t>(selectedXmlIndex)].path
            );
        }
    }
    else
    {
        RenderDisabledButton(
            "Import Selected",
            ImVec2(actionWidth, 0.0f)
        );
    }

    ImGui::TextDisabled("%s", status.c_str());
    ImGui::Spacing();

    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    RenderSectionHeading("Position & Rotation");

    if (ImGui::RadioButton("Move", &manipulatorMode, 0))
    {
        hoveredAxis = 0;
        activeAxis = 0;
        inputCaptured = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", &manipulatorMode, 1))
    {
        hoveredAxis = 0;
        activeAxis = 0;
        inputCaptured = false;
    }

    ImGui::Text("Move");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(360.0f);
    if (hasXml)
    {
        float editedAnchor[3] =
        {
            anchorPosition[0],
            anchorPosition[1],
            anchorPosition[2]
        };
        if (ImGui::InputFloat3("##AnchorPosition", editedAnchor, "%.3f"))
        {
            MoveAnchorTo(editedAnchor);
            status = "Updated the decoration group position.";
        }
    }
    else
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        ImGui::InputFloat3("##AnchorPosition", anchorPosition, "%.3f");
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }

    if (hasXml)
    {
        if (ImGui::Button("Move to Character"))
        {
            MoveToCharacter();
        }
    }
    else
    {
        RenderDisabledButton("Move to Character");
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::Text("Rotate");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(360.0f);
    if (hasXml)
    {
        float editedRotation[3] =
        {
            groupRotationDegrees[0],
            groupRotationDegrees[1],
            groupRotationDegrees[2]
        };
        if (ImGui::InputFloat3(
            "##GroupRotation",
            editedRotation,
            "%.3f"
        ))
        {
            ApplyNumericGroupRotation(editedRotation);
            status =
                "Updated group rotation in X, Y, Z order.";
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Degrees. Numeric values apply in X, Y, Z order."
            );
        }
    }
    else
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            ImGui::GetStyle().Alpha * 0.5f
        );
        ImGui::InputFloat3(
            "##GroupRotation",
            groupRotationDegrees,
            "%.3f"
        );
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }

    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    RenderSectionHeading("Preview");

    if (ImGui::Checkbox("Show Bounding Box", &settings.showBoundingBox))
    {
        AppSettings::MarkDirty();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Show Solid Faces", &settings.showSolidFaces))
    {
        AppSettings::MarkDirty();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Show Decoration Points", &settings.showDecorationPoints))
    {
        AppSettings::MarkDirty();
    }

    if (ImGui::ColorEdit4(
        "Box Color",
        settings.boxColor,
        ImGuiColorEditFlags_NoInputs
    ))
    {
        AppSettings::MarkDirty();
    }
    if (ImGui::ColorEdit4(
        "Face Color",
        settings.faceColor,
        ImGuiColorEditFlags_NoInputs
    ))
    {
        AppSettings::MarkDirty();
    }
    if (ImGui::ColorEdit4(
        "Point Color",
        settings.pointColor,
        ImGuiColorEditFlags_NoInputs
    ))
    {
        AppSettings::MarkDirty();
    }

    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::SliderFloat(
        "Point Size",
        &settings.pointSize,
        1.0f,
        12.0f,
        "%.0f px"
    ))
    {
        AppSettings::MarkDirty();
    }

    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    if (hasXml)
    {
        if (ImGui::Button("Export Updated XML"))
        {
            ExportXml();
        }
    }
    else
    {
        RenderDisabledButton("Export Updated XML");
    }
}

void MoveToolTab::RenderOverlay()
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
    DrawManipulator(camera, viewport, draw);
}

void MoveToolTab::ClearImportedData()
{
    DecorationCounterWindow::Clear();
    std::string().swap(xmlSource);
    std::string().swap(importedFileName);
    std::vector<PropPosition>().swap(props);

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
    activeDecoUnitsPerPixel = 0.0f;
    manipulatorMode = 0;
    groupRotationDegrees[0] = 0.0f;
    groupRotationDegrees[1] = 0.0f;
    groupRotationDegrees[2] = 0.0f;
    accumulatedGroupRotation = IdentityMatrix();
    rotationDragStartPositions.clear();
    rotationDragStartOrientations.clear();
}

UINT MoveToolTab::WndProc(HWND, UINT message, WPARAM, LPARAM lParam)
{
    switch (message)
    {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
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
        hoveredAxis != 0;

    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK)
    {
        if (canClaim)
        {
            inputCaptured = true;
            mouseDown = true;
            clickPending = true;
            return 0;
        }
    }
    else if (message == WM_LBUTTONUP)
    {
        mouseDown = false;
        if (inputCaptured || activeAxis != 0)
        {
            inputCaptured = false;
            activeAxis = 0;
            rotationDragStartPositions.clear();
            rotationDragStartOrientations.clear();
            return 0;
        }
    }
    else if (message == WM_MOUSEMOVE && (inputCaptured || activeAxis != 0))
    {
        return 0;
    }

    return 1;
}
