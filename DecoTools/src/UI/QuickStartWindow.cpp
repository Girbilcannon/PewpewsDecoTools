// Pewpew's Deco Tools - First Launch and Quick Start Guide
// Guides initial folder/API configuration, offers a complete layout backup,
// and remains available afterward as a non-modal grouping/recovery tutorial.

#include "QuickStartWindow.h"

#include "../Core/AppSettings.h"
#include "../Core/GroupBackupDatabase.h"
#include "../Core/Utf8Paths.h"
#include "../Core/XmlFileUtils.h"
#include "../imgui/imgui.h"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    // Central visual controls for the complete Quick Start window.
    constexpr float BodyFontScale = 1.10f;
    constexpr float ContentPadding = 16.0f;
    constexpr float VerticalItemSpacing = 8.0f;

    enum class Page
    {
        Welcome,
        BackupOffer,
        BackupSelection,
        BackupProgress,
        BackupResult,
        ApiKey,
        TutorialGroups,
        TutorialMoving,
        TutorialRestoring
    };

    struct BackupCandidate
    {
        std::string name;
        std::string path;
        int xmlType = -1;
        size_t groupCount = 0;
        size_t propCount = 0;
        bool selected = true;
    };

    bool initialized = false;
    bool visible = false;
    bool initialSetupActive = false;
    Page page = Page::Welcome;
    std::vector<BackupCandidate> backupCandidates;
    size_t backupCreated = 0;
    size_t backupFailed = 0;
    size_t backupProcessedIndex = 0;
    size_t backupSelectedTotal = 0;
    size_t backupProcessedTotal = 0;
    size_t invalidFiles = 0;
    std::string backupDetails;

    void Heading(const char* text)
    {
        ImGui::SetWindowFontScale(1.35f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.68f, 1.0f, 1.0f));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(BodyFontScale);
        ImGui::Separator();
        ImGui::Dummy({ 0.0f, 4.0f });
    }

    void Subheading(const char* text)
    {
        ImGui::SetWindowFontScale(1.15f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.68f, 1.0f, 1.0f));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(BodyFontScale);
        ImGui::Separator();
        ImGui::Dummy({ 0.0f, 3.0f });
    }

    void Note(const char* text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.25f, 1.0f));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

    void Bullet(const char* text)
    {
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextWrapped("%s", text);
    }

    void MarkSetupComplete()
    {
        AppSettings::Data& settings = AppSettings::Get();
        settings.quickStartCompleted = true;
        settings.windowVisible = true;
        AppSettings::SaveNow();
        initialSetupActive = false;
    }

    void CloseGuide()
    {
        MarkSetupComplete();
        visible = false;
    }

    const char* TypeLabel(int xmlType)
    {
        return xmlType == 1 ? "Guild Hall" : "Homestead";
    }

    void AddCandidatesFromFolder(const char* folder)
    {
        if (folder == nullptr || folder[0] == '\0') return;
        std::vector<XmlFileUtils::Entry> files;
        if (!XmlFileUtils::List(folder, false, files)) return;

        std::unordered_set<std::string> knownPaths;
        for (const BackupCandidate& candidate : backupCandidates)
            knownPaths.insert(candidate.path);

        for (const XmlFileUtils::Entry& file : files)
        {
            if (!knownPaths.insert(file.path).second) continue;
            int xmlType = -1;
            size_t groupCount = 0;
            size_t propCount = 0;
            if (!GroupBackupDatabase::InspectFile(
                file.path, xmlType, groupCount, propCount))
            {
                ++invalidFiles;
                continue;
            }
            backupCandidates.push_back({
                file.name, file.path, xmlType, groupCount, propCount, true });
        }
    }

    void RefreshBackupCandidates()
    {
        backupCandidates.clear();
        invalidFiles = 0;
        const AppSettings::Data& settings = AppSettings::Get();
        AddCandidatesFromFolder(settings.homesteadFolder.data());
        AddCandidatesFromFolder(settings.guildHallFolder.data());
        std::sort(backupCandidates.begin(), backupCandidates.end(),
            [](const BackupCandidate& left, const BackupCandidate& right)
            {
                if (left.xmlType != right.xmlType) return left.xmlType < right.xmlType;
                return left.name < right.name;
            });
    }

    void StartSelectedBackups()
    {
        backupCreated = 0;
        backupFailed = 0;
        backupProcessedIndex = 0;
        backupProcessedTotal = 0;
        backupSelectedTotal = static_cast<size_t>(std::count_if(
            backupCandidates.begin(), backupCandidates.end(),
            [](const BackupCandidate& candidate) { return candidate.selected; }));
        backupDetails.clear();
        page = backupSelectedTotal == 0 ? Page::BackupResult : Page::BackupProgress;
    }

    void ProcessNextBackup()
    {
        while (backupProcessedIndex < backupCandidates.size() &&
            !backupCandidates[backupProcessedIndex].selected)
            ++backupProcessedIndex;

        if (backupProcessedIndex >= backupCandidates.size())
        {
            page = Page::BackupResult;
            return;
        }

        const BackupCandidate& candidate = backupCandidates[backupProcessedIndex++];
        std::string status;
        if (GroupBackupDatabase::RecordFile(
            candidate.path,
            candidate.xmlType,
            GroupBackupDatabase::RestorePointType::Manual,
            "Initial Setup Backup",
            status))
        {
            ++backupCreated;
        }
        else
        {
            ++backupFailed;
            if (!backupDetails.empty()) backupDetails += "\n";
            backupDetails += candidate.name + ": " + status;
        }
        ++backupProcessedTotal;
    }

    void SelectAll(bool selected)
    {
        for (BackupCandidate& candidate : backupCandidates)
            candidate.selected = selected;
    }

    void RenderWelcome()
    {
        Heading("WELCOME TO DECO TOOLS");
        ImGui::TextWrapped(
            "If this is not your first time using Deco Tools, please feel free "
            "to close this window. If you are new, take a moment to follow this "
            "quick start guide.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Before we begin, please check that the following XML storage "
            "locations are correct. If not, type or paste the full path now.");

        AppSettings::Data& settings = AppSettings::Get();
        ImGui::Dummy({ 0.0f, 10.0f });
        ImGui::TextUnformatted("Homestead XML Storage");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##QuickStartHomesteadFolder",
            settings.homesteadFolder.data(), settings.homesteadFolder.size()))
            AppSettings::MarkDirty();

        ImGui::Spacing();
        ImGui::TextUnformatted("Guild Hall XML Storage");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##QuickStartGuildHallFolder",
            settings.guildHallFolder.data(), settings.guildHallFolder.size()))
            AppSettings::MarkDirty();

        ImGui::Dummy({ 0.0f, 12.0f });
        if (ImGui::Button("Looks Good!", { -1.0f, 0.0f }))
        {
            AppSettings::SaveNow();
            page = Page::BackupOffer;
        }
    }

    void RenderBackupOffer()
    {
        Heading("BACK UP YOUR LAYOUTS");
        ImGui::TextWrapped(
            "Now that the XML folders are defined, would you like Deco Tools "
            "to create a full backup of your layouts?");
        ImGui::Dummy({ 0.0f, 8.0f });
        Subheading("Why does Deco Tools create automatic backups?");
        ImGui::TextWrapped(
            "Exporting a Layout from Guild Wars 2 recreates the XML file and "
            "removes all Deco Tools group information without warning. Automatic "
            "backups allow Deco Tools to recognize that Layout the next time it "
            "is imported and restore its groups. They also provide recovery points "
            "that can help rebuild a lost or accidentally overwritten Layout.");
        ImGui::Spacing();
        Note(
            "This setup backup is manual and complete: it stores every grouped "
            "and ungrouped decoration in each selected Layout.");

        ImGui::Dummy({ 0.0f, 14.0f });
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float width = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
        if (ImGui::Button("Yes Please!", { width, 0.0f }))
        {
            RefreshBackupCandidates();
            page = Page::BackupSelection;
        }
        ImGui::SameLine();
        if (ImGui::Button("No, Thank You", { width, 0.0f }))
            page = Page::ApiKey;
    }

    void RenderBackupSelection()
    {
        Heading("SELECT LAYOUTS TO BACK UP");
        ImGui::TextWrapped(
            "Select any Layouts you want stored as an Initial Setup Backup. "
            "Only XML files in the root of the two configured folders are shown.");
        ImGui::Spacing();
        if (ImGui::SmallButton("Select All")) SelectAll(true);
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear All")) SelectAll(false);
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh")) RefreshBackupCandidates();

        const float listHeight = (std::max)(130.0f, ImGui::GetContentRegionAvail().y - 82.0f);
        ImGui::BeginChild("##QuickStartBackupList", { 0.0f, listHeight }, true);
        if (backupCandidates.empty())
        {
            ImGui::TextWrapped(
                "No valid Layout XML files were found. Check the folder paths on "
                "the previous page, or skip this step and create backups later.");
        }
        else
        {
            int previousType = -1;
            for (size_t index = 0; index < backupCandidates.size(); ++index)
            {
                BackupCandidate& candidate = backupCandidates[index];
                if (candidate.xmlType != previousType)
                {
                    if (previousType >= 0) ImGui::Spacing();
                    ImGui::TextDisabled("%s Layouts", TypeLabel(candidate.xmlType));
                    previousType = candidate.xmlType;
                }
                ImGui::PushID(static_cast<int>(index));
                ImGui::Checkbox("##Selected", &candidate.selected);
                ImGui::SameLine();
                ImGui::Text("%s (%zu decorations, %zu groups)",
                    candidate.name.c_str(), candidate.propCount, candidate.groupCount);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", candidate.path.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        if (invalidFiles > 0)
            ImGui::TextDisabled("%zu invalid XML file(s) were excluded.", invalidFiles);

        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float width = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
        if (ImGui::Button("Back Up Selected", { width, 0.0f })) StartSelectedBackups();
        ImGui::SameLine();
        if (ImGui::Button("Skip", { width, 0.0f })) page = Page::ApiKey;
    }

    void RenderBackupProgress()
    {
        Heading("BACKING UP YOUR LAYOUTS");
        ImGui::TextWrapped(
            "Deco Tools is creating complete Initial Setup Backup restore points. "
            "You may continue when the selected files are finished.");
        ImGui::Dummy({ 0.0f, 18.0f });
        const float progress = backupSelectedTotal == 0 ? 1.0f :
            static_cast<float>(backupProcessedTotal) /
            static_cast<float>(backupSelectedTotal);
        const std::string overlay = std::to_string(backupProcessedTotal) + " / " +
            std::to_string(backupSelectedTotal);
        ImGui::ProgressBar(progress, { -1.0f, 0.0f }, overlay.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("%zu backed up | %zu failed", backupCreated, backupFailed);

        // Process one XML per rendered frame so a large folder never locks the UI.
        ProcessNextBackup();
    }

    void RenderBackupResult()
    {
        Heading(backupCreated > 0 ? "BACKUP COMPLETE" : "BACKUP NOT CREATED");
        if (backupCreated > 0)
        {
            ImGui::TextWrapped(
                "Awesome! Your selected files have been stored in the backup "
                "database. If you ever need to rebuild one, you may do so from "
                "Group Backup/Restore at any time.");
        }
        else
        {
            ImGui::TextWrapped(
                "No backup was created. You can create a complete manual backup "
                "from Group Backup/Restore at any time.");
        }
        ImGui::Dummy({ 0.0f, 10.0f });
        ImGui::Text("%zu backed up | %zu failed", backupCreated, backupFailed);
        if (!backupDetails.empty())
        {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", backupDetails.c_str());
        }
        ImGui::Dummy({ 0.0f, 14.0f });
        if (ImGui::Button("Next", { -1.0f, 0.0f })) page = Page::ApiKey;
    }

    void RenderApiKey()
    {
        Heading("GUILD WARS 2 API KEY");
        ImGui::TextWrapped(
            "A valid GW2 API key unlocks accurate decoration counts and guild "
            "information. If you are not interested, leave this field blank and "
            "continue.");
        ImGui::Spacing();
        Note(
            "Your API key is stored locally in Deco Tools settings and is only "
            "sent to the official Guild Wars 2 API when account or guild "
            "information is requested.");

        AppSettings::Data& settings = AppSettings::Get();
        ImGui::Dummy({ 0.0f, 10.0f });
        ImGui::TextUnformatted("API Key");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##QuickStartApiKey", settings.apiKey.data(),
            settings.apiKey.size(), ImGuiInputTextFlags_Password))
            AppSettings::MarkDirty();

        ImGui::Dummy({ 0.0f, 12.0f });
        ImGui::PushStyleColor(ImGuiCol_Text, { 0.30f, 0.65f, 1.0f, 1.0f });
        if (ImGui::Selectable("Create a Guild Wars 2 API key", false))
            ShellExecuteW(nullptr, L"open",
                L"https://account.arena.net/applications", nullptr, nullptr, SW_SHOWNORMAL);
        ImGui::PopStyleColor();
        ImGui::TextWrapped(
            "Please enable Guilds, Account, Inventories, and Unlocks at minimum.");

        ImGui::Dummy({ 0.0f, 16.0f });
        if (ImGui::Button("Finish Setup", { -1.0f, 0.0f }))
        {
            MarkSetupComplete();
            AppSettings::Get().showDecorationCounter = true;
            AppSettings::SaveNow();
            page = Page::TutorialGroups;
        }
    }

    void RenderTutorialGroups()
    {
        Heading("QUICK START: CREATING GROUPS");
        ImGui::TextWrapped(
            "Congratulations on getting everything set up! Start by importing a "
            "Layout XML at the top of the main window, preferably the Layout you "
            "currently have open in-game. Then open Group Tools.");
        ImGui::Dummy({ 0.0f, 8.0f });
        Bullet("Choose Group Decorations. It is the default Group Tools operation.");
        Bullet("Ungrouped decoration points are orange. Selected points are blue. Grouped points are gray when shown.");
        Bullet("Left-click a point to select it and right-click to deselect it.");
        Bullet("Enter a group name and click Create Group.");
        Bullet("Use Visibility Distance when too many distant decoration points are visible.");
        Bullet("Enable Marquee Select to drag a box around multiple points at once.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Congratulations, you have created your first group! Create as many "
            "groups as you need. The active Layout is updated whenever a group is created.");
        ImGui::Dummy({ 0.0f, 12.0f });
        if (ImGui::Button("Next Tutorial: Moving", { -1.0f, 0.0f }))
            page = Page::TutorialMoving;
    }

    void RenderTutorialMoving()
    {
        Heading("QUICK START: MOVING GROUPS");
        ImGui::TextWrapped(
            "Now that your Layout contains one or more groups, navigate to the Move Tool.");
        ImGui::Dummy({ 0.0f, 8.0f });
        Bullet("Your groups appear automatically.");
        Bullet("Select one or more groups to move at the same time.");
        Bullet("Left-click a group point to select it and right-click to deselect it.");
        Bullet("Choose Move or Rotate and use the colored controls or numerical fields.");
        Bullet("Move to Character places the selected group center at your character's current 3D position.");
        Bullet("Click Apply to XML when the group is positioned correctly.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Apply updates the active XML. Reload that Layout through Guild Wars 2's "
            "Layout menu to see the changes in-game. Repeat this workflow as often as needed.");
        ImGui::Dummy({ 0.0f, 12.0f });
        if (ImGui::Button("Next Tutorial: Restoring Your Layout", { -1.0f, 0.0f }))
            page = Page::TutorialRestoring;
    }

    void RenderTutorialRestoring()
    {
        Heading("QUICK START: RESTORING YOUR LAYOUT");
        ImGui::TextWrapped(
            "Let's learn how Deco Tools can rebuild a Layout from one of its backups.");
        ImGui::Spacing();
        Note(
            "This creates a new recovered XML file; your current Layout will remain unchanged.");
        ImGui::Dummy({ 0.0f, 8.0f });
        Bullet("Navigate to Group Backup/Restore.");
        Bullet("At the bottom of the page, click Rebuild XML.");
        Bullet("Select Initial Setup Backup. If you skipped it, select the earliest complete backup available for this Layout.");
        Bullet("Enter a new name, such as My Layout Recovered.");
        Bullet("Click Rebuild. Deco Tools adds .xml automatically if needed and saves the recovered file in the correct Layout folder.");
        ImGui::Spacing();
        Note(
            "Grouped Layouts are backed up automatically so their groups can be "
            "restored after an in-game export. Enable Backup Ungrouped XMLs in "
            "Settings if you also want automatic complete-Layout backups before "
            "a Layout contains any groups.");
        ImGui::Dummy({ 0.0f, 10.0f });
        Subheading("YOU HAVE DONE IT!");
        ImGui::TextWrapped(
            "You have reached the end of the current tutorial. Additional tutorials "
            "may be added in future releases and can be accessed through Settings.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Please refer to the Documentation window to learn about each tool "
            "and its features in greater detail.");
        ImGui::Spacing();
        ImGui::TextWrapped("Thank you, and enjoy Pewpew's Deco Tools!");
        ImGui::Dummy({ 0.0f, 12.0f });
        if (ImGui::Button("Finish and Close", { -1.0f, 0.0f })) CloseGuide();
    }
}

