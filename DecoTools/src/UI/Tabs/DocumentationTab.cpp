// Pewpew's Deco Tools - Documentation Tab
// Provides scrollable, structured help pages for every tool and the addon
// settings, based on the official Deco Tools documentation.

#include "DocumentationTab.h"

#include "../../imgui/imgui.h"

namespace
{
    enum class DocumentationPage
    {
        MoveTool,
        Patterns,
        MapSwap,
        GroupTools,
        GroupMover,
        Settings
    };

    DocumentationPage currentPage = DocumentationPage::MoveTool;

    struct PageChoice
    {
        DocumentationPage page;
        const char* label;
    };

    constexpr PageChoice PageChoices[] =
    {
        { DocumentationPage::MoveTool, "Move Tool" },
        { DocumentationPage::Patterns, "Patterns" },
        { DocumentationPage::MapSwap, "Map Swap" },
        { DocumentationPage::GroupTools, "Group Tools" },
        { DocumentationPage::GroupMover, "Group Mover" },
        { DocumentationPage::Settings, "Settings" }
    };

    const ImVec4 AccentColor(0.25f, 0.68f, 1.0f, 1.0f);
    const ImVec4 NoteColor(1.0f, 0.72f, 0.16f, 1.0f);

    const char* CurrentPageLabel()
    {
        for (const PageChoice& choice : PageChoices)
        {
            if (choice.page == currentPage)
            {
                return choice.label;
            }
        }
        return "Move Tool";
    }

