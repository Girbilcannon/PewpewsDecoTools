// Pewpew's Deco Tools - Procedural Patterns Tool
// Builds line, circle, square, and cube repetitions from an imported XML unit,
// previews immutable-source transforms in game, and exports indexed PATTERN XMLs.

#include "PatternsTab.h"

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
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr float DecorationScale = 0.025400052f;
    constexpr float NearClip = 0.05f;
    constexpr float DefaultFovRadians = 0.872664626f;
    constexpr double DegreesToRadians = 0.01745329251994329577;

    struct Vec3 { float x = 0.0f; float y = 0.0f; float z = 0.0f; };
    struct DVec3 { double x = 0.0; double y = 0.0; double z = 0.0; };
    struct Mat3 { double m[3][3] = {}; };

    struct SourceProp
    {
        size_t positionStart = 0;
        size_t positionLength = 0;
        size_t rotationStart = 0;
        size_t rotationLength = 0;
        size_t tagEnd = 0;
        size_t elementStart = 0;
        size_t elementEnd = 0;
        bool hasRotation = false;
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
        size_t commentStart = 0;
        size_t commentEnd = 0;
        size_t contentEnd = 0;
        size_t insertStart = 0;
        bool selected = false;
    };

    struct Instance
    {
        DVec3 offset;
        Mat3 patternFacing;
    };

    struct GeneratedProp
    {
        DVec3 position;
        DVec3 rotation;
        size_t sourceIndex = 0;
        size_t instanceIndex = 0;
    };

    struct Camera
    {
        Vec3 position;
        Vec3 forward;
        Vec3 up;
        Vec3 right;
        float fovRadians = DefaultFovRadians;
        bool Project(Vec3 world, ImVec2 viewport, ImVec2& screen) const;
    };

    using XmlFileEntry = XmlFileUtils::Entry;

    enum class SourceMode { FullXml, XmlGroups };
    enum class PatternType { Line, Circle, Square, Cube };
    enum class SquareOrigin { Corner, Edge, Center };

    std::string xmlSource;
    std::string importedPath;
    std::string importedFileName;
    std::string status = "No XML imported";
    std::vector<SourceProp> allSourceProps;
    std::vector<SourceProp> sourceProps;
    std::vector<GroupInfo> groups;
    std::vector<Instance> instances;
    std::vector<GeneratedProp> generatedProps;
    std::vector<XmlFileEntry> availableXmlFiles;
    size_t copyInsertStart = 0;
    int xmlType = -1;
    int selectedFolderType = 0;
    int selectedXmlIndex = -1;
    bool fileListInitialized = false;
    bool listedSubFolders = false;
    SourceMode sourceMode = SourceMode::FullXml;
    int selectedGroupIndex = -1;

    PatternType patternType = PatternType::Line;
    SquareOrigin squareOrigin = SquareOrigin::Corner;
    int lineCopies = 3;
    bool lineFromCenter = false;
    float lineStep[3] = { 300.0f, 0.0f, 0.0f };
    int circleCount = 6;
    int circleSweep = 360;
    float circleRadius = 300.0f;
    float circleVerticalStep = 0.0f;
    bool circleKeepOrientation = false;
    int squareX = 3;
    int squareY = 3;
    int squareCenterX = 1;
    int squareCenterY = 1;
    float squareSpacing[2] = { 100.0f, 100.0f };
    float squareVerticalOffset = 0.0f;
    int cubeCount[3] = { 2, 2, 2 };
    float cubeSpacing[3] = { 100.0f, 100.0f, 100.0f };

    DVec3 sourcePivot;
    DVec3 translation;
    DVec3 unrotatedPatternPivot;
    DVec3 mainCenter;
    DVec3 patternWorldPivot;
    DVec3 offsetHandleCenter;
    DVec3 centerHeightHandleCenter;
    int referenceInstance = -1;
    Mat3 objectRotation = { { {1,0,0}, {0,1,0}, {0,0,1} } };
    Mat3 wholePatternRotation = { { {1,0,0}, {0,1,0}, {0,0,1} } };
    float objectRotationDegrees[3] = { 0.0f, 0.0f, 0.0f };
    float patternRotationDegrees[3] = { 0.0f, 0.0f, 0.0f };
    int operationMode = 0;

    int hoveredControl = 0;
    int activeControl = 0;
    int activeAxis = 0;
    bool inputCaptured = false;
    bool mouseDown = false;
    bool clickPending = false;
    ImVec2 wndMousePosition(0.0f, 0.0f);
    ImVec2 dragStartMouse(0.0f, 0.0f);
    ImVec2 activeAxisDirection(0.0f, 0.0f);
    float activeDecoUnitsPerPixel = 0.0f;
    float activeWorldUnitsPerPixel = 0.0f;
    Vec3 dragViewRight;
    Vec3 dragViewUp;
    DVec3 dragStartTranslation;
    float dragStartLineStep[3] = {};
    float dragStartSquareSpacing[2] = {};
    float dragStartSquareVertical = 0.0f;
    float dragStartCubeSpacing[3] = {};
    float dragStartCircleRadius = 0.0f;
    float dragStartCircleVertical = 0.0f;
    Mat3 dragStartRotation;

    struct PatternState
    {
        PatternType patternType = PatternType::Line;
        SquareOrigin squareOrigin = SquareOrigin::Corner;
        int lineCopies = 0;
        bool lineFromCenter = false;
        float lineStep[3] = {};
        int circleCount = 0;
        int circleSweep = 0;
        float circleRadius = 0.0f;
        float circleVerticalStep = 0.0f;
        bool circleKeepOrientation = false;
        int squareX = 0;
        int squareY = 0;
        int squareCenterX = 0;
        int squareCenterY = 0;
        float squareSpacing[2] = {};
        float squareVerticalOffset = 0.0f;
        int cubeCount[3] = {};
        float cubeSpacing[3] = {};
        DVec3 translation;
        Mat3 objectRotation;
        Mat3 wholePatternRotation;
        float objectRotationDegrees[3] = {};
        float patternRotationDegrees[3] = {};
    };

    struct PatternHistoryEntry
    {
        std::string label;
        PatternState before;
        PatternState after;
    };

    constexpr size_t MaxPatternHistory = 100;
    std::vector<PatternHistoryEntry> undoHistory;
    std::vector<PatternHistoryEntry> redoHistory;
    PatternState pendingHistoryBefore;
    std::string pendingHistoryLabel;
    bool historyActionActive = false;
    bool uiHistoryActive = false;
    int hoveredPointGroup = -1;
    bool pointClickPending = false;
    bool pointRightClickPending = false;
    bool pointRightClickCaptured = false;
    bool groupApplyWritten = false;

    Vec3 Add(Vec3 a, Vec3 b) { return { a.x+b.x, a.y+b.y, a.z+b.z }; }
    Vec3 Subtract(Vec3 a, Vec3 b) { return { a.x-b.x, a.y-b.y, a.z-b.z }; }
    Vec3 Multiply(Vec3 v, float s) { return { v.x*s, v.y*s, v.z*s }; }
    DVec3 Add(DVec3 a, DVec3 b) { return { a.x+b.x, a.y+b.y, a.z+b.z }; }
    DVec3 Subtract(DVec3 a, DVec3 b) { return { a.x-b.x, a.y-b.y, a.z-b.z }; }
    DVec3 Multiply(DVec3 v, double s) { return { v.x*s, v.y*s, v.z*s }; }
    float Dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
    Vec3 Cross(Vec3 a, Vec3 b)
    {
        return { a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x };
    }
    float Length(Vec3 v) { return std::sqrt(Dot(v,v)); }
    Vec3 Normalize(Vec3 v, Vec3 fallback)
    {
        const float length = Length(v);
        return length <= 0.000001f ? fallback : Multiply(v, 1.0f/length);
    }
    Vec3 FromMumble(const Mumble::Vector3& v) { return { v.X, v.Y, v.Z }; }
    Vec3 DecorationToWorld(DVec3 d)
    {
        return { static_cast<float>(d.x*DecorationScale),
            static_cast<float>(-d.z*DecorationScale),
            static_cast<float>(d.y*DecorationScale) };
    }
    DVec3 WorldToDecoration(Vec3 world)
    {
        return { world.x/DecorationScale,world.z/DecorationScale,-world.y/DecorationScale };
    }

    Mat3 IdentityMatrix()
    {
        return { { {1,0,0}, {0,1,0}, {0,0,1} } };
    }
    Mat3 Multiply(const Mat3& a, const Mat3& b)
    {
        Mat3 r = {};
        for (int row=0; row<3; ++row)
            for (int col=0; col<3; ++col)
                r.m[row][col] = a.m[row][0]*b.m[0][col] +
                    a.m[row][1]*b.m[1][col] + a.m[row][2]*b.m[2][col];
        return r;
    }
    DVec3 Multiply(const Mat3& m, DVec3 v)
    {
        return { m.m[0][0]*v.x+m.m[0][1]*v.y+m.m[0][2]*v.z,
            m.m[1][0]*v.x+m.m[1][1]*v.y+m.m[1][2]*v.z,
            m.m[2][0]*v.x+m.m[2][1]*v.y+m.m[2][2]*v.z };
    }
    Mat3 Transpose(const Mat3& m)
    {
        Mat3 r = {};
        for (int row=0; row<3; ++row)
            for (int col=0; col<3; ++col) r.m[row][col]=m.m[col][row];
        return r;
    }
    Mat3 RotationX(double a)
    {
        Mat3 r=IdentityMatrix(); const double c=std::cos(a),s=std::sin(a);
        r.m[1][1]=c; r.m[1][2]=-s; r.m[2][1]=s; r.m[2][2]=c; return r;
    }
    Mat3 RotationY(double a)
    {
        Mat3 r=IdentityMatrix(); const double c=std::cos(a),s=std::sin(a);
        r.m[0][0]=c; r.m[0][2]=s; r.m[2][0]=-s; r.m[2][2]=c; return r;
    }
    Mat3 RotationZ(double a)
    {
        Mat3 r=IdentityMatrix(); const double c=std::cos(a),s=std::sin(a);
        r.m[0][0]=c; r.m[0][1]=-s; r.m[1][0]=s; r.m[1][1]=c; return r;
    }
    Mat3 AxisRotation(int axis, double radians)
    {
        return axis==0 ? RotationX(radians) : axis==1 ? RotationY(radians) : RotationZ(radians);
    }
    double NormalizeRadians(double a)
    {
        constexpr double TwoPi=6.28318530717958647692;
        a=std::fmod(a,TwoPi); return a<0.0 ? a+TwoPi : a;
    }
    Mat3 Gw2EulerToMatrix(DVec3 r)
    {
        return Multiply(RotationZ(r.z), Multiply(RotationX(r.x),RotationY(r.y)));
    }
    DVec3 Gw2MatrixToEuler(const Mat3& m)
    {
        constexpr double Epsilon=1.0e-8;
        const double sx=std::clamp(m.m[2][1],-1.0,1.0);
        const double x=std::asin(sx), cx=std::cos(x);
        double y=0.0,z=0.0;
        if (std::abs(cx)>Epsilon)
        {
            y=std::atan2(-m.m[2][0],m.m[2][2]);
            z=std::atan2(-m.m[0][1],m.m[1][1]);
        }
        else y=std::atan2(m.m[0][2],m.m[0][0]);
        return { NormalizeRadians(x),NormalizeRadians(y),NormalizeRadians(z) };
    }
    Mat3 DegreesToMatrix(const float degrees[3])
    {
        return Multiply(RotationZ(degrees[2]*DegreesToRadians),
            Multiply(RotationY(degrees[1]*DegreesToRadians),
                RotationX(degrees[0]*DegreesToRadians)));
    }
    void MatrixToDegrees(const Mat3& m, float degrees[3])
    {
        constexpr double Epsilon=1.0e-8, R2D=57.2957795130823208768;
        const double sy=std::clamp(-m.m[2][0],-1.0,1.0);
        const double y=std::asin(sy), cy=std::cos(y);
        double x=0.0,z=0.0;
        if (std::abs(cy)>Epsilon)
        {
            x=std::atan2(m.m[2][1],m.m[2][2]);
            z=std::atan2(m.m[1][0],m.m[0][0]);
        }
        else x=std::atan2(-m.m[1][2],m.m[1][1]);
        degrees[0]=static_cast<float>(x*R2D);
        degrees[1]=static_cast<float>(y*R2D);
        degrees[2]=static_cast<float>(z*R2D);
    }

    Camera CameraFromMumble(const Mumble::Data& mumble)
    {
        Camera camera;
        camera.position=FromMumble(mumble.CameraPosition);
        camera.forward=Normalize(FromMumble(mumble.CameraFront),{0,0,1});
        const Vec3 worldUp={0,1,0};
        camera.right=Normalize(Cross(worldUp,camera.forward),{1,0,0});
        camera.up=Normalize(Cross(camera.forward,camera.right),worldUp);
        const Mumble::Identity* identity=AppRuntime::GetMumbleIdentity();
        if (identity!=nullptr && std::isfinite(identity->FOV) && identity->FOV>0.1f && identity->FOV<3.0f)
            camera.fovRadians=identity->FOV;
        return camera;
    }
    bool Camera::Project(Vec3 world, ImVec2 viewport, ImVec2& screen) const
    {
        if (viewport.x<=1.0f || viewport.y<=1.0f) return false;
        const Vec3 relative=Subtract(world,position);
        const float x=Dot(relative,right), y=Dot(relative,up), z=Dot(relative,forward);
        if (z<=NearClip) return false;
        const float focal=(viewport.y*0.5f)/std::tan(fovRadians*0.5f);
        screen.x=viewport.x*0.5f+x*focal/z;
        screen.y=viewport.y*0.5f-y*focal/z;
        return screen.x>=-4000 && screen.x<=viewport.x+4000 &&
            screen.y>=-4000 && screen.y<=viewport.y+4000;
    }
    float WorldSizeForScreenPixels(const Camera& camera,ImVec2 viewport,
        Vec3 world,float pixels)
    {
        return ManipulatorUtils::WorldSizeForScreenPixels(
            Dot(Subtract(world,camera.position),camera.forward),
            NearClip,viewport.y,camera.fovRadians,pixels);
    }

    void RenderSectionHeading(const char* label)
    {
        ImGui::SetWindowFontScale(1.2f); ImGui::Text("%s",label);
        ImGui::SetWindowFontScale(1.0f); ImGui::Separator();
    }
    void RenderDisabledButton(const char* label,const ImVec2& size=ImVec2())
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled,true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*0.5f);
        ImGui::Button(label,size); ImGui::PopStyleVar(); ImGui::PopItemFlag();
    }
    void RenderSweepMarks()
    {
        const ImVec2 minimum=ImGui::GetItemRectMin();
        const ImVec2 maximum=ImGui::GetItemRectMax();
        ImDrawList* draw=ImGui::GetWindowDrawList();
        const ImU32 tickColor=ImGui::GetColorU32(ImGuiCol_TextDisabled);
        constexpr int marks[3]={360,720,1080};
        for (int mark:marks)
        {
            const float amount=static_cast<float>(mark-1)/1079.0f;
            const float x=minimum.x+(maximum.x-minimum.x)*amount;
            draw->AddLine({x,maximum.y+2.0f},{x,maximum.y+7.0f},tickColor,1.0f);
            const std::string label=std::to_string(mark);
            const ImVec2 textSize=ImGui::CalcTextSize(label.c_str());
            float textX=x-textSize.x*0.5f;
            textX=(std::max)(minimum.x,(std::min)(textX,maximum.x-textSize.x));
            draw->AddText({textX,maximum.y+8.0f},tickColor,label.c_str());
        }
        ImGui::Dummy({0.0f,22.0f});
    }
    bool ParseFloat3(const std::string& value,DVec3& result)
    {
        std::istringstream stream(value); std::string extra;
        return static_cast<bool>(stream>>result.x>>result.y>>result.z) && !(stream>>extra);
    }
    std::string Trim(const std::string& value)
    {
        size_t first=0;
        while (first<value.size() && std::isspace(static_cast<unsigned char>(value[first]))!=0) ++first;
        size_t last=value.size();
        while (last>first && std::isspace(static_cast<unsigned char>(value[last-1]))!=0) --last;
        return value.substr(first,last-first);
    }
    size_t FindTagEnd(const std::string& source,size_t start)
    {
        char quote='\0';
        for (size_t i=start;i<source.size();++i)
        {
            const char c=source[i];
            if (quote!='\0') { if (c==quote) quote='\0'; continue; }
            if (c=='"' || c=='\'') quote=c; else if (c=='>') return i;
        }
        return std::string::npos;
    }
    bool ReadAttribute(const std::string& source,size_t tagStart,size_t tagEnd,
        const char* attribute,std::string& value,size_t* valueStart=nullptr,size_t* valueLength=nullptr)
    {
        const std::string name(attribute); size_t position=tagStart;
        while (true)
        {
            position=source.find(name,position);
            if (position==std::string::npos || position>=tagEnd) return false;
            const bool validBefore=position==tagStart ||
                std::isspace(static_cast<unsigned char>(source[position-1]))!=0;
            size_t equals=position+name.size();
            while (equals<tagEnd && std::isspace(static_cast<unsigned char>(source[equals]))!=0) ++equals;
            if (!validBefore || equals>=tagEnd || source[equals]!='=') { position+=name.size(); continue; }
            ++equals;
            while (equals<tagEnd && std::isspace(static_cast<unsigned char>(source[equals]))!=0) ++equals;
            if (equals>=tagEnd || (source[equals]!='"' && source[equals]!='\'')) return false;
            const char quote=source[equals]; const size_t start=equals+1;
            const size_t end=source.find(quote,start);
            if (end==std::string::npos || end>tagEnd) return false;
            value=source.substr(start,end-start);
            if (valueStart) *valueStart=start;
            if (valueLength) *valueLength=end-start;
            return true;
        }
    }
    bool IsSelfClosing(const std::string& source,size_t start,size_t tagEnd)
    {
        size_t i=tagEnd;
        while (i>start && std::isspace(static_cast<unsigned char>(source[i-1]))!=0) --i;
        return i>start && source[i-1]=='/';
    }

    std::string FolderForType(int type)
    {
        const AppSettings::Data& settings=AppSettings::Get();
        return type==1 ? settings.guildHallFolder.data() : settings.homesteadFolder.data();
    }

    void RefreshXmlList()
    {
        const std::string folder=FolderForType(selectedFolderType);
        const std::string previous=selectedXmlIndex>=0 && selectedXmlIndex<static_cast<int>(availableXmlFiles.size())
            ? availableXmlFiles[static_cast<size_t>(selectedXmlIndex)].path : std::string();
        availableXmlFiles.clear(); selectedXmlIndex=-1; fileListInitialized=true;
        listedSubFolders=AppSettings::Get().showXmlsFromSubFolders;
        if (folder.empty()) { status="Set this XML folder path in Settings first."; return; }
        if (!XmlFileUtils::List(folder,listedSubFolders,availableXmlFiles))
        { status="XML folder not found or could not be read. Check the path in Settings."; return; }
        for (size_t i=0;i<availableXmlFiles.size();++i)
            if (availableXmlFiles[i].path==previous) { selectedXmlIndex=static_cast<int>(i); break; }
        if (selectedXmlIndex<0 && !availableXmlFiles.empty()) selectedXmlIndex=0;
        status=availableXmlFiles.empty() ? "No XML files found in this folder." :
            "Found "+std::to_string(availableXmlFiles.size())+
            (availableXmlFiles.size()==1 ? " XML file." : " XML files.");
    }
    void InitializeXmlList()
    {
        if (!fileListInitialized || listedSubFolders!=AppSettings::Get().showXmlsFromSubFolders)
            RefreshXmlList();
    }

    int FindInstanceAtOffset(DVec3 offset)
    {
        for (size_t i=0;i<instances.size();++i)
            if (std::abs(instances[i].offset.x-offset.x)<0.001 &&
                std::abs(instances[i].offset.y-offset.y)<0.001 &&
                std::abs(instances[i].offset.z-offset.z)<0.001) return static_cast<int>(i);
        return -1;
    }

    void BuildInstances()
    {
        instances.clear(); referenceInstance=-1;
        const Mat3 identity=IdentityMatrix();
        instances.push_back({{},identity});
        if (patternType==PatternType::Line)
        {
            for (int copy=1;copy<=lineCopies;++copy)
            {
                const double amount=static_cast<double>(copy)/static_cast<double>(lineCopies);
                const DVec3 offset={lineStep[0]*amount,lineStep[1]*amount,lineStep[2]*amount};
                instances.push_back({offset,identity});
                if (copy==lineCopies) referenceInstance=static_cast<int>(instances.size()-1);
                if (lineFromCenter) instances.push_back({Multiply(offset,-1.0),identity});
            }
        }
        else if (patternType==PatternType::Circle)
        {
            const bool closedFlatCircle=circleSweep==360 && std::abs(circleVerticalStep)<0.000001f;
            const double angleDivisor=closedFlatCircle
                ? static_cast<double>(circleCount)
                : static_cast<double>((std::max)(1,circleCount-1));
            const double verticalDivisor=static_cast<double>((std::max)(1,circleCount-1));
            for (int copy=1;copy<circleCount;++copy)
            {
                const double angle=static_cast<double>(circleSweep)*copy/angleDivisor*DegreesToRadians;
                const double verticalAmount=static_cast<double>(copy)/verticalDivisor;
                const DVec3 offset={circleRadius*(std::cos(angle)-1.0),
                    circleRadius*std::sin(angle),circleVerticalStep*verticalAmount};
                instances.push_back({offset,circleKeepOrientation ? identity : RotationZ(angle)});
                if (copy==1) referenceInstance=static_cast<int>(instances.size()-1);
            }
        }
        else if (patternType==PatternType::Square)
        {
            if (squareOrigin==SquareOrigin::Center)
            {
                const int mainLevel=(std::min)(squareCenterX,squareCenterY);
                instances[0].offset.z=squareVerticalOffset*mainLevel;
                for (int y=-squareCenterY;y<=squareCenterY;++y)
                {
                    for (int x=-squareCenterX;x<=squareCenterX;++x)
                    {
                        if (x==0 && y==0) continue;
                        const int level=(std::min)(squareCenterX-std::abs(x),squareCenterY-std::abs(y));
                        instances.push_back({{x*squareSpacing[0],y*squareSpacing[1],
                            level*squareVerticalOffset},identity});
                    }
                }
                const int rx=squareCenterX>0 ? 1 : 0;
                const int ry=squareCenterY>0 ? 1 : 0;
                const int referenceLevel=(std::min)(squareCenterX-rx,squareCenterY-ry);
                referenceInstance=FindInstanceAtOffset({rx*squareSpacing[0],ry*squareSpacing[1],
                    referenceLevel*squareVerticalOffset});
                if (referenceInstance<0 && instances.size()>1) referenceInstance=1;
            }
            else
            {
                for (int y=0;y<squareY;++y)
                    for (int x=0;x<squareX;++x)
                    {
                        if (x==0 && y==0) continue;
                        const int level=squareOrigin==SquareOrigin::Corner ? x+y : y;
                        instances.push_back({{x*squareSpacing[0],y*squareSpacing[1],
                            level*squareVerticalOffset},identity});
                    }
                const int rx=squareX>1 ? 1 : 0, ry=squareY>1 ? 1 : 0;
                referenceInstance=FindInstanceAtOffset({rx*squareSpacing[0],ry*squareSpacing[1],
                    (squareOrigin==SquareOrigin::Corner ? rx+ry : ry)*squareVerticalOffset});
                if (referenceInstance<0 && instances.size()>1) referenceInstance=1;
            }
        }
        else
        {
            for (int z=0;z<cubeCount[2];++z)
                for (int y=0;y<cubeCount[1];++y)
                    for (int x=0;x<cubeCount[0];++x)
                    {
                        if (x==0 && y==0 && z==0) continue;
                        instances.push_back({{x*cubeSpacing[0],y*cubeSpacing[1],z*cubeSpacing[2]},identity});
                    }
            const int rx=cubeCount[0]>1 ? 1 : 0, ry=cubeCount[1]>1 ? 1 : 0, rz=cubeCount[2]>1 ? 1 : 0;
            referenceInstance=FindInstanceAtOffset({rx*cubeSpacing[0],ry*cubeSpacing[1],rz*cubeSpacing[2]});
            if (referenceInstance<0 && instances.size()>1) referenceInstance=1;
        }
    }

    void UpdateCounter()
    {
        std::map<int,DecorationCounterWindow::Requirement> byId;
        if (sourceMode==SourceMode::XmlGroups)
        {
            for (const SourceProp& prop:allSourceProps)
            {
                auto& item=byId[prop.id]; item.id=prop.id; item.name=prop.name;
                ++item.required;
            }
            const int extraCopies=(std::max)(0,static_cast<int>(instances.size())-1);
            for (const SourceProp& prop:sourceProps)
            {
                auto& item=byId[prop.id]; item.id=prop.id; item.name=prop.name;
                item.required+=extraCopies;
            }
        }
        else
        {
            for (const SourceProp& prop:sourceProps)
            {
                auto& item=byId[prop.id]; item.id=prop.id; item.name=prop.name;
                item.required+=static_cast<int>(instances.size());
            }
        }
        std::vector<DecorationCounterWindow::Requirement> output;
        for (const auto& [id,item]:byId) { static_cast<void>(id); output.push_back(item); }
        DecorationCounterWindow::UpdateRequirements(importedFileName+" Pattern",output);
    }

    void RebuildPattern(bool updateCounter=true)
    {
        generatedProps.clear();
        if (sourceProps.empty()) return;
        BuildInstances();
        if (patternType==PatternType::Line && lineCopies>0)
        {
            unrotatedPatternPivot=lineFromCenter
                ? sourcePivot
                : Add(sourcePivot,{lineStep[0]*0.5,lineStep[1]*0.5,lineStep[2]*0.5});
        }
        else if (patternType==PatternType::Circle)
        {
            const double sweepRadians=static_cast<double>(circleSweep)*DegreesToRadians;
            const double averageCos=std::sin(sweepRadians)/sweepRadians;
            const double averageSin=(1.0-std::cos(sweepRadians))/sweepRadians;
            unrotatedPatternPivot=Add(sourcePivot,{
                circleRadius*(averageCos-1.0),
                circleRadius*averageSin,
                circleVerticalStep*0.5
            });
        }
        else
        {
            unrotatedPatternPivot={};
            for (const Instance& instance:instances)
                unrotatedPatternPivot=Add(unrotatedPatternPivot,Add(sourcePivot,instance.offset));
            unrotatedPatternPivot=Multiply(unrotatedPatternPivot,1.0/static_cast<double>(instances.size()));
        }
        patternWorldPivot=Add(unrotatedPatternPivot,translation);
        generatedProps.reserve(sourceProps.size()*instances.size());
        for (size_t instanceIndex=0;instanceIndex<instances.size();++instanceIndex)
        {
            const Instance& instance=instances[instanceIndex];
            const Mat3 localRotation=Multiply(objectRotation,instance.patternFacing);
            const DVec3 center=Add(sourcePivot,instance.offset);
            for (size_t sourceIndex=0;sourceIndex<sourceProps.size();++sourceIndex)
            {
                const SourceProp& source=sourceProps[sourceIndex];
                const DVec3 local=Multiply(localRotation,Subtract(source.position,sourcePivot));
                const DVec3 beforePattern=Add(center,local);
                GeneratedProp generated;
                generated.position=Add(patternWorldPivot,
                    Multiply(wholePatternRotation,Subtract(beforePattern,unrotatedPatternPivot)));
                Mat3 orientation=Multiply(Gw2EulerToMatrix(source.rotation),Transpose(localRotation));
                orientation=Multiply(orientation,Transpose(wholePatternRotation));
                generated.rotation=Gw2MatrixToEuler(orientation);
                generated.sourceIndex=sourceIndex;
                generated.instanceIndex=instanceIndex;
                generatedProps.push_back(generated);
            }
        }
        auto transformedCenter=[&](int index)
        {
            const DVec3 center=Add(sourcePivot,instances[static_cast<size_t>(index)].offset);
            return Add(patternWorldPivot,Multiply(wholePatternRotation,Subtract(center,unrotatedPatternPivot)));
        };
        mainCenter=transformedCenter(0);
        offsetHandleCenter=referenceInstance>=0 ? transformedCenter(referenceInstance) : mainCenter;
        const double side=(std::max)(25.0,std::abs(static_cast<double>(squareSpacing[0]))*0.2);
        centerHeightHandleCenter=Add(
            mainCenter,
            Multiply(wholePatternRotation,DVec3{side,0,0})
        );
        if (updateCounter) UpdateCounter();
    }

    void RebuildPatternPreservingMain(bool updateCounter=true)
    {
        const bool preserveMain=!generatedProps.empty();
        const DVec3 fixedMain=mainCenter;
        RebuildPattern(false);
        if (preserveMain)
        {
            translation=Add(translation,Subtract(fixedMain,mainCenter));
            RebuildPattern(updateCounter);
        }
        else if (updateCounter)
        {
            UpdateCounter();
        }
    }

    PatternState CapturePatternState()
    {
        PatternState state;
        state.patternType=patternType; state.squareOrigin=squareOrigin;
        state.lineCopies=lineCopies; state.lineFromCenter=lineFromCenter;
        std::copy(std::begin(lineStep),std::end(lineStep),std::begin(state.lineStep));
        state.circleCount=circleCount; state.circleSweep=circleSweep;
        state.circleRadius=circleRadius; state.circleVerticalStep=circleVerticalStep;
        state.circleKeepOrientation=circleKeepOrientation;
        state.squareX=squareX; state.squareY=squareY;
        state.squareCenterX=squareCenterX; state.squareCenterY=squareCenterY;
        std::copy(std::begin(squareSpacing),std::end(squareSpacing),std::begin(state.squareSpacing));
        state.squareVerticalOffset=squareVerticalOffset;
        std::copy(std::begin(cubeCount),std::end(cubeCount),std::begin(state.cubeCount));
        std::copy(std::begin(cubeSpacing),std::end(cubeSpacing),std::begin(state.cubeSpacing));
        state.translation=translation; state.objectRotation=objectRotation;
        state.wholePatternRotation=wholePatternRotation;
        std::copy(std::begin(objectRotationDegrees),std::end(objectRotationDegrees),
            std::begin(state.objectRotationDegrees));
        std::copy(std::begin(patternRotationDegrees),std::end(patternRotationDegrees),
            std::begin(state.patternRotationDegrees));
        return state;
    }

    bool PatternStatesEqual(const PatternState& a,const PatternState& b)
    {
        if (a.patternType!=b.patternType || a.squareOrigin!=b.squareOrigin ||
            a.lineCopies!=b.lineCopies || a.lineFromCenter!=b.lineFromCenter ||
            a.circleCount!=b.circleCount || a.circleSweep!=b.circleSweep ||
            a.circleRadius!=b.circleRadius || a.circleVerticalStep!=b.circleVerticalStep ||
            a.circleKeepOrientation!=b.circleKeepOrientation || a.squareX!=b.squareX ||
            a.squareY!=b.squareY || a.squareCenterX!=b.squareCenterX ||
            a.squareCenterY!=b.squareCenterY || a.squareVerticalOffset!=b.squareVerticalOffset)
            return false;
        for (int i=0;i<3;++i)
        {
            if (a.lineStep[i]!=b.lineStep[i] || a.cubeCount[i]!=b.cubeCount[i] ||
                a.cubeSpacing[i]!=b.cubeSpacing[i] ||
                a.objectRotationDegrees[i]!=b.objectRotationDegrees[i] ||
                a.patternRotationDegrees[i]!=b.patternRotationDegrees[i]) return false;
            for (int row=0;row<3;++row)
                if (a.objectRotation.m[row][i]!=b.objectRotation.m[row][i] ||
                    a.wholePatternRotation.m[row][i]!=b.wholePatternRotation.m[row][i]) return false;
        }
        for (int i=0;i<2;++i) if (a.squareSpacing[i]!=b.squareSpacing[i]) return false;
        return a.translation.x==b.translation.x && a.translation.y==b.translation.y &&
            a.translation.z==b.translation.z;
    }

    void ApplyPatternState(const PatternState& state)
    {
        patternType=state.patternType; squareOrigin=state.squareOrigin;
        lineCopies=state.lineCopies; lineFromCenter=state.lineFromCenter;
        std::copy(std::begin(state.lineStep),std::end(state.lineStep),std::begin(lineStep));
        circleCount=state.circleCount; circleSweep=state.circleSweep;
        circleRadius=state.circleRadius; circleVerticalStep=state.circleVerticalStep;
        circleKeepOrientation=state.circleKeepOrientation;
        squareX=state.squareX; squareY=state.squareY;
        squareCenterX=state.squareCenterX; squareCenterY=state.squareCenterY;
        std::copy(std::begin(state.squareSpacing),std::end(state.squareSpacing),std::begin(squareSpacing));
        squareVerticalOffset=state.squareVerticalOffset;
        std::copy(std::begin(state.cubeCount),std::end(state.cubeCount),std::begin(cubeCount));
        std::copy(std::begin(state.cubeSpacing),std::end(state.cubeSpacing),std::begin(cubeSpacing));
        translation=state.translation; objectRotation=state.objectRotation;
        wholePatternRotation=state.wholePatternRotation;
        std::copy(std::begin(state.objectRotationDegrees),std::end(state.objectRotationDegrees),
            std::begin(objectRotationDegrees));
        std::copy(std::begin(state.patternRotationDegrees),std::end(state.patternRotationDegrees),
            std::begin(patternRotationDegrees));
        RebuildPattern();
    }

    void ClearPatternHistory()
    {
        undoHistory.clear(); redoHistory.clear(); historyActionActive=false;
        uiHistoryActive=false; pendingHistoryLabel.clear();
    }

    void BeginPatternHistory(const char* label)
    {
        if (sourceMode!=SourceMode::XmlGroups || sourceProps.empty() || historyActionActive) return;
        pendingHistoryBefore=CapturePatternState(); pendingHistoryLabel=label;
        historyActionActive=true;
    }

    void CommitPatternHistory()
    {
        if (!historyActionActive) return;
        PatternHistoryEntry entry;
        entry.label=pendingHistoryLabel; entry.before=pendingHistoryBefore;
        entry.after=CapturePatternState();
        historyActionActive=false; pendingHistoryLabel.clear();
        if (PatternStatesEqual(entry.before,entry.after)) return;
        undoHistory.push_back(std::move(entry));
        if (undoHistory.size()>MaxPatternHistory) undoHistory.erase(undoHistory.begin());
        redoHistory.clear();
    }

    void UndoPattern()
    {
        CommitPatternHistory();
        if (undoHistory.empty()) return;
        PatternHistoryEntry entry=undoHistory.back(); undoHistory.pop_back();
        ApplyPatternState(entry.before); redoHistory.push_back(std::move(entry));
        status="Undid the last pattern change. Apply to XML to save the restored state.";
    }

    void RedoPattern()
    {
        CommitPatternHistory();
        if (redoHistory.empty()) return;
        PatternHistoryEntry entry=redoHistory.back(); redoHistory.pop_back();
        ApplyPatternState(entry.after); undoHistory.push_back(std::move(entry));
        status="Redid the pattern change. Apply to XML to save the restored state.";
    }

    void ResetPatternTransform()
    {
        translation={}; objectRotation=IdentityMatrix(); wholePatternRotation=IdentityMatrix();
        objectRotationDegrees[0]=objectRotationDegrees[1]=objectRotationDegrees[2]=0.0f;
        patternRotationDegrees[0]=patternRotationDegrees[1]=patternRotationDegrees[2]=0.0f;
        activeControl=activeAxis=hoveredControl=0; inputCaptured=false;
    }

    void ComputeSourcePivot()
    {
        sourcePivot={};
        if (sourceProps.empty()) return;
        for (const SourceProp& prop:sourceProps) sourcePivot=Add(sourcePivot,prop.position);
        sourcePivot=Multiply(sourcePivot,1.0/static_cast<double>(sourceProps.size()));
    }

    bool ImportXml(const std::string& path);
    void SetSourceMode(SourceMode mode);

    void SelectGroup(int index)
    {
        if (groupApplyWritten && index>=0 && index!=selectedGroupIndex)
        {
            const std::string requestedName=groups[static_cast<size_t>(index)].name;
            const std::string path=importedPath;
            if (!ImportXml(path)) return;
            SetSourceMode(SourceMode::XmlGroups);
            for (size_t groupIndex=0;groupIndex<groups.size();++groupIndex)
                if (groups[groupIndex].name==requestedName)
                { SelectGroup(static_cast<int>(groupIndex)); return; }
            status="The requested group was not found after reloading the applied XML.";
            return;
        }
        for (GroupInfo& group:groups) group.selected=false;
        selectedGroupIndex=index>=0 && index<static_cast<int>(groups.size()) ? index : -1;
        sourceProps.clear();
        if (selectedGroupIndex>=0)
        {
            GroupInfo& group=groups[static_cast<size_t>(selectedGroupIndex)];
            group.selected=true; sourceProps.reserve(group.propIndices.size());
            for (size_t propIndex:group.propIndices) sourceProps.push_back(allSourceProps[propIndex]);
        }
        ResetPatternTransform(); ClearPatternHistory(); ComputeSourcePivot();
        generatedProps.clear(); instances.clear();
        if (!sourceProps.empty()) RebuildPattern(); else UpdateCounter();
    }

    void SetSourceMode(SourceMode mode)
    {
        if (groupApplyWritten && mode==SourceMode::FullXml)
        {
            const std::string path=importedPath;
            if (ImportXml(path))
                status="Full XML mode selected with the applied group pattern reloaded.";
            return;
        }
        sourceMode=mode;
        for (GroupInfo& group:groups) group.selected=false;
        selectedGroupIndex=-1; sourceProps.clear(); ResetPatternTransform(); ClearPatternHistory();
        if (sourceMode==SourceMode::FullXml) sourceProps=allSourceProps;
        ComputeSourcePivot(); generatedProps.clear(); instances.clear();
        if (!sourceProps.empty()) RebuildPattern(); else UpdateCounter();
        status=sourceMode==SourceMode::FullXml
            ? "Full XML mode selected."
            : groups.empty() ? "This XML does not contain any named decoration groups."
                : "XML Groups mode selected. Choose one group to pattern.";
    }

    bool ImportXml(const std::string& path)
    {
        const GroupBackupDatabase::ImportResult groupRestore=
            GroupBackupDatabase::PrepareImport(
                path,-1,AppSettings::Get().automaticGroupBackupRestore,
                AppSettings::Get().backupUngroupedXmls);
        if (groupRestore.action==GroupBackupDatabase::ImportAction::NeedsUserChoice ||
            groupRestore.action==GroupBackupDatabase::ImportAction::Error)
        { status=groupRestore.message; return false; }
        std::ifstream file(Utf8Paths::FromUtf8(path),std::ios::binary);
        if (!file.is_open()) { status="Could not open the selected XML file."; return false; }
        std::ostringstream contents; contents<<file.rdbuf(); std::string source=contents.str();
        const size_t rootStart=source.find("<Decorations");
        const size_t rootEnd=rootStart==std::string::npos ? std::string::npos : FindTagEnd(source,rootStart);
        const size_t rootClose=source.rfind("</Decorations>");
        if (rootStart==std::string::npos || rootEnd==std::string::npos || rootClose==std::string::npos)
        { status="Invalid XML: missing the Decorations root element."; return false; }
        std::string typeText;
        if (!ReadAttribute(source,rootStart,rootEnd,"type",typeText) || (typeText!="0" && typeText!="1"))
        { status="Invalid XML: Decorations type must be 0 or 1."; return false; }

        std::vector<SourceProp> parsed;
        size_t search=rootEnd+1;
        while (true)
        {
            const size_t propStart=source.find("<prop",search);
            if (propStart==std::string::npos || propStart>=rootClose) break;
            const size_t propTagEnd=FindTagEnd(source,propStart);
            if (propTagEnd==std::string::npos) { status="Invalid XML: unfinished prop tag."; return false; }
            size_t elementEnd=propTagEnd+1;
            if (!IsSelfClosing(source,propStart,propTagEnd))
            {
                const size_t close=source.find("</prop>",propTagEnd+1);
                if (close==std::string::npos || close>=rootClose)
                { status="Invalid XML: prop element has no closing tag."; return false; }
                elementEnd=close+sizeof("</prop>")-1;
            }
            SourceProp prop; prop.elementStart=propStart; prop.elementEnd=elementEnd;
            prop.tagEnd=IsSelfClosing(source,propStart,propTagEnd) ? propTagEnd-1 : propTagEnd;
            std::string positionText,rotationText,idText;
            if (!ReadAttribute(source,propStart,propTagEnd,"pos",positionText,
                &prop.positionStart,&prop.positionLength) || !ParseFloat3(positionText,prop.position))
            { status="Invalid XML: a decoration has an invalid pos value."; return false; }
            prop.hasRotation=ReadAttribute(source,propStart,propTagEnd,"rot",rotationText,
                &prop.rotationStart,&prop.rotationLength);
            if (prop.hasRotation && !ParseFloat3(rotationText,prop.rotation))
            { status="Invalid XML: a decoration has an invalid rot value."; return false; }
            if (ReadAttribute(source,propStart,propTagEnd,"id",idText))
                try { prop.id=std::stoi(idText); } catch (...) { prop.id=-1; }
            const char* name=DecorationDatabase::FindNameById(prop.id,typeText=="1" ? 1 : 0);
            prop.name=name==nullptr ? "Unknown Decoration" : name;
            parsed.push_back(std::move(prop)); search=elementEnd;
        }
        if (parsed.empty()) { status="Invalid XML: no positioned prop entries were found."; return false; }

        std::vector<GroupInfo> parsedGroups;
        size_t comment=source.find("<!--",rootEnd+1);
        while (comment!=std::string::npos && comment<rootClose)
        {
            const size_t commentEndMarker=source.find("-->",comment+4);
            if (commentEndMarker==std::string::npos || commentEndMarker>=rootClose) break;
            const size_t next=source.find("<!--",commentEndMarker+3);
            const size_t groupEnd=next==std::string::npos || next>=rootClose ? rootClose : next;
            GroupInfo group;
            group.name=Trim(source.substr(comment+4,commentEndMarker-comment-4));
            group.commentStart=comment; group.commentEnd=commentEndMarker+3;
            group.contentEnd=groupEnd; group.insertStart=group.commentEnd;
            if (!group.name.empty())
            {
                const int groupIndex=static_cast<int>(parsedGroups.size());
                for (size_t propIndex=0;propIndex<parsed.size();++propIndex)
                {
                    SourceProp& prop=parsed[propIndex];
                    if (prop.elementStart>commentEndMarker && prop.elementStart<groupEnd)
                    {
                        prop.groupIndex=groupIndex; group.propIndices.push_back(propIndex);
                        group.insertStart=(std::max)(group.insertStart,prop.elementEnd);
                    }
                }
                if (!group.propIndices.empty()) parsedGroups.push_back(std::move(group));
            }
            comment=next;
        }

        xmlSource=std::move(source); allSourceProps=std::move(parsed); groups=std::move(parsedGroups);
        importedPath=path;
        groupApplyWritten=false;
        const size_t firstGroupComment=xmlSource.find("<!--",rootEnd+1);
        copyInsertStart=firstGroupComment!=std::string::npos && firstGroupComment<rootClose
            ? firstGroupComment : rootClose;
        xmlType=typeText=="1" ? 1 : 0;
        importedFileName=Utf8Paths::ToUtf8(Utf8Paths::FromUtf8(path).filename());
        sourceMode=SourceMode::FullXml; selectedGroupIndex=-1;
        sourceProps=allSourceProps; ResetPatternTransform(); ClearPatternHistory();
        ComputeSourcePivot(); RebuildPattern(false);

        std::map<int,DecorationCounterWindow::Requirement> byId;
        for (const SourceProp& prop:allSourceProps)
        {
            auto& item=byId[prop.id]; item.id=prop.id; item.name=prop.name;
            item.required+=static_cast<int>(instances.size());
        }
        std::vector<DecorationCounterWindow::Requirement> counter;
        for (const auto& [id,item]:byId) { static_cast<void>(id); counter.push_back(item); }
        DecorationCounterWindow::SetRequirements(importedFileName+" Pattern",xmlType,counter);
        status="Loaded "+std::to_string(allSourceProps.size())+" decorations and "+
            std::to_string(groups.size())+" named groups; Full XML mode is active.";
        return true;
    }

    std::string FormatFloat(double value)
    {
        if (std::fabs(value)<0.0000005) value=0.0;
        std::ostringstream stream; stream<<std::fixed<<std::setprecision(6)<<value;
        std::string text=stream.str();
        while (text.size()>1 && text.back()=='0') text.pop_back();
        if (!text.empty() && text.back()=='.') text.pop_back();
        return text;
    }
    std::string PositionText(DVec3 p)
    { return FormatFloat(p.x)+" "+FormatFloat(p.y)+" "+FormatFloat(p.z); }
    std::string RotationText(DVec3 r)
    { return FormatFloat(r.x)+" "+FormatFloat(r.y)+" "+FormatFloat(r.z); }

    struct Replacement { size_t start=0; size_t length=0; std::string value; };
    std::string RewriteElement(const SourceProp& source,const GeneratedProp& generated)
    {
        std::string element=xmlSource.substr(source.elementStart,source.elementEnd-source.elementStart);
        std::vector<Replacement> replacements;
        replacements.push_back({source.positionStart-source.elementStart,source.positionLength,
            PositionText(generated.position)});
        if (source.hasRotation)
            replacements.push_back({source.rotationStart-source.elementStart,source.rotationLength,
                RotationText(generated.rotation)});
        else
            replacements.push_back({source.tagEnd-source.elementStart,0,
                " rot=\""+RotationText(generated.rotation)+"\""});
        std::sort(replacements.begin(),replacements.end(),[](const Replacement& a,const Replacement& b)
        { return a.start>b.start; });
        for (const Replacement& replacement:replacements)
            element.replace(replacement.start,replacement.length,replacement.value);
        return element;
    }

    std::string BuildPatternXml()
    {
        std::vector<Replacement> originals;
        originals.reserve(sourceProps.size()*2);
        for (size_t i=0;i<sourceProps.size();++i)
        {
            const SourceProp& source=sourceProps[i]; const GeneratedProp& generated=generatedProps[i];
            originals.push_back({source.positionStart,source.positionLength,PositionText(generated.position)});
            originals.push_back(source.hasRotation
                ? Replacement{source.rotationStart,source.rotationLength,RotationText(generated.rotation)}
                : Replacement{source.tagEnd,0," rot=\""+RotationText(generated.rotation)+"\""});
        }
        std::sort(originals.begin(),originals.end(),[](const Replacement& a,const Replacement& b)
        { return a.start>b.start; });
        std::string output=xmlSource;
        std::ptrdiff_t adjustedInsert=static_cast<std::ptrdiff_t>(copyInsertStart);
        for (const Replacement& replacement:originals)
        {
            output.replace(replacement.start,replacement.length,replacement.value);
            if (replacement.start<copyInsertStart)
                adjustedInsert+=static_cast<std::ptrdiff_t>(replacement.value.size())-
                    static_cast<std::ptrdiff_t>(replacement.length);
        }
        const char* newline=xmlSource.find("\r\n")!=std::string::npos ? "\r\n" : "\n";
        std::ostringstream copies;
        for (size_t instanceIndex=1;instanceIndex<instances.size();++instanceIndex)
            for (size_t sourceIndex=0;sourceIndex<sourceProps.size();++sourceIndex)
            {
                const size_t generatedIndex=instanceIndex*sourceProps.size()+sourceIndex;
                copies<<"  "<<RewriteElement(sourceProps[sourceIndex],generatedProps[generatedIndex])<<newline;
            }
        output.insert(static_cast<size_t>(adjustedInsert),copies.str());
        return output;
    }

    std::string BuildGroupPatternXml()
    {
        if (selectedGroupIndex<0 || selectedGroupIndex>=static_cast<int>(groups.size())) return xmlSource;
        const GroupInfo& group=groups[static_cast<size_t>(selectedGroupIndex)];
        std::vector<Replacement> originals;
        originals.reserve(sourceProps.size()*2);
        for (size_t i=0;i<sourceProps.size();++i)
        {
            const SourceProp& source=sourceProps[i]; const GeneratedProp& generated=generatedProps[i];
            originals.push_back({source.positionStart,source.positionLength,PositionText(generated.position)});
            originals.push_back(source.hasRotation
                ? Replacement{source.rotationStart,source.rotationLength,RotationText(generated.rotation)}
                : Replacement{source.tagEnd,0," rot=\""+RotationText(generated.rotation)+"\""});
        }
        std::sort(originals.begin(),originals.end(),[](const Replacement& a,const Replacement& b)
        { return a.start>b.start; });
        std::string output=xmlSource;
        std::ptrdiff_t adjustedInsert=static_cast<std::ptrdiff_t>(group.insertStart);
        for (const Replacement& replacement:originals)
        {
            output.replace(replacement.start,replacement.length,replacement.value);
            if (replacement.start<group.insertStart)
                adjustedInsert+=static_cast<std::ptrdiff_t>(replacement.value.size())-
                    static_cast<std::ptrdiff_t>(replacement.length);
        }
        const char* newline=xmlSource.find("\r\n")!=std::string::npos ? "\r\n" : "\n";
        std::set<std::string> usedGroupNames;
        for (const GroupInfo& existingGroup:groups) usedGroupNames.insert(existingGroup.name);
        int nextCopyNumber=1;
        std::ostringstream copies;
        for (size_t instanceIndex=1;instanceIndex<instances.size();++instanceIndex)
        {
            std::string copyName;
            do
            {
                copyName=group.name+" (Copy "+std::to_string(nextCopyNumber++)+")";
            }
            while (usedGroupNames.find(copyName)!=usedGroupNames.end());
            usedGroupNames.insert(copyName);
            copies<<newline<<"  <!-- "<<copyName<<" -->";
            for (size_t sourceIndex=0;sourceIndex<sourceProps.size();++sourceIndex)
            {
                const size_t generatedIndex=instanceIndex*sourceProps.size()+sourceIndex;
                copies<<newline<<"  "<<RewriteElement(sourceProps[sourceIndex],generatedProps[generatedIndex]);
            }
        }
        output.insert(static_cast<size_t>(adjustedInsert),copies.str());
        return output;
    }

    void ApplyPatternToXml()
    {
        CommitPatternHistory();
        if (sourceMode!=SourceMode::XmlGroups || sourceProps.empty() || selectedGroupIndex<0)
        { status="Select one XML group to pattern first."; return; }
        const std::string output=BuildGroupPatternXml();
        std::ofstream file(Utf8Paths::FromUtf8(importedPath),std::ios::binary|std::ios::trunc);
        if (!file.is_open()) { status="Could not open the imported XML for writing."; return; }
        file.write(output.data(),static_cast<std::streamsize>(output.size()));
        if (!file.good()) { status="The pattern could not be written completely."; return; }
        file.close();
        if (AppSettings::Get().automaticGroupBackupRestore)
        {
            std::string backupStatus;
            GroupBackupDatabase::RecordFile(importedPath,xmlType,
                GroupBackupDatabase::RestorePointType::Auto,std::string(),backupStatus,
                AppSettings::Get().backupUngroupedXmls);
        }
        groupApplyWritten=true;
        status="Applied the pattern to group \""+
            groups[static_cast<size_t>(selectedGroupIndex)].name+
            "\" in "+importedFileName+". Undo and Redo history remains available.";
    }

    void ExportPattern()
    {
        if (sourceProps.empty()) return;
        const std::string folder=FolderForType(xmlType);
        if (folder.empty()) { status="Set this XML folder path in Settings first."; return; }
        std::error_code error; std::filesystem::create_directories(folder,error);
        if (error) { status="The destination XML folder could not be created."; return; }
        const std::string baseName=Utf8Paths::ToUtf8(Utf8Paths::FromUtf8(importedFileName).stem());
        const std::filesystem::path outputPath=XmlFileUtils::IndexedOperationPath(
            std::filesystem::path(folder),baseName,"_PATTERN");
        if (outputPath.empty()) { status="The destination XML folder could not be checked."; return; }
        const std::string output=BuildPatternXml();
        std::ofstream file(outputPath,std::ios::binary|std::ios::trunc);
        if (!file.is_open()) { status="Could not create the pattern XML file."; return; }
        file.write(output.data(),static_cast<std::streamsize>(output.size()));
        if (!file.good()) { status="The pattern XML could not be written completely."; return; }
        file.close();
        if (AppSettings::Get().automaticGroupBackupRestore)
        {
            std::string backupStatus;
            GroupBackupDatabase::RecordFile(Utf8Paths::ToUtf8(outputPath),xmlType,
                GroupBackupDatabase::RestorePointType::Auto,std::string(),backupStatus,
                AppSettings::Get().backupUngroupedXmls);
        }
        if (selectedFolderType==xmlType) RefreshXmlList();
        status="Exported "+Utf8Paths::ToUtf8(outputPath.filename())+" with "+
            std::to_string(generatedProps.size())+" decorations.";
    }

    float DistanceToSegment(ImVec2 point,ImVec2 start,ImVec2 end)
    {
        const float x=end.x-start.x,y=end.y-start.y,lengthSquared=x*x+y*y;
        if (lengthSquared<=0.000001f)
        {
            const float dx=point.x-start.x,dy=point.y-start.y;
            return std::sqrt(dx*dx+dy*dy);
        }
        float amount=((point.x-start.x)*x+(point.y-start.y)*y)/lengthSquared;
        amount=std::clamp(amount,0.0f,1.0f);
        const float dx=point.x-(start.x+amount*x),dy=point.y-(start.y+amount*y);
        return std::sqrt(dx*dx+dy*dy);
    }
    void DrawArrow(ImDrawList* draw,ImVec2 start,ImVec2 end,ImU32 color,float thickness)
    {
        draw->AddLine(start,end,color,thickness);
        const float dx=end.x-start.x,dy=end.y-start.y,length=std::sqrt(dx*dx+dy*dy);
        if (length<=1.0f) return;
        const float ux=dx/length,uy=dy/length,px=-uy,py=ux;
        const ImVec2 a(end.x-ux*13.0f+px*6.0f,end.y-uy*13.0f+py*6.0f);
        const ImVec2 b(end.x-ux*13.0f-px*6.0f,end.y-uy*13.0f-py*6.0f);
        draw->AddTriangleFilled(end,a,b,color);
    }

    struct TranslationGeometry
    {
        ImVec2 origin;
        ImVec2 ends[3];
        bool visible[3] = {};
        float worldLength = 0.0f;
    };

    TranslationGeometry BuildTranslationGeometry(const Camera& camera,ImVec2 viewport,
        DVec3 origin,const Mat3& basis,int mask)
    {
        TranslationGeometry geometry;
        const Vec3 originWorld=DecorationToWorld(origin);
        if (!camera.Project(originWorld,viewport,geometry.origin)) return geometry;
        geometry.worldLength=WorldSizeForScreenPixels(camera,viewport,originWorld,
            ManipulatorUtils::MoveAxisPixels);
        const double decoLength=geometry.worldLength/DecorationScale;
        for (int axis=0;axis<3;++axis)
        {
            if ((mask&(1<<axis))==0) continue;
            DVec3 local={};
            if (axis==0) local.x=decoLength;
            else if (axis==1) local.y=decoLength;
            else local.z=-decoLength;
            geometry.visible[axis]=camera.Project(
                DecorationToWorld(Add(origin,Multiply(basis,local))),viewport,geometry.ends[axis]);
        }
        return geometry;
    }

    DVec3 RingPoint(DVec3 origin,const Mat3& basis,int axis,double angle,double radius)
    {
        DVec3 point;
        if (axis==0) point={0.0,std::cos(angle)*radius,std::sin(angle)*radius};
        else if (axis==1) point={std::cos(angle)*radius,0.0,std::sin(angle)*radius};
        else point={std::cos(angle)*radius,std::sin(angle)*radius,0.0};
        return Add(origin,Multiply(basis,point));
    }
    float DistanceToRing(const Camera& camera,ImVec2 viewport,DVec3 origin,
        const Mat3& basis,int axis,double radius,ImVec2 mouse)
    {
        constexpr int Segments=72; float best=std::numeric_limits<float>::infinity();
        ImVec2 previous; bool previousVisible=false;
        for (int segment=0;segment<=Segments;++segment)
        {
            const double angle=6.28318530717958647692*segment/Segments;
            ImVec2 current; const bool visible=camera.Project(
                DecorationToWorld(RingPoint(origin,basis,axis,angle,radius)),viewport,current);
            if (visible && previousVisible) best=(std::min)(best,DistanceToSegment(mouse,previous,current));
            previous=current; previousVisible=visible;
        }
        return best;
    }
    void DrawRing(ImDrawList* draw,const Camera& camera,ImVec2 viewport,DVec3 origin,
        const Mat3& basis,int axis,double radius,ImU32 color,float thickness)
    {
        constexpr int Segments=72; ImVec2 previous; bool previousVisible=false;
        for (int segment=0;segment<=Segments;++segment)
        {
            const double angle=6.28318530717958647692*segment/Segments;
            ImVec2 current; const bool visible=camera.Project(
                DecorationToWorld(RingPoint(origin,basis,axis,angle,radius)),viewport,current);
            if (visible && previousVisible) draw->AddLine(previous,current,color,thickness);
            previous=current; previousVisible=visible;
        }
    }

    void CaptureOffsetSnapshot()
    {
        for (int i=0;i<3;++i)
        {
            dragStartLineStep[i]=lineStep[i];
            dragStartCubeSpacing[i]=cubeSpacing[i];
        }
        dragStartSquareSpacing[0]=squareSpacing[0];
        dragStartSquareSpacing[1]=squareSpacing[1];
        dragStartSquareVertical=squareVerticalOffset;
        dragStartCircleRadius=circleRadius;
        dragStartCircleVertical=circleVerticalStep;
    }

    void ApplyOffsetDrag(float delta)
    {
        const int axis=activeAxis-1;
        if (activeControl==5)
        {
            squareVerticalOffset=dragStartSquareVertical+delta;
        }
        else if (patternType==PatternType::Line)
        {
            lineStep[axis]=dragStartLineStep[axis]+delta;
        }
        else if (patternType==PatternType::Circle)
        {
            if (axis==2)
                circleVerticalStep=dragStartCircleVertical+
                    delta*static_cast<float>((std::max)(1,circleCount-1));
            else circleRadius=(std::max)(1.0f,dragStartCircleRadius+delta);
        }
        else if (patternType==PatternType::Square)
        {
            if (axis<2) squareSpacing[axis]=dragStartSquareSpacing[axis]+delta;
            else squareVerticalOffset=dragStartSquareVertical+delta;
        }
        else
        {
            cubeSpacing[axis]=dragStartCubeSpacing[axis]+delta;
        }
        RebuildPatternPreservingMain();
        status="Adjusted pattern spacing with the gray manipulator.";
    }

    void ApplyOffsetViewDrag(DVec3 localDelta)
    {
        if (patternType==PatternType::Line)
        {
            lineStep[0]=dragStartLineStep[0]+static_cast<float>(localDelta.x);
            lineStep[1]=dragStartLineStep[1]+static_cast<float>(localDelta.y);
            lineStep[2]=dragStartLineStep[2]+static_cast<float>(localDelta.z);
        }
        else if (patternType==PatternType::Circle)
        {
            DVec3 radial={1.0,0.0,0.0};
            if (referenceInstance>=0)
            {
                radial=instances[static_cast<size_t>(referenceInstance)].offset;
                radial.z=0.0;
                const double length=std::sqrt(radial.x*radial.x+radial.y*radial.y);
                if (length>0.000001)
                { radial.x/=length; radial.y/=length; }
                else radial={1.0,0.0,0.0};
            }
            const double radiusDelta=localDelta.x*radial.x+localDelta.y*radial.y;
            circleRadius=(std::max)(1.0f,
                dragStartCircleRadius+static_cast<float>(radiusDelta));
            circleVerticalStep=dragStartCircleVertical+
                static_cast<float>(localDelta.z)*static_cast<float>((std::max)(1,circleCount-1));
        }
        else if (patternType==PatternType::Square)
        {
            squareSpacing[0]=dragStartSquareSpacing[0]+static_cast<float>(localDelta.x);
            squareSpacing[1]=dragStartSquareSpacing[1]+static_cast<float>(localDelta.y);
            if (squareOrigin!=SquareOrigin::Center)
                squareVerticalOffset=dragStartSquareVertical+static_cast<float>(localDelta.z);
        }
        else
        {
            cubeSpacing[0]=dragStartCubeSpacing[0]+static_cast<float>(localDelta.x);
            cubeSpacing[1]=dragStartCubeSpacing[1]+static_cast<float>(localDelta.y);
            cubeSpacing[2]=dragStartCubeSpacing[2]+static_cast<float>(localDelta.z);
        }
        RebuildPatternPreservingMain();
        status="Adjusted pattern spacing with the gray multidirectional handle.";
    }

    void DrawManipulators(const Camera& camera,ImVec2 viewport,ImDrawList* draw)
    {
        const ImVec2 mouse=inputCaptured || activeControl!=0 ? wndMousePosition : ImGui::GetIO().MousePos;
        const bool canHover=!ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) && activeControl==0;
        const ImU32 axisColors[3]={
            ImGui::ColorConvertFloat4ToU32({1.0f,0.18f,0.15f,1.0f}),
            ImGui::ColorConvertFloat4ToU32({0.20f,0.90f,0.25f,1.0f}),
            ImGui::ColorConvertFloat4ToU32({0.25f,0.55f,1.0f,1.0f}) };
        const ImU32 gray=ImGui::ColorConvertFloat4ToU32({0.68f,0.70f,0.74f,1.0f});
        const ImU32 highlight=ImGui::ColorConvertFloat4ToU32({0.0f,0.95f,1.0f,1.0f});
        hoveredControl=0; int hoveredAxisLocal=0; float best=42.0f;

        TranslationGeometry primaryTranslation;
        TranslationGeometry offsetTranslation;
        TranslationGeometry centerHeightTranslation;
        Mat3 offsetBasis=wholePatternRotation;
        int offsetMask=patternType==PatternType::Square && squareOrigin==SquareOrigin::Center ? 3 : 7;

        if (operationMode==0)
        {
            primaryTranslation=BuildTranslationGeometry(camera,viewport,mainCenter,IdentityMatrix(),7);
            if (canHover && std::fabs(mouse.x-primaryTranslation.origin.x)<=
                ManipulatorUtils::CenterHitHalfSize &&
                std::fabs(mouse.y-primaryTranslation.origin.y)<=
                ManipulatorUtils::CenterHitHalfSize)
            { hoveredControl=1; hoveredAxisLocal=4; best=0.0f; }
            else if (canHover)
                for (int axis=0;axis<3;++axis) if (primaryTranslation.visible[axis])
                {
                    const float distance=DistanceToSegment(mouse,primaryTranslation.origin,primaryTranslation.ends[axis]);
                    if (distance<best) { best=distance; hoveredControl=1; hoveredAxisLocal=axis+1; }
                }
        }

        if (operationMode!=2 && instances.size()>1)
        {
            offsetTranslation=BuildTranslationGeometry(camera,viewport,offsetHandleCenter,offsetBasis,offsetMask);
            const float centerDistanceX=std::fabs(mouse.x-offsetTranslation.origin.x);
            const float centerDistanceY=std::fabs(mouse.y-offsetTranslation.origin.y);
            if (canHover && centerDistanceX<=ManipulatorUtils::CenterHitHalfSize &&
                centerDistanceY<=ManipulatorUtils::CenterHitHalfSize &&
                centerDistanceX+centerDistanceY<best)
            { hoveredControl=2; hoveredAxisLocal=4; best=centerDistanceX+centerDistanceY; }
            else if (canHover && best>0.0f)
                for (int axis=0;axis<3;++axis) if (offsetTranslation.visible[axis])
                {
                    const float distance=DistanceToSegment(mouse,offsetTranslation.origin,offsetTranslation.ends[axis]);
                    if (distance<best) { best=distance; hoveredControl=2; hoveredAxisLocal=axis+1; }
                }
            if (patternType==PatternType::Square && squareOrigin==SquareOrigin::Center)
            {
                centerHeightTranslation=BuildTranslationGeometry(
                    camera,viewport,centerHeightHandleCenter,offsetBasis,4);
                if (canHover && centerHeightTranslation.visible[2])
                {
                    const float distance=DistanceToSegment(mouse,centerHeightTranslation.origin,
                        centerHeightTranslation.ends[2]);
                    if (distance<best) { best=distance; hoveredControl=5; hoveredAxisLocal=3; }
                }
            }
        }

        DVec3 ringOrigin; Mat3 ringBasis; int ringControl=0;
        double ringRadius=0.0;
        if (operationMode==1)
        {
            ringOrigin=mainCenter; ringBasis=Multiply(wholePatternRotation,objectRotation); ringControl=3;
        }
        else if (operationMode==2)
        {
            ringOrigin=patternWorldPivot; ringBasis=wholePatternRotation; ringControl=4;
        }
        if (ringControl!=0)
        {
            const Vec3 originWorld=DecorationToWorld(ringOrigin);
            ringRadius=WorldSizeForScreenPixels(camera,viewport,originWorld,
                ManipulatorUtils::RotationRingPixels)/DecorationScale;
            if (canHover)
                for (int axis=0;axis<3;++axis)
                {
                    const float distance=DistanceToRing(camera,viewport,ringOrigin,ringBasis,axis,ringRadius,mouse);
                    if (distance<best) { best=distance; hoveredControl=ringControl; hoveredAxisLocal=axis+1; }
                }
        }

        if (operationMode==0)
        {
            for (int axis=0;axis<3;++axis) if (primaryTranslation.visible[axis])
            {
                const bool selected=(hoveredControl==1 && hoveredAxisLocal==axis+1) ||
                    (activeControl==1 && activeAxis==axis+1);
                DrawArrow(draw,primaryTranslation.origin,primaryTranslation.ends[axis],
                    selected ? highlight : axisColors[axis],selected ? 5.0f : 3.0f);
            }
            const bool centerSelected=(hoveredControl==1 && hoveredAxisLocal==4) ||
                (activeControl==1 && activeAxis==4);
            const ImU32 centerColor=centerSelected ? highlight : IM_COL32(140,140,140,255);
            draw->AddRectFilled({primaryTranslation.origin.x-ManipulatorUtils::CenterHalfSize,
                primaryTranslation.origin.y-ManipulatorUtils::CenterHalfSize},
                {primaryTranslation.origin.x+ManipulatorUtils::CenterHalfSize,
                    primaryTranslation.origin.y+ManipulatorUtils::CenterHalfSize},centerColor,1.5f);
            draw->AddRect({primaryTranslation.origin.x-ManipulatorUtils::CenterHalfSize,
                primaryTranslation.origin.y-ManipulatorUtils::CenterHalfSize},
                {primaryTranslation.origin.x+ManipulatorUtils::CenterHalfSize,
                    primaryTranslation.origin.y+ManipulatorUtils::CenterHalfSize},
                IM_COL32(216,216,216,255),1.5f,0,centerSelected ? 2.0f : 1.0f);
        }
        if (operationMode!=2 && instances.size()>1)
        {
            for (int axis=0;axis<3;++axis) if (offsetTranslation.visible[axis])
            {
                const bool selected=(hoveredControl==2 && hoveredAxisLocal==axis+1) ||
                    (activeControl==2 && activeAxis==axis+1);
                DrawArrow(draw,offsetTranslation.origin,offsetTranslation.ends[axis],
                    selected ? highlight : gray,selected ? 5.0f : 3.0f);
            }
            const bool centerSelected=(hoveredControl==2 && hoveredAxisLocal==4) ||
                (activeControl==2 && activeAxis==4);
            const ImU32 centerColor=centerSelected ? highlight : IM_COL32(140,140,140,255);
            draw->AddRectFilled({offsetTranslation.origin.x-ManipulatorUtils::CenterHalfSize,
                offsetTranslation.origin.y-ManipulatorUtils::CenterHalfSize},
                {offsetTranslation.origin.x+ManipulatorUtils::CenterHalfSize,
                    offsetTranslation.origin.y+ManipulatorUtils::CenterHalfSize},centerColor,1.5f);
            draw->AddRect({offsetTranslation.origin.x-ManipulatorUtils::CenterHalfSize,
                offsetTranslation.origin.y-ManipulatorUtils::CenterHalfSize},
                {offsetTranslation.origin.x+ManipulatorUtils::CenterHalfSize,
                    offsetTranslation.origin.y+ManipulatorUtils::CenterHalfSize},
                IM_COL32(216,216,216,255),1.5f,0,centerSelected ? 2.0f : 1.0f);
            if (centerHeightTranslation.visible[2])
            {
                const bool selected=hoveredControl==5 || activeControl==5;
                DrawArrow(draw,centerHeightTranslation.origin,centerHeightTranslation.ends[2],
                    selected ? highlight : gray,selected ? 5.0f : 3.0f);
            }
        }
        if (ringControl!=0)
            for (int axis=0;axis<3;++axis)
            {
                const bool selected=(hoveredControl==ringControl && hoveredAxisLocal==axis+1) ||
                    (activeControl==ringControl && activeAxis==axis+1);
                DrawRing(draw,camera,viewport,ringOrigin,ringBasis,axis,ringRadius,
                    selected ? highlight : axisColors[axis],selected ? 3.5f : 2.0f);
            }

        const bool clicked=clickPending || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow));
        clickPending=false;
        if (clicked && activeControl==0 && hoveredControl!=0)
        {
            BeginPatternHistory(hoveredControl==3 || hoveredControl==4
                ? "pattern rotation" : "pattern move");
            activeControl=hoveredControl; activeAxis=hoveredAxisLocal; inputCaptured=true;
            dragStartMouse=mouse; dragStartTranslation=translation; CaptureOffsetSnapshot();
            if (activeControl==3) dragStartRotation=objectRotation;
            else if (activeControl==4) dragStartRotation=wholePatternRotation;

            const TranslationGeometry* geometry=nullptr;
            if (activeControl==1) geometry=&primaryTranslation;
            else if (activeControl==2) geometry=&offsetTranslation;
            else if (activeControl==5) geometry=&centerHeightTranslation;
            if (geometry!=nullptr)
            {
                if (activeAxis==4)
                {
                    const DVec3 activeOrigin=activeControl==1 ? mainCenter : offsetHandleCenter;
                    activeWorldUnitsPerPixel=WorldSizeForScreenPixels(camera,viewport,
                        DecorationToWorld(activeOrigin),1.0f);
                    dragViewRight=camera.right; dragViewUp=camera.up;
                }
                else
                {
                    const ImVec2 end=geometry->ends[activeAxis-1];
                    const float x=end.x-geometry->origin.x,y=end.y-geometry->origin.y;
                    const float screenLength=std::sqrt(x*x+y*y);
                    if (screenLength>1.0f)
                    {
                        activeAxisDirection={x/screenLength,y/screenLength};
                        activeDecoUnitsPerPixel=geometry->worldLength/screenLength/DecorationScale;
                        if (activeAxis==3) activeDecoUnitsPerPixel=-activeDecoUnitsPerPixel;
                    }
                }
            }
        }

        if (activeControl!=0 && mouseDown)
        {
            const float mouseX=mouse.x-dragStartMouse.x,mouseY=mouse.y-dragStartMouse.y;
            if (activeControl<=2 || activeControl==5)
            {
                if (activeAxis==4)
                {
                    const Vec3 worldDelta=Add(
                        Multiply(dragViewRight,mouseX*activeWorldUnitsPerPixel),
                        Multiply(dragViewUp,-mouseY*activeWorldUnitsPerPixel));
                    const DVec3 decorationDelta=WorldToDecoration(worldDelta);
                    if (activeControl==1)
                    {
                        translation=Add(dragStartTranslation,decorationDelta);
                        RebuildPattern(false); status="Moved the complete pattern.";
                    }
                    else
                    {
                        const DVec3 localDelta=Multiply(Transpose(wholePatternRotation),decorationDelta);
                        ApplyOffsetViewDrag(localDelta);
                    }
                }
                else
                {
                    const float pixels=mouseX*activeAxisDirection.x+mouseY*activeAxisDirection.y;
                    const float delta=pixels*activeDecoUnitsPerPixel;
                    if (activeControl==1)
                    {
                        translation=dragStartTranslation;
                        if (activeAxis==1) translation.x+=delta;
                        else if (activeAxis==2) translation.y+=delta;
                        else translation.z+=delta;
                        RebuildPattern(false); status="Moved the complete pattern.";
                    }
                    else ApplyOffsetDrag(delta);
                }
            }
            else
            {
                const double deltaDegrees=(std::fabs(mouseX)>=std::fabs(mouseY) ? mouseX : -mouseY)*0.5;
                const double direction=activeAxis==3 ? 1.0 : -1.0;
                const Mat3 updated=Multiply(dragStartRotation,
                    AxisRotation(activeAxis-1,deltaDegrees*direction*DegreesToRadians));
                if (activeControl==3)
                {
                    objectRotation=updated; MatrixToDegrees(objectRotation,objectRotationDegrees);
                    status="Rotated every pattern instance in place.";
                }
                else
                {
                    wholePatternRotation=updated; MatrixToDegrees(wholePatternRotation,patternRotationDegrees);
                    status="Rotated the complete pattern around its center.";
                }
                RebuildPattern(false);
            }
        }

        if (hoveredControl!=0 || activeControl!=0)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::GetIO().WantCaptureMouse=true;
        }
        if (activeControl!=0 && !mouseDown && inputCaptured)
        {
            CommitPatternHistory();
            activeControl=0; activeAxis=0; inputCaptured=false;
        }
    }

    void DrawGroupSelection(const Camera& camera,ImVec2 viewport,ImDrawList* draw)
    {
        if (sourceMode!=SourceMode::XmlGroups) { hoveredPointGroup=-1; return; }
        const AppSettings::Data& settings=AppSettings::Get();
        const ImVec2 mouse=ImGui::GetIO().MousePos;
        const float pointSize=(std::max)(2.0f,settings.pointSize);
        const float hitRadius=pointSize+5.0f;
        const ImU32 contextColor=IM_COL32(255,166,36,255);
        hoveredPointGroup=-1; float closest=std::numeric_limits<float>::infinity();
        auto considerPoint=[&](DVec3 position,int groupIndex,bool drawContext)
        {
            ImVec2 point;
            if (!camera.Project(DecorationToWorld(position),viewport,point)) return;
            if (drawContext && settings.showDecorationPoints)
                draw->AddCircleFilled(point,pointSize,contextColor,12);
            const float dx=mouse.x-point.x,dy=mouse.y-point.y;
            const float distance=std::sqrt(dx*dx+dy*dy);
            if (distance<=hitRadius && distance<closest)
            { closest=distance; hoveredPointGroup=groupIndex; }
        };
        for (const SourceProp& prop:allSourceProps)
        {
            if (prop.groupIndex<0 || prop.groupIndex>=static_cast<int>(groups.size())) continue;
            if (prop.groupIndex!=selectedGroupIndex)
                considerPoint(prop.position,prop.groupIndex,true);
        }
        if (selectedGroupIndex>=0)
            for (const GeneratedProp& prop:generatedProps)
                if (prop.instanceIndex==0) considerPoint(prop.position,selectedGroupIndex,false);
        const bool leftClicked=pointClickPending || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow));
        const bool rightClicked=pointRightClickPending || (ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
            !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow));
        pointClickPending=false; pointRightClickPending=false;
        if (hoveredPointGroup<0 || hoveredPointGroup>=static_cast<int>(groups.size())) return;

        const std::string& name=groups[static_cast<size_t>(hoveredPointGroup)].name;
        const ImVec2 textSize=ImGui::CalcTextSize(name.c_str()); const ImVec2 padding(7.0f,5.0f);
        ImVec2 tooltip(mouse.x+18.0f,mouse.y+8.0f);
        const ImVec2 size(textSize.x+padding.x*2.0f,textSize.y+padding.y*2.0f);
        tooltip.x=(std::min)(tooltip.x,(std::max)(4.0f,viewport.x-size.x-4.0f));
        tooltip.y=(std::min)(tooltip.y,(std::max)(4.0f,viewport.y-size.y-4.0f));
        ImDrawList* foreground=ImGui::GetForegroundDrawList();
        foreground->AddRectFilled(tooltip,{tooltip.x+size.x,tooltip.y+size.y},IM_COL32(24,24,28,245),4.0f);
        foreground->AddText({tooltip.x+padding.x,tooltip.y+padding.y},IM_COL32(255,255,255,255),name.c_str());
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::GetIO().WantCaptureMouse=true;
        if (activeControl==0)
        {
            if (rightClicked && selectedGroupIndex==hoveredPointGroup)
            {
                const std::string deselected=name; SelectGroup(-1);
                status="Deselected group: "+deselected+".";
            }
            else if (leftClicked && selectedGroupIndex!=hoveredPointGroup)
            {
                const int selected=hoveredPointGroup; const std::string selectedName=name;
                SelectGroup(selected); status="Selected group: "+selectedName+".";
            }
        }
    }

    void DrawPreview(const Camera& camera,ImVec2 viewport,ImDrawList* draw)
    {
        if (generatedProps.empty()) return;
        const AppSettings::Data& settings=AppSettings::Get();
        const ImU32 originalColor=sourceMode==SourceMode::XmlGroups
            ? IM_COL32(50,150,255,255)
            : ImGui::ColorConvertFloat4ToU32({settings.pointColor[0],settings.pointColor[1],
                settings.pointColor[2],settings.pointColor[3]});
        const ImU32 copyColor=ImGui::ColorConvertFloat4ToU32({0.70f,0.72f,0.76f,0.95f});
        if (settings.showDecorationPoints)
            for (const GeneratedProp& prop:generatedProps)
            {
                ImVec2 point;
                if (camera.Project(DecorationToWorld(prop.position),viewport,point))
                    draw->AddCircleFilled(point,settings.pointSize,
                        prop.instanceIndex==0 ? originalColor : copyColor,12);
            }
        if (!settings.showBoundingBox && !settings.showSolidFaces) return;
        DVec3 minimum={std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity()};
        DVec3 maximum={-minimum.x,-minimum.y,-minimum.z};
        for (const GeneratedProp& prop:generatedProps)
        {
            minimum.x=(std::min)(minimum.x,prop.position.x); minimum.y=(std::min)(minimum.y,prop.position.y);
            minimum.z=(std::min)(minimum.z,prop.position.z); maximum.x=(std::max)(maximum.x,prop.position.x);
            maximum.y=(std::max)(maximum.y,prop.position.y); maximum.z=(std::max)(maximum.z,prop.position.z);
        }
        const DVec3 points[8]={{minimum.x,minimum.y,minimum.z},{maximum.x,minimum.y,minimum.z},
            {maximum.x,maximum.y,minimum.z},{minimum.x,maximum.y,minimum.z},{minimum.x,minimum.y,maximum.z},
            {maximum.x,minimum.y,maximum.z},{maximum.x,maximum.y,maximum.z},{minimum.x,maximum.y,maximum.z}};
        ImVec2 corners[8]; bool visible[8]={};
        for (int i=0;i<8;++i) visible[i]=camera.Project(DecorationToWorld(points[i]),viewport,corners[i]);
        if (settings.showSolidFaces)
        {
            const ImU32 color=ImGui::ColorConvertFloat4ToU32({settings.faceColor[0],settings.faceColor[1],
                settings.faceColor[2],settings.faceColor[3]});
            const int faces[6][4]={{0,1,2,3},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}};
            for (const auto& face:faces) if (visible[face[0]]&&visible[face[1]]&&visible[face[2]]&&visible[face[3]])
            {
                draw->AddTriangleFilled(corners[face[0]],corners[face[1]],corners[face[2]],color);
                draw->AddTriangleFilled(corners[face[0]],corners[face[2]],corners[face[3]],color);
            }
        }
        if (settings.showBoundingBox)
        {
            const ImU32 color=ImGui::ColorConvertFloat4ToU32({settings.boxColor[0],settings.boxColor[1],
                settings.boxColor[2],settings.boxColor[3]});
            const int edges[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
            for (const auto& edge:edges) if (visible[edge[0]]&&visible[edge[1]])
                draw->AddLine(corners[edge[0]],corners[edge[1]],color,2.0f);
        }
    }
}

