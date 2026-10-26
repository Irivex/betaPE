#include "ChatScreen.h"
#include "DialogDefinitions.h"
#include "../Gui.h"
#include "../../Minecraft.h"
#include "../../player/LocalPlayer.h"
#include "../../../AppPlatform.h"
#include "../../../platform/log.h"

void ChatScreen::init() {
	minecraft->platform()->createUserInput(DialogDefinitions::DIALOG_NEW_CHAT_MESSAGE);
}

void ChatScreen::render(int xm, int ym, float a)
{
	int status = minecraft->platform()->getUserInputStatus();
	if (status > -1) {
		if (status == 1) {
			std::vector<std::string> v = minecraft->platform()->getUserInput();
            if (v.size() && !v[0].empty()) {
                const std::string& input = v[0];

                if (input[0] == '/') {
                    if (!minecraft->level || !minecraft->player) {
                        minecraft->gui.addMessage("World not loaded");
                    }
                    else {
                        // Remove the leading slash before command parsing.
                        std::string commandInput = input.substr(1);

                        std::string result =
                            minecraft->commandManager().execute(
                                *minecraft,
                                *minecraft->player,
                                commandInput);

                        if (!result.empty()) {
                            minecraft->gui.addMessage(result);
                        }
                    }
                }
                else {
                    // Preserve existing behaviour for ordinary text.
                    minecraft->gui.addMessage(input);
                }
            }
		}

		minecraft->setScreen(NULL);
	}
}
