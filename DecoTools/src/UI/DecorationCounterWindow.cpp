// Pewpew's Deco Tools - Decoration Counter Window
// Loads available decoration counts asynchronously, compares them with the
// current XML requirements, displays shortages, and exports count reports.

#include "DecorationCounterWindow.h"
#include "PrimaryActionButton.h"
#include "NewDecos.h"

#include "../Core/AppSettings.h"
#include "../Core/DecorationDatabase.h"
#include "../Core/GroupBackupDatabase.h"
#include "../Core/Gw2Api.h"
#include "../Core/SharedXmlWorkspace.h"
#include "../Core/Utf8Paths.h"
#include "StatusBar.h"
#include "../imgui/imgui.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <set>
#include <sstream>
#include <utility>

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
    std::string rememberedUnifiedGuildId;
    int selectedGuildIndex = -1;
    std::vector<Gw2Api::Guild> guilds;
    std::vector<Gw2Api::Guild> cachedGuilds;
    std::string cachedGuildApiKey;
    bool guildCacheValid = false;
    DecorationCounterWindow::GuildSelectionMode guildSelectionMode =
        DecorationCounterWindow::GuildSelectionMode::UnifiedSelector;
    std::string status = "Load an XML to view decoration counts.";
    std::future<JobResult> job;
    JobKind jobKind = JobKind::None;
    unsigned generation = 0;

    struct EditableTextInstance
    {
        int decorationId = -1;
        std::string decorationName;
        std::string position;
        std::string text;
        size_t payloadTextStart = 0;
        size_t payloadTextLength = 0;
    };

    std::vector<EditableTextInstance> editableTextInstances;
    std::string editableTextPath;
    std::uint64_t editableTextRevision = 0;
    bool textEditorOpen = false;
    int textEditorDecorationId = -1;
    int textEditorSelection = 0;
    std::array<char, 2048> textEditorBuffer = {};
    std::array<char, 4096> wrappedTextEditorBuffer = {};
    std::string previousWrappedEditorText;
    std::vector<size_t> wrappedEditorSoftBreaks;
    float wrappedEditorWidth = 0.0f;
    bool wrappedEditorNeedsRebuild = true;
    bool wrappedEditorInputActive = false;
    bool wrappedEditorForceReflow = false;
    float wrappedEditorCallbackWidth = 100.0f;
    std::string textEditorStatus;

    struct WrappedText
    {
        std::string display;
        std::vector<size_t> softBreaks;
    };

    bool ReadTextFile(const std::string& path, std::string& output)
    {
        std::ifstream file(Utf8Paths::FromUtf8(path), std::ios::binary);
        if (!file.is_open()) return false;
        output.assign(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>());
        return file.good() || file.eof();
    }

    bool AttributeValue(
        const std::string& tag,
        const std::string& name,
        std::string& value)
    {
        size_t search = 0;
        while ((search = tag.find(name, search)) != std::string::npos)
        {
            const bool validLeft = search == 0 ||
                std::isspace(static_cast<unsigned char>(tag[search - 1])) != 0 ||
                tag[search - 1] == '<';
            size_t cursor = search + name.size();
            while (cursor < tag.size() &&
                std::isspace(static_cast<unsigned char>(tag[cursor])) != 0)
                ++cursor;
            if (!validLeft || cursor >= tag.size() || tag[cursor] != '=')
            {
                search += name.size();
                continue;
            }
            ++cursor;
            while (cursor < tag.size() &&
                std::isspace(static_cast<unsigned char>(tag[cursor])) != 0)
                ++cursor;
            if (cursor >= tag.size() || (tag[cursor] != '"' && tag[cursor] != '\''))
                return false;
            const char quote = tag[cursor++];
            const size_t end = tag.find(quote, cursor);
            if (end == std::string::npos) return false;
            value = tag.substr(cursor, end - cursor);
            return true;
        }
        return false;
    }

    void AppendUtf8(std::string& output, unsigned codePoint)
    {
        if (codePoint <= 0x7F) output += static_cast<char>(codePoint);
        else if (codePoint <= 0x7FF)
        {
            output += static_cast<char>(0xC0 | (codePoint >> 6));
            output += static_cast<char>(0x80 | (codePoint & 0x3F));
        }
        else if (codePoint <= 0xFFFF)
        {
            output += static_cast<char>(0xE0 | (codePoint >> 12));
            output += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            output += static_cast<char>(0x80 | (codePoint & 0x3F));
        }
        else
        {
            output += static_cast<char>(0xF0 | (codePoint >> 18));
            output += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
            output += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            output += static_cast<char>(0x80 | (codePoint & 0x3F));
        }
    }

    std::string DecodeXmlText(const std::string& value)
    {
        std::string output;
        for (size_t index = 0; index < value.size();)
        {
            if (value[index] != '&')
            {
                output += value[index++];
                continue;
            }
            const size_t semicolon = value.find(';', index + 1);
            if (semicolon == std::string::npos)
            {
                output += value[index++];
                continue;
            }
            const std::string entity = value.substr(index + 1, semicolon - index - 1);
            if (entity == "amp") output += '&';
            else if (entity == "lt") output += '<';
            else if (entity == "gt") output += '>';
            else if (entity == "quot") output += '"';
            else if (entity == "apos") output += '\'';
            else if (!entity.empty() && entity[0] == '#')
            {
                try
                {
                    const bool hex = entity.size() > 2 &&
                        (entity[1] == 'x' || entity[1] == 'X');
                    const unsigned codePoint = static_cast<unsigned>(std::stoul(
                        entity.substr(hex ? 2 : 1), nullptr, hex ? 16 : 10));
                    AppendUtf8(output, codePoint);
                }
                catch (...)
                {
                    output.append(value, index, semicolon - index + 1);
                }
            }
            else output.append(value, index, semicolon - index + 1);
            index = semicolon + 1;
        }
        return output;
    }

    std::string EncodeXmlText(const std::string& value)
    {
        std::string output;
        output.reserve(value.size());
        for (char character : value)
        {
            if (character == '&') output += "&amp;";
            else if (character == '<') output += "&lt;";
            else if (character == '>') output += "&gt;";
            else output += character;
        }
        return output;
    }

    std::string NormalizeLineEndings(std::string value)
    {
        std::string output;
        output.reserve(value.size());
        for (size_t index = 0; index < value.size(); ++index)
        {
            if (value[index] == '\r')
            {
                if (index + 1 < value.size() && value[index + 1] == '\n') ++index;
                output += '\n';
            }
            else output += value[index];
        }
        return output;
    }

    size_t Utf8CharacterCount(const std::string& value)
    {
        size_t count = 0;
        for (unsigned char byte : value)
        {
            if ((byte & 0xC0) != 0x80) ++count;
        }
        return count;
    }

    void TruncateUtf8(std::string& value, size_t maximumCharacters)
    {
        size_t count = 0;
        size_t index = 0;
        while (index < value.size())
        {
            const unsigned char byte = static_cast<unsigned char>(value[index]);
            size_t length = 1;
            if ((byte & 0xE0) == 0xC0) length = 2;
            else if ((byte & 0xF0) == 0xE0) length = 3;
            else if ((byte & 0xF8) == 0xF0) length = 4;
            if (count == maximumCharacters) break;
            index += (std::min)(length, value.size() - index);
            ++count;
        }
        if (index < value.size()) value.erase(index);
    }

    size_t Utf8ByteOffsetForCharacter(const std::string& value, size_t characterIndex)
    {
        size_t currentCharacter = 0;
        size_t byteOffset = 0;
        while (byteOffset < value.size() && currentCharacter < characterIndex)
        {
            ++byteOffset;
            while (byteOffset < value.size() &&
                (static_cast<unsigned char>(value[byteOffset]) & 0xC0) == 0x80)
                ++byteOffset;
            ++currentCharacter;
        }
        return byteOffset;
    }

    bool IsSoftBreakAt(const std::vector<size_t>& breaks, size_t byteOffset)
    {
        return std::binary_search(breaks.begin(), breaks.end(), byteOffset);
    }

    size_t LogicalCharacterAtDisplayByte(
        const std::string& display,
        const std::vector<size_t>& softBreaks,
        size_t displayByte)
    {
        size_t logicalCharacter = 0;
        for (size_t byteOffset = 0;
            byteOffset < display.size() && byteOffset < displayByte;)
        {
            const bool soft = display[byteOffset] == '\n' &&
                IsSoftBreakAt(softBreaks, byteOffset);
            size_t next = byteOffset + 1;
            while (next < display.size() &&
                (static_cast<unsigned char>(display[next]) & 0xC0) == 0x80)
                ++next;
            if (!soft) ++logicalCharacter;
            byteOffset = next;
        }
        return logicalCharacter;
    }

    size_t DisplayCharacterForLogical(
        const WrappedText& wrapped,
        size_t logicalCharacterTarget)
    {
        size_t logicalCharacter = 0;
        size_t displayCharacter = 0;
        for (size_t byteOffset = 0; byteOffset < wrapped.display.size();)
        {
            if (logicalCharacter >= logicalCharacterTarget) break;
            const bool soft = wrapped.display[byteOffset] == '\n' &&
                IsSoftBreakAt(wrapped.softBreaks, byteOffset);
            size_t next = byteOffset + 1;
            while (next < wrapped.display.size() &&
                (static_cast<unsigned char>(wrapped.display[next]) & 0xC0) == 0x80)
                ++next;
            if (!soft) ++logicalCharacter;
            ++displayCharacter;
            byteOffset = next;
        }
        return displayCharacter;
    }

    WrappedText BuildWrappedText(const std::string& logical, float wrapWidth)
    {
        WrappedText result;
        const ImFont* font = ImGui::GetFont();
        const float scale = font != nullptr && font->FontSize > 0.0f
            ? ImGui::GetFontSize() / font->FontSize
            : 1.0f;
        size_t paragraphStart = 0;
        while (paragraphStart <= logical.size())
        {
            const size_t manualBreak = logical.find('\n', paragraphStart);
            const size_t paragraphEnd = manualBreak == std::string::npos
                ? logical.size()
                : manualBreak;
            size_t cursor = paragraphStart;
            while (cursor < paragraphEnd)
            {
                const char* begin = logical.data() + cursor;
                const char* end = logical.data() + paragraphEnd;
                const char* wrappedEnd = font != nullptr
                    ? font->CalcWordWrapPositionA(scale, begin, end, wrapWidth)
                    : end;
                if (wrappedEnd <= begin)
                {
                    wrappedEnd = begin + 1;
                    while (wrappedEnd < end &&
                        (static_cast<unsigned char>(*wrappedEnd) & 0xC0) == 0x80)
                        ++wrappedEnd;
                }
                size_t segmentEnd = static_cast<size_t>(wrappedEnd - logical.data());
                if (segmentEnd >= paragraphEnd)
                {
                    result.display.append(logical, cursor, paragraphEnd - cursor);
                    cursor = paragraphEnd;
                    break;
                }

                size_t nextCursor = segmentEnd;
                while (nextCursor < paragraphEnd &&
                    (logical[nextCursor] == ' ' || logical[nextCursor] == '\t'))
                    ++nextCursor;
                result.display.append(logical, cursor, nextCursor - cursor);
                result.softBreaks.push_back(result.display.size());
                result.display += '\n';
                cursor = nextCursor;
            }
            if (manualBreak == std::string::npos) break;
            result.display += '\n';
            paragraphStart = manualBreak + 1;
        }
        return result;
    }

    void StoreWrappedEditorText(const WrappedText& wrapped)
    {
        wrappedTextEditorBuffer.fill('\0');
        std::memcpy(
            wrappedTextEditorBuffer.data(),
            wrapped.display.data(),
            (std::min)(wrapped.display.size(), wrappedTextEditorBuffer.size() - 1));
        previousWrappedEditorText = wrapped.display;
        wrappedEditorSoftBreaks = wrapped.softBreaks;
    }

    void RebuildWrappedEditor(float wrapWidth)
    {
        StoreWrappedEditorText(BuildWrappedText(textEditorBuffer.data(), wrapWidth));
        wrappedEditorWidth = wrapWidth;
        wrappedEditorNeedsRebuild = false;
    }

    int WrappedEditorCallback(ImGuiInputTextCallbackData* data)
    {
        if (data == nullptr || data->EventFlag != ImGuiInputTextFlags_CallbackAlways)
            return 0;
        const std::string editedDisplay(data->Buf, static_cast<size_t>(data->BufTextLen));
        if (!wrappedEditorForceReflow && editedDisplay == previousWrappedEditorText)
            return 0;
        size_t prefix = 0;
        while (prefix < previousWrappedEditorText.size() &&
            prefix < editedDisplay.size() &&
            previousWrappedEditorText[prefix] == editedDisplay[prefix])
            ++prefix;
        size_t suffix = 0;
        while (suffix < previousWrappedEditorText.size() - prefix &&
            suffix < editedDisplay.size() - prefix &&
            previousWrappedEditorText[previousWrappedEditorText.size() - 1 - suffix] ==
                editedDisplay[editedDisplay.size() - 1 - suffix])
            ++suffix;

        std::vector<size_t> retainedSoftBreaks;
        const ptrdiff_t byteDelta = static_cast<ptrdiff_t>(editedDisplay.size()) -
            static_cast<ptrdiff_t>(previousWrappedEditorText.size());
        for (size_t oldBreak : wrappedEditorSoftBreaks)
        {
            if (oldBreak < prefix)
                retainedSoftBreaks.push_back(oldBreak);
            else if (oldBreak >= previousWrappedEditorText.size() - suffix)
                retainedSoftBreaks.push_back(static_cast<size_t>(
                    static_cast<ptrdiff_t>(oldBreak) + byteDelta));
        }
        std::sort(retainedSoftBreaks.begin(), retainedSoftBreaks.end());

        const size_t cursorDisplayByte = static_cast<size_t>((std::max)(0, data->CursorPos));
        const size_t selectStartByte = static_cast<size_t>((std::max)(0, data->SelectionStart));
        const size_t selectEndByte = static_cast<size_t>((std::max)(0, data->SelectionEnd));
        size_t logicalCursor = LogicalCharacterAtDisplayByte(
            editedDisplay, retainedSoftBreaks, cursorDisplayByte);
        size_t logicalSelectStart = LogicalCharacterAtDisplayByte(
            editedDisplay, retainedSoftBreaks, selectStartByte);
        size_t logicalSelectEnd = LogicalCharacterAtDisplayByte(
            editedDisplay, retainedSoftBreaks, selectEndByte);

        std::string logical;
        logical.reserve(editedDisplay.size());
        for (size_t byteOffset = 0; byteOffset < editedDisplay.size(); ++byteOffset)
        {
            if (editedDisplay[byteOffset] == '\n' &&
                IsSoftBreakAt(retainedSoftBreaks, byteOffset))
                continue;
            logical += editedDisplay[byteOffset];
        }
        logical = NormalizeLineEndings(std::move(logical));
        TruncateUtf8(logical, 140);
        const size_t logicalLength = Utf8CharacterCount(logical);
        logicalCursor = (std::min)(logicalCursor, logicalLength);
        logicalSelectStart = (std::min)(logicalSelectStart, logicalLength);
        logicalSelectEnd = (std::min)(logicalSelectEnd, logicalLength);
        textEditorBuffer.fill('\0');
        std::memcpy(textEditorBuffer.data(), logical.data(),
            (std::min)(logical.size(), textEditorBuffer.size() - 1));

        const WrappedText wrapped = BuildWrappedText(logical, wrappedEditorCallbackWidth);
        const size_t cursorCharacter = DisplayCharacterForLogical(wrapped, logicalCursor);
        const size_t selectStartCharacter = DisplayCharacterForLogical(
            wrapped, logicalSelectStart);
        const size_t selectEndCharacter = DisplayCharacterForLogical(
            wrapped, logicalSelectEnd);
        const size_t cursorByte = Utf8ByteOffsetForCharacter(
            wrapped.display, cursorCharacter);
        const size_t selectStartDisplayByte = Utf8ByteOffsetForCharacter(
            wrapped.display, selectStartCharacter);
        const size_t selectEndDisplayByte = Utf8ByteOffsetForCharacter(
            wrapped.display, selectEndCharacter);

        if (wrapped.display != editedDisplay)
        {
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, wrapped.display.c_str());
        }
        data->CursorPos = static_cast<int>(cursorByte);
        data->SelectionStart = static_cast<int>(selectStartDisplayByte);
        data->SelectionEnd = static_cast<int>(selectEndDisplayByte);
        StoreWrappedEditorText(wrapped);
        wrappedEditorWidth = wrappedEditorCallbackWidth;
        wrappedEditorNeedsRebuild = false;
        wrappedEditorForceReflow = false;
        return 0;
    }

    bool FindTagEnd(const std::string& source, size_t start, size_t& end)
    {
        char quote = 0;
        for (size_t index = start; index < source.size(); ++index)
        {
            const char character = source[index];
            if (quote != 0)
            {
                if (character == quote) quote = 0;
            }
            else if (character == '"' || character == '\'') quote = character;
            else if (character == '>')
            {
                end = index;
                return true;
            }
        }
        return false;
    }

    void ParseEditableTextInstances(const std::string& source)
    {
        editableTextInstances.clear();
        size_t propStart = 0;
        while ((propStart = source.find("<prop", propStart)) != std::string::npos)
        {
            const size_t afterName = propStart + 5;
            if (afterName < source.size() &&
                std::isspace(static_cast<unsigned char>(source[afterName])) == 0)
            {
                propStart = afterName;
                continue;
            }
            size_t propOpenEnd = 0;
            if (!FindTagEnd(source, propStart, propOpenEnd)) break;
            size_t propTagTail = propOpenEnd;
            while (propTagTail > propStart &&
                std::isspace(static_cast<unsigned char>(source[propTagTail - 1])) != 0)
                --propTagTail;
            if (propTagTail > propStart && source[propTagTail - 1] == '/')
            {
                propStart = propOpenEnd + 1;
                continue;
            }
            const size_t propClose = source.find("</prop>", propOpenEnd + 1);
            if (propClose == std::string::npos) break;
            const std::string propTag = source.substr(
                propStart, propOpenEnd - propStart + 1);
            std::string idText;
            std::string name;
            std::string position;
            if (!AttributeValue(propTag, "id", idText))
            {
                propStart = propClose + 7;
                continue;
            }
            AttributeValue(propTag, "name", name);
            AttributeValue(propTag, "pos", position);

            size_t payloadStart = source.find("<payload", propOpenEnd + 1);
            if (payloadStart == std::string::npos || payloadStart >= propClose)
            {
                propStart = propClose + 7;
                continue;
            }
            size_t payloadOpenEnd = 0;
            if (!FindTagEnd(source, payloadStart, payloadOpenEnd) ||
                payloadOpenEnd >= propClose)
            {
                propStart = propClose + 7;
                continue;
            }
            const std::string payloadTag = source.substr(
                payloadStart, payloadOpenEnd - payloadStart + 1);
            std::string payloadType;
            std::string payloadVersion;
            if (!AttributeValue(payloadTag, "pt", payloadType) ||
                !AttributeValue(payloadTag, "v", payloadVersion) ||
                payloadType != "1" || payloadVersion != "1")
            {
                propStart = propClose + 7;
                continue;
            }
            const size_t payloadClose = source.find("</payload>", payloadOpenEnd + 1);
            if (payloadClose == std::string::npos || payloadClose > propClose)
            {
                propStart = propClose + 7;
                continue;
            }
            try
            {
                EditableTextInstance instance;
                instance.decorationId = std::stoi(idText);
                instance.decorationName = DecodeXmlText(name);
                instance.position = position;
                instance.payloadTextStart = payloadOpenEnd + 1;
                instance.payloadTextLength = payloadClose - instance.payloadTextStart;
                instance.text = NormalizeLineEndings(DecodeXmlText(source.substr(
                    instance.payloadTextStart, instance.payloadTextLength)));
                editableTextInstances.push_back(std::move(instance));
            }
            catch (...) {}
            propStart = propClose + 7;
        }
    }

    void LoadEditorSelection();

    void RefreshEditableTextInstances(bool force = false)
    {
        if (!SharedXmlWorkspace::HasImport())
        {
            editableTextInstances.clear();
            editableTextPath.clear();
            editableTextRevision = 0;
            textEditorOpen = false;
            return;
        }
        const std::string& path = SharedXmlWorkspace::Path();
        const std::uint64_t revision = SharedXmlWorkspace::Revision();
        if (!force && path == editableTextPath && revision == editableTextRevision)
            return;
        std::string source;
        if (!ReadTextFile(path, source))
        {
            editableTextInstances.clear();
            return;
        }
        editableTextPath = path;
        editableTextRevision = revision;
        ParseEditableTextInstances(source);
        if (textEditorOpen) LoadEditorSelection();
    }

    std::vector<size_t> InstancesForDecoration(int decorationId)
    {
        std::vector<size_t> result;
        for (size_t index = 0; index < editableTextInstances.size(); ++index)
        {
            if (editableTextInstances[index].decorationId == decorationId)
                result.push_back(index);
        }
        return result;
    }

    void LoadEditorSelection()
    {
        const std::vector<size_t> matches = InstancesForDecoration(textEditorDecorationId);
        if (matches.empty())
        {
            textEditorOpen = false;
            return;
        }
        textEditorSelection = (std::max)(0, (std::min)(
            textEditorSelection, static_cast<int>(matches.size()) - 1));
        const std::string& value = editableTextInstances[
            matches[static_cast<size_t>(textEditorSelection)]].text;
        textEditorBuffer.fill('\0');
        std::memcpy(
            textEditorBuffer.data(),
            value.data(),
            (std::min)(value.size(), textEditorBuffer.size() - 1));
        wrappedEditorNeedsRebuild = true;
        wrappedEditorInputActive = false;
        wrappedEditorForceReflow = false;
    }

    void OpenTextEditor(int decorationId)
    {
        RefreshEditableTextInstances();
        textEditorDecorationId = decorationId;
        textEditorSelection = 0;
        textEditorStatus.clear();
        textEditorOpen = true;
        LoadEditorSelection();
    }

    bool WriteTextFileAtomically(
        const std::string& path,
        const std::string& output,
        std::string& errorMessage)
    {
        const std::filesystem::path target = Utf8Paths::FromUtf8(path);
        std::filesystem::path temporary = target;
        temporary += L".decotools.text.tmp";
        std::error_code error;
        std::filesystem::remove(temporary, error);
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            errorMessage = "Could not create the temporary XML file.";
            return false;
        }
        file.write(output.data(), static_cast<std::streamsize>(output.size()));
        file.flush();
        if (!file.good())
        {
            file.close();
            std::filesystem::remove(temporary, error);
            errorMessage = "The updated XML could not be written completely.";
            return false;
        }
        file.close();
        if (!MoveFileExW(temporary.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::filesystem::remove(temporary, error);
            errorMessage = "The updated XML could not replace the original; the original was left unchanged.";
            return false;
        }
        return true;
    }

    void ApplyEditedText()
    {
        if (!SharedXmlWorkspace::HasImport())
        {
            textEditorStatus = "The working XML is no longer loaded.";
            return;
        }
        std::string source;
        if (!ReadTextFile(SharedXmlWorkspace::Path(), source))
        {
            textEditorStatus = "The working XML could not be read.";
            return;
        }
        ParseEditableTextInstances(source);
        const std::vector<size_t> matches = InstancesForDecoration(textEditorDecorationId);
        if (textEditorSelection < 0 ||
            textEditorSelection >= static_cast<int>(matches.size()))
        {
            textEditorStatus = "That text element could no longer be found.";
            return;
        }

        std::string value = NormalizeLineEndings(textEditorBuffer.data());
        TruncateUtf8(value, 140);
        const EditableTextInstance& instance = editableTextInstances[
            matches[static_cast<size_t>(textEditorSelection)]];
        std::string encoded = EncodeXmlText(value);
        if (source.find("\r\n") != std::string::npos)
        {
            std::string windowsEncoded;
            windowsEncoded.reserve(encoded.size() + 8);
            for (char character : encoded)
            {
                if (character == '\n') windowsEncoded += "\r\n";
                else windowsEncoded += character;
            }
            encoded = std::move(windowsEncoded);
        }
        std::string output = source;
        output.replace(instance.payloadTextStart, instance.payloadTextLength, encoded);

        std::string backupStatus;
        if (!GroupBackupDatabase::RecordFile(
            SharedXmlWorkspace::Path(),
            SharedXmlWorkspace::XmlType(),
            GroupBackupDatabase::RestorePointType::Safety,
            "Pre-Text Edit Safety",
            backupStatus,
            true))
        {
            textEditorStatus = "The safety backup failed, so the text was not changed. " +
                backupStatus;
            return;
        }
        std::string writeError;
        if (!WriteTextFileAtomically(SharedXmlWorkspace::Path(), output, writeError))
        {
            textEditorStatus = writeError;
            return;
        }
        SharedXmlWorkspace::NotifyCurrentFileChanged();
        std::string automaticBackupStatus;
        if (AppSettings::Get().automaticGroupBackupRestore)
        {
            GroupBackupDatabase::RecordFile(
                SharedXmlWorkspace::Path(),
                SharedXmlWorkspace::XmlType(),
                GroupBackupDatabase::RestorePointType::Auto,
                std::string(),
                automaticBackupStatus,
                AppSettings::Get().backupUngroupedXmls);
        }
        RefreshEditableTextInstances(true);
        LoadEditorSelection();
        textEditorStatus = "Element text applied to " + SharedXmlWorkspace::FileName() + ".";
        status = textEditorStatus;
        StatusBar::Publish(textEditorStatus);
    }

    void RenderTextEditor()
    {
        if (!textEditorOpen) return;
        RefreshEditableTextInstances();
        const std::vector<size_t> matches = InstancesForDecoration(textEditorDecorationId);
        if (matches.empty())
        {
            textEditorOpen = false;
            return;
        }

        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(
            ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f),
            ImGuiCond_Appearing,
            ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(850.0f, 360.0f),
            ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowSize(ImVec2(880.0f, 440.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Edit Element Text", &textEditorOpen))
        {
            ImGui::BeginChild("##TextElementInstances", ImVec2(250.0f, 0.0f), true);
            for (size_t listIndex = 0; listIndex < matches.size(); ++listIndex)
            {
                const EditableTextInstance& instance = editableTextInstances[matches[listIndex]];
                std::string label = instance.decorationName + " " +
                    std::to_string(listIndex + 1);
                if (ImGui::Selectable(label.c_str(),
                    textEditorSelection == static_cast<int>(listIndex)))
                {
                    textEditorSelection = static_cast<int>(listIndex);
                    textEditorStatus.clear();
                    LoadEditorSelection();
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("##TextElementEditor", ImVec2(0.0f, 0.0f), false);
            ImGui::TextUnformatted("Element Text");
            constexpr float EditorRightInset = 16.0f;
            const float inputWidth = (std::max)(
                260.0f,
                ImGui::GetContentRegionAvail().x - EditorRightInset);
            const float inputWrapWidth = (std::max)(
                100.0f,
                inputWidth - ImGui::GetStyle().FramePadding.x * 2.0f - 4.0f);
            const bool wrapWidthChanged =
                std::abs(wrappedEditorWidth - inputWrapWidth) > 0.5f;
            wrappedEditorCallbackWidth = inputWrapWidth;
            if (wrappedEditorNeedsRebuild ||
                (wrapWidthChanged && !wrappedEditorInputActive))
            {
                RebuildWrappedEditor(inputWrapWidth);
            }
            else if (wrapWidthChanged)
            {
                wrappedEditorForceReflow = true;
            }
            ImGui::InputTextMultiline(
                "##ElementTextPayload",
                wrappedTextEditorBuffer.data(),
                wrappedTextEditorBuffer.size(),
                ImVec2(inputWidth, 155.0f),
                ImGuiInputTextFlags_NoHorizontalScroll |
                    ImGuiInputTextFlags_CallbackAlways,
                WrappedEditorCallback);
            wrappedEditorInputActive = ImGui::IsItemActive();
            const std::string editedText = NormalizeLineEndings(textEditorBuffer.data());
            const size_t characterCount = Utf8CharacterCount(editedText);
            if (characterCount >= 140)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.30f, 1.0f));
            else if (characterCount >= 125)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.20f, 1.0f));
            ImGui::Text("%zu / 140 characters", characterCount);
            if (characterCount >= 125) ImGui::PopStyleColor();

            if (editedText.find('<') != std::string::npos)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.20f, 1.0f));
                ImGui::TextWrapped("Note: Guild Wars 2 may interpret text beginning with < as formatting and hide part of it.");
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();
            ImGui::TextUnformatted("Approximate In-Game Preview");
            constexpr float InGamePreviewTextWidth = 525.0f;
            const float previewOuterWidth = InGamePreviewTextWidth +
                ImGui::GetStyle().WindowPadding.x * 2.0f + 2.0f;
            ImGui::BeginChild(
                "##ElementTextPreview",
                ImVec2(previewOuterWidth, 105.0f),
                true,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + InGamePreviewTextWidth);
            ImGui::TextUnformatted(editedText.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndChild();

            if (PrimaryActionButton::Draw(
                "Apply to XML",
                ImVec2(previewOuterWidth, 0.0f)))
                ApplyEditedText();
            if (!textEditorStatus.empty())
                ImGui::TextWrapped("%s", textEditorStatus.c_str());
            ImGui::EndChild();
        }
        ImGui::End();
    }

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

    bool ResolveSelectedGuild()
    {
        selectedGuildIndex = -1;
        if (selectedGuildId.empty()) return false;
        for (size_t index = 0; index < guilds.size(); ++index)
        {
            if (guilds[index].id == selectedGuildId)
            {
                selectedGuildIndex = static_cast<int>(index);
                return true;
            }
        }
        selectedGuildId.clear();
        rememberedUnifiedGuildId.clear();
        return false;
    }

    void ResumeAvailabilityLoad()
    {
        if (requirements.empty()) return;
        if (decorationType == 0 || !selectedGuildId.empty())
        {
            StartCounts();
            return;
        }
        if (guildSelectionMode !=
            DecorationCounterWindow::GuildSelectionMode::UnifiedSelector)
        {
            return;
        }
        const std::string apiKey = AppSettings::Get().apiKey.data();
        if (!guildCacheValid || cachedGuildApiKey != apiKey)
            StartGuilds();
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
            ResumeAvailabilityLoad();
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
            cachedGuilds = guilds;
            cachedGuildApiKey = AppSettings::Get().apiKey.data();
            guildCacheValid = true;
            selectedGuildId = rememberedUnifiedGuildId;
            if (ResolveSelectedGuild())
            {
                StartCounts();
            }
            else
            {
                status = guilds.empty()
                    ? "No guilds were returned for this account."
                    : "No specific guild selected; available counts are not being checked.";
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
        const AppSettings::Data& settings=AppSettings::Get();
        const std::string configuredFolder=decorationType==1
            ? settings.guildHallFolder.data()
            : settings.homesteadFolder.data();
        if (configuredFolder.empty())
        {
            status=decorationType==1
                ? "Set the Guild Hall XML folder in Settings before exporting."
                : "Set the Homestead XML folder in Settings before exporting.";
            return;
        }

        const std::filesystem::path folder=Utf8Paths::FromUtf8(configuredFolder);
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        if (error)
        {
            status=decorationType==1
                ? "The configured Guild Hall folder could not be created or opened."
                : "The configured Homestead folder could not be created or opened.";
            return;
        }
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
            << "Decoration" << " | Required | Available | Max Count\n";
        file << std::string(width, '-') << "-+----------+-----------+----------\n";
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
            file << " | ";
            const int maxCount =
                DecorationDatabase::FindMaxCountById(item.id, decorationType);
            if (maxCount >= 0) file << std::setw(9) << maxCount;
            else file << std::setw(9) << "N/A";
            file << "\n";
        }
        status = "Exported " + output.filename().string() + " to the configured " +
            (decorationType==1 ? "Guild Hall" : "Homestead") + " folder.";
    }
}

