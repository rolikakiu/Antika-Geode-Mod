#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/EventLinkTrigger.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/RingObject.hpp>
#include <cocos2d.h>
#include <cmath>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

/* The Antika orb throws the player up to the moon, a fixed distance above
   wherever the orb was touched, and the return orb drops them back down to
   that same spot. GD has no space objects, so both orbs are vanilla objects
   the mod restyles: the "Ring" object (and the teleport portal built on it)
   is the orb going up, the "Event Link" object is the orb coming back. The
   editor's object list, its previews and its name label are all pointed at
   the orbs too, so the entry in the Orbs tab shows a real orb. */

namespace {

constexpr float kMoonHeight = 2000.f;
constexpr float kTouchRadius = 46.f;
constexpr float kOrbSize = 30.f;
constexpr float kOrbCooldown = 0.5f;
constexpr float kFloorHalfWidth = 2600.f;
constexpr float kCeilingHeight = 1400.f;
constexpr float kUnderFloor = 700.f;

/* ---------------- orb art ---------------- */

cocos2d::ccColor4F shade(int r, int g, int b, float a) {
    return cocos2d::ccColor4F{ r / 255.f, g / 255.f, b / 255.f, a };
}

// A real orb sprite out of the game, tinted. GD has no orb of our own colour,
// so a vanilla one is borrowed and recoloured; the frame is only used once the
// game has confirmed it actually has it, and the drawn rim below covers the
// orb even in the unlikely case nothing is found.
cocos2d::CCSprite* orbSprite() {
    static const char* frames[] = {
        "orbs_001.png", "orbs_002.png", "orbs_003.png", "orbs_004.png",
        "orbs_005.png", "orbs_006.png", "orbs_007.png", "orbs_008.png",
        "orbs_009.png", "orbs_010.png", "orbs_011.png", "orbs_012.png",
        "orbs_013.png", "orbs_014.png", "orbs_015.png", "orbs_016.png",
        "orbs_017.png", "orbs_018.png", "orbs_019.png", "orbs_020.png",
        "orb_001.png", "orb_002.png", "orb_003.png", "orb_004.png",
        "yellow_orb.png", "jumpRing_001.png", "ring_001.png", "ring_002.png",
        "coin_001.png", "ball_001.png", "star_001.png", "moon_001.png",
        "GJ_stars_001.png", "GJ_particle_001.png", "GJ_coin_001.png",
    };
    static const char* found = nullptr;
    if (found) return cocos2d::CCSprite::createWithSpriteFrameName(found);

    static bool tried = false;
    if (tried) return nullptr;
    tried = true;

    for (auto frame : frames) {
        if (!cocos2d::CCSprite::createWithSpriteFrameName(frame)) continue;
        found = frame;
        return cocos2d::CCSprite::createWithSpriteFrameName(frame);
    }
    return nullptr;
}

// the orb sits on top of whatever the vanilla object draws
void dressOrb(cocos2d::CCNode* object, int r, int g, int b) {
    if (auto sprite = orbSprite()) {
        auto size = sprite->getContentSize();
        auto longest = std::max(size.width, size.height);
        if (longest > 1.f) sprite->setScale(kOrbSize * 0.95f / longest);
        sprite->setColor(cocos2d::ccColor3B{ (GLubyte)r, (GLubyte)g, (GLubyte)b });
        sprite->setID("rolikakiu.multimode-orb-sprite"_spr);
        object->addChild(sprite, 9);
    }

    auto art = cocos2d::CCDrawNode::create();
    art->setID("rolikakiu.multimode-orb"_spr);
    art->drawCircle(cocos2d::CCPoint(0, 0), 18.f, shade(r, g, b, 0.18f), 0.f, shade(0, 0, 0, 0.f), 32);
    art->drawCircle(cocos2d::CCPoint(0, 0), 15.f, shade(r, g, b, 0.12f), 2.f, shade(255, 255, 255, 0.45f), 32);
    art->drawDot(cocos2d::CCPoint(-4.5f, 4.5f), 4.f, shade(255, 255, 255, 0.7f));
    object->addChild(art, 10);
}

// the moon, its craters, the stars and the ground the player runs on
void dressMoon(cocos2d::CCNode* node, cocos2d::CCPoint surface) {
    // the ground is a plain coloured band, so there is always something solid
    // looking to stand on even if the drawn art below goes missing
    auto band = cocos2d::CCLayerColor::create(cocos2d::ccc4(150, 150, 160, 255), kFloorHalfWidth * 2.f, 200.f);
    band->setPosition(cocos2d::CCPoint(surface.x, surface.y - 100.f));
    band->setID("rolikakiu.multimode-moon-ground"_spr);
    node->addChild(band, 0);

    auto art = cocos2d::CCDrawNode::create();
    art->setID("rolikakiu.multimode-moon-art"_spr);

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

/* ---------------- remembering which objects became orbs ---------------- */

std::unordered_set<int> s_upOrbIds;
std::unordered_set<int> s_backOrbIds;
bool s_loadedOrbIds = false;

void loadRememberedOrbIds() {
    if (s_loadedOrbIds) return;
    s_loadedOrbIds = true;
    auto mod = Mod::get();
    if (!mod) return;
    if (auto id = mod->getSavedValue<int>("antika-orb-up-id", -1); id > 0) s_upOrbIds.insert(id);
    if (auto id = mod->getSavedValue<int>("antika-orb-back-id", -1); id > 0) s_backOrbIds.insert(id);
}

void rememberOrbId(int id, bool back) {
    if (id <= 0) return;
    auto mod = Mod::get();
    if (back) {
        if (!s_backOrbIds.insert(id).second) return;
        if (mod && mod->getSavedValue<int>("antika-orb-back-id", -1) != id) mod->setSavedValue("antika-orb-back-id", id);
    } else {
        if (!s_upOrbIds.insert(id).second) return;
        if (mod && mod->getSavedValue<int>("antika-orb-up-id", -1) != id) mod->setSavedValue("antika-orb-up-id", id);
    }
}

bool isUpOrb(int id) {
    loadRememberedOrbIds();
    return s_upOrbIds.count(id) > 0;
}

bool isBackOrb(int id) {
    loadRememberedOrbIds();
    return s_backOrbIds.count(id) > 0;
}

/* ---------------- moon state ---------------- */

struct AntikaOrb {
    cocos2d::CCNode* node = nullptr;
    bool back = false;
};

struct MoonState {
    cocos2d::CCPoint surface = cocos2d::CCPoint(0, 0);
    cocos2d::CCPoint backPos = cocos2d::CCPoint(0, 0);
    bool hasBack = false;
    bool built = false;
    cocos2d::CCNode* node = nullptr;
    cocos2d::CCNode* night = nullptr;
};

MoonState s_moon;
std::vector<AntikaOrb> s_orbs;
float s_clock = 0.f;
float s_lastUp = -100.f;
float s_lastDown = -100.f;

void registerOrb(cocos2d::CCNode* node, bool back) {
    for (auto& orb : s_orbs) {
        if (orb.node != node) continue;
        orb.back = back;
        return;
    }
    s_orbs.push_back({ node, back });
}

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
void moonTick(float) {
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
    if (s_orbs.empty()) return;

    eachPlayer([](PlayerObject* player) {
        if (player->m_isDead) return;

        for (auto& orb : s_orbs) {
            if (!orb.node) continue;
            auto pos = orb.node->getPosition();
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
        moonTick(dt);
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
    node->setID("rolikakiu.multimode-moon"_spr);
    layer->addChild(node, -500);
    s_moon.node = node;

    auto win = cocos2d::CCDirector::get()->getWinSize();
    auto night = cocos2d::CCLayerColor::create(cocos2d::ccc4(12, 14, 34, 120), win.width, win.height);
    night->setZOrder(-900);
    night->setVisible(false);
    layer->addChild(night, -900);
    s_moon.night = night;
}

} // namespace

// the vanilla "Ring" object (and the teleport portal built on top of it),
// now the orb that throws the player up to the moon
class $modify(AntikaMoonOrb, RingObject) {
    static RingObject* create(char const* frame) {
        auto orb = RingObject::create(frame);
        if (!orb) return nullptr;
        dressOrb(orb, 178, 120, 255);
        orb->m_objectRect = cocos2d::CCRectMake(-kOrbSize / 2, -kOrbSize / 2, kOrbSize, kOrbSize);
        rememberOrbId(orb->m_objectID, false);
        registerOrb(orb, false);
        return orb;
    }
};

// the vanilla "Event Link" object, now the orb that drops the player back down
class $modify(AntikaBackOrb, EventLinkTrigger) {
    bool init() {
        if (!EventLinkTrigger::init()) return false;
        dressOrb(this, 96, 216, 255);
        m_objectRect = cocos2d::CCRectMake(-kOrbSize / 2, -kOrbSize / 2, kOrbSize, kOrbSize);
        rememberOrbId(this->m_objectID, true);
        registerOrb(this, true);
        return true;
    }
};

class $modify(AntikaMoonLevelHook, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        // the previous level's nodes died with it, so just forget them
        s_moon = MoonState{};
        s_orbs.clear();
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

// name the orbs in the editor's info bar, so the entry in the Orbs tab reads
// as an Antika orb rather than as the vanilla object behind it
class $modify(AntikaEditorInfo, EditorUI) {
    void updateObjectInfoLabel() {
        EditorUI::updateObjectInfoLabel();
        if (!m_objectInfoLabel || !m_selectedObject) return;
        if (isUpOrb(m_selectedObject->m_objectID)) {
            m_objectInfoLabel->setString("Antika Orb");
        } else if (isBackOrb(m_selectedObject->m_objectID)) {
            m_objectInfoLabel->setString("Antika Return Orb");
        }
    }
};