void QuickStartWindow::Initialize()
{
    if (initialized) return;
    initialized = true;
    initialSetupActive = !AppSettings::Get().quickStartCompleted;
    visible = initialSetupActive;
    page = Page::Welcome;
}

void QuickStartWindow::Shutdown()
{
    backupCandidates.clear();
    backupDetails.clear();
    initialized = false;
    visible = false;
    initialSetupActive = false;
}

void QuickStartWindow::Open()
{
    if (!initialized) Initialize();
    visible = true;
    initialSetupActive = false;
    page = Page::Welcome;
}

bool QuickStartWindow::IsInitialSetupActive()
{
    return initialized && visible && initialSetupActive;
}

void QuickStartWindow::Render()
{
    if (!initialized) Initialize();
    if (!visible) return;

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(
        ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f),
        ImGuiCond_Appearing,
        ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(
        ImVec2(620.0f, 560.0f),
        ImGuiCond_FirstUseEver);
    bool open = visible;
    if (ImGui::Begin("Welcome to Pewpew's Deco Tools", &open,
        ImGuiWindowFlags_NoCollapse))
    {
        const ImVec2 defaultSpacing = ImGui::GetStyle().ItemSpacing;
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(ContentPadding, ContentPadding));
        ImGui::PushStyleVar(
            ImGuiStyleVar_ItemSpacing,
            ImVec2(defaultSpacing.x, VerticalItemSpacing));
        ImGui::BeginChild(
            "##QuickStartContent",
            { 0.0f, 0.0f },
            false,
            ImGuiWindowFlags_AlwaysUseWindowPadding);
        ImGui::SetWindowFontScale(BodyFontScale);
        switch (page)
        {
        case Page::Welcome: RenderWelcome(); break;
        case Page::BackupOffer: RenderBackupOffer(); break;
        case Page::BackupSelection: RenderBackupSelection(); break;
        case Page::BackupProgress: RenderBackupProgress(); break;
        case Page::BackupResult: RenderBackupResult(); break;
        case Page::ApiKey: RenderApiKey(); break;
        case Page::TutorialGroups: RenderTutorialGroups(); break;
        case Page::TutorialMoving: RenderTutorialMoving(); break;
        case Page::TutorialRestoring: RenderTutorialRestoring(); break;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    }
    ImGui::End();

    if (!open) CloseGuide();
}
