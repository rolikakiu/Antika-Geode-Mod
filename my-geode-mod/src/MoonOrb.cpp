#include <Geode/Geode.hpp>
#include <Geode/modify/EventLinkTrigger.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/RingObject.hpp>
#include <cocos2d.h>
#include <cmath>
#include <vector>

using namespace geode::prelude;

/* The Antika orb throws the player up to the moon, a fixed distance above
   wherever the orb was touched, and the return orb drops them back down to
   that same spot. GD has no space objects, so both orbs are vanilla objects
   the mod restyles: the "Ring" object (and the teleport portal built on it)
   is the orb going up, the "Event Link" object is the orb coming back.

   Nothing here loads or uploads an image: every bit of art is a CCDrawNode
   child, which is how GD's own objects draw their extra sprites, so it shows
   up the same way in the editor as it does in game. */

namespace {

constexpr float kMoonHeight = 2000.f;
constexpr float kTouchRadius = 46.f;
constexpr float kOrbSize = 30.f;
constexpr float kOrbCooldown = 0.5f;
constexpr float kFloorHalfWidth = 2600.f;
constexpr float kCeilingHeight = 1400.f;
constexpr float kUnderFloor = 700.f;

/* ---------------- drawn art ---------------- */

cocos2d::ccColor4F shade(int r, int g, int b, float a) {
    return cocos2d::ccColor4F{ r / 255.f, g / 255.f, b / 255.f, a };
}

// the orb sits on top of whatever the vanilla object draws
void dressOrb(cocos2d::CCNode* object, int r, int g, int b) {
    auto art = cocos2d::CCDrawNode::create();
    art->setID("rolikakiu.multimode-orb"_spr);

    art->drawCircle(cocos2d::CCPoint(0, 0), 18.f, shade(r, g, b, 0.20f), 0.f, shade(0, 0, 0, 0.f), 32);
    art->drawCircle(cocos2d::CCPoint(0, 0), 14.f, shade(r, g, b, 0.95f), 1.5f, shade(255, 255, 255, 0.55f), 32);
    art->drawCircle(cocos2d::CCPoint(0, 0), 9.f, shade(255, 255, 255, 0.22f), 0.f, shade(0, 0, 0, 0.f), 32);
    art->drawDot(cocos2d::CCPoint(-4.f, 4.f), 4.5f, shade(255, 255, 255, 0.85f));

    object->addChild(art, 10);
}

// the moon, its craters, the stars and the ground the player runs on
void dressMoon(cocos2d::CCNode* node, cocos2d::CCPoint surface) {
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
