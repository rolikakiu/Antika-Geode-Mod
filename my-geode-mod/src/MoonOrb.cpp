#include <Geode/Geode.hpp>
#include <Geode/modify/EditButtonBar.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <cocos2d.h>
#include <cmath>
#include <vector>

using namespace geode::prelude;

/* The Antika orb throws the player up to the moon, a fixed distance above
   wherever the orb was touched, and the return orb drops them back down to
   that same spot.

   The orbs are their own objects now. GD's level format only stores object
   IDs and has no free slots for brand-new objects, so each Antika orb rides
   on a vanilla object that is invisible and does nothing by itself: the
   "Thin Invisible Outline" (Object 1340) is the way-up orb and the "Thick
   Invisible Outline" (Object 1343) is the way-back orb. Neither has a
   visible sprite, a hitbox or any gameplay, so reusing them changes nothing
   about any existing level. No vanilla orb, ring, or trigger is touched:
   Ring, Event Link, and the teleport portal are all back to normal.

   Everything the orb is made of is drawn with CCDrawNode, only while a level
   is actually running. In the editor a GameObject lives inside a sprite
   batch node, and the editor reorders that batch node every frame by walking
   each object's children as if they were all sprites — so the mod never
   attaches anything to a GameObject you are editing, and placing an Antika
   orb can never crash the game again. */

namespace {

constexpr float kMoonHeight = 2000.f;
constexpr float kTouchRadius = 46.f;
constexpr float kOrbSize = 30.f;
constexpr float kOrbCooldown = 0.5f;
constexpr float kFloorHalfWidth = 2600.f;
constexpr float kCeilingHeight = 1400.f;
constexpr float kUnderFloor = 700.f;

// the two invisible vanilla decorations the Antika orbs are saved as
constexpr int kOrbUpCarrier = 1340;   // Thin Invisible Outline
constexpr int kOrbBackCarrier = 1343; // Thick Invisible Outline

// the in-game orb art is added as a tagged child exactly once
constexpr int kOrbArtTag = 0x0A19;

bool isOrbObject(cocos2d::CCNode* node) {
    auto obj = typeinfo_cast<GameObject*>(node);
    return obj && (obj->m_objectID == kOrbUpCarrier || obj->m_objectID == kOrbBackCarrier);
}

bool isBackOrbObject(cocos2d::CCNode* node) {
    auto obj = typeinfo_cast<GameObject*>(node);
    return obj && obj->m_objectID == kOrbBackCarrier;
}

/* ---------------- orb art ---------------- */

cocos2d::ccColor4F shade(int r, int g, int b, float a) {
    return cocos2d::ccColor4F{ r / 255.f, g / 255.f, b / 255.f, a };
}

// a glowing orb: soft halo, bright core, hard rim, one highlight
//
// This runs only from the in-game tick below, never in the editor, so the
// child it draws can never be walked like a sprite by the editor's batch
// node. The invisible carrier still gets a small real hitbox so it can be
// selected and moved while editing.
void ensureOrbArt(GameObject* object, bool back) {
    if (!object || object->getChildByTag(kOrbArtTag)) return;

    int r = back ? 96 : 178;
    int g = back ? 216 : 120;
    int b = back ? 255 : 255;

    auto art = cocos2d::CCDrawNode::create();
    art->drawCircle(cocos2d::CCPoint(0, 0), 21.f, shade(r, g, b, 0.08f), 0.f, shade(0, 0, 0, 0.f), 32);
    art->drawCircle(cocos2d::CCPoint(0, 0), 18.f, shade(r, g, b, 0.14f), 0.f, shade(0, 0, 0, 0.f), 32);
    art->drawCircle(cocos2d::CCPoint(0, 0), 15.f, shade(r, g, b, 0.30f), 0.f, shade(0, 0, 0, 0.f), 32);
    art->drawCircle(cocos2d::CCPoint(0, 0), 12.f, shade(r, g, b, 0.92f), 0.f, shade(0, 0, 0, 0.f), 32);
    art->drawCircle(cocos2d::CCPoint(0, 0), 15.f, shade(r, g, b, 0.f), 2.5f, shade(255, 255, 255, 0.80f), 32);
    art->drawDot(cocos2d::CCPoint(-4.5f, 4.5f), 4.5f, shade(255, 255, 255, 0.85f));
    art->drawDot(cocos2d::CCPoint(-5.5f, 5.5f), 2.f, shade(255, 255, 255, 0.95f));
    art->setTag(kOrbArtTag);
    object->addChild(art, 10);

    // a real box, so the invisible object can be selected in the editor
    object->m_objectRect = cocos2d::CCRectMake(-kOrbSize / 2, -kOrbSize / 2, kOrbSize, kOrbSize);
}

// the moon, its craters, the stars and the ground the player runs on
void dressMoon(cocos2d::CCNode* node, cocos2d::CCPoint surface) {
    // the ground is a plain coloured band, so there is always something solid
    // looking to stand on even if the drawn art below goes missing
    auto band = cocos2d::CCLayerColor::create(cocos2d::ccc4(150, 150, 160, 255), kFloorHalfWidth * 2.f, 200.f);
    band->setPosition(cocos2d::CCPoint(surface.x, surface.y - 100.f));
    node->addChild(band, 0);

    auto art = cocos2d::CCDrawNode::create();

    auto centre = cocos2d::CCPoint(surface.x, surface.y + 560.f);
    art->drawCircle(centre, 130.f, shade(216, 216, 224, 1.f), 3.f, shade(178, 178, 190, 1.f), 64);

    struct Crater { float x, y, r; };
    static const Crater craters[] = {
        { -38.f, 32.f, 26.f }, { 30.f, -22.f, 18.f }, { 58.f, 44.f, 13.f },
        { -64.f, -44.f, 16.f }, { 6.f, 70.f, 11.f }, { -12.f, -70.f, 21.f },
    };
    for (auto& crater : craters) {
        auto at = cocos2d::CCPoint(centre.x + crater.x, centre.y + crater.y);
        art->drawCircle(at, crater.r, shade(180, 180, 192, 0.85f), 1.5f, shade(238, 238, 244, 0.5f), 32);
    }

    unsigned int seed = 0x5eed1234u;
    auto rand01 = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / static_cast<float>(1 << 24);
    };
    for (int i = 0; i < 34; ++i) {
        float angle = rand01() * 6.2831853f;
        float dist = 320.f + rand01() * 1600.f;
        float y = surface.y + 140.f + (rand01() - 0.35f) * 1700.f;
        art->drawDot(
            cocos2d::CCPoint(surface.x + std::cos(angle) * dist, y),
            2.f + rand01() * 4.f,
            shade(255, 255, 255, 0.35f + rand01() * 0.6f)
        );
    }