void PatternsTab::Render()
{
    const bool hasImported=!allSourceProps.empty();
    const bool hasXml=!sourceProps.empty();
    const PatternState frameBefore=CapturePatternState();
    bool uiChanged=false;
    InitializeXmlList();
    RenderSectionHeading("Import");
    ImGui::Dummy({0,12}); ImGui::Text("Import Decoration XML");
    if (ImGui::RadioButton("Homestead",&selectedFolderType,0)) RefreshXmlList();
    ImGui::SameLine(); if (ImGui::RadioButton("Guild Hall",&selectedFolderType,1)) RefreshXmlList();
    const bool hasSelection=selectedXmlIndex>=0 && selectedXmlIndex<static_cast<int>(availableXmlFiles.size());
    const char* selectedName=hasSelection ? availableXmlFiles[static_cast<size_t>(selectedXmlIndex)].name.c_str() : "No XML files available";
    ImGui::SetNextItemWidth(-1); XmlComboHelpers::SetPopupWidth(availableXmlFiles);
    if (ImGui::BeginCombo("##PatternXmlList",selectedName))
    {
        for (size_t i=0;i<availableXmlFiles.size();++i)
        {
            const bool selected=selectedXmlIndex==static_cast<int>(i); ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(availableXmlFiles[i].name.c_str(),selected)) selectedXmlIndex=static_cast<int>(i);
            if (selected) ImGui::SetItemDefaultFocus(); ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    const float actionWidth=(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x)*0.5f;
    if (ImGui::Button("Refresh List",{actionWidth,0})) RefreshXmlList(); ImGui::SameLine();
    if (hasSelection)
    {
        if (ImGui::Button("Import Selected",{actionWidth,0}))
            ImportXml(availableXmlFiles[static_cast<size_t>(selectedXmlIndex)].path);
    }
    else RenderDisabledButton("Import Selected",{actionWidth,0});
    ImGui::TextDisabled("%s",status.c_str());

    ImGui::Dummy({0,16}); RenderSectionHeading("Pattern Source");
    int sourceValue=static_cast<int>(sourceMode);
    bool sourceModeChanged=false;
    if (ImGui::RadioButton("Full XML",&sourceValue,0)) sourceModeChanged=true;
    ImGui::SameLine();
    if (ImGui::RadioButton("XML Groups",&sourceValue,1)) sourceModeChanged=true;
    if (sourceModeChanged && hasImported) SetSourceMode(static_cast<SourceMode>(sourceValue));
    else if (!hasImported) sourceMode=static_cast<SourceMode>(sourceValue);

    if (sourceMode==SourceMode::XmlGroups && hasImported)
    {
        ImGui::TextDisabled("Select one group here, or click any grouped point in the scene.");
        if (groups.empty()) ImGui::TextDisabled("This XML does not contain any named decoration groups.");
        else
        {
            ImGui::BeginChild("##PatternGroupList",ImVec2(0.0f,170.0f),true);
            for (size_t index=0;index<groups.size();++index)
            {
                const GroupInfo& group=groups[index];
                bool selected=selectedGroupIndex==static_cast<int>(index);
                const std::string label=group.name+" ("+std::to_string(group.propIndices.size())+
                    " decorations)##PatternGroup"+std::to_string(index);
                if (ImGui::Checkbox(label.c_str(),&selected))
                {
                    const std::string name=group.name;
                    SelectGroup(selected ? static_cast<int>(index) : -1);
                    status=selected ? "Selected group: "+name+"." : "Deselected group: "+name+".";
                }
            }
            ImGui::EndChild();
        }
    }

    ImGui::Dummy({0,16}); RenderSectionHeading("Pattern Type");
    bool patternChanged=false;
    int typeValue=static_cast<int>(patternType);
    if (ImGui::RadioButton("Line",&typeValue,0)) patternChanged=true;
    ImGui::SameLine(); if (ImGui::RadioButton("Circle",&typeValue,1)) patternChanged=true;
    ImGui::SameLine(); if (ImGui::RadioButton("Square",&typeValue,2)) patternChanged=true;
    ImGui::SameLine(); if (ImGui::RadioButton("Cube",&typeValue,3)) patternChanged=true;
    patternType=static_cast<PatternType>(typeValue);

    if (patternType==PatternType::Line)
    {
        patternChanged|=ImGui::SliderInt("Copies",&lineCopies,0,50);
        patternChanged|=ImGui::Checkbox("From Center",&lineFromCenter);
        ImGui::SetNextItemWidth(360); patternChanged|=ImGui::InputFloat3("Total Offset XYZ",lineStep,"%.3f");
    }
    else if (patternType==PatternType::Circle)
    {
        patternChanged|=ImGui::SliderInt("Pattern Count",&circleCount,2,72);
        ImGui::Text("Sweep"); ImGui::SetNextItemWidth(-1.0f);
        patternChanged|=ImGui::SliderInt("##CircleSweep",&circleSweep,1,1080,"%d degrees");
        RenderSweepMarks();
        patternChanged|=ImGui::InputFloat("Radius",&circleRadius,1.0f,10.0f,"%.3f");
        circleRadius=(std::max)(1.0f,circleRadius);
        patternChanged|=ImGui::InputFloat("Total Vertical Offset",&circleVerticalStep,1.0f,10.0f,"%.3f");
        patternChanged|=ImGui::Checkbox("Keep Orientation",&circleKeepOrientation);
    }
    else if (patternType==PatternType::Square)
    {
        int origin=static_cast<int>(squareOrigin);
        if (ImGui::RadioButton("From Corner",&origin,0)) patternChanged=true;
        ImGui::SameLine(); if (ImGui::RadioButton("From Edge",&origin,1)) patternChanged=true;
        ImGui::SameLine(); if (ImGui::RadioButton("From Center",&origin,2)) patternChanged=true;
        squareOrigin=static_cast<SquareOrigin>(origin);
        if (squareOrigin==SquareOrigin::Center)
        {
            patternChanged|=ImGui::SliderInt("Copies Each Side X",&squareCenterX,0,10);
            patternChanged|=ImGui::SliderInt("Copies Each Side Y",&squareCenterY,0,10);
        }
        else
        {
            patternChanged|=ImGui::SliderInt("Count X",&squareX,1,10);
            patternChanged|=ImGui::SliderInt("Count Y",&squareY,1,10);
        }
        ImGui::SetNextItemWidth(300); patternChanged|=ImGui::InputFloat2("Spacing XY",squareSpacing,"%.3f");
        patternChanged|=ImGui::InputFloat("Vertical Offset",&squareVerticalOffset,1.0f,10.0f,"%.3f");
    }
    else
    {
        patternChanged|=ImGui::SliderInt3("Count XYZ",cubeCount,1,10);
        ImGui::SetNextItemWidth(360); patternChanged|=ImGui::InputFloat3("Spacing XYZ",cubeSpacing,"%.3f");
    }
    if (patternChanged && hasXml)
    {
        uiChanged=true;
        RebuildPatternPreservingMain();
        status="Updated pattern: "+std::to_string(instances.size())+" total instances, "+
            std::to_string(generatedProps.size())+" decorations.";
    }
    if (hasXml) ImGui::Text("Total Instances: %d    Total Decorations: %d",
        static_cast<int>(instances.size()),static_cast<int>(generatedProps.size()));

    ImGui::Dummy({0,16}); RenderSectionHeading("Position & Rotation");
    if (ImGui::RadioButton("Move",&operationMode,0)) { activeControl=0; inputCaptured=false; }
    ImGui::SameLine(); if (ImGui::RadioButton("Rotate",&operationMode,1)) { activeControl=0; inputCaptured=false; }
    ImGui::SameLine(); if (ImGui::RadioButton("Pattern Rotate",&operationMode,2)) { activeControl=0; inputCaptured=false; }

    float position[3]={static_cast<float>(mainCenter.x),static_cast<float>(mainCenter.y),static_cast<float>(mainCenter.z)};
    ImGui::Text("Move"); ImGui::SameLine(); ImGui::SetNextItemWidth(360);
    if (hasXml && ImGui::InputFloat3("##PatternPosition",position,"%.3f"))
    {
        uiChanged=true;
        translation=Add(translation,{position[0]-mainCenter.x,position[1]-mainCenter.y,position[2]-mainCenter.z});
        RebuildPattern(false); status="Updated the complete pattern position.";
    }
    else if (!hasXml)
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled,true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha,0.5f);
        ImGui::InputFloat3("##PatternPosition",position,"%.3f"); ImGui::PopStyleVar(); ImGui::PopItemFlag();
    }
    ImGui::Text("Object Rotate"); ImGui::SameLine(); ImGui::SetNextItemWidth(320);
    float objectEdited[3]={objectRotationDegrees[0],objectRotationDegrees[1],objectRotationDegrees[2]};
    if (hasXml && ImGui::InputFloat3("##PatternObjectRotation",objectEdited,"%.3f"))
    {
        uiChanged=true;
        objectRotation=DegreesToMatrix(objectEdited); MatrixToDegrees(objectRotation,objectRotationDegrees);
        RebuildPattern(false); status="Rotated every pattern instance in place.";
    }
    ImGui::Text("Pattern Rotate"); ImGui::SameLine(); ImGui::SetNextItemWidth(320);
    float patternEdited[3]={patternRotationDegrees[0],patternRotationDegrees[1],patternRotationDegrees[2]};
    if (hasXml && ImGui::InputFloat3("##WholePatternRotation",patternEdited,"%.3f"))
    {
        uiChanged=true;
        wholePatternRotation=DegreesToMatrix(patternEdited); MatrixToDegrees(wholePatternRotation,patternRotationDegrees);
        RebuildPattern(false); status="Rotated the complete pattern around its center.";
    }

    if (sourceMode==SourceMode::XmlGroups && hasXml)
    {
        if (uiChanged && !historyActionActive)
        {
            pendingHistoryBefore=frameBefore; pendingHistoryLabel="pattern settings";
            historyActionActive=true; uiHistoryActive=true;
        }
        if (uiHistoryActive && historyActionActive && !ImGui::IsAnyItemActive())
        { CommitPatternHistory(); uiHistoryActive=false; }
    }

    ImGui::Dummy({0,16});
    if (sourceMode==SourceMode::FullXml)
    {
        if (hasXml)
        {
            if (ImGui::Button("Export Pattern XML")) ExportPattern();
        }
        else RenderDisabledButton("Export Pattern XML");
    }
    else
    {
        const float buttonWidth=(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x*2.0f)/3.0f;
        if (!undoHistory.empty())
        { if (ImGui::Button("Undo##Pattern",{buttonWidth,0})) UndoPattern(); }
        else RenderDisabledButton("Undo##Pattern",{buttonWidth,0});
        ImGui::SameLine();
        if (!redoHistory.empty())
        { if (ImGui::Button("Redo##Pattern",{buttonWidth,0})) RedoPattern(); }
        else RenderDisabledButton("Redo##Pattern",{buttonWidth,0});
        ImGui::SameLine();
        if (hasXml)
        { if (ImGui::Button("Apply to XML",{buttonWidth,0})) ApplyPatternToXml(); }
        else RenderDisabledButton("Apply to XML",{buttonWidth,0});
    }
}

