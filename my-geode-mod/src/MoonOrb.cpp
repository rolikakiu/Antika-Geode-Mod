#include <Geode/Geode.hpp>
#include <Geode/modify/EventLinkTrigger.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/RingObject.hpp>
#include <cocos2d.h>
#include <cmath>
#include <cstdlib>
#include <vector>

using namespace geode::prelude;

/* The Antika orb throws the player up to the moon, a fixed distance above
   wherever the orb was touched, and the return orb drops them back down to
   that same spot. GD has no space objects, so both orbs are vanilla objects
   the mod restyles: the "Ring" object (and the teleport portal built on it)
   is the orb going up, the "Event Link" object is the orb coming back.

   Neither of those classes declares its own update, so the orbs are checked
   from the player side instead: every level gets one node that ticks the orb
   touches and holds anyone who sinks into the moon on its surface. */

namespace {

constexpr float kMoonHeight = 2000.f;
constexpr float kTouchRadius = 46.f;
constexpr float kOrbSize = 30.f;
constexpr float kOrbCooldown = 0.5f;
constexpr float kFloorHalfWidth = 2600.f;
constexpr float kCeilingHeight = 1400.f;
constexpr float kUnderFloor = 700.f;

/* ---------------- drawn sprites ---------------- */
// the mod ships no images for the moon, every texture is filled in by hand
struct Pixels {
    int size;
    unsigned char* data;

    explicit Pixels(int s) : size(s) {
        data = static_cast<unsigned char*>(std::malloc(static_cast<size_t>(s) * s * 4));
        for (int i = 0; i < s * s * 4; ++i) data[i] = 0;
    }

    void blend(int x, int y, int r, int g, int b, float a) {
        if (a <= 0.f || x < 0 || y < 0 || x >= size || y >= size) return;
        if (a > 1.f) a = 1.f;

        auto i = (y * size + x) * 4;
        float dstA = data[i + 3] / 255.f;
        float outA = a + dstA * (1.f - a);
        if (outA <= 0.f) return;

        float keep = dstA * (1.f - a);
        data[i] = static_cast<unsigned char>((r * a + data[i] * keep) / outA);
        data[i + 1] = static_cast<unsigned char>((g * a + data[i + 1] * keep) / outA);
        data[i + 2] = static_cast<unsigned char>((b * a + data[i + 2] * keep) / outA);
        data[i + 3] = static_cast<unsigned char>(outA * 255.f + 0.5f);
    }

