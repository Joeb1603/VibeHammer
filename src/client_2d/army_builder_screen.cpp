#include "army_builder_screen.hpp"

#include <imgui.h>
#include <imgui_internal.h>  // ImHashStr: the combo popup's id, to open a slot's combo for --open-slot screenshots

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "unit_style.hpp"

namespace wh40k::client {
namespace {

using core::Side;

const ImVec4 kTitleColour(1.0f, 0.85f, 0.4f, 1.0f);
const ImVec4 kErrorColour(1.0f, 0.5f, 0.45f, 1.0f);
const ImVec4 kOkColour(0.55f, 0.9f, 0.55f, 1.0f);

ImVec4 to_imvec(Rgb c) { return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f); }

const char* side_title(Side side) { return side == Side::A ? "Side A (south edge)" : "Side B (north edge)"; }
const char* side_name(Side side) { return side == Side::A ? "Side A" : "Side B"; }

// D-065: the unit's shade and number as the board will draw them (its position in the list; no number for a lone
// unit), then SameLine.
void swatch(Side side, std::size_t index, std::size_t side_units) {
    const float em = ImGui::GetTextLineHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Rgb tint = side_shade(side, static_cast<int>(index) + 1);
    dl->AddRectFilled(p, ImVec2(p.x + em, p.y + em), ImGui::GetColorU32(to_imvec(tint)), 2.0f);
    if (side_units > 1) {
        const std::string label = std::to_string(index + 1);
        const ImVec2 t = ImGui::CalcTextSize(label.c_str());
        const ImVec2 at(p.x + (em - t.x) * 0.5f, p.y + (em - t.y) * 0.5f);
        dl->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 255), label.c_str());
        dl->AddText(at, IM_COL32(255, 255, 255, 255), label.c_str());
    }
    ImGui::Dummy(ImVec2(em, em));
    ImGui::SameLine();
}

// "Legionaries 2" (D-065's numbered name: the datasheet name and its position; the name alone for a lone unit).
std::string unit_label(const ArmyBuilder& b, Side side, std::size_t unit) {
    const std::optional<core::Datasheet> sheet = b.datasheet(side, unit);
    std::string name = sheet ? sheet->name : b.list(side).units[unit].unit_file;
    if (b.list(side).units.size() > 1) name += " " + std::to_string(unit + 1);
    return name;
}

std::string option_label(const OptionView& o) {
    return o.points != 0 ? o.name + " (+" + std::to_string(o.points) + " pts)" : o.name;
}

// The problems of one unit among its side's.
std::vector<std::string> of_unit(const std::vector<BuilderProblem>& all, std::size_t unit) {
    std::vector<std::string> out;
    for (const BuilderProblem& p : all) {
        if (p.unit == unit) out.push_back(p.text);
    }
    return out;
}

}  // namespace

void ArmyBuilderScreen::open(ArmyBuilder& builder) {
    builder.refresh_files();
    message_.clear();
    name_active_ = {false, false};
}

void ArmyBuilderScreen::note(const std::optional<std::string>& refusal) {
    message_ = refusal ? plain_first(*refusal) : std::string();  // a done edit clears the last refusal
}

