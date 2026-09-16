// Pewpew's Deco Tools - Documentation Tab
// Provides scrollable, structured help pages for every tool and the addon
// settings, based on the official Deco Tools documentation.

#include "DocumentationTab.h"

#include "../../imgui/imgui.h"

namespace
{
    enum class DocumentationPage
    {
        SharedImport,
        GroupTools,
        MoveTool,
        Patterns,
        MapSwap,
        GroupBackupRestore,
        Settings
    };

    DocumentationPage currentPage = DocumentationPage::SharedImport;

    struct PageChoice
    {
        DocumentationPage page;
        const char* label;
    };

    constexpr PageChoice PageChoices[] =
    {
        { DocumentationPage::SharedImport, "Import & Workflow" },
        { DocumentationPage::GroupTools, "Group Tools" },
        { DocumentationPage::MoveTool, "Move Tool" },
        { DocumentationPage::Patterns, "Patterns" },
        { DocumentationPage::MapSwap, "Map Swap" },
        { DocumentationPage::GroupBackupRestore, "Group Backup/Restore" },
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
        return "Import & Workflow";
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

    void RenderSharedImportPage()
    {
        RenderPageTitle("Import & Shared Workflow");
        RenderParagraph(
            "Version 1.4 uses one shared working XML across the complete addon. Import once at the top of the main window, switch freely between tools, and continue working without refreshing and importing the same file again."
        );

        RenderSectionHeading("Importing an XML");
        RenderStep(1, "Choose Homestead or Guild Hall",
            "Use the centered switch at the top of the addon. The selected side determines which default XML folder is shown. Folder paths can be changed in Settings.");
        RenderStep(2, "Choose the working XML",
            "Open the XML list and select a file. The closed list remains compact, while the open list expands far enough to show long and indexed filenames. Use Refresh List after saving a new XML from Guild Wars 2.");
        RenderStep(3, "Import Selected",
            "The selected file becomes the shared working XML for Move Tool, Patterns, Map Swap, Group Tools, and Group Backup/Restore. Switching tools does not unload it or create another import backup.");

        RenderSectionHeading("Automatic Working Mode");
        RenderBullet("Full XML",
            "Selected automatically when the imported file contains no named groups. Tools operate on the complete decoration layout and generally create a new indexed output file.");
        RenderBullet("XML Groups",
            "Selected automatically when at least one named group is detected. Tools expose their group-selection workflow and apply group changes directly to the current XML.");
        RenderNote(
            "There is no manual Full XML / XML Groups switch. Creating the first group changes every tool to XML Groups mode. Ungrouping, extracting, or deleting the final group changes every tool back to Full XML mode."
        );

        RenderSectionHeading("Continuing Between Tools");
        RenderParagraph(
            "After Apply, Merge, Group, Extract, Delete Selected, or another direct XML operation succeeds, every tool reloads the updated shared file. When a Full XML operation creates an indexed output such as _MOVED#, _PATTERN#, _MERGED#, or a map-swapped XML, that successful output becomes the new shared working XML automatically."
        );
        RenderBullet("Direct changes",
            "Group-mode Apply operations modify the current XML. Reload that file in Guild Wars 2 to see the result.");
        RenderBullet("Indexed exports",
            "Full XML operations preserve the source and create a new numbered file. Deco Tools continues working from the newly created output.");
        RenderBullet("Failure protection",
            "XML replacements use a temporary file. A failed write leaves the original XML unchanged, and the shared workspace changes only after a successful output can be read back.");
    }

    void RenderMoveToolPage()
    {
        RenderPageTitle("Move Tool");
        RenderParagraph(
            "Move Tool repositions either the complete shared XML or selected named groups. Its source mode is detected automatically from the XML imported at the top of the addon."
        );

        RenderSectionHeading("Move Source");
        RenderBullet("Full XML",
            "Uses the complete imported layout as one construction. Export Updated XML creates a new indexed _MOVED#.xml file and leaves the imported file unchanged.");
        RenderBullet("XML Groups",
            "Provides the complete former Group Mover workflow inside Move Tool. It displays named groups, permits one or multiple group selections, and applies changes directly to the imported XML.");
        RenderNote(
            "The former Group Mover is fully consolidated here. There is no separate Group Mover tool or documentation section, and mode selection is automatic."
        );

        RenderSectionHeading("How to Use");
        RenderStep(1, "Import the shared XML",
            "Use the import area at the top of the addon. See Import & Workflow for the common process and automatic mode rules.");
        RenderStep(2, "Select groups when available",
            "In XML Groups mode, choose one or several groups from the list or by clicking their orange layout points. Selected groups turn blue. Full XML mode skips this step.");
        RenderStep(3, "Move",
            "The Move operation lets you reposition the layout in world X, Y, and Z with the manipulator handles. Drag a colored axis handle to move along only that axis, or drag the gray center box to move freely across the current camera view. The handles retain a consistent on-screen size as the camera moves closer or farther away. The manipulator is placed at the average center of all decorations and at their lowest point, which is usually ground level and easier to locate. If you are unsure where the manipulator is, move your character anywhere on the map and click Move to Character to bring every decoration point to your location.");
        RenderStep(4, "Rotate",
            "The Rotate operation uses advanced group-rotation math while providing three simple rings that rotate the layout around whichever local axis you choose. The rings retain a consistent on-screen size as the camera moves closer or farther away.");
        RenderStep(5, "Export or Apply",
            "Full XML uses Export Updated XML and creates an indexed _MOVED#.xml file, which becomes the new shared working XML. XML Groups uses Undo, Redo, and Apply to XML; Apply writes the selected-group result directly to the current file without clearing the session history.");

        RenderSectionHeading("Moving XML Groups");
        RenderBullet("Group selection",
            "Select one or several named groups with the checkboxes or by clicking their orange layout points. Selected groups turn blue. The manipulator anchor is averaged across every selected group.");
        RenderBullet("Move and Rotate",
            "Move changes the selected groups' world position; Rotate turns their complete construction around the shared averaged anchor using the same advanced XYZ rotation math as Full XML.");
        RenderBullet("Undo and Redo",
            "The XML Groups session retains as many as 100 completed move and rotation operations. Apply to XML does not clear that history, so you can step back through earlier changes and apply the restored state again.");
        RenderBullet("Apply to XML",
            "Writes the selected groups directly into the shared XML instead of creating another file. Importing a different XML or changing the relevant group source starts a new history; simply switching tools does not require another import.");

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
        RenderStep(1, "Import the shared XML",
            "Use the import area at the top of the addon. Patterns automatically follows the Full XML or XML Groups mode detected from that file.");

        RenderStep(2, "Confirm the automatic pattern source", "");
        RenderBullet("Full XML",
            "When no groups exist, the complete imported layout becomes one pattern unit and uses the indexed _PATTERN#.xml export workflow.", 18.0f);
        RenderBullet("XML Groups",
            "When groups exist, Patterns displays only named groups. Select exactly one group from the list or by clicking any orange point belonging to it. The selected group turns blue; other groups remain orange. Ungrouped decorations stay safely in the file but are not shown.", 18.0f);

        RenderStep(3, "Choose a pattern type", "");
        RenderBullet("Line",
            "Creates a straight line by repeating the imported decorations across a fixed Total Offset XYZ. Changing Copies redistributes the instances evenly without moving the outer endpoint. From Center creates the selected number of copies on both sides of the original while preserving both outer endpoints.", 18.0f);
        RenderBullet("Circle",
            "Creates a circular pattern with 2 to 72 total instances. Pattern Count defaults to 6. Sweep defaults to 360 degrees and ranges continuously from 1 to 1080; enter an exact value in the numeric field or use the marked slider. Sweep and Total Vertical Offset establish fixed first and last points, so changing Pattern Count fills the path without changing its endpoint or final height. A flat 360-degree circle remains closed without placing the final copy over the original. Keep Orientation is off by default so replicas follow the circle; enable it to preserve the source orientation around the path.", 18.0f);
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
            "Provides a colored three-axis manipulator on the main object. Drag a colored arrow for a single axis or the gray center box to move freely across the camera view. Moving it repositions the entire pattern, and the handle remains a consistent on-screen size at any camera distance.", 18.0f);
        RenderBullet("Rotate",
            "Provides colored three-axis rotation rings on the main object. Rotating it causes every copy to rotate by the same amount in place.", 18.0f);
        RenderBullet("Pattern Rotate",
            "Provides three-axis rotation rings at the center of the complete pattern and rotates the arrangement as one unit. Object Rotate, Pattern Rotate, and Step Rotation rings retain a consistent on-screen size as the camera moves.", 18.0f);
        RenderBullet("Step Rotation",
            "Keeps the original source orientation unchanged, then cumulatively applies the entered X, Y, and Z rotation to each replica. A 30-degree Z step produces 30 degrees on Copy 1, 60 degrees on Copy 2, 90 degrees on Copy 3, and so on. Combined axes use the same full group-rotation math as the other rotation tools instead of multiplying Euler values directly.", 18.0f);

        RenderStep(5, "Control spacing and offsets",
            "Each pattern type provides a second gray manipulator for changing spacing and offsets. Its gray center box can adjust multiple supported directions together relative to the camera view, while its arrows remain single-axis controls. Line uses a total XYZ offset to its outer endpoint, while Circle uses radius and the total vertical difference between its first and last instances. These adjustments preserve the colored source handle's current position, including after Pattern Rotate. The gray offset manipulator is not available during the Pattern Rotate operation.");
        RenderStep(6, "Export or Apply",
            "Full XML exports a new indexed _PATTERN#.xml file and promotes it to the shared workspace. XML Groups uses Apply to XML instead: the original instance remains in its existing group, and every generated replica becomes a separate adjacent group named Original Group (Copy 1), Original Group (Copy 2), and so on. Existing copy names are skipped automatically. Every other group and every ungrouped decoration remains untouched.");
        RenderStep(7, "Undo and Redo",
            "XML Groups retains as many as 100 pattern-setting, move, and rotation changes during the current selected-group session. Apply to XML does not clear the history, so an earlier result can be restored and applied again.");

        RenderSectionHeading("Common Practices");
        RenderParagraph(
            "Full XML remains useful for creating a separate reusable pattern that can be merged into another build. XML Groups is faster when the source pieces already exist inside a complete workspace: create a named group in Group Tools, switch to Patterns without importing again, pattern that group in place, apply it, and reload the same XML in-game."
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
        RenderStep(1, "Import the source XML",
            "Use the shared import area at the top of the addon, then open Map Swap. The globally imported XML is already loaded as the source.");
        RenderStep(2, "Choose a Homestead destination",
            "If you choose Hearth's Glow or Comosus Isle, simply select the destination and run Pre-Check.");
        RenderStep(3, "Choose a Guild Hall destination",
            "Choosing one of the four Guild Halls reveals the Destination Guild dropdown. If you are not a guild owner, leave it set to No Specific Guild because only guild owners can access decoration counts. If you are the owner, allow the dropdown to populate and select the guild that corresponds to the chosen hall so the Decoration Counter can provide an accurate count.");
        RenderStep(4, "Include or exclude missing decorations",
            "When Include Missing Decorations is enabled, decorations you do not currently have are still written into the XML. When it is disabled, missing decorations are removed from the exported XML.");
        RenderStep(5, "Run Pre-Check",
            "Pre-Check counts decorations in the Decoration Counter and reports what can or cannot be placed at the destination, excluding impossible items. It also provides other important transfer information before export.");
        RenderStep(6, "Swap and Export",
            "Click Swap Maps and Export to create a new XML with the same base title and the destination map suffix. Decorations created on the source map will then load correctly on the destination map, regardless of map type. A successful output becomes the new shared working XML and updates the global Homestead or Guild Hall folder selection to match.");

        RenderSectionHeading("Common Uses");
        RenderParagraph(
            "Map Swap is ideal when changing Homesteads or Guild Halls without losing completed work. It can also convert a Guild Hall build to a Homestead, or the reverse, when corresponding decorations exist. After export, switch directly to Move Tool, Patterns, or Group Tools to continue refining the converted layout without importing it again."
        );
    }

    void RenderGroupToolsPage()
    {
        RenderPageTitle("Group Tools");
        RenderParagraph(
            "Group Tools organizes the shared working XML. Group creates named sections, Merge adds other XML files to the current layout, and Extract can copy, remove, or permanently delete selected groups."
        );

        RenderSectionHeading("Shared Source");
        RenderParagraph(
            "Import the working XML once at the top of the addon. It is automatically used as the current XML and the Merge base layout. Group is the first and default operation."
        );

        RenderSectionHeading("Group Decorations");
        RenderStep(1, "Select decorations",
            "Ungrouped decorations appear as orange layout points. Left-click to select and right-click to deselect. Enable Marquee Select to drag over several points at once; selected points turn blue.");
        RenderStep(2, "Control layout visibility",
            "Lower Visibility Distance when a large layout makes selection crowded. Hide Grouped Decorations removes already organized gray points from the view.");
        RenderStep(3, "Create the group",
            "Enter a unique group name and click Create Group. The current XML is rewritten directly, and creating its first group automatically switches the entire addon to XML Groups mode.");
        RenderStep(4, "Ungroup when needed",
            "Click the X beside a group to remove its heading and return its decorations to the ungrouped section. The final group can also be removed; doing so switches every tool back to Full XML mode.");

        RenderSectionHeading("Merge XML Files");
        RenderStep(1, "Select additional XML files",
            "The shared working XML is already the Base Layout. Check every other XML you want to add. Use Refresh List if a newly saved file does not appear.");
        RenderStep(2, "Prepare Merge",
            "Run the pre-check and review map compatibility, final decoration totals, and the Decoration Counter before continuing.");
        RenderStep(3, "Merge",
            "In Full XML mode, Merge and Export creates an indexed _MERGED#.xml and makes it the new shared working file. In XML Groups mode, Merge applies directly to the current XML.");
        RenderBullet("Named incoming groups",
            "Remain separate groups. Duplicate names receive a deterministic numbered suffix so every group stays selectable.", 18.0f);
        RenderBullet("Incoming ungrouped decorations",
            "Remain in the ungrouped section and are never forced into an artificial group.", 18.0f);
        RenderBullet("Complete prop data",
            "Is copied exactly, including extended payloads used by special decorations such as grave markers and Café props.", 18.0f);

        RenderSectionHeading("Extract Groups");
        RenderStep(1, "Select groups",
            "Choose one or several named groups from the list. Extract is available only when the shared XML contains groups.");
        RenderStep(2, "Choose the result", "");
        RenderBullet("Extract to XML",
            "Creates one indexed Group Name_EXTRACTED#.xml for each selected group, then removes those groups and their decorations from the current XML.", 18.0f);
        RenderBullet("Copy to XML",
            "Creates the same individual extracted XML files but leaves the current XML completely unchanged.", 18.0f);
        RenderBullet("Delete Selected",
            "The red group-mode-only button deletes the selected groups and every decoration inside them from the current XML without creating extracted files. A Safety backup is required and created before deletion.", 18.0f);
        RenderNote(
            "Extract no longer creates a separate _STRIPPED# file. Removing or deleting the final group updates the shared workspace to Full XML mode."
        );

        RenderSectionHeading("More Info");
        RenderParagraph(
            "Guild Wars 2 removes group comments whenever it saves a layout. Deco Tools automatically backs up eligible imports and outputs so those groups can be restored later. Grouping a build also unlocks the fastest Move Tool and Patterns workflows because every tool immediately sees the same named sections."
        );
    }

    void RenderGroupBackupRestorePage()
    {
        RenderPageTitle("Group Backup/Restore");
        RenderParagraph(
            "Group Backup/Restore protects named XML groups from being lost when Guild Wars 2 or another operation rewrites the decoration list without its group comments. Automatic restore points are stored locally, while the manual tools remain available even when automation is disabled."
        );

        RenderSectionHeading("Automatic Backup and Restore");
        RenderBullet("Automatic backup",
            "When enabled in Settings, the one shared import records a complete restore point for an XML containing named groups. Successful grouped Apply, Export, Merge, Group, Extract, Copy, and other qualifying outputs also create restore points. These backups include grouped and ungrouped decorations. Repeated unchanged files are deduplicated. The most recent 20 automatic points are retained for each XML lineage; Manual and Safety points are never removed by that limit.");
        RenderBullet("Backup Ungrouped XMLs",
            "This separate option is disabled by default and applies only to automatic backups. When enabled, imports and successful outputs containing decorations but no named groups also receive complete-XML rebuild backups. The first import of a new groupless filename receives a lightweight confirmation asking whether it should be treated as new. Zero-group backups are available in Rebuild XML but are excluded from Restore Groups because they contain no group membership.");
        RenderBullet("Automatic restore",
            "When an XML without groups is imported, the addon first checks the complete filename and then its normalized lineage. Known suffixes such as _MOVED#, _MERGED#, _STRIPPED#, _PATTERN#, and map-swap names are removed while finding the related build.");
        RenderBullet("Conservative matching",
            "Decorations are matched using normalized prop attributes. Attribute order, whitespace, equivalent numeric formatting, and the tiny transform precision changes introduced by an in-game XML save do not matter. A decoration that was genuinely moved, rotated, modified, or deleted is excluded from its old group and remains ungrouped above the restored group sections. Payload data remains part of the match, and duplicate identical props are assigned only once.");
        RenderNote(
            "A confident related backup restores silently. If a related grouped history exists but cannot be matched safely, Deco Tools first asks whether this is a new file. Choose Import as New to continue without restoring, or Restore Groups Now to open the full candidate list. New groupless files import silently when Backup Ungrouped XMLs is disabled."
        );

        RenderSectionHeading("Manual Backup");
        RenderStep(1, "Import the XML to protect",
            "Use the shared importer at the top of the addon, then open Group Backup/Restore. The current working XML is already available to the manual backup controls.");
        RenderStep(2, "Add an optional name",
            "A custom name makes important milestones easier to identify. If left blank, the restore point uses the XML filename.");
        RenderStep(3, "Create the backup",
            "Create Manual Backup always stores the complete XML, including every grouped and ungrouped decoration, its map metadata, and complete prop payloads. Manual backups also support XMLs containing no named groups, regardless of the Backup Ungrouped XMLs setting.");

        RenderSectionHeading("Manual Restore");
        RenderStep(1, "Import the target XML",
            "Use the shared importer to select the file whose groups should be rebuilt. Homestead restore points cannot be applied to Guild Hall XMLs, or the reverse.");
        RenderStep(2, "Choose a restore source",
            "Use the category dropdown to show All Restore Options, Saved XMLs, Automatic Backups, Manual Backups, or Safety Backups. Choose the specific source from the visible scrollable list below it. Saved XMLs containing groups are read from the configured folder and listed newest first. Database restore points show their custom name or XML filename, persistent type, timestamp, and group count.");
        RenderStep(3, "Review the preview",
            "Matched shows decorations that can return to a group. Missing/Modified shows backed-up decorations that no longer match. Ungrouped shows current decorations that will remain outside restored groups.");
        RenderStep(4, "Restore",
            "Restore Selected Groups rewrites the target atomically and keeps a blank line between group sections. If the target already contains groups, a persistent Safety point is recorded first. If that safety backup cannot be saved, the restore is canceled; a failed XML write also leaves the original untouched.");

        RenderSectionHeading("Manage Backups");
        RenderParagraph(
            "Manage Backups opens a separate window for stored Manual and Safety restore points. Filter the list, select one or several entries, rename one selected backup, or delete multiple selected backups after confirmation. Renaming a Safety backup does not change its type. These controls never rename or delete saved XML files."
        );

        RenderSectionHeading("Emergency XML Recovery");
        RenderParagraph(
            "Rebuild XML creates a new decoration XML directly from an Automatic, Manual, or Safety database backup. Choose the restore point, enter a new filename, and press Rebuild. The .xml extension is added automatically when omitted, and an existing file is never overwritten."
        );
        RenderNote(
            "Backups created by version 1.3.3.4 and later retain the Decorations root metadata, all ungrouped props, every group, and each prop's complete raw XML payload. This preserves special decorations such as grave markers. Older database backups can rebuild grouped decorations only. For those older backups, load into the intended Homestead or Guild Hall first so current Mumble map data can supply the required mapId, mapName, and type header."
        );

        RenderSectionHeading("Local Storage");
        RenderParagraph(
            "Restore points are stored in group_backups.db inside the addon's DecoTools data folder beside its other databases. Automatic, Manual, and Safety are separate persistent types. Older Pre-Restore Safety entries are migrated automatically. Database updates use a temporary file and a recovery .bak copy so an interrupted save cannot silently replace the last valid database."
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
        RenderBullet("Guilds", "");
        RenderBullet("Account", "");
        RenderBullet("Inventories", "");
        RenderBullet("Unlocks", "");

        RenderSectionHeading("Default XML Folders");
        RenderParagraph(
            "For security purposes, these fields do not use a manual browse dialog. If the default in-game XML folders are incorrect, copy and paste the correct paths into the Homestead and Guild Hall fields. The switch in the shared import area changes between these two folders. Enable Show XMLs from Sub-Folders to include files stored in folders beneath the selected location."
        );

        RenderSectionHeading("Local Data");
        RenderBullet("Check for decoration database updates",
            "When enabled, the addon quickly checks the API during launch for newly added decorations and adds their information to the local database.");
        RenderBullet("Remember addon window state",
            "Remembers the addon's window state and tool options between sessions.");
        RenderBullet("Show decoration count window",
            "Shows or hides the Decoration Counter, which follows the current tool result. Red rows indicate that Required is greater than Available or greater than the legal Homestead Max Count. Reaching a legal maximum exactly is not treated as an error. Export List writes the report to the configured Homestead or Guild Hall folder for the current XML type.");
        RenderBullet("Use Deco Tools Interface Style",
            "Enabled by default. Protects Deco Tools with its own 15 px font, spacing, colors, and transparency so global Nexus style changes cannot disrupt the addon's layout. Disable it to inherit the complete Nexus interface style instead. Nexus DPI and UI scaling still apply in either mode.");
        RenderBullet("Automatically backup and restore XML groups",
            "Enabled by default. Grouped imports and successful grouped Apply and Export operations create automatic restore points, while ungrouped imports attempt to recover lost group comments. Turning it off disables both automatic actions but never removes the Group Backup/Restore page or its manual tools.");
        RenderBullet("Backup Ungrouped XMLs",
            "Disabled by default. When enabled, the automatic backup system also creates complete rebuild points for XMLs containing decorations but no named groups. It does not control manual or safety backups, and grouped backups always retain their ungrouped decorations.");

        RenderSectionHeading("Preview");
        RenderParagraph(
            "Bounding Box, Solid Faces, and the box, face, and decoration-point colors are configured here. Decoration-point visibility and Point Size remain accessible at all times from the far right of the main window's bottom status bar."
        );

        RenderSectionHeading("Quick Start Guide");
        RenderParagraph(
            "Open Quick Start / Tutorials reopens the initial folder, backup, and API setup pages together with the guided grouping, moving, and recovery tutorials. Closing the guide never blocks the addon or resets completed setup."
        );
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
    if (currentPage == DocumentationPage::SharedImport) RenderSharedImportPage();
    else if (currentPage == DocumentationPage::GroupTools) RenderGroupToolsPage();
    else if (currentPage == DocumentationPage::MoveTool) RenderMoveToolPage();
    else if (currentPage == DocumentationPage::Patterns) RenderPatternsPage();
    else if (currentPage == DocumentationPage::MapSwap) RenderMapSwapPage();
    else if (currentPage == DocumentationPage::GroupBackupRestore) RenderGroupBackupRestorePage();
    else if (currentPage == DocumentationPage::Settings) RenderSettingsPage();
    ImGui::EndChild();
}