void PatternsTab::RenderOverlay()
{
    if (allSourceProps.empty()) { hoveredControl=activeControl=0; hoveredPointGroup=-1; inputCaptured=false; return; }
    Mumble::Data* mumble=AppRuntime::GetMumble();
    if (mumble==nullptr || mumble->Context.MapID==0) { hoveredControl=activeControl=0; inputCaptured=false; return; }
    ImGuiIO& io=ImGui::GetIO();
    if (!inputCaptured && io.MousePos.x>=0 && io.MousePos.y>=0)
    { wndMousePosition=io.MousePos; mouseDown=io.MouseDown[0]; }
    const Camera camera=CameraFromMumble(*mumble); ImDrawList* draw=ImGui::GetBackgroundDrawList();
    DrawGroupSelection(camera,io.DisplaySize,draw);
    if (!sourceProps.empty())
    { DrawPreview(camera,io.DisplaySize,draw); DrawManipulators(camera,io.DisplaySize,draw); }
}

void PatternsTab::ClearImportedData()
{
    DecorationCounterWindow::Clear(); xmlSource.clear(); importedPath.clear(); importedFileName.clear();
    allSourceProps.clear(); sourceProps.clear(); groups.clear(); instances.clear(); generatedProps.clear();
    copyInsertStart=0; xmlType=-1; sourceMode=SourceMode::FullXml; selectedGroupIndex=-1;
    selectedXmlIndex=-1; status="No XML imported"; sourcePivot={}; translation={};
    objectRotation=IdentityMatrix(); wholePatternRotation=IdentityMatrix();
    objectRotationDegrees[0]=objectRotationDegrees[1]=objectRotationDegrees[2]=0;
    patternRotationDegrees[0]=patternRotationDegrees[1]=patternRotationDegrees[2]=0;
    hoveredControl=activeControl=activeAxis=0; inputCaptured=mouseDown=clickPending=false;
    activeDecoUnitsPerPixel=0.0f; activeWorldUnitsPerPixel=0.0f;
    hoveredPointGroup=-1; pointClickPending=pointRightClickPending=pointRightClickCaptured=false;
    groupApplyWritten=false;
    ClearPatternHistory();
    operationMode=0;
}