BuilderRequest ArmyBuilderScreen::draw(ArmyBuilder& b, AppSettings& s, const std::string& status) {
    for (const Side side : {Side::A, Side::B}) problems_[core::side_index(side)] = b.problems(side);
    const ImGuiIO& io = ImGui::GetIO();
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                        ImGuiWindowFlags_NoScrollbar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em * 1.0f, em * 0.7f));
    ImGui::Begin("##builder", nullptr, kFlags);
    ImGui::PopStyleVar();

    BuilderRequest out;
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.0f);
    ImGui::TextColored(kTitleColour, "Custom battle");
    ImGui::PopFont();
    ImGui::SameLine(0.0f, em * 1.5f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + em * 0.55f);
    ImGui::TextDisabled("Incursion: up to %d points a side. 44\" x 60\", 12\" zones on the south (A) and north (B) "
                        "edges, %d battle rounds, %s%s.",
                        core::kIncursionPointsLimit, core::kCustomBattleRounds,
                        b.mission() != nullptr
                            ? "the objective mission: VP for objectives and kills, then points destroyed (D-084)"
                            : "decided on points destroyed (D-076)",
                        b.terrain() != nullptr ? "; fought over terrain: ruins, barricades and craters (D-087)" : "");

    // The three columns over everything but the battle options at the bottom.
    const float bottom = ImGui::GetFrameHeightWithSpacing() * 3.0f + ImGui::GetStyle().ItemSpacing.y * 3.0f + em;
    const float height = std::max(em * 12.0f, ImGui::GetContentRegionAvail().y - bottom);
    constexpr ImGuiTableFlags kTable = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##columns", 3, kTable, ImVec2(0.0f, height))) {
        ImGui::TableSetupColumn("A", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("editor", ImGuiTableColumnFlags_WidthStretch, 1.15f);
        ImGui::TableSetupColumn("B", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();
        for (const int column : {0, 1, 2}) {
            ImGui::TableNextColumn();
            ImGui::PushID(column);
            ImGui::BeginChild("##column", ImVec2(0.0f, height - ImGui::GetStyle().CellPadding.y * 2.0f));
            if (column == 1) {
                draw_editor(b, out);
            } else {
                draw_side(b, column == 0 ? Side::A : Side::B, out);
            }
            ImGui::EndChild();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    draw_battle_options(b, s, out);
    const std::string& line = !status.empty() ? status : message_;
    if (!line.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kErrorColour, "%s", line.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    return out;
}

void ArmyBuilderScreen::draw_side(ArmyBuilder& b, Side side, BuilderRequest& /*out*/) {
    const std::size_t si = core::side_index(side);
    const float em = ImGui::GetFontSize();
    const core::ArmyList& list = b.list(side);
    const std::vector<BuilderProblem>& problems = problems_[si];

    ImGui::TextColored(to_imvec(side_rgb(side)), "%s", side_title(side));
    if (b.edited(side)) {
        ImGui::SameLine();
        ImGui::TextDisabled("(edited)");
    }
    if (!b.source(side).empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", std::filesystem::path(b.source(side)).filename().string().c_str());
        ImGui::SetItemTooltip("Loaded from or saved to %s", b.source(side).c_str());
    }

    // Faction (D-079: two different factions).
    const float label_w = em * 4.2f;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Faction");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##faction", b.faction(side).name.c_str())) {
        for (const core::Faction& f : b.factions()) {
            const bool other = b.list(core::opponent(side)).faction == f.id;
            if (ImGui::Selectable(f.name.c_str(), f.id == list.faction)) note(b.set_faction(side, f.id));
            if (other) {
                ImGui::SetItemTooltip("%s plays %s: picking it swaps the two armies (a custom battle needs two "
                                      "different factions, D-079).",
                                      side_name(core::opponent(side)), f.name.c_str());
            }
        }
        ImGui::EndCombo();
    }

    // The army's name (saved lists are named after it).
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Name");
    ImGui::SameLine(label_w);
    if (!name_active_[si]) std::snprintf(names_[si].data(), names_[si].size(), "%s", list.name.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##name", names_[si].data(), names_[si].size())) note(b.rename(side, names_[si].data()));
    name_active_[si] = ImGui::IsItemActive();

    // Points.
    const int points = b.points(side);
    const bool over = points > core::kIncursionPointsLimit;
    char bar[64];
    std::snprintf(bar, sizeof(bar), "%d / %d pts", points, core::kIncursionPointsLimit);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Points");
    ImGui::SameLine(label_w);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, over ? ImVec4(0.8f, 0.25f, 0.2f, 1.0f) : ImVec4(0.3f, 0.6f, 0.35f, 1.0f));
    ImGui::ProgressBar(std::min(1.0f, static_cast<float>(points) / static_cast<float>(core::kIncursionPointsLimit)),
                       ImVec2(-(ImGui::CalcTextSize("0000 / 0000 pts").x + ImGui::GetStyle().ItemSpacing.x), 0.0f), "");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (over) {
        ImGui::TextColored(kErrorColour, "%s", bar);
        ImGui::SetItemTooltip("Over the Incursion limit of %d points (D-079).", core::kIncursionPointsLimit);
    } else {
        ImGui::TextUnformatted(bar);
    }

    // The units, with the add-unit combo under them.
    ImGui::SeparatorText("Units");
    const float frame = ImGui::GetFrameHeightWithSpacing();
    const float problems_h = em * 7.5f;
    const float units_h = std::max(frame * 3.0f, ImGui::GetContentRegionAvail().y - problems_h - frame * 3.4f);
    ImGui::BeginChild("##units", ImVec2(0.0f, units_h), ImGuiChildFlags_Borders);
    constexpr ImGuiTableFlags kUnits = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    std::optional<std::pair<int, std::size_t>> action;  // (0 edit, 1 copy, 2 remove, unit)
    if (ImGui::BeginTable("##unit_table", 4, kUnits)) {
        ImGui::TableSetupColumn("unit", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("models", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("pts", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("buttons", ImGuiTableColumnFlags_WidthFixed);
        for (std::size_t i = 0; i < list.units.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            swatch(side, i, list.units.size());
            const std::vector<std::string> own = of_unit(problems, i);
            const bool selected = b.selected_side() == side && b.selected_unit() == i;
            const std::string label = (own.empty() ? "" : "! ") + unit_label(b, side, i);
            if (!own.empty()) ImGui::PushStyleColor(ImGuiCol_Text, kErrorColour);
            if (ImGui::Selectable(label.c_str(), selected)) action = std::pair{0, i};
            if (!own.empty()) {
                ImGui::PopStyleColor();
                ImGui::SetItemTooltip("%s", plain_first(own.front()).c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%d", b.models(side, i));
            ImGui::SetItemTooltip("models");
            ImGui::TableNextColumn();
            ImGui::Text("%d", b.unit_points(side, i));
            ImGui::SetItemTooltip("points (D-079)");
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Edit")) action = std::pair{0, i};
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy")) action = std::pair{1, i};
            ImGui::SetItemTooltip("Add a copy of this unit, with the same models and wargear.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) action = std::pair{2, i};
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (list.units.empty()) ImGui::TextDisabled("No units yet: add some below.");
    ImGui::EndChild();
    if (action) {
        switch (action->first) {
            case 0: b.select(side, action->second); break;
            case 1: note(b.duplicate_unit(side, action->second)); break;
            default: note(b.remove_unit(side, action->second)); break;
        }
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##add", "Add unit...", ImGuiComboFlags_HeightLarge)) {
        for (const AddUnitView& v : b.add_list(side)) {
            char label[160];
            std::snprintf(label, sizeof(label), "%s   from %d pts   %d/%d", v.name.c_str(), v.points, v.copies, v.limit);
            ImGui::BeginDisabled(v.refusal.has_value());
            if (ImGui::Selectable(label, false)) note(b.add_unit(side, v.unit_file));
            ImGui::EndDisabled();
            if (v.refusal) {
                ImGui::SetItemTooltip("%s", plain_first(*v.refusal).c_str());
            } else {
                ImGui::SetItemTooltip("Copies in the list / allowed at Incursion (D-079). Added with its playable "
                                      "default wargear.");
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("A datasheet of the faction: its points from its smallest unit, and its copies in the list "
                          "out of those allowed at Incursion (2, Battleline 4; D-079).");

    // Problems (rule-cited; the plain words first).
    ImGui::SeparatorText(problems.empty() ? "Problems: none" : "Problems");
    ImGui::BeginChild("##problems", ImVec2(0.0f, std::max(frame * 2.0f, ImGui::GetContentRegionAvail().y - frame * 1.2f)));
    ImGui::PushTextWrapPos(0.0f);
    if (problems.empty()) ImGui::TextColored(kOkColour, "Ready to play.");
    for (const BuilderProblem& p : problems) {
        const std::string text = (p.unit ? unit_label(b, side, *p.unit) + ": " : std::string()) + plain_first(p.text);
        ImGui::Bullet();
        ImGui::TextColored(kErrorColour, "%s", text.c_str());
        if (p.unit && ImGui::IsItemClicked()) b.select(side, p.unit);
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();

    // Load, Save, Reset.
    const std::vector<ArmyFile> files = b.army_files(side);
    ImGui::SetNextItemWidth(em * 7.0f);
    if (ImGui::BeginCombo("##load", "Load...", ImGuiComboFlags_HeightLarge)) {
        for (const ArmyFile& f : files) {
            const std::string label = std::string(f.stock ? "Stock: " : "") + f.name + "  (" +
                                      std::filesystem::path(f.path).filename().string() + ")";
            if (ImGui::Selectable(label.c_str(), b.source(side) == f.path)) {
                const auto refused = b.load(side, f.path);
                message_ = refused ? plain_first(*refused) : "Loaded " + f.path + " for " + side_name(side) + ".";
            }
            ImGui::SetItemTooltip("%s", f.path.c_str());
        }
        if (files.empty()) ImGui::TextDisabled("No %s lists", b.faction(side).name.c_str());
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Load a %s list: the stock lists in data/armies and yours in armies/.",
                          b.faction(side).name.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        const auto refused = b.save_user(side);
        message_ = refused ? plain_first(*refused) : std::string("Saved ") + side_name(side) + "'s list to " +
                                                         b.user_army_path(side).generic_string() + ".";
    }
    ImGui::SetItemTooltip("Save this list to %s (named after the army; the stock lists are never overwritten).",
                          b.user_army_path(side).generic_string().c_str());
    ImGui::SameLine();
    if (ImGui::Button("Reset to stock")) b.reset_to_stock(side);
    ImGui::SetItemTooltip("Go back to the %s stock Incursion list.", b.faction(side).name.c_str());
}

void ArmyBuilderScreen::draw_editor(ArmyBuilder& b, BuilderRequest& out) {
    const float em = ImGui::GetFontSize();
    ImGui::TextColored(kTitleColour, "Unit editor");
    const std::optional<std::size_t> sel = b.selected_unit();
    const Side side = b.selected_side();
    if (!sel || *sel >= b.list(side).units.size()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Click Edit (or a unit's name) to change its models and wargear. Each model type has a "
                            "count within the datasheet's limits (02.06); each loadout has one option per wargear "
                            "slot (02.07). Options with a weapon the engine can't play yet are greyed out, and so are "
                            "model types with no playable loadout (D-079).");
        ImGui::PopTextWrapPos();
        return;
    }
    const std::size_t unit = *sel;
    const std::size_t si = core::side_index(side);
    swatch(side, unit, b.list(side).units.size());
    ImGui::TextColored(to_imvec(side_rgb(side)), "%s", unit_label(b, side, unit).c_str());
    ImGui::SameLine();
    const int models = b.models(side, unit);
    ImGui::TextDisabled("%s, %d model%s, %d pts", side_name(side), models, models == 1 ? "" : "s", b.unit_points(side, unit));
    if (ImGui::Button("Datasheet")) out.datasheet = std::pair{side, unit};
    ImGui::SetItemTooltip("The unit's datasheet: profiles, weapons, abilities and keywords.");
    ImGui::SameLine();
    if (ImGui::Button("Close")) b.select(side, std::nullopt);
    const std::vector<std::string> own = of_unit(problems_[si], unit);
    ImGui::PushTextWrapPos(0.0f);
    for (const std::string& p : own) {
        ImGui::Bullet();
        ImGui::TextColored(kErrorColour, "%s", plain_first(p).c_str());
    }
    ImGui::PopTextWrapPos();

    const std::optional<core::Datasheet> sheet = b.datasheet(side, unit);
    if (sheet) {
        ImGui::SeparatorText(("Models: " + std::to_string(b.models(side, unit)) + " (unit size " +
                              std::to_string(sheet->min_models) + "-" + std::to_string(sheet->max_models) + ")")
                                 .c_str());
    }
    ImGui::BeginChild("##editor_body", ImVec2(0.0f, 0.0f));
    const std::vector<GroupView> groups = b.groups(side, unit);
    const std::vector<ModelTypeView> types = b.model_types(side, unit);
    for (std::size_t t = 0; t < types.size(); ++t) {
        const ModelTypeView& tv = types[t];
        ImGui::PushID(static_cast<int>(t));
        int type_groups = 0;
        for (const GroupView& g : groups) type_groups += g.type == tv.name ? 1 : 0;
        if (tv.unavailable && tv.count == 0) {
            ImGui::TextDisabled("%s  (unavailable)", tv.name.c_str());
            ImGui::SetItemTooltip("%s", plain_first(*tv.unavailable).c_str());
            ImGui::PopID();
            continue;
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tv.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%d of %d-%d", tv.count, tv.min, tv.max);
        ImGui::SetItemTooltip("Models of this type in the unit, and the datasheet's range (02.06).");
        if (tv.count == 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("+ Add")) note(b.add_model_type(side, unit, tv.name));
            ImGui::SetItemTooltip("Add a '%s' model with its default wargear.", tv.name.c_str());
        }
        for (std::size_t gi = 0; gi < groups.size(); ++gi) {
            if (groups[gi].type == tv.name) draw_group(b, side, unit, gi, groups[gi], tv, type_groups);
        }
        ImGui::Spacing();
        ImGui::PopID();
    }
    (void)em;
    ImGui::EndChild();
}

void ArmyBuilderScreen::draw_group(ArmyBuilder& b, Side side, std::size_t unit, std::size_t group, const GroupView& g,
                                   const ModelTypeView& type, int type_groups) {
    const float em = ImGui::GetFontSize();
    ImGui::PushID(static_cast<int>(group) + 1000);
    ImGui::Indent(em * 1.0f);
    // The count stepper (02.06: within the type's range and the unit's maximum).
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("x");
    ImGui::SameLine();
    if (ImGui::ArrowButton("##less", ImGuiDir_Left)) note(b.set_count(side, unit, group, g.count - 1));
    ImGui::SameLine();
    ImGui::Text("%2d", g.count);
    ImGui::SameLine();
    if (ImGui::ArrowButton("##more", ImGuiDir_Right)) note(b.set_count(side, unit, group, g.count + 1));
    if (type.has_options && type.max > 1) {
        ImGui::SameLine();
        ImGui::BeginDisabled(g.count < 2);
        if (ImGui::SmallButton("+ another loadout")) note(b.split_group(side, unit, group));
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Give some of these models different wargear: one model moves to a new loadout of its "
                              "own (raise its count with the arrows).");
    }
    if (type_groups > 1) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Merge")) note(b.merge_group(side, unit, group));
        ImGui::SetItemTooltip("These models join the other '%s' loadout.", g.type.c_str());
    }
    if (type_groups > 1 || type.count - g.count >= type.min) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) note(b.remove_group(side, unit, group));
        ImGui::SetItemTooltip("Remove these models from the unit.");
    }
    if (!g.fits) {
        ImGui::TextColored(kErrorColour, "These weapons don't fit the datasheet's options (02.07).");
    }
    if (g.slots.empty()) {
        std::string weapons;
        for (const std::string& w : g.weapons) weapons += (weapons.empty() ? "" : ", ") + w;
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", weapons.c_str());
        ImGui::PopTextWrapPos();
    }
    for (std::size_t s = 0; s < g.slots.size(); ++s) {
        const SlotView& slot = g.slots[s];
        ImGui::PushID(static_cast<int>(s));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(slot.name.c_str());
        // The combo beside its slot's name, or under it when the name is too long for the label column.
        const float label_w = em * 10.0f;
        const bool indented = ImGui::GetItemRectSize().x + ImGui::GetStyle().ItemSpacing.x >= label_w - em * 1.0f;
        if (indented) {
            ImGui::Indent(em * 1.0f);
        } else {
            ImGui::SameLine(label_w);
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        const std::string preview = slot.chosen ? option_label(slot.options[*slot.chosen]) : std::string("(doesn't fit)");
        if (open_slot_ && b.selected_unit() == unit && open_slot_->first == group && open_slot_->second == s) {
            ImGui::OpenPopup(ImHashStr("##ComboPopup", 0, ImGui::GetID("##slot")));  // --open-slot (screenshots)
            open_slot_.reset();
        }
        if (ImGui::BeginCombo("##slot", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
            for (std::size_t o = 0; o < slot.options.size(); ++o) {
                const OptionView& ov = slot.options[o];
                ImGui::BeginDisabled(ov.refusal.has_value());
                if (ImGui::Selectable(option_label(ov).c_str(), slot.chosen == o)) {
                    note(b.set_option(side, unit, group, s, o));
                }
                ImGui::EndDisabled();
                if (ov.refusal) ImGui::SetItemTooltip("%s", plain_first(*ov.refusal).c_str());
            }
            ImGui::EndCombo();
        }
        if (indented) ImGui::Unindent(em * 1.0f);
        ImGui::PopID();
    }
    ImGui::Unindent(em * 1.0f);
    ImGui::PopID();
}

void ArmyBuilderScreen::draw_battle_options(ArmyBuilder& b, AppSettings& s, BuilderRequest& out) {
    const float em = ImGui::GetFontSize();
    const float gap = em * 2.0f;
    ImGui::Separator();
    // Row 1: first turn, deployment, bot level.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("First turn:");
    for (const Side side : {Side::A, Side::B}) {
        ImGui::SameLine();
        if (ImGui::RadioButton(side_name(side), b.first_turn() == side)) b.set_first_turn(side);
    }
    ImGui::SameLine(0.0f, gap);
    ImGui::TextUnformatted("Deployment:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Auto-place", b.deployment() == core::DeploymentMode::Auto)) {
        b.set_deployment(core::DeploymentMode::Auto);
    }
    ImGui::SetItemTooltip("Every unit starts where the auto-layout puts it: in list order, across its zone (D-079).");
    ImGui::SameLine();
    if (ImGui::RadioButton("Place by hand", b.deployment() == core::DeploymentMode::Hand)) {
        b.set_deployment(core::DeploymentMode::Hand);
    }
    // M76 (D-082 part 2): placing by hand is the board's placement editor now.
    ImGui::SetItemTooltip("The sides take turns setting up one unit at a time, the side taking the second turn first; "
                          "once one side is done, the other sets up the rest (D-081). Drag each unit into place in "
                          "your deployment zone, or auto-place it.");

    // Row 2: how it is played, then Back and Start.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Play:");
    const std::pair<GameMode, const char*> modes[] = {
        {GameMode::Hotseat, "Hotseat"}, {GameMode::VsBot, "Vs bot"}, {GameMode::BotVsBot, "Bot vs bot"}};
    for (const auto& [mode, label] : modes) {
        ImGui::SameLine();
        if (ImGui::RadioButton(label, b.play_mode() == mode)) b.set_play_mode(mode);
    }
    ImGui::SameLine(0.0f, gap);
    ImGui::BeginDisabled(b.play_mode() != GameMode::VsBot);
    ImGui::TextUnformatted("You play:");
    for (const Side side : {Side::A, Side::B}) {
        ImGui::SameLine();
        const std::string label = std::string(side_name(side)) + "##you";
        if (ImGui::RadioButton(label.c_str(), s.human_side == side)) s.human_side = side;
    }
    ImGui::EndDisabled();

    const std::optional<std::string> problem = b.start_problem();
    const float start_w = em * 8.0f;
    const float back_w = em * 5.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + gap,
                             ImGui::GetWindowContentRegionMax().x - start_w - back_w - ImGui::GetStyle().ItemSpacing.x));
    if (ImGui::Button("Back", ImVec2(back_w, 0.0f))) out.kind = BuilderRequest::Kind::Back;
    ImGui::SetItemTooltip("Back to the start screen (this setup is kept).");
    ImGui::SameLine();
    ImGui::BeginDisabled(problem.has_value());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.25f, 1.0f));
    if (ImGui::Button("Start", ImVec2(start_w, 0.0f))) out.kind = BuilderRequest::Kind::Start;
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    if (problem) {
        ImGui::SetItemTooltip("%s", problem->c_str());
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kErrorColour, "Can't start yet. %s", problem->c_str());
        ImGui::PopTextWrapPos();
    } else {
        ImGui::TextColored(kOkColour, "Both armies are ready.");
    }
}

}  // namespace wh40k::client
