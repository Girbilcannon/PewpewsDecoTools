#include "DecorationCounterWindow.h"

#include "../Core/AppSettings.h"
#include "../Core/Gw2Api.h"
#include "../imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <set>
#include <sstream>

namespace AppRuntime
{
    const std::string& GetAddonDirectory();
}

namespace
{
    enum class JobKind { None, Guilds, Counts };
    struct JobResult
    {
        JobKind kind = JobKind::None;
        unsigned generation = 0;
        bool success = false;
        std::string error;
        std::vector<Gw2Api::Guild> guilds;
        std::map<int, int> counts;
    };

    std::string context;
    int decorationType = 0;
    std::vector<DecorationCounterWindow::Requirement> requirements;
    std::map<int, int> available;
    bool availabilityKnown = false;
    std::string selectedGuildId;
    int selectedGuildIndex = -1;
    std::vector<Gw2Api::Guild> guilds;
    std::string status = "Load an XML to view decoration counts.";
    std::future<JobResult> job;
    JobKind jobKind = JobKind::None;
    unsigned generation = 0;

    std::vector<int> RequiredIds()
    {
        std::set<int> unique;
        for (const auto& item : requirements)
        {
            if (item.id >= 0) unique.insert(item.id);
        }
        return { unique.begin(), unique.end() };
    }

    void StartCounts()
    {
        if (jobKind != JobKind::None || requirements.empty()) return;
        const std::string apiKey = AppSettings::Get().apiKey.data();
        if (apiKey.empty())
        {
            status = "Enter an API key in Settings to load available counts.";
            return;
        }
        if (decorationType == 1 && selectedGuildId.empty())
        {
            status = "Select a guild to load available counts.";
            return;
        }

        const unsigned currentGeneration = generation;
        const int type = decorationType;
        const std::string guildId = selectedGuildId;
        const std::vector<int> ids = RequiredIds();
        jobKind = JobKind::Counts;
        status = "Loading available decoration counts...";
        job = std::async(std::launch::async,
            [apiKey, currentGeneration, type, guildId, ids]()
            {
                JobResult result;
                result.kind = JobKind::Counts;
                result.generation = currentGeneration;
                result.success = Gw2Api::LoadCounts(
                    apiKey, type, guildId, ids, result.counts, result.error);
                return result;
            });
    }

    void StartGuilds()
    {
        if (jobKind != JobKind::None) return;
        const std::string apiKey = AppSettings::Get().apiKey.data();
        if (apiKey.empty())
        {
            status = "Enter an API key in Settings to load guilds.";
            return;
        }
        const unsigned currentGeneration = generation;
        jobKind = JobKind::Guilds;
        status = "Loading guilds...";
        job = std::async(std::launch::async, [apiKey, currentGeneration]()
        {
            JobResult result;
            result.kind = JobKind::Guilds;
            result.generation = currentGeneration;
            result.success =
                Gw2Api::LoadGuilds(apiKey, result.guilds, result.error);
            return result;
        });
    }

    void Poll()
    {
        if (jobKind == JobKind::None || !job.valid() ||
            job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            return;
        }
        JobResult result;
        try { result = job.get(); }
        catch (...) { result.error = "The background count request failed."; }
        jobKind = JobKind::None;
        if (result.generation != generation)
        {
            if (!requirements.empty())
            {
                if (decorationType == 0 || !selectedGuildId.empty()) StartCounts();
                else StartGuilds();
            }
            return;
        }
        if (!result.success)
        {
            status = result.error.empty() ? "Could not load decoration counts." : result.error;
            return;
        }
        if (result.kind == JobKind::Guilds)
        {
            guilds = std::move(result.guilds);
            selectedGuildIndex = guilds.size() == 1 ? 0 : -1;
            if (selectedGuildIndex == 0)
            {
                selectedGuildId = guilds[0].id;
                StartCounts();
            }
            else
            {
                status = guilds.empty()
                    ? "No guilds were returned for this account."
                    : "Select the guild whose available decorations should be checked.";
            }
        }
        else
        {
            available = std::move(result.counts);
            availabilityKnown = true;
            status = "Decoration counts loaded.";
        }
    }

    std::string SafeStem(std::string value)
    {
        for (char& character : value)
        {
            const unsigned char byte = static_cast<unsigned char>(character);
            if (!(std::isalnum(byte) || character == '-' || character == '_'))
            {
                character = '_';
            }
        }
        while (!value.empty() && value.back() == '_') value.pop_back();
        return value.empty() ? "Decoration_List" : value;
    }

    void ExportList()
    {
        const std::filesystem::path folder(AppRuntime::GetAddonDirectory());
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        const std::string stem = SafeStem(context) + "_DecoCount";
        std::filesystem::path output = folder / (stem + ".txt");
        for (int index = 2; std::filesystem::exists(output, error); ++index)
        {
            output = folder / (stem + std::to_string(index) + ".txt");
        }

        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            status = "Could not create the decoration count text file.";
            return;
        }
        size_t width = 10;
        for (const auto& item : requirements)
        {
            width = (std::max)(width, item.name.size());
        }
        file << context << "\n";
        file << std::string(context.size(), '-') << "\n\n";
        file << std::left << std::setw(static_cast<int>(width))
            << "Decoration" << " | Required | Available\n";
        file << std::string(width, '-') << "-+----------+----------\n";
        for (const auto& item : requirements)
        {
            file << std::left << std::setw(static_cast<int>(width)) << item.name
                << " | " << std::right << std::setw(8) << item.required << " | ";
            if (availabilityKnown)
            {
                const auto found = available.find(item.id);
                file << std::setw(9) << (found == available.end() ? 0 : found->second);
            }
            else
            {
                file << std::setw(9) << "N/A";
            }
            file << "\n";
        }
        status = "Exported " + output.filename().string() + ".";
    }
}

