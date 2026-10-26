// add gold apples
#ifndef NET_MINECRAFT_WORLD_ITEM__GoldenAppleItem_H__
#define NET_MINECRAFT_WORLD_ITEM__GoldenAppleItem_H__

#include "FoodItem.h"
#include "ItemInstance.h"

class GoldenAppleItem : public FoodItem
{
    typedef FoodItem super;

public:
    explicit GoldenAppleItem(int id)
        : super(id, 42, false)
    {
    }

    ItemInstance* use(
        ItemInstance* instance,
        Level* level,
        Player* player) override
    {
        // Allow eating even at full health, unless invulnerable.
        if (instance && player && !player->abilities.invulnerable) {
            player->startUsingItem(
                *instance,
                getUseDuration(instance));
        }

        return instance;
    }

    ItemInstance useTimeDepleted(
        ItemInstance* instance,
        Level* level,
        Player* player) override
    {
        if (!instance || !level || !player) {
            return instance ? *instance : ItemInstance();
        }

        // Consume one golden apple.
        --instance->count;

        // Fully restore health.
        player->heal(player->getMaxHealth());

        // Keep the normal food completion sound.
        level->playSound(
            player,
            "random.burp",
            0.5f,
            level->random.nextFloat() * 0.1f + 0.9f);

        return *instance;
    }
};

#endif