    // the texture keeps the pixels, so the buffer is never freed by hand
    cocos2d::CCTexture2D* texture() const {
        auto tex = new cocos2d::CCTexture2D();
        tex->initWithData(
            data, cocos2d::kCCTexture2DPixelFormat_RGBA8888, size, size,
            cocos2d::CCSizeMake(size, size)
        );
        return tex;
    }
};

float smoothstep(float edge0, float edge1, float x) {
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    return t * t * (3.f - 2.f * t);
}

void disc(Pixels& p, float cx, float cy, float radius, int r, int g, int b, float a, float soft = 1.5f) {
    for (int y = 0; y < p.size; ++y) {
        for (int x = 0; x < p.size; ++x) {
            float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
            float cover = 1.f - smoothstep(radius - soft, radius, d);
            if (cover > 0.f) p.blend(x, y, r, g, b, a * cover);
        }
    }
}

cocos2d::CCTexture2D* drawOrb(int r, int g, int b) {
    Pixels p(64);
    float c = 32.f;
    disc(p, c, c, 31.f, r, g, b, 0.26f, 5.f);
    disc(p, c, c, 22.f, 255, 255, 255, 0.30f);
    disc(p, c, c, 20.f, r, g, b, 0.95f);
    disc(p, c, c, 13.f, 255, 255, 255, 0.28f);
    disc(p, c - 4.f, c + 4.f, 7.f, 255, 255, 255, 0.85f);
    return p.texture();
}

cocos2d::CCTexture2D* drawMoon() {
    Pixels p(192);
    float c = 96.f;
    disc(p, c, c, 86.f, 216, 216, 224, 1.f, 2.f);

    struct Crater { float x, y, r; };
    static const Crater craters[] = {
        { -24.f, 20.f, 16.f }, { 20.f, -14.f, 11.f }, { 36.f, 28.f, 8.f },
        { -40.f, -28.f, 10.f }, { 4.f, 44.f, 7.f }, { -8.f, -44.f, 13.f },
    };
    for (auto& crater : craters) {
        disc(p, c + crater.x, c + crater.y, crater.r + 1.5f, 242, 242, 248, 0.35f);
        disc(p, c + crater.x, c + crater.y, crater.r, 178, 178, 190, 0.85f);
    }

    // a cheap stand-in for shading on the far side of the moon
    disc(p, c + 34.f, c - 30.f, 74.f, 118, 122, 150, 0.16f, 6.f);
    return p.texture();
}

cocos2d::CCTexture2D* drawStar() {
    Pixels p(32);
    disc(p, 16.f, 16.f, 15.f, 255, 255, 255, 0.28f, 4.f);
    disc(p, 16.f, 16.f, 4.5f, 255, 255, 255, 1.f, 1.2f);
    return p.texture();
}

cocos2d::CCTexture2D* drawGround() {
    Pixels p(64);
    disc(p, 32.f, 32.f, 30.f, 198, 198, 208, 1.f, 2.f);
    disc(p, 23.f, 40.f, 12.f, 176, 176, 190, 0.55f);
    disc(p, 45.f, 36.f, 8.f, 176, 176, 190, 0.45f);
    return p.texture();
}

cocos2d::CCTexture2D* orbTexture() { static auto* tex = drawOrb(178, 120, 255); return tex; }
cocos2d::CCTexture2D* backOrbTexture() { static auto* tex = drawOrb(96, 216, 255); return tex; }
cocos2d::CCTexture2D* moonTexture() { static auto* tex = drawMoon(); return tex; }
cocos2d::CCTexture2D* starTexture() { static auto* tex = drawStar(); return tex; }
cocos2d::CCTexture2D* groundTexture() { static auto* tex = drawGround(); return tex; }

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

/* ---------------- the moon itself ---------------- */

void buildMoon(cocos2d::CCPoint surface) {
    auto node = s_moon.node;
    if (!node) return;

    node->removeAllChildren();
    s_moon.surface = surface;

    if (auto tex = moonTexture()) {
        auto moon = cocos2d::CCSprite::createWithTexture(tex);
        moon->setPosition(cocos2d::CCPoint(surface.x, surface.y + 560.f));
        moon->setScale(1.4f);
        node->addChild(moon, 0);
    }

    if (auto tex = starTexture()) {
        unsigned int seed = 0x5eed1234u;
        auto rand01 = [&seed]() {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float>(seed >> 8) / static_cast<float>(1 << 24);
        };
        for (int i = 0; i < 28; ++i) {
            auto star = cocos2d::CCSprite::createWithTexture(tex);
            float angle = rand01() * 6.2831853f;
            float dist = 320.f + rand01() * 1500.f;
            star->setPosition(cocos2d::CCPoint(
                surface.x + std::cos(angle) * dist,
                surface.y + 140.f + (rand01() - 0.35f) * 1600.f
            ));
            star->setScale(0.6f + rand01() * 1.5f);
            star->setOpacity(static_cast<GLubyte>(110.f + rand01() * 120.f));
            node->addChild(star, 0);
        }
    }

    if (auto tex = groundTexture()) {
        for (int i = -14; i <= 14; ++i) {
            auto bump = cocos2d::CCSprite::createWithTexture(tex);
            bump->setPosition(cocos2d::CCPoint(surface.x + i * 190.f, surface.y + 6.f));
            bump->setScaleX(3.2f);
            bump->setScaleY(1.1f);
            bump->setOpacity(static_cast<GLubyte>(205));
            node->addChild(bump, 0);
        }
    }

    s_moon.built = true;
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
        if (auto tex = orbTexture()) orb->setTexture(tex);
        orb->m_objectRect = cocos2d::CCRectMake(-kOrbSize / 2, -kOrbSize / 2, kOrbSize, kOrbSize);
        registerOrb(orb, false);
        return orb;
    }
};

// the vanilla "Event Link" object, now the orb that drops the player back down
class $modify(AntikaBackOrb, EventLinkTrigger) {
    bool init() {
        if (!EventLinkTrigger::init()) return false;
        if (auto tex = backOrbTexture()) this->setTexture(tex);
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