void DecorationCounterWindow::SetRequirements(
    const std::string& newContext,
    int newType,
    const std::vector<Requirement>& newRequirements,
    const std::string& guildId
)
{
    ++generation;
    context = newContext;
    decorationType = newType;
    requirements = newRequirements;
    std::sort(requirements.begin(), requirements.end(),
        [](const Requirement& left, const Requirement& right)
        {
            return left.name < right.name;
        });
    available.clear();
    availabilityKnown = false;
    selectedGuildId = guildId;
    selectedGuildIndex = -1;
    guilds.clear();
    status = requirements.empty()
        ? "The loaded XML contains no countable decorations."
        : "Preparing decoration counts...";

    if (!requirements.empty())
    {
        if (decorationType == 0 || !selectedGuildId.empty()) StartCounts();
        else StartGuilds();
    }
}

void DecorationCounterWindow::SetResolvedRequirements(
    const std::string& newContext,
    int newType,
    const std::vector<Requirement>& newRequirements,
    const std::map<int, int>& newAvailable
)
{
    ++generation;
    context = newContext;
    decorationType = newType;
    requirements = newRequirements;
    std::sort(requirements.begin(), requirements.end(),
        [](const Requirement& left, const Requirement& right)
        {
            return left.name < right.name;
        });
    available = newAvailable;
    availabilityKnown = true;
    selectedGuildId.clear();
    selectedGuildIndex = -1;
    guilds.clear();
    status = "Decoration counts loaded.";
}

void DecorationCounterWindow::Clear()
{
    ++generation;
    context.clear();
    requirements.clear();
    available.clear();
    availabilityKnown = false;
    selectedGuildId.clear();
    selectedGuildIndex = -1;
    guilds.clear();
    status = "Load an XML to view decoration counts.";
}

void DecorationCounterWindow::Render()
{
    Poll();
    AppSettings::Data& settings = AppSettings::Get();
    if (!settings.showDecorationCounter) return;

    ImGui::SetNextWindowSize(ImVec2(500.0f, 390.0f), ImGuiCond_FirstUseEver);
    bool open = settings.showDecorationCounter;
    if (ImGui::Begin("Decoration Count", &open))
    {
        if (ImGui::Button("Export List"))
        {
            ExportList();
        }
        if (decorationType == 1 && !guilds.empty() && !requirements.empty())
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            const char* label = selectedGuildIndex >= 0
                ? guilds[static_cast<size_t>(selectedGuildIndex)].name.c_str()
                : "Select Guild...";
            if (ImGui::BeginCombo("##CounterGuild", label))
            {
                for (size_t index = 0; index < guilds.size(); ++index)
                {
                    const std::string itemLabel = guilds[index].tag.empty()
                        ? guilds[index].name
                        : guilds[index].name + " [" + guilds[index].tag + "]";
                    if (ImGui::Selectable(
                        itemLabel.c_str(),
                        selectedGuildIndex == static_cast<int>(index)))
                    {
                        selectedGuildIndex = static_cast<int>(index);
                        selectedGuildId = guilds[index].id;
                        StartCounts();
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (!context.empty())
        {
            ImGui::TextWrapped("%s", context.c_str());
        }
        ImGui::Separator();

        ImGui::Columns(3, "##CounterColumns", false);
        ImGui::SetColumnWidth(0, 310.0f);
        ImGui::SetColumnWidth(1, 80.0f);
        ImGui::TextUnformatted("Decoration"); ImGui::NextColumn();
        ImGui::TextUnformatted("Required"); ImGui::NextColumn();
        ImGui::TextUnformatted("Available"); ImGui::NextColumn();
        ImGui::Separator();

        for (const Requirement& item : requirements)
        {
            const auto found = available.find(item.id);
            const int count = found == available.end() ? 0 : found->second;
            const bool missing = availabilityKnown && count < item.required;
            if (missing) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.28f, 0.28f, 1.0f));
            ImGui::TextUnformatted(item.name.c_str()); ImGui::NextColumn();
            ImGui::Text("%d", item.required); ImGui::NextColumn();
            if (availabilityKnown) ImGui::Text("%d", count);
            else ImGui::TextUnformatted("-");
            ImGui::NextColumn();
            if (missing) ImGui::PopStyleColor();
        }
        ImGui::Columns(1);
        ImGui::Separator();
        ImGui::TextDisabled("%s", status.c_str());
    }
    ImGui::End();

    if (open != settings.showDecorationCounter)
    {
        settings.showDecorationCounter = open;
        AppSettings::MarkDirty();
    }
}

void DecorationCounterWindow::Shutdown()
{
    if (job.valid())
    {
        job.wait();
        try { job.get(); } catch (...) {}
    }
    jobKind = JobKind::None;
}