void DecorationCounterWindow::SetRequirements(
    const std::string& newContext,
    int newType,
    const std::vector<Requirement>& newRequirements,
    const std::string& guildId,
    bool loadAvailability,
    GuildSelectionMode newGuildSelectionMode
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
    guildSelectionMode = newGuildSelectionMode;
    selectedGuildId = guildSelectionMode == GuildSelectionMode::UnifiedSelector
        ? rememberedUnifiedGuildId
        : guildId;
    selectedGuildIndex = -1;
    guilds.clear();
    status = requirements.empty()
        ? "The loaded XML contains no countable decorations."
        : "Preparing decoration counts...";

    if (requirements.empty()) return;

    if (decorationType == 1 &&
        guildSelectionMode == GuildSelectionMode::UnifiedSelector)
    {
        const std::string apiKey = AppSettings::Get().apiKey.data();
        if (guildCacheValid && cachedGuildApiKey == apiKey)
        {
            guilds = cachedGuilds;
            ResolveSelectedGuild();
            if (!selectedGuildId.empty()) StartCounts();
            else status = guilds.empty()
                ? "No guilds were returned for this account."
                : "No specific guild selected; available counts are not being checked.";
        }
        else
        {
            StartGuilds();
        }
    }
    else if (!loadAvailability)
    {
        status = "Select a specific guild to load available counts.";
    }
    else
    {
        StartCounts();
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
    guildSelectionMode = GuildSelectionMode::ExternalSelection;
    selectedGuildId.clear();
    selectedGuildIndex = -1;
    guilds.clear();
    status = "Decoration counts loaded.";
}

void DecorationCounterWindow::UpdateRequirements(
    const std::string& newContext,
    const std::vector<Requirement>& newRequirements
)
{
    context = newContext;
    requirements = newRequirements;
    std::sort(requirements.begin(), requirements.end(),
        [](const Requirement& left, const Requirement& right)
        {
            return left.name < right.name;
        });
}

void DecorationCounterWindow::Clear()
{
    ++generation;
    context.clear();
    requirements.clear();
    available.clear();
    availabilityKnown = false;
    guildSelectionMode = GuildSelectionMode::UnifiedSelector;
    selectedGuildId.clear();
    selectedGuildIndex = -1;
    guilds.clear();
    status = "Load an XML to view decoration counts.";
}

void DecorationCounterWindow::Render()
{
    Poll();
    RefreshEditableTextInstances();
    AppSettings::Data& settings = AppSettings::Get();
    if (!settings.showDecorationCounter)
    {
        RenderTextEditor();
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(610.0f, 390.0f), ImGuiCond_FirstUseEver);
    bool open = settings.showDecorationCounter;
    if (ImGui::Begin("Decoration Count", &open))
    {
        const float newDecosButtonWidth =
            ImGui::CalcTextSize("New Decos").x +
            ImGui::GetStyle().FramePadding.x * 2.0f;
        if (PrimaryActionButton::Draw("Export List"))
        {
            ExportList();
        }
        if (decorationType == 1 && !requirements.empty() &&
            guildSelectionMode == GuildSelectionMode::UnifiedSelector)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(
                -(newDecosButtonWidth + ImGui::GetStyle().ItemSpacing.x));
            const char* label = selectedGuildIndex >= 0
                ? guilds[static_cast<size_t>(selectedGuildIndex)].name.c_str()
                : "No Specific Guild";
            if (ImGui::BeginCombo("##CounterGuild", label))
            {
                if (ImGui::Selectable(
                    "No Specific Guild",
                    selectedGuildId.empty()))
                {
                    ++generation;
                    selectedGuildId.clear();
                    rememberedUnifiedGuildId.clear();
                    selectedGuildIndex = -1;
                    available.clear();
                    availabilityKnown = false;
                    status = "No specific guild selected; available counts are not being checked.";
                }
                if (!guilds.empty()) ImGui::Separator();
                for (size_t index = 0; index < guilds.size(); ++index)
                {
                    const std::string itemLabel = guilds[index].tag.empty()
                        ? guilds[index].name
                        : guilds[index].name + " [" + guilds[index].tag + "]";
                    if (ImGui::Selectable(
                        itemLabel.c_str(),
                        selectedGuildIndex == static_cast<int>(index)))
                    {
                        ++generation;
                        selectedGuildIndex = static_cast<int>(index);
                        selectedGuildId = guilds[index].id;
                        rememberedUnifiedGuildId = selectedGuildId;
                        available.clear();
                        availabilityKnown = false;
                        StartCounts();
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::SameLine();
        if (!(decorationType == 1 && !requirements.empty() &&
            guildSelectionMode == GuildSelectionMode::UnifiedSelector))
        {
            const float rightAligned = ImGui::GetCursorPosX() +
                ImGui::GetContentRegionAvail().x - newDecosButtonWidth;
            if (rightAligned > ImGui::GetCursorPosX())
                ImGui::SetCursorPosX(rightAligned);
        }
        if (ImGui::Button("New Decos", ImVec2(newDecosButtonWidth, 0.0f)))
        {
            NewDecos::Open();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Show decorations discovered by API updates within the last 14 days.");
        }

        if (!context.empty())
        {
            ImGui::TextWrapped("%s", context.c_str());
        }
        ImGui::Separator();

        ImGui::Columns(4, "##CounterColumns", false);
        ImGui::SetColumnWidth(0, 330.0f);
        ImGui::SetColumnWidth(1, 80.0f);
        ImGui::SetColumnWidth(2, 80.0f);
        ImGui::TextUnformatted("Decoration"); ImGui::NextColumn();
        ImGui::TextUnformatted("Required"); ImGui::NextColumn();
        ImGui::TextUnformatted("Available"); ImGui::NextColumn();
        ImGui::TextUnformatted("Max Count"); ImGui::NextColumn();
        ImGui::Separator();

        for (const Requirement& item : requirements)
        {
            ImGui::PushID(item.id);
            const auto found = available.find(item.id);
            const int count = found == available.end() ? 0 : found->second;
            const bool missing = availabilityKnown && count < item.required;
            const int maxCount =
                DecorationDatabase::FindMaxCountById(item.id, decorationType);
            const bool overMaximum = maxCount >= 0 && item.required > maxCount;
            const bool editable = !InstancesForDecoration(item.id).empty();
            const bool newlyDiscovered =
                DecorationDatabase::IsNewDecorationId(item.id, decorationType);
            if (missing || overMaximum)
            {
                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    ImVec4(1.0f, 0.28f, 0.28f, 1.0f)
                );
            }
            else if (newlyDiscovered)
            {
                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    ImVec4(0.36f, 0.86f, 0.48f, 1.0f)
                );
            }
            else if (editable)
            {
                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    ImVec4(0.35f, 0.72f, 1.0f, 1.0f)
                );
            }
            ImGui::TextUnformatted(item.name.c_str());
            if (newlyDiscovered && ImGui::IsItemHovered())
                ImGui::SetTooltip("New decoration discovered within the last 14 days.");
            else if (editable && ImGui::IsItemHovered())
                ImGui::SetTooltip("Editable element text detected. Right-click to edit.");
            if (editable && ImGui::BeginPopupContextItem("##ElementTextMenu"))
            {
                if (ImGui::MenuItem("Edit Element Text"))
                    OpenTextEditor(item.id);
                ImGui::EndPopup();
            }
            ImGui::NextColumn();
            ImGui::Text("%d", item.required); ImGui::NextColumn();
            if (availabilityKnown) ImGui::Text("%d", count);
            else ImGui::TextUnformatted("-");
            ImGui::NextColumn();
            if (maxCount >= 0) ImGui::Text("%d", maxCount);
            else ImGui::TextUnformatted("-");
            ImGui::NextColumn();
            if (missing || overMaximum || newlyDiscovered || editable)
                ImGui::PopStyleColor();
            ImGui::PopID();
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
    RenderTextEditor();
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