    for (int i = -15; i <= 15; ++i) {
        art->drawCircle(
            cocos2d::CCPoint(surface.x + i * 185.f, surface.y - 70.f),
            88.f, shade(198, 198, 208, 0.95f), 0.f, shade(0, 0, 0, 0.f), 40
        );
    }

    node->addChild(art, 0);
}

/* ---------------- finding the orbs ---------------- */

// The orbs are found by walking the running scene for the carrier IDs above
// instead of keeping raw pointers to them: a pointer kept across frames can
// outlive the object it points at, and reading it takes the game down.
struct OrbRef {
    GameObject* object = nullptr;
    bool back = false;
};

void collectOrbs(cocos2d::CCNode* node, std::vector<OrbRef>& out) {
    if (!node) return;
    if (typeinfo_cast<GameObject*>(node) && isOrbObject(node)) {
        out.push_back({ static_cast<GameObject*>(node), isBackOrbObject(node) });
        return;
    }
    auto children = node->getChildren();
    for (int i = 0; children && i < children->count(); ++i) {
        collectOrbs(static_cast<cocos2d::CCNode*>(children->objectAtIndex(i)), out);
    }
}

std::vector<OrbRef> findOrbs() {
    std::vector<OrbRef> orbs;
    auto scene = cocos2d::CCDirector::get()->getRunningScene();
    if (scene) collectOrbs(scene, orbs);
    return orbs;
}

/* ---------------- moon state ---------------- */

struct MoonState {
    cocos2d::CCPoint surface = cocos2d::CCPoint(0, 0);
    cocos2d::CCPoint backPos = cocos2d::CCPoint(0, 0);
    bool hasBack = false;
    bool built = false;
    cocos2d::CCNode* node = nullptr;
    cocos2d::CCNode* night = nullptr;
};