UINT PatternsTab::WndProc(HWND,UINT message,WPARAM,LPARAM lParam)
{
    switch (message)
    {
    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        wndMousePosition={static_cast<float>(static_cast<short>(LOWORD(lParam))),
            static_cast<float>(static_cast<short>(HIWORD(lParam)))}; break;
    default: return 1;
    }
    const bool hoveringGroup=sourceMode==SourceMode::XmlGroups && hoveredPointGroup>=0;
    const bool canClaim=inputCaptured || activeControl!=0 || hoveredControl!=0 || hoveringGroup;
    if (message==WM_LBUTTONDOWN || message==WM_LBUTTONDBLCLK)
    {
        if (hoveringGroup && activeControl==0)
        { pointClickPending=true; return 0; }
        if (canClaim) { inputCaptured=true; mouseDown=true; clickPending=true; return 0; }
    }
    else if (message==WM_LBUTTONUP)
    {
        mouseDown=false;
        if (inputCaptured || activeControl!=0)
        { CommitPatternHistory(); inputCaptured=false; activeControl=activeAxis=0; return 0; }
    }
    else if (message==WM_RBUTTONDOWN && hoveringGroup && activeControl==0)
    { pointRightClickPending=true; pointRightClickCaptured=true; return 0; }
    else if (message==WM_RBUTTONUP && pointRightClickCaptured)
    { pointRightClickCaptured=false; return 0; }
    else if (message==WM_MOUSEMOVE && (inputCaptured || activeControl!=0)) return 0;
    return 1;
}
