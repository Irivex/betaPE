#include "CommandGamemode.hpp"

#include <algorithm>
#include <cctype>

#include <client/Minecraft.h>
#include <world/level/Level.h>
#include <world/level/LevelSettings.h>
#include <world/level/storage/LevelData.h>
#include <world/entity/player/Player.h>
#include <world/entity/player/Inventory.h>

CommandGamemode::CommandGamemode()
    : Command("gamemode")
{
}

std::string CommandGamemode::execute(
    Minecraft& mc,
    Player& player,
    const std::vector<std::string>& args)
{
    if (!mc.level) {
        return "World not loaded";
    }

    if (!mc.level->getLevelData()->getAllowCheats()) {
        return "Cheats are not allowed!";
    }

    if (args.size() != 1) {
        return help(mc);
    }

    std::string mode = args[0];

    // Make the command case-insensitive.
    std::transform(
        mode.begin(),
        mode.end(),
        mode.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

    bool creative;

    if (mode == "1") {
        creative = true;
    }
    else if (mode == "0") {
        creative = false;
    }
    else {
        return help(mc);
    }

    const bool modeChanged = mc.isCreativeMode() != creative;

    const int gameType = creative
        ? GameType::Creative
        : GameType::Survival;

    // Update the world and active game mode.
    mc.level->getLevelData()->setGameType(gameType);
    mc.setIsCreativeMode(creative);

    // Reload the mode-specific inventory when switching modes.
    if (modeChanged && player.inventory) {
        player.inventory->clearInventoryWithDefault();
    }

    return "Set game mode to " + mode + ".";
}

std::string CommandGamemode::help(Minecraft& mc)
{
    return "Usage: /gamemode <0|1>";
}