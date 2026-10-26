#pragma once

#include "Command.hpp"

class CommandGamemode : public Command {
public:
    CommandGamemode();

    std::string execute(
        Minecraft& mc,
        Player& player,
        const std::vector<std::string>& args) override;

    std::string help(Minecraft& mc) override;
};