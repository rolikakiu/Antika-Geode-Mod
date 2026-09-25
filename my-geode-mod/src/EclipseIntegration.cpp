#include <Geode/Geode.hpp>
#include <eclipse.eclipse-menu/include/eclipse.hpp>
#include <eclipse.eclipse-menu/include/labels.hpp>
#include <eclipse.eclipse-menu/include/modules.hpp>

using namespace geode::prelude;

static void eclipseSyncAll() {
    if (!Loader::get()->isModLoaded("eclipse.eclipse-menu")) return;
    auto mod = Mod::get();
    eclipse::config::set("antika-disable", mod->getSettingValue<bool>("disable"));
    eclipse::config::set("antika-neghit", mod->getSettingValue<bool>("neghit"));
    eclipse::config::set("antika-negative", mod->getSettingValue<bool>("negative"));
    eclipse::config::set("antika-accurate", mod->getSettingValue<bool>("accurate"));
    eclipse::config::set("antika-truehit", mod->getSettingValue<bool>("truehit"));
    eclipse::config::set("antika-noclip", mod->getSettingValue<bool>("noclip"));
    eclipse::config::set("antika-force-ice", mod->getSettingValue<bool>("force-ice"));
    eclipse::config::set("antika-force-platformer", mod->getSettingValue<bool>("force-platformer"));
    eclipse::config::set("antika-all-modes-platformer", mod->getSettingValue<bool>("all-modes-platformer"));
    eclipse::config::set("antika-make-3d", mod->getSettingValue<bool>("make-3d"));
}

$on_mod(Loaded) {
    auto loader = Loader::get();
    if (!loader->isModLoaded("eclipse.eclipse-menu")) return;

    loader->queueInMainThread([]() {
        auto mod = Mod::get();

        eclipse::modules::registerCheat("antika", [mod]() {
            return mod->getSettingValue<bool>("neghit")
                || mod->getSettingValue<bool>("negative")
                || mod->getSettingValue<bool>("accurate")
                || mod->getSettingValue<bool>("truehit")
                || mod->getSettingValue<bool>("noclip")
                || mod->getSettingValue<bool>("make-3d");
        });

        auto tab = eclipse::MenuTab::find("antika");

        tab.addToggle("antika-disable", "Story Mode", [](bool value) {
            Mod::get()->setSettingValue("disable", value);
        }).setDescription("Plays level 149188646 on the main menu and shows the ending dialog after beating it.");

        tab.addToggle("antika-neghit", "Negative Hitboxes", [](bool value) {
            Mod::get()->setSettingValue("neghit", value);
        }).setDescription("Set every object's size to -1 and turn off the level's \"Fix Negative Scale\" so every hitbox becomes negative. It is recommended to re-enter the level.");

        tab.addToggle("antika-negative", "Negative", [](bool value) {
            Mod::get()->setSettingValue("negative", value);
        }).setDescription("Inverted world: upside-down play and photo-negative colors. Requires Negative Hitboxes.");

        tab.addToggle("antika-accurate", "Accurate Hitboxes", [](bool value) {
            Mod::get()->setSettingValue("accurate", value);
        }).setDescription("Accurate spike and saw hitboxes: you die on the real shape instead of the rough rectangle.");

        tab.addToggle("antika-truehit", "TRUE Hitboxes", [](bool value) {
            Mod::get()->setSettingValue("truehit", value);
        }).setDescription("Every hazard gets an accurate hitbox. Requires Accurate Hitboxes.");

        tab.addToggle("antika-noclip", "Noclip", [](bool value) {
            Mod::get()->setSettingValue("noclip", value);
        }).setDescription("Spikes can't touch you and blocks are passable: fly through everything and never die.");

        tab.addToggle("antika-force-ice", "Force Ice", [](bool value) {
            Mod::get()->setSettingValue("force-ice", value);
        }).setDescription("Every block becomes an ice block in platformer mode.");

        tab.addToggle("antika-force-platformer", "Force Platformer", [](bool value) {
            Mod::get()->setSettingValue("force-platformer", value);
        }).setDescription("Play classic levels in platformer mode.");

        tab.addToggle("antika-all-modes-platformer", "All Modes in Platformer", [](bool value) {
            Mod::get()->setSettingValue("all-modes-platformer", value);
        }).setDescription("Play platformer levels as any game mode instead of only Cube. Pick the mode in the mod settings.");

        tab.addToggle("antika-make-3d", "Make Everything 3D", [](bool value) {
            Mod::get()->setSettingValue("make-3d", value);
        }).setDescription("Apply the famous 3D shader look to every level (Radial Blur -0.50 size / 500000 intensity / ref channel 1234, Bulge 0.25, B5 to Max).");

        eclipseSyncAll();

        geode::listenForSettingChanges<bool>("disable", [](bool value) {
            eclipse::config::set("antika-disable", value);
        });
        geode::listenForSettingChanges<bool>("neghit", [](bool value) {
            eclipse::config::set("antika-neghit", value);
        });
        geode::listenForSettingChanges<bool>("negative", [](bool value) {
            eclipse::config::set("antika-negative", value);
        });
        geode::listenForSettingChanges<bool>("accurate", [](bool value) {
            eclipse::config::set("antika-accurate", value);
        });
        geode::listenForSettingChanges<bool>("truehit", [](bool value) {
            eclipse::config::set("antika-truehit", value);
        });
        geode::listenForSettingChanges<bool>("noclip", [](bool value) {
            eclipse::config::set("antika-noclip", value);
        });
        geode::listenForSettingChanges<bool>("force-ice", [](bool value) {
            eclipse::config::set("antika-force-ice", value);
        });
        geode::listenForSettingChanges<bool>("force-platformer", [](bool value) {
            eclipse::config::set("antika-force-platformer", value);
        });
        geode::listenForSettingChanges<bool>("all-modes-platformer", [](bool value) {
            eclipse::config::set("antika-all-modes-platformer", value);
        });
        geode::listenForSettingChanges<bool>("make-3d", [](bool value) {
            eclipse::config::set("antika-make-3d", value);
        });
    });
}