MoonState s_moon;
float s_clock = 0.f;
float s_lastUp = -100.f;
float s_lastDown = -100.f;

template <typename F>
void eachPlayer(F&& fn) {
    auto layer = PlayLayer::get();
    if (!layer) return;
    if (layer->m_player1) fn(layer->m_player1);
    if (layer->m_player2) fn(layer->m_player2);
}

bool inMoonArea(PlayerObject* player) {
    return std::fabs(player->m_position.x - s_moon.surface.x) <= kFloorHalfWidth
        && player->m_position.y <= s_moon.surface.y + kCeilingHeight
        && player->m_position.y >= s_moon.surface.y - kUnderFloor;
}

void moveTo(PlayerObject* player, cocos2d::CCPoint dest) {
    player->setYVelocity(0, 0);
    player->setPosition(dest);
    player->m_position = dest;
}

void buildMoon(cocos2d::CCPoint surface);

void enterMoon(PlayerObject* player) {
    auto dest = cocos2d::CCPointMake(player->m_position.x, player->m_position.y + kMoonHeight);
    s_moon.hasBack = true;
    s_moon.backPos = player->m_position;
    buildMoon(dest);
    moveTo(player, dest);
}

void leaveMoon(PlayerObject* player) {
    if (!s_moon.hasBack) return;
    auto dest = s_moon.backPos;
    s_moon.hasBack = false;
    moveTo(player, dest);
}

// the moon has no real ground, so hold anyone who sinks into it on the surface
void moonTick() {
    if (!s_moon.built) return;

    bool anyInside = false;
    eachPlayer([&anyInside](PlayerObject* player) {
        if (player->m_isDead || !inMoonArea(player)) return;
        anyInside = true;
        if (player->m_position.y > s_moon.surface.y) return;

        player->setYVelocity(0, 0);
        player->m_isOnGround = true;
        moveTo(player, cocos2d::CCPointMake(player->m_position.x, s_moon.surface.y));
    });

    if (s_moon.night) s_moon.night->setVisible(anyInside);
}

void orbTick(float dt) {
    s_clock += dt;

    auto orbs = findOrbs();
    if (orbs.empty()) return;

    eachPlayer([&orbs](PlayerObject* player) {
        if (player->m_isDead) return;

        for (auto& orb : orbs) {
            ensureOrbArt(orb.object, orb.back);

            auto pos = orb.object->getPosition();
            float dx = player->m_position.x - pos.x;
            float dy = player->m_position.y - pos.y;
            if (std::hypot(dx, dy) > kTouchRadius) continue;

            if (orb.back) {
                if (s_clock - s_lastDown < kOrbCooldown) continue;
                s_lastDown = s_clock;
                leaveMoon(player);
            } else {
                if (s_clock - s_lastUp < kOrbCooldown) continue;
                s_lastUp = s_clock;
                enterMoon(player);
            }
        }
    });
}

class AntikaMoonNode : public cocos2d::CCNode {
public:
    static AntikaMoonNode* create() {
        auto node = new AntikaMoonNode();
        node->autorelease();
        node->scheduleUpdate();
        return node;
    }

    void update(float dt) override {
        cocos2d::CCNode::update(dt);
        moonTick();
        orbTick(dt);
    }
};

void buildMoon(cocos2d::CCPoint surface) {
    auto node = s_moon.node;
    if (!node) return;

    node->removeAllChildren();
    s_moon.surface = surface;
    s_moon.built = true;

    dressMoon(node, surface);
}

void startMoon(PlayLayer* layer) {
    auto node = AntikaMoonNode::create();
    layer->addChild(node, -500);
    s_moon.node = node;

    auto win = cocos2d::CCDirector::get()->getWinSize();
    auto night = cocos2d::CCLayerColor::create(cocos2d::ccc4(12, 14, 34, 120), win.width, win.height);
    night->setZOrder(-900);
    night->setVisible(false);
    layer->addChild(night, -900);
    s_moon.night = night;
}

/* ---------------- the editor ---------------- */

// When the editor opens a build tab it builds a tab bar and feeds it the
// tab's objects through EditButtonBar::loadFromItems. The Orbs tab is hooked
// here: its two Antika entries are added to that object list, created with
// the same helper GD uses for its own tab buttons, before the bar is laid
// out — so they appear exactly like vanilla orbs and place like them too.

