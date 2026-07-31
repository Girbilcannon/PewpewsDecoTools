#include "MergeExtractTab.h"

#include "../../Core/AppSettings.h"
#include "../../Core/DecorationDatabase.h"
#include "../../imgui/imgui.h"
#include "../../imgui/imgui_internal.h"
#include "../DecorationCounterWindow.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    struct XmlFileEntry
    {
        std::string name;
        std::string path;
    };

    struct Prop
    {
        size_t start = 0;
        size_t end = 0;
        int id = -1;
        std::string name;
    };

    struct XmlDocument
    {
        std::string source;
        std::string path;
        std::string fileName;
        std::string mapName;
        int type = -1;
        size_t rootOpenEnd = 0;
        size_t rootCloseStart = 0;
        std::vector<Prop> props;
    };

    struct Group
    {
        std::string name;
        size_t start = 0;
        size_t end = 0;
        std::vector<Prop> props;
        bool selected = false;
    };

    int operation = 0;
    int selectedFolderType = 0;
    int baseXmlIndex = -1;
    int extractXmlIndex = -1;
    bool fileListInitialized = false;
    std::vector<XmlFileEntry> availableXmlFiles;
    std::vector<unsigned char> additionalSelected;
    std::vector<XmlDocument> mergeDocuments;
    XmlDocument extractDocument;
    std::vector<Group> extractGroups;
    std::string status = "No XML imported";
    std::string report;

    void RenderSectionHeading(const char* label)
    {
        ImGui::SetWindowFontScale(1.2f);
        ImGui::Text("%s", label);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
    }

    void RenderDisabledButton(
        const char* label,
        const ImVec2& size = ImVec2(0.0f, 0.0f)
    )
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        ImGui::Button(label, size);
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }

    std::string FolderForType(int type)
    {
        const AppSettings::Data& settings = AppSettings::Get();
        return type == 1
            ? settings.guildHallFolder.data()
            : settings.homesteadFolder.data();
    }

    bool HasXmlExtension(const std::filesystem::path& path)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        return extension == ".xml";
    }

    void ClearLoaded()
    {
        std::vector<XmlDocument>().swap(mergeDocuments);
        extractDocument = XmlDocument{};
        std::vector<Group>().swap(extractGroups);
        std::string().swap(report);
        DecorationCounterWindow::Clear();
        status = "No XML imported";
    }

    void RefreshXmlList()
    {
        ClearLoaded();
        availableXmlFiles.clear();
        additionalSelected.clear();
        baseXmlIndex = -1;
        extractXmlIndex = -1;
        fileListInitialized = true;

        const std::string folder = FolderForType(selectedFolderType);
        if (folder.empty())
        {
            status = "Set this XML folder path in Settings first.";
            return;
        }

        std::error_code error;
        if (!std::filesystem::is_directory(folder, error))
        {
            status = "XML folder not found. Check the path in Settings.";
            return;
        }

        for (std::filesystem::directory_iterator iterator(folder, error), end;
            !error && iterator != end;
            iterator.increment(error))
        {
            if (iterator->is_regular_file(error) && HasXmlExtension(iterator->path()))
            {
                availableXmlFiles.push_back(
                    { iterator->path().filename().string(), iterator->path().string() });
            }
        }
        std::sort(availableXmlFiles.begin(), availableXmlFiles.end(),
            [](const XmlFileEntry& left, const XmlFileEntry& right)
            {
                return left.name < right.name;
            });
        additionalSelected.assign(availableXmlFiles.size(), false);
        if (!availableXmlFiles.empty())
        {
            baseXmlIndex = 0;
            extractXmlIndex = 0;
        }
        status = availableXmlFiles.empty()
            ? "No XML files found in the selected folder."
            : "Found " + std::to_string(availableXmlFiles.size()) + " XML files.";
    }

    size_t FindTagEnd(const std::string& source, size_t tagStart)
    {
        char quote = '\0';
        for (size_t index = tagStart; index < source.size(); ++index)
        {
            const char character = source[index];
            if (quote != '\0')
            {
                if (character == quote) quote = '\0';
            }
            else if (character == '"' || character == '\'')
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

    bool IsSelfClosingTag(
        const std::string& source,
        size_t tagStart,
        size_t tagEnd
    )
    {
        if (tagEnd <= tagStart || tagEnd > source.size()) return false;
        size_t position = tagEnd;
        while (position > tagStart &&
            std::isspace(static_cast<unsigned char>(source[position - 1])) != 0)
        {
            --position;
        }
        return position > tagStart && source[position - 1] == '/';
    }

    bool ReadAttribute(
        const std::string& source,
        size_t tagStart,
        size_t tagEnd,
        const char* attribute,
        std::string& value
    )
    {
        const std::string name(attribute);
        size_t position = tagStart;
        while (true)
        {
            position = source.find(name, position);
            if (position == std::string::npos || position >= tagEnd) return false;
            const bool validBefore =
                position == tagStart ||
                std::isspace(static_cast<unsigned char>(source[position - 1])) != 0;
            size_t equals = position + name.size();
            while (equals < tagEnd &&
                std::isspace(static_cast<unsigned char>(source[equals])) != 0) ++equals;
            if (!validBefore || equals >= tagEnd || source[equals] != '=')
            {
                position += name.size();
                continue;
            }
            ++equals;
            while (equals < tagEnd &&
                std::isspace(static_cast<unsigned char>(source[equals])) != 0) ++equals;
            if (equals >= tagEnd || (source[equals] != '"' && source[equals] != '\''))
            {
                return false;
            }
            const char quote = source[equals];
            const size_t start = equals + 1;
            const size_t end = source.find(quote, start);
            if (end == std::string::npos || end > tagEnd) return false;
            value = source.substr(start, end - start);
            return true;
        }
    }

    bool LoadXml(const std::string& path, XmlDocument& document, std::string& error)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            error = "Could not open " + std::filesystem::path(path).filename().string() + ".";
            return false;
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        document = XmlDocument{};
        document.source = contents.str();
        document.path = path;
        document.fileName = std::filesystem::path(path).filename().string();

        const size_t rootStart = document.source.find("<Decorations");
        const size_t rootEnd = rootStart == std::string::npos
            ? std::string::npos : FindTagEnd(document.source, rootStart);
        const size_t rootClose = document.source.rfind("</Decorations>");
        if (rootStart == std::string::npos ||
            rootEnd == std::string::npos ||
            rootClose == std::string::npos)
        {
            error = document.fileName + " is not a valid Decorations XML.";
            return false;
        }
        document.rootOpenEnd = rootEnd;
        document.rootCloseStart = rootClose;

        std::string typeText;
        if (!ReadAttribute(document.source, rootStart, rootEnd, "type", typeText) ||
            (typeText != "0" && typeText != "1"))
        {
            error = document.fileName + " has an invalid map type.";
            return false;
        }
        document.type = typeText == "1" ? 1 : 0;
        ReadAttribute(document.source, rootStart, rootEnd, "mapName", document.mapName);

        size_t search = rootEnd + 1;
        while (true)
        {
            const size_t propStart = document.source.find("<prop", search);
            if (propStart == std::string::npos || propStart >= rootClose) break;
            const size_t propEnd = FindTagEnd(document.source, propStart);
            if (propEnd == std::string::npos)
            {
                error = document.fileName + " contains an unfinished prop tag.";
                return false;
            }
            Prop prop;
            prop.start = propStart;
            if (IsSelfClosingTag(document.source, propStart, propEnd))
            {
                prop.end = propEnd + 1;
            }
            else
            {
                const size_t propClose = document.source.find("</prop>", propEnd + 1);
                if (propClose == std::string::npos || propClose >= rootClose)
                {
                    error = document.fileName +
                        " contains a prop element without a closing </prop> tag.";
                    return false;
                }
                prop.end = propClose + sizeof("</prop>") - 1;
            }
            std::string idText;
            if (ReadAttribute(document.source, propStart, propEnd, "id", idText))
            {
                try { prop.id = std::stoi(idText); }
                catch (...) { prop.id = -1; }
            }
            const char* name =
                DecorationDatabase::FindNameById(prop.id, document.type);
            prop.name = name == nullptr ? "Unknown Decoration" : name;
            document.props.push_back(std::move(prop));
            search = document.props.back().end;
        }
        return true;
    }

    std::vector<DecorationCounterWindow::Requirement> BuildRequirements(
        const std::vector<XmlDocument>& documents
    )
    {
        std::map<int, DecorationCounterWindow::Requirement> byId;
        for (const XmlDocument& document : documents)
        {
            for (const Prop& prop : document.props)
            {
                auto& entry = byId[prop.id];
                entry.id = prop.id;
                entry.name = prop.name;
                ++entry.required;
            }
        }
        std::vector<DecorationCounterWindow::Requirement> output;
        for (const auto& [id, entry] : byId)
        {
            static_cast<void>(id);
            output.push_back(entry);
        }
        return output;
    }

    std::string Stem(const std::string& fileName)
    {
        return std::filesystem::path(fileName).stem().string();
    }

    std::filesystem::path IndexedPath(
        const std::filesystem::path& folder,
        const std::string& stem,
        const std::string& suffix
    )
    {
        std::error_code error;
        std::filesystem::path output = folder / (stem + suffix + "1.xml");
        for (int index = 2; std::filesystem::exists(output, error); ++index)
        {
            output = folder / (stem + suffix + std::to_string(index) + ".xml");
        }
        return output;
    }

    bool WriteFile(
        const std::filesystem::path& output,
        const std::string& contents,
        std::string& error
    )
    {
        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            error = "Could not create " + output.filename().string() + ".";
            return false;
        }
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!file.good())
        {
            error = "Could not finish writing " + output.filename().string() + ".";
            return false;
        }
        return true;
    }

    void PrepareMerge()
    {
        mergeDocuments.clear();
        report.clear();
        if (baseXmlIndex < 0 ||
            baseXmlIndex >= static_cast<int>(availableXmlFiles.size()))
        {
            status = "Select a base layout first.";
            return;
        }

        std::vector<int> selected;
        for (size_t index = 0; index < additionalSelected.size(); ++index)
        {
            if (additionalSelected[index] &&
                static_cast<int>(index) != baseXmlIndex)
            {
                selected.push_back(static_cast<int>(index));
            }
        }
        if (selected.empty())
        {
            status = "Select at least one additional XML to merge.";
            return;
        }

        std::string error;
        XmlDocument base;
        if (!LoadXml(
            availableXmlFiles[static_cast<size_t>(baseXmlIndex)].path,
            base,
            error))
        {
            status = error;
            return;
        }
        mergeDocuments.push_back(std::move(base));

        bool mismatch = false;
        for (const int index : selected)
        {
            XmlDocument document;
            if (!LoadXml(
                availableXmlFiles[static_cast<size_t>(index)].path,
                document,
                error))
            {
                status = error;
                mergeDocuments.clear();
                return;
            }
            mismatch = mismatch || document.type != mergeDocuments[0].type;
            mergeDocuments.push_back(std::move(document));
        }

        size_t total = 0;
        std::ostringstream output;
        output << "Base Layout: " << mergeDocuments[0].fileName << "\n";
        output << "Map Type: "
            << (mergeDocuments[0].type == 0 ? "Homestead" : "Guild Hall") << "\n\n";
        output << "Additional Files:\n";
        for (size_t index = 1; index < mergeDocuments.size(); ++index)
        {
            output << "  " << mergeDocuments[index].fileName;
            if (mergeDocuments[index].type != mergeDocuments[0].type)
            {
                output << " (MAP TYPE MISMATCH)";
            }
            output << "\n";
        }
        for (const XmlDocument& document : mergeDocuments) total += document.props.size();
        output << "\nTotal Decorations: " << total << " / 2000\n";
        if (total > 2000)
        {
            output << "WARNING: This merge exceeds the 2000 decoration limit.\n";
        }
        output << (mismatch
            ? "\nMerge blocked - all XML map types must match."
            : "\nAll map types match - ready to merge.");
        report = output.str();

        if (mismatch)
        {
            status = "Map type mismatch. Merge is disabled.";
            DecorationCounterWindow::Clear();
            return;
        }

        DecorationCounterWindow::SetRequirements(
            Stem(mergeDocuments[0].fileName) + " Merged Layout",
            mergeDocuments[0].type,
            BuildRequirements(mergeDocuments)
        );
        status = "Merge pre-check complete.";
    }

    void ExportMerge()
    {
        if (mergeDocuments.size() < 2)
        {
            status = "Prepare a valid merge first.";
            return;
        }
        const XmlDocument& base = mergeDocuments[0];
        for (const XmlDocument& document : mergeDocuments)
        {
            if (document.type != base.type)
            {
                status = "Map type mismatch. Merge is disabled.";
                return;
            }
        }

        std::ostringstream addition;
        for (size_t index = 1; index < mergeDocuments.size(); ++index)
        {
            const XmlDocument& document = mergeDocuments[index];
            addition << "\n  <!--" << Stem(document.fileName) << "-->\n";
            for (const Prop& prop : document.props)
            {
                addition << "  "
                    << document.source.substr(prop.start, prop.end - prop.start)
                    << "\n";
            }
        }

        const std::string merged =
            base.source.substr(0, base.rootCloseStart) +
            addition.str() +
            base.source.substr(base.rootCloseStart);
        const std::filesystem::path folder = std::filesystem::path(base.path).parent_path();
        const std::filesystem::path output =
            IndexedPath(folder, Stem(base.fileName), "_MERGED");
        std::string error;
        if (WriteFile(output, merged, error))
        {
            status = "Exported " + output.filename().string() + ".";
        }
        else
        {
            status = error;
        }
    }

    void ImportExtract()
    {
        extractDocument = XmlDocument{};
        extractGroups.clear();
        report.clear();
        if (extractXmlIndex < 0 ||
            extractXmlIndex >= static_cast<int>(availableXmlFiles.size()))
        {
            status = "Select a merged XML first.";
            return;
        }
        std::string error;
        if (!LoadXml(
            availableXmlFiles[static_cast<size_t>(extractXmlIndex)].path,
            extractDocument,
            error))
        {
            status = error;
            return;
        }

        size_t comment = extractDocument.source.find(
            "<!--", extractDocument.rootOpenEnd + 1);
        while (comment != std::string::npos && comment < extractDocument.rootCloseStart)
        {
            const size_t commentEnd = extractDocument.source.find("-->", comment + 4);
            if (commentEnd == std::string::npos) break;
            const size_t next = extractDocument.source.find("<!--", commentEnd + 3);
            const size_t groupEnd =
                next == std::string::npos || next >= extractDocument.rootCloseStart
                ? extractDocument.rootCloseStart
                : next;
            Group group;
            group.name = extractDocument.source.substr(
                comment + 4, commentEnd - comment - 4);
            while (!group.name.empty() &&
                std::isspace(static_cast<unsigned char>(group.name.front())) != 0)
            {
                group.name.erase(group.name.begin());
            }
            while (!group.name.empty() &&
                std::isspace(static_cast<unsigned char>(group.name.back())) != 0)
            {
                group.name.pop_back();
            }
            group.start = comment;
            group.end = groupEnd;
            for (const Prop& prop : extractDocument.props)
            {
                if (prop.start > commentEnd && prop.start < groupEnd)
                {
                    group.props.push_back(prop);
                }
            }
            if (!group.name.empty()) extractGroups.push_back(std::move(group));
            comment = next;
        }

        if (extractGroups.empty())
        {
            status =
                "No merger groups found. Extract works with XMLs created by DecoTools Merge.";
            DecorationCounterWindow::Clear();
            return;
        }

        DecorationCounterWindow::SetRequirements(
            extractDocument.fileName,
            extractDocument.type,
            BuildRequirements({ extractDocument })
        );
        report = "Found " + std::to_string(extractGroups.size()) +
            " named decoration groups.";
        status = "Select one or more groups to extract.";
    }

    std::string SafeFileStem(std::string value)
    {
        for (char& character : value)
        {
            const unsigned char byte = static_cast<unsigned char>(character);
            if (!(std::isalnum(byte) || character == '-' || character == '_'))
            {
                character = '_';
            }
        }
        return value.empty() ? "Decoration_Group" : value;
    }

    void ExportExtract()
    {
        std::vector<const Group*> selected;
        for (const Group& group : extractGroups)
        {
            if (group.selected) selected.push_back(&group);
        }
        if (selected.empty())
        {
            status = "Select at least one group to extract.";
            return;
        }

        const std::filesystem::path folder =
            std::filesystem::path(extractDocument.path).parent_path();
        std::string stripped = extractDocument.source;
        std::vector<const Group*> descending = selected;
        std::sort(descending.begin(), descending.end(),
            [](const Group* left, const Group* right)
            {
                return left->start > right->start;
            });
        for (const Group* group : descending)
        {
            stripped.erase(group->start, group->end - group->start);
        }

        std::string error;
        const std::filesystem::path strippedPath =
            IndexedPath(folder, Stem(extractDocument.fileName), "_STRIPPED");
        if (!WriteFile(strippedPath, stripped, error))
        {
            status = error;
            return;
        }

        int exported = 0;
        for (const Group* group : selected)
        {
            std::ostringstream body;
            body << "\n  <!--" << group->name << "-->\n";
            for (const Prop& prop : group->props)
            {
                body << "  "
                    << extractDocument.source.substr(prop.start, prop.end - prop.start)
                    << "\n";
            }
            const std::string outputXml =
                extractDocument.source.substr(0, extractDocument.rootOpenEnd + 1) +
                body.str() +
                extractDocument.source.substr(extractDocument.rootCloseStart);
            const std::filesystem::path output = IndexedPath(
                folder, SafeFileStem(group->name), "_EXTRACTED");
            if (!WriteFile(output, outputXml, error))
            {
                status = error;
                return;
            }
            ++exported;
        }
        status = "Exported " + std::to_string(exported) +
            " group file(s) and " + strippedPath.filename().string() + ".";
    }

    void RenderFolderChoice()
    {
        if (ImGui::RadioButton("Homestead##MergeFolder", selectedFolderType == 0))
        {
            selectedFolderType = 0;
            RefreshXmlList();
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Guild Hall##MergeFolder", selectedFolderType == 1))
        {
            selectedFolderType = 1;
            RefreshXmlList();
        }
    }

    void RenderXmlCombo(const char* id, int& selectedIndex)
    {
        const char* label =
            selectedIndex >= 0 &&
            selectedIndex < static_cast<int>(availableXmlFiles.size())
            ? availableXmlFiles[static_cast<size_t>(selectedIndex)].name.c_str()
            : "No XML files found";
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo(id, label))
        {
            for (size_t index = 0; index < availableXmlFiles.size(); ++index)
            {
                const bool selected = selectedIndex == static_cast<int>(index);
                if (ImGui::Selectable(availableXmlFiles[index].name.c_str(), selected))
                {
                    selectedIndex = static_cast<int>(index);
                    ClearLoaded();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
}

void MergeExtractTab::Render()
{
    if (!fileListInitialized) RefreshXmlList();

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    RenderSectionHeading("Operation");
    if (ImGui::RadioButton("Merge XML Files", operation == 0))
    {
        operation = 0;
        ClearLoaded();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Extract Groups", operation == 1))
    {
        operation = 1;
        ClearLoaded();
    }

    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    RenderSectionHeading(operation == 0 ? "Merge Source Files" : "Extract Source File");
    RenderFolderChoice();
    ImGui::Spacing();

    if (operation == 0)
    {
        ImGui::Text("Base Layout");
        RenderXmlCombo("##MergeBaseXml", baseXmlIndex);

        ImGui::Spacing();
        ImGui::Text("Additional XML Files");
        ImGui::BeginChild("##MergeFileChecklist", ImVec2(0.0f, 130.0f), true);
        for (size_t index = 0; index < availableXmlFiles.size(); ++index)
        {
            if (static_cast<int>(index) == baseXmlIndex) continue;
            bool selected = additionalSelected[index] != 0;
            if (ImGui::Checkbox(
                (availableXmlFiles[index].name + "##MergeAdd" +
                    std::to_string(index)).c_str(),
                &selected
            ))
            {
                additionalSelected[index] = selected ? 1 : 0;
                ClearLoaded();
            }
        }
        ImGui::EndChild();

        const float width =
            (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Refresh List##Merge", ImVec2(width, 0.0f)))
        {
            RefreshXmlList();
        }
        ImGui::SameLine();
        if (ImGui::Button("Prepare Merge", ImVec2(width, 0.0f)))
        {
            PrepareMerge();
        }

        if (!report.empty())
        {
            ImGui::Spacing();
            ImGui::BeginChild("##MergeReport", ImVec2(0.0f, 120.0f), true);
            ImGui::TextUnformatted(report.c_str());
            ImGui::EndChild();
        }

        ImGui::Dummy(ImVec2(0.0f, 16.0f));
        RenderSectionHeading("Export");
        if (mergeDocuments.size() >= 2)
        {
            if (ImGui::Button("Merge and Export")) ExportMerge();
        }
        else
        {
            RenderDisabledButton("Merge and Export");
        }
    }
    else
    {
        RenderXmlCombo("##ExtractXml", extractXmlIndex);
        const float width =
            (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Refresh List##Extract", ImVec2(width, 0.0f)))
        {
            RefreshXmlList();
        }
        ImGui::SameLine();
        if (ImGui::Button("Import Selected##Extract", ImVec2(width, 0.0f)))
        {
            ImportExtract();
        }

        if (!extractGroups.empty())
        {
            ImGui::Spacing();
            ImGui::Text("Decoration Groups");
            ImGui::BeginChild("##ExtractGroups", ImVec2(0.0f, 170.0f), true);
            for (Group& group : extractGroups)
            {
                const std::string label = group.name + " (" +
                    std::to_string(group.props.size()) + " decorations)";
                ImGui::Checkbox(label.c_str(), &group.selected);
            }
            ImGui::EndChild();
        }

        ImGui::Dummy(ImVec2(0.0f, 16.0f));
        RenderSectionHeading("Export");
        if (!extractGroups.empty())
        {
            if (ImGui::Button("Extract Selected Groups")) ExportExtract();
        }
        else
        {
            RenderDisabledButton("Extract Selected Groups");
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("%s", status.c_str());
}

void MergeExtractTab::ClearImportedData()
{
    ClearLoaded();
    baseXmlIndex = -1;
    extractXmlIndex = -1;
    additionalSelected.assign(availableXmlFiles.size(), false);
}