    void RenderPageTitle(const char* text)
    {
        ImGui::SetWindowFontScale(1.30f);
        ImGui::TextColored(AccentColor, "%s", text);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 7.0f));
    }

    void RenderSectionHeading(const char* text)
    {
        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        ImGui::SetWindowFontScale(1.14f);
        ImGui::TextUnformatted(text);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    void RenderParagraph(const char* text)
    {
        ImGui::TextWrapped("%s", text);
        ImGui::Dummy(ImVec2(0.0f, 5.0f));
    }

    void RenderStep(int number, const char* title, const char* body)
    {
        ImGui::TextColored(AccentColor, "%d. %s", number, title);
        if (body != nullptr && body[0] != '\0')
        {
            ImGui::Indent(22.0f);
            ImGui::TextWrapped("%s", body);
            ImGui::Unindent(22.0f);
        }
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }

    void RenderBullet(const char* title, const char* body, float indent = 0.0f)
    {
        if (indent > 0.0f) ImGui::Indent(indent);
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextColored(AccentColor, "%s", title);
        if (body != nullptr && body[0] != '\0')
        {
            ImGui::Indent(22.0f);
            ImGui::TextWrapped("%s", body);
            ImGui::Unindent(22.0f);
        }
        if (indent > 0.0f) ImGui::Unindent(indent);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    void RenderSubStep(int number, const char* text)
    {
        ImGui::Indent(24.0f);
        ImGui::TextColored(AccentColor, "%d.", number);
        ImGui::SameLine();
        ImGui::TextWrapped("%s", text);
        ImGui::Unindent(24.0f);
        ImGui::Dummy(ImVec2(0.0f, 3.0f));
    }

    void RenderNote(const char* text)
    {
        ImGui::TextColored(NoteColor, "NOTE");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", text);
        ImGui::Dummy(ImVec2(0.0f, 7.0f));
    }

    void RenderMoveToolPage()
    {
        RenderPageTitle("Move Tool");
        RenderParagraph(
            "Move Tool provides one location for repositioning either an entire XML layout or selected XML groups. Choose the source mode that matches what you need to move, then use the corresponding workflow below."
        );

        RenderSectionHeading("Move Source");
        RenderBullet("Full XML",
            "Uses the complete imported layout as one construction. Export Updated XML creates a new indexed _MOVED#.xml file and leaves the imported file unchanged.");
        RenderBullet("XML Groups",
            "Provides the complete former Group Mover workflow inside Move Tool. It displays named groups, permits one or multiple group selections, and applies changes directly to the imported XML.");
        RenderNote(
            "XML Groups was consolidated into Move Tool because both workflows perform the same fundamental move and rotation operations. See the separate Group Mover documentation page for its complete selection, movement, rotation, Undo/Redo, and Apply instructions."
        );

        RenderSectionHeading("How to Use Full XML");
        RenderStep(1, "Choose your source type: Homestead or Guild Hall",
            "This determines which default game folder the tool uses for importing and exporting. Default folders can be verified or changed in Settings.");
        RenderStep(2, "Save what you are working on in-game",
            "The tool reads XML files that are already saved. It is good practice to save the layout you are working on under a new name so that you never lose the original.");
        RenderStep(3, "Refresh - Select - Import",
            "Refresh the list to show your new save, choose the file from the dropdown, and press Import Selected to load it into the tool.");
        RenderStep(4, "Move",
            "The Move operation lets you reposition the layout in world X, Y, and Z with the manipulator handles. The red, green, and blue handles are placed at the average center of all decorations and at their lowest point, which is usually ground level and easier to locate. If you are unsure where the manipulator is, move your character anywhere on the map and click Move to Character to bring every decoration point to your location.");
        RenderStep(5, "Rotate",
            "The Rotate operation uses advanced group-rotation math while providing three simple rings that rotate the layout around whichever local axis you choose.");
        RenderStep(6, "Export",
            "When the position is complete, click Export Updated XML. The file is saved to the default location and can be loaded in-game to view the result. Files exported from this tool use the suffix _MOVED#.xml, with the number automatically indexed for each new save.");

        RenderSectionHeading("Other Options");
        RenderBullet("Bounding Box",
            "Displays a wireframe box surrounding all decoration points in the imported file.");
        RenderBullet("Solid Faces",
            "Fills the bounding box to make the complete area easier to see. Leaving solid faces off while actively working is generally recommended.");
        RenderBullet("Decoration Points",
            "Displays each decoration's actual anchor point. When decorations are spread across a very large area, fewer distant points may be visible. Point size can be adjusted here, and that size is shared by every tool.");

        RenderSectionHeading("Best Use Cases");
        RenderParagraph(
            "The Move Tool is best for complex builds that occupy a relatively small area and need to be repositioned or rotated as one unit. It also works across maps. Import a build made on another map while standing in the destination map, place it where you want it, then export and load the converted result."
        );
    }

    void RenderPatternsPage()
    {
        RenderPageTitle("Patterns");
        RenderParagraph(
            "Patterns is designed for more advanced workflows. It is useful for large groups, but also excels at manipulating a single decoration to create unique and precise layouts that can be merged into a larger build later."
        );

        RenderSectionHeading("How to Use");
        RenderStep(1, "Import",
            "Select and import the XML you are currently working on. For more information about importing, see the first three steps in the Move Tool documentation.");

        RenderStep(2, "Choose the pattern source", "");
        RenderBullet("Full XML",
            "Uses the complete imported layout as one pattern unit and retains the indexed _PATTERN#.xml export workflow.", 18.0f);
        RenderBullet("XML Groups",
            "Displays only decorations contained in named XML groups. Select exactly one group from the list or by clicking any of its scene points. Ungrouped decorations remain safely in the file but are hidden from this workspace.", 18.0f);

        RenderStep(3, "Choose a pattern type", "");
        RenderBullet("Line",
            "Creates a straight line by repeating the imported decorations across a fixed Total Offset XYZ. Changing Copies redistributes the instances evenly without moving the outer endpoint. From Center creates the selected number of copies on both sides of the original while preserving both outer endpoints.", 18.0f);
        RenderBullet("Circle",
            "Creates a circular pattern with 2 to 72 total instances. Sweep ranges continuously from 1 to 1080 degrees, with visual marks at 360, 720, and 1080 degrees for building as many as three revolutions. Pattern Count defaults to 6 and Sweep defaults to 360 degrees. Sweep and Total Vertical Offset define fixed first and last points, so changing Pattern Count only redistributes instances between them. A flat 360-degree circle remains a closed loop without an overlapping final copy.", 18.0f);
        RenderBullet("Square",
            "Creates a two-dimensional repeating grid with different vertical-offset styles for three-dimensional effects:", 18.0f);
        RenderBullet("Corner", "Builds a diagonal ramp from corner to corner.", 42.0f);
        RenderBullet("Edge", "Builds a linear ramp from edge to edge.", 42.0f);
        RenderBullet("Center",
            "Builds a pyramid effect and calculates matching copies around the center so the primary shape remains perfectly centered.", 42.0f);
        RenderBullet("Cube",
            "Works similarly to Square while adding a third axis for the pattern to grow from.", 18.0f);

        RenderStep(4, "Select your move operation", "");
        RenderBullet("Move",
            "Provides a colored three-axis manipulator on the main object. Moving it repositions the entire pattern.", 18.0f);
        RenderBullet("Rotate",
            "Provides colored three-axis rotation rings on the main object. Rotating it causes every copy to rotate by the same amount in place.", 18.0f);
        RenderBullet("Pattern Rotate",
            "Provides three-axis rotation rings at the center of the complete pattern and rotates the arrangement as one unit.", 18.0f);

        RenderStep(5, "Control spacing and offsets",
            "Each pattern type provides a second gray manipulator for changing spacing and offsets. Line uses a total XYZ offset to its outer endpoint, while Circle uses radius and the total vertical difference between its first and last instances. These adjustments preserve the colored source handle's current position, including after Pattern Rotate. The gray offset manipulator is not available during the Pattern Rotate operation.");
        RenderStep(6, "Export or Apply",
            "Full XML exports a new file using the automatically indexed _PATTERN#.xml suffix. XML Groups uses Apply to XML instead: the original instance remains in its existing group, and every generated replica becomes a separate adjacent group named Original Group (Copy 1), Original Group (Copy 2), and so on. Existing copy names are skipped automatically. Every other group and every ungrouped decoration remains untouched.");
        RenderStep(7, "Undo and Redo",
            "XML Groups retains as many as 100 pattern-setting, move, and rotation changes during the current selected-group session. Apply to XML does not clear the history, so an earlier result can be restored and applied again.");

        RenderSectionHeading("Common Practices");
        RenderParagraph(
            "Full XML remains useful for creating a separate reusable pattern that can be merged into another build. XML Groups is faster when the source pieces already exist inside a complete workspace: create a named group in Group Tools, pattern that group in place, apply it, and reload the same XML in-game."
        );
    }

    void RenderMapSwapPage()
    {
        RenderPageTitle("Map Swap");
        RenderParagraph(
            "Map Swap can transfer layouts from Homestead to Homestead or Guild Hall to Guild Hall, as well as between Homesteads and Guild Halls. When Pewpew's Deco Tools is launched for the first time, it uses the Guild Wars 2 API to create a local database of decoration names and IDs for both layout types and maps their relationships so conversions can be performed quickly."
        );
        RenderNote(
            "To use guild-specific counts correctly, you must be a guild owner and provide an API key in Settings."
        );

        RenderSectionHeading("How to Use");
        RenderStep(1, "Import",
            "Select and import the XML you are currently working on. For more information about importing, see the first three steps in the Move Tool documentation.");
        RenderStep(2, "Choose a Homestead destination",
            "If you choose Hearth's Glow or Comosus Isle, simply select the destination and run Pre-Check.");
        RenderStep(3, "Choose a Guild Hall destination",
            "Choosing one of the four Guild Halls reveals the Destination Guild dropdown. If you are not a guild owner, leave it set to No Specific Guild because only guild owners can access decoration counts. If you are the owner, allow the dropdown to populate and select the guild that corresponds to the chosen hall so the Decoration Counter can provide an accurate count.");
        RenderStep(4, "Include or exclude missing decorations",
            "When Include Missing Decorations is enabled, decorations you do not currently have are still written into the XML. When it is disabled, missing decorations are removed from the exported XML.");
        RenderStep(5, "Run Pre-Check",
            "Pre-Check counts decorations in the Decoration Counter and reports what can or cannot be placed at the destination, excluding impossible items. It also provides other important transfer information before export.");
        RenderStep(6, "Swap and Export",
            "Click Swap and Export to create a new XML with the same base title and the suffix _Map-Name.xml. Decorations created on the current map will then load correctly on the destination map, regardless of map type.");

        RenderSectionHeading("Common Uses");
        RenderParagraph(
            "Map Swap is ideal when changing Homesteads or Guild Halls without losing completed work. It can also convert a Guild Hall build to a Homestead, or the reverse, when corresponding decorations exist. Move, grouping, and Group Mover operations can be used afterward to refine the converted layout."
        );
    }

    void RenderGroupToolsPage()
    {
        RenderPageTitle("Group Tools");
        RenderParagraph(
            "Group Tools brings multiple XML files together, creates groups from multiple decorations within one XML, and extracts groups into separate files for safe reuse elsewhere."
        );

        RenderSectionHeading("How to Use");
        RenderStep(1, "Choose an operation", "Select Merge, Group, or Extract according to the workflow you need.");

        RenderBullet("Merge Operation", "");
        RenderSubStep(1, "Refresh the list so the most up-to-date XML files are displayed.");
        RenderSubStep(2, "Choose the Base Layout, which becomes the central XML that everything else merges into.");
        RenderSubStep(3, "Select each additional XML file you want to merge.");
        RenderSubStep(4, "Click Prepare Merge.");
        RenderSubStep(5, "Review the merge details, including the final decoration count.");
        RenderSubStep(6, "Check the Decoration Counter. If needed, click the addon menu icon twice to close and reopen both windows without losing your place. Red entries indicate decorations you do not have in sufficient quantities when a valid API key is available. The merge can still be completed regardless of count warnings.");
        RenderSubStep(7, "Click Merge and Export. The new _MERGED#.xml file will be ready to load in-game, and each XML added to the merge will already have its own subgroup.");

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        RenderBullet("Group Operation", "");
        RenderSubStep(1, "Refresh the XML list and import the current working XML. Save the in-game layout under the name you want before importing it.");
        RenderSubStep(2, "Orange points appear throughout the map. Left-click a point to select it and right-click it to deselect it.");
        RenderSubStep(3, "Enable Marquee Select to click and drag a box around multiple decorations. Disable it again when the selection is complete.");
        RenderSubStep(4, "If the map becomes too crowded, lower Visibility Distance to hide decoration points that are farther away.");
        RenderSubStep(5, "After selecting the desired decorations, enter a group name and click Create Group.");
        RenderSubStep(6, "Grouped decoration points turn gray and the group is added to the list. Hide Grouped Decorations can hide those gray points, and the X beside a group name removes that group and returns its points to orange.");
        RenderSubStep(7, "There is no export button. The Group operation directly reorganizes the imported XML so its groups remain neatly stacked inside the same file.");

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        RenderBullet("Extract Operation", "");
        RenderSubStep(1, "Refresh and import an XML containing previously created groups.");
        RenderSubStep(2, "Select every group you want to extract from the current file.");
        RenderSubStep(3, "Click Extract to export the selected groups.");
        RenderSubStep(4, "Each extracted file is named Group_Name_EXTRACTED.xml.");
        RenderSubStep(5, "A second file uses the original name with the suffix _STRIPPED.xml. It contains the imported XML with every selected group's decorations completely removed.");

        RenderSectionHeading("More Info");
        RenderParagraph(
            "This is one of the addon's most important toolsets. Although in-game saving removes XML group comments, building first and merging later creates a much more organized workflow and makes future operations faster. It is especially valuable when sharing individual designs from a larger layout: Group and Extract can isolate the needed decorations without manually sorting through hundreds or thousands of entries, while preserving the original layout."
        );
    }

    void RenderGroupMoverPage()
    {
        RenderPageTitle("Group Mover");
        RenderParagraph(
            "The complete Group Mover workflow can now be found under Move Tool -> XML Groups. It was consolidated into Move Tool because moving a complete XML and moving selected groups use the same fundamental operations, while the source mode determines what is affected and how the result is saved. This documentation remains separate to help existing users transition without losing the familiar Group Mover instructions."
        );
        RenderNote(
            "Open Move Tool and select XML Groups to use the functionality described on this page."
        );

        RenderSectionHeading("How to Use");
        RenderStep(1, "Refresh and import an XML",
            "The XML must contain embedded groups created through Group Tools.");
        RenderStep(2, "Select a group",
            "Click one of the group's decoration points in the scene or select it from the group list.");
        RenderStep(3, "Use Move or Rotate",
            "Change the complete position or orientation of the selected group. Selecting multiple groups moves or rotates them together around an anchor averaged between those groups.");
        RenderStep(4, "Undo and Redo",
            "Undo and Redo step backward or forward through as many as 100 move and rotation operations in the current XML Groups session. Applying changes does not clear this history, so you can undo earlier adjustments and apply the restored state again. Importing another XML, switching source modes, or leaving Move Tool starts a new history.");
        RenderStep(5, "Apply to the XML",
            "Like the Group operation in Group Tools, Group Mover does not create a separate exported XML. This reduces file clutter during small adjustments and makes checking changes faster. After moving and applying, reload the XML in-game to view the result.");

        RenderSectionHeading("Common Uses");
        RenderParagraph(
            "Group Mover becomes exceptionally fast once you are familiar with the grouping workflows in Group Tools. Because organized sections can be adjusted directly inside a complete build, it may become one of the tools you use most often."
        );
    }

    void RenderSettingsPage()
    {
        RenderPageTitle("Settings");
        RenderParagraph(
            "Settings controls how Pewpew's Deco Tools connects to Guild Wars 2, locates XML files, maintains local data, and remembers the addon's interface state."
        );

        RenderSectionHeading("API Key");
        RenderParagraph(
            "Create an API key from your Guild Wars 2 account on the official website. The key should include at least the following permissions:"
        );
        RenderBullet("Progression", "");
        RenderBullet("Guilds", "");
        RenderBullet("Account", "");
        RenderBullet("Unlocks", "");

        RenderSectionHeading("Default XML Folders");
        RenderParagraph(
            "For security purposes, these fields do not use a manual browse dialog. If the default in-game XML folders are incorrect, copy and paste the correct folder paths into the Homestead and Guild Hall fields. Enable Show XMLs from Sub-Folders to include XML files stored inside folders beneath those default locations in each import dropdown."
        );

        RenderSectionHeading("Local Data");
        RenderBullet("Check for decoration database updates",
            "When enabled, the addon quickly checks the API during launch for newly added decorations and adds their information to the local database.");
        RenderBullet("Remember addon window state",
            "Remembers the addon's window state and tool options between sessions.");
        RenderBullet("Show decoration count window",
            "Shows or hides the Decoration Counter, which is especially useful for merging, map swapping, and pattern creation. Export List saves its text report to the configured Homestead or Guild Hall folder according to the current XML type. The window can also be reopened by clicking the addon icon in the game menu bar twice to turn the addon windows off and back on without changing the active tool state.");
    }
}

void DocumentationTab::Render()
{
    ImGui::SetWindowFontScale(1.18f);
    ImGui::TextUnformatted("Documentation");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    ImGui::TextUnformatted("Section");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##DocumentationPage", CurrentPageLabel()))
    {
        for (const PageChoice& choice : PageChoices)
        {
            const bool selected = currentPage == choice.page;
            if (ImGui::Selectable(choice.label, selected))
            {
                currentPage = choice.page;
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::BeginChild(
        "##DocumentationPageContent",
        ImVec2(0.0f, 0.0f),
        true,
        ImGuiWindowFlags_AlwaysVerticalScrollbar
    );
    if (currentPage == DocumentationPage::MoveTool) RenderMoveToolPage();
    else if (currentPage == DocumentationPage::Patterns) RenderPatternsPage();
    else if (currentPage == DocumentationPage::MapSwap) RenderMapSwapPage();
    else if (currentPage == DocumentationPage::GroupTools) RenderGroupToolsPage();
    else if (currentPage == DocumentationPage::GroupMover) RenderGroupMoverPage();
    else if (currentPage == DocumentationPage::Settings) RenderSettingsPage();
    ImGui::EndChild();
}