// the tab bar's parent is the EditorUI, but some GD versions lay the bar out
// before it is parented; keep a pointer from EditorUI::init as a fallback
EditorUI* s_orbEditorUI = nullptr;

class $modify(AntikaEditorUI, EditorUI) {
    bool init(LevelEditorLayer* editorLayer) {
        s_orbEditorUI = this;
        return EditorUI::init(editorLayer);
    }
};

class $modify(AntikaEditButtonBar, EditButtonBar) {
    struct Fields {
        bool m_orbAdded = false;
    };

    void loadFromItems(cocos2d::CCArray* objects, int rows, int columns, bool keepPage) {
        auto ui = static_cast<EditorUI*>(this->getParent());
        if (!ui) ui = s_orbEditorUI;
        if (isOrbBar(objects)) {
            log::info("antika: orbs tab bar id='{}' tabIndex={} items={} orbAdded={} ui={}", getID(), m_tabIndex, objects ? objects->count() : -1, m_fields->m_orbAdded, (void*)ui);
        }
        if (ui && !m_fields->m_orbAdded && isOrbBar(objects)) {
            if (auto up = ui->getCreateBtn(kOrbUpCarrier, 4)) {
                objects->addObject(up);
                log::info("antika: added Antika Orb (1340) to orbs tab");
            }
            if (auto back = ui->getCreateBtn(kOrbBackCarrier, 4)) {
                objects->addObject(back);
                log::info("antika: added Antika Return Orb (1343) to orbs tab");
            }
            m_fields->m_orbAdded = true;
        }
        EditButtonBar::loadFromItems(objects, rows, columns, keepPage);
    }

    static bool isVanillaOrbId(int id) {
        switch (id) {
            case 36: case 84: case 141: case 1022: case 1330: case 1333:
            case 1594: case 1704: case 1886: case 1887: case 1888:
            case 3004: case 3027:
                return true;
            default:
                return false;
        }
    }

    bool isOrbBar(cocos2d::CCArray* objects) {
        if (m_tabIndex == 10) return true;
        if (!objects) return false;
        for (int i = 0; i < objects->count(); ++i) {
            auto item = typeinfo_cast<CreateMenuItem*>(objects->objectAtIndex(i));
            if (item && isVanillaOrbId(item->m_objectID)) return true;
        }
        return false;
    }
};

// give a freshly placed Antika orb a real hitbox box, so even though the
// object is invisible it can be selected and moved in the editor
class $modify(AntikaEditorButtons, EditorUI) {
    GameObject* createObject(int objectID, cocos2d::CCPoint position) {
        auto obj = EditorUI::createObject(objectID, position);
        if (obj && (obj->m_objectID == kOrbUpCarrier || obj->m_objectID == kOrbBackCarrier)) {
            obj->m_objectRect = cocos2d::CCRectMake(-kOrbSize / 2, -kOrbSize / 2, kOrbSize, kOrbSize);
        }
        return obj;
    }
};

// name the orbs in the editor's info bar, so the entry in the Orbs tab reads
// as an Antika orb rather than as the vanilla object behind it
class $modify(AntikaEditorInfo, EditorUI) {
    void updateObjectInfoLabel() {
        EditorUI::updateObjectInfoLabel();
        if (!m_objectInfoLabel || !m_selectedObject) return;
        if (m_selectedObject->m_objectID == kOrbUpCarrier) {
            m_objectInfoLabel->setString("Antika Orb");
        } else if (m_selectedObject->m_objectID == kOrbBackCarrier) {
            m_objectInfoLabel->setString("Antika Return Orb");
        }
    }
};

} // namespace

class $modify(AntikaMoonLevelHook, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        // the previous level's nodes died with it, so just forget them
        s_moon = MoonState{};
        s_clock = 0.f;
        s_lastUp = -100.f;
        s_lastDown = -100.f;

        startMoon(this);
        return true;
    }
};

class $modify(AntikaMoonRespawnHook, PlayerObject) {
    void resetObject() {
        PlayerObject::resetObject();
        // respawning means the trip to the moon is over
        s_moon.hasBack = false;
    }
};