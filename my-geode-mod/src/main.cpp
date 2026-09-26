#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <cocos2d.h>
#include <set>
#include <cmath>

using namespace geode::prelude;

constexpr int kEndLevelID = 149188646;

/* ---------------- Negative hitboxes / negative mode ---------------- */
static bool neghitOn() { return Mod::get()->getSettingValue<bool>("neghit"); }
static bool negativeOn() { return Mod::get()->getSettingValue<bool>("negative"); }
static bool noclipOn() { return Mod::get()->getSettingValue<bool>("noclip"); }
static bool forceIceOn() { return Mod::get()->getSettingValue<bool>("force-ice"); }
static bool forcePlatformerOn() { return Mod::get()->getSettingValue<bool>("force-platformer"); }
static bool forceClassicOn() { return Mod::get()->getSettingValue<bool>("force-classic"); }
static bool allModesPlatformerOn() { return Mod::get()->getSettingValue<bool>("all-modes-platformer"); }
static bool make3dOn() { return Mod::get()->getSettingValue<bool>("make-3d"); }
static bool noCameraMoveOn() { return Mod::get()->getSettingValue<bool>("no-camera-move"); }
static bool forceGamemodeOn() { return Mod::get()->getSettingValue<bool>("force-gamemode"); }

enum class ForcedMode { Cube, Ship, Ball, UFO, Wave, Robot, Spider, Swing };

static ForcedMode modeFromName(std::string const& s) {
    if (s == "Ship") return ForcedMode::Ship;
    if (s == "Ball") return ForcedMode::Ball;
    if (s == "UFO") return ForcedMode::UFO;
    if (s == "Wave") return ForcedMode::Wave;
    if (s == "Robot") return ForcedMode::Robot;
    if (s == "Spider") return ForcedMode::Spider;
    if (s == "Swing") return ForcedMode::Swing;
    return ForcedMode::Cube;
}

static ForcedMode playerMode(PlayerObject* player) {
    if (player->m_isDart) return ForcedMode::Wave;
    if (player->m_isSwing) return ForcedMode::Swing;
    if (player->m_isRobot) return ForcedMode::Robot;
    if (player->m_isSpider) return ForcedMode::Spider;
    if (player->m_isBird) return ForcedMode::UFO;
    if (player->m_isShip) return ForcedMode::Ship;
    if (player->m_isBall) return ForcedMode::Ball;
    return ForcedMode::Cube;
}

static void enterMode(PlayerObject* player, ForcedMode mode) {
    switch (mode) {
        case ForcedMode::Ship: player->toggleFlyMode(true, true); break;
        case ForcedMode::Ball: player->toggleRollMode(true, true); break;
        case ForcedMode::UFO: player->toggleBirdMode(true, true); break;
        case ForcedMode::Wave: player->toggleDartMode(true, true); break;
        case ForcedMode::Robot: player->toggleRobotMode(true, true); break;
        case ForcedMode::Spider: player->toggleSpiderMode(true, true); break;
        case ForcedMode::Swing: player->toggleSwingMode(true, true); break;
        default: break;
    }
}

static void leaveMode(PlayerObject* player, ForcedMode mode) {
    switch (mode) {
        case ForcedMode::Ship: player->toggleFlyMode(false, true); break;
        case ForcedMode::Ball: player->toggleRollMode(false, true); break;
        case ForcedMode::UFO: player->toggleBirdMode(false, true); break;
        case ForcedMode::Wave: player->toggleDartMode(false, true); break;
        case ForcedMode::Robot: player->toggleRobotMode(false, true); break;
        case ForcedMode::Spider: player->toggleSpiderMode(false, true); break;
        case ForcedMode::Swing: player->toggleSwingMode(false, true); break;
        default: break;
    }
}

// the mode the level wants this player to be in, if any toggle is forcing one
static bool wantedForcedMode(PlayerObject* player, ForcedMode& out) {
    if (!player) return false;
    if (player->m_isPlatformer) {
        if (!allModesPlatformerOn()) return false;
        out = modeFromName(Mod::get()->getSettingValue<std::string>("platformer-mode"));
        return true;
    }
    if (!forceGamemodeOn()) return false;
    out = modeFromName(Mod::get()->getSettingValue<std::string>("gamemode"));
    return true;
}

// re-applied every frame so level portals can't take the mode away again
static void applyForcedMode(PlayerObject* player) {
    if (!player || player->m_editorEnabled) return;

    ForcedMode want;
    if (!wantedForcedMode(player, want)) return;

    auto current = playerMode(player);
    if (current == want) return;

    if (want == ForcedMode::Cube) leaveMode(player, current);
    else enterMode(player, want);
}

static void applyForceGameMode(PlayLayer* layer) {
    if (forceClassicOn()) {
        if (!layer->m_isPlatformer) return;
        layer->m_isPlatformer = false;
        if (layer->m_levelSettings) layer->m_levelSettings->m_platformerMode = false;
        if (layer->m_player1) layer->m_player1->togglePlatformerMode(false);
        if (layer->m_player2) layer->m_player2->togglePlatformerMode(false);
        if (layer->m_uiLayer) layer->m_uiLayer->togglePlatformerMode(false);
        if (layer->m_groundLayer) layer->m_groundLayer->setVisible(false);
        return;
    }

    if (!forcePlatformerOn() || layer->m_isPlatformer) return;
    layer->m_isPlatformer = true;
    if (layer->m_levelSettings) layer->m_levelSettings->m_platformerMode = true;
    if (layer->m_player1) layer->m_player1->togglePlatformerMode(true);
    if (layer->m_player2) layer->m_player2->togglePlatformerMode(true);
    if (layer->m_uiLayer) layer->m_uiLayer->togglePlatformerMode(true);
    if (layer->m_groundLayer) layer->m_groundLayer->setVisible(true);
}

class $modify(AntikaPlayerObject, PlayerObject) {
    bool collidedWithObject(float dt, GameObject* object, cocos2d::CCRect rect, bool skipCheck) {
        if (noclipOn()) return false;
        return PlayerObject::collidedWithObject(dt, object, rect, skipCheck);
    }

    bool collidedWithObjectInternal(float dt, GameObject* object, cocos2d::CCRect rect, bool skipCheck) {
        if (noclipOn()) return false;
        return PlayerObject::collidedWithObjectInternal(dt, object, rect, skipCheck);
    }

    void update(float dt) {
        PlayerObject::update(dt);
        applyForcedMode(this);
    }

    void resetObject() {
        PlayerObject::resetObject();
        applyForcedMode(this);
    }
};

class $modify(AntikaGameObject, GameObject) {
    void activateObject() override {
        GameObject::activateObject();

        if (m_editorEnabled) return;

        if (forceIceOn() && (m_objectType == GameObjectType::Solid || m_objectType == GameObjectType::Slope)) {
            m_isIceBlock = true;
        }

        if (!neghitOn()) return;

        bool isPositive = m_scaleX > 0 && m_scaleY > 0;
        if (isPositive && m_objectType != GameObjectType::Decoration) {
            m_scaleX = -m_scaleX;
            m_scaleY = -m_scaleY;
        }
    }
};

/* ---------------- Accurate / TRUE hitboxes ---------------- */
static bool accurateOn() { return Mod::get()->getSettingValue<bool>("accurate"); }
static bool trueHitOn() { return Mod::get()->getSettingValue<bool>("truehit"); }

inline const std::set<int> sSpikeIDs = {
    8, 39, 103, 144, 145, 177, 178, 179, 205, 216, 217, 218, 392, 458, 459
};
inline const std::set<int> sSawIDs = {
    88, 89, 98, 186, 187, 188, 678, 679, 680, 740, 741, 742,
    1619, 1620, 1701, 1702, 1703, 1705, 1706, 1707, 1708, 1709, 1710, 1734, 1735, 1736
};

static float hitDeg2rad(float degrees) {
    return degrees * (3.14159265359f / 180.0f);
}

static float hitSignPt(cocos2d::CCPoint p1, cocos2d::CCPoint p2, cocos2d::CCPoint p3) {
    return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
}

static bool hitPointInTriangle(cocos2d::CCPoint pt, cocos2d::CCPoint v1, cocos2d::CCPoint v2, cocos2d::CCPoint v3) {
    float d1 = hitSignPt(pt, v1, v2);
    float d2 = hitSignPt(pt, v2, v3);
    float d3 = hitSignPt(pt, v3, v1);
    bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(hasNeg && hasPos);
}

static float hitDist(cocos2d::CCPoint a, cocos2d::CCPoint b) {
    return sqrtf(powf(b.x - a.x, 2.0f) + powf(b.y - a.y, 2.0f));
}

static float hitPlayerSize(PlayerObject* player) {
    float base = 32.0f;
    if (player->m_isDart) base *= 0.28f;
    base *= player->m_vehicleSize;
    return base / 2.208f;
}

static bool hitTriangle(PlayerObject* player, GameObject* gObj) {
    cocos2d::CCPoint playerPos = player->getPosition();
    cocos2d::CCPoint gPos = gObj->getUnmodifiedPosition();
    cocos2d::CCSize cs = gObj->getContentSize();
    cs.width *= gObj->getScaleX();
    cs.height *= gObj->getScaleY();
    float rot = hitDeg2rad(gObj->getRotation());

    float playerSize = hitPlayerSize(player);
    cocos2d::CCRect pr(playerPos.x - playerSize, playerPos.y - playerSize, playerSize * 2, playerSize * 2);

    auto rotPoint = [&](float x, float y) -> cocos2d::CCPoint {
        return {
            gPos.x + (x * cosf(rot) + y * sinf(rot)),
            gPos.y + (-x * sinf(rot) + y * cosf(rot))
        };
    };
    cocos2d::CCPoint top = rotPoint(0.0f, cs.height / 2);
    cocos2d::CCPoint bl = rotPoint(cs.width / -2.0f, cs.height / -2.0f);
    cocos2d::CCPoint br = rotPoint(cs.width / 2.0f, cs.height / -2.0f);

    auto inside = [&](cocos2d::CCPoint p) {
        return p.x >= pr.origin.x && p.x <= pr.getMaxX() && p.y >= pr.origin.y && p.y <= pr.getMaxY();
    };
    if (inside(top) || inside(bl) || inside(br)) return true;

    cocos2d::CCPoint corners[4] = {
        { pr.origin.x, pr.origin.y },
        { pr.getMaxX(), pr.origin.y },
        { pr.origin.x, pr.getMaxY() },
        { pr.getMaxX(), pr.getMaxY() }
    };
    for (auto& c : corners) {
        if (hitPointInTriangle(c, top, bl, br)) return true;
    }
    return false;
}

static bool hitCircle(PlayerObject* player, GameObject* gObj) {
    cocos2d::CCPoint gPos = gObj->getPosition();
    cocos2d::CCSize cs = gObj->getContentSize();
    float radius = (cs.width + cs.height) / 2.0f * gObj->getScale() / 2.08f;

    float ps = hitPlayerSize(player);
    cocos2d::CCPoint corners[4] = {
        { player->getPositionX() - ps, player->getPositionY() - ps },
        { player->getPositionX() + ps, player->getPositionY() - ps },
        { player->getPositionX() - ps, player->getPositionY() + ps },
        { player->getPositionX() + ps, player->getPositionY() + ps }
    };
    for (auto& c : corners) {
        if (hitDist(c, gPos) <= radius) return true;
    }
    return false;
}

static bool accurateTarget(GameObject* g) {
    if (sSawIDs.contains(g->m_objectID)) return true;
    if (trueHitOn()) return g->m_objectType == GameObjectType::Hazard;
    return sSpikeIDs.contains(g->m_objectID);
}

static bool accurateIsSaw(GameObject* g) {
    return sSawIDs.contains(g->m_objectID);
}

static void hitDrawSpike(cocos2d::CCDrawNode* node, GameObject* gObj) {
    cocos2d::CCPoint gPos = gObj->getUnmodifiedPosition();
    cocos2d::CCSize cs = gObj->getContentSize();
    cs.width *= gObj->getScaleX();
    cs.height *= gObj->getScaleY();
    float rot = hitDeg2rad(gObj->getRotation());

    auto rotPoint = [&](float x, float y) -> cocos2d::CCPoint {
        return {
            gPos.x + (x * cosf(rot) + y * sinf(rot)),
            gPos.y + (-x * sinf(rot) + y * cosf(rot))
        };
    };
    cocos2d::CCPoint verts[3] = {
        rotPoint(0.0f, cs.height / 2.0f),
        rotPoint(cs.width / -2.0f, cs.height / -2.0f),
        rotPoint(cs.width / 2.0f, cs.height / -2.0f)
    };
    node->drawPolygon(verts, 3, { 0.f, 0.f, 0.f, 0.f }, 0.5f, { 1.f, 0.f, 0.f, 1.f });
}

static void hitDrawSaw(cocos2d::CCDrawNode* node, GameObject* gObj) {
    cocos2d::CCPoint gPos = gObj->getPosition();
    cocos2d::CCSize cs = gObj->getContentSize();
    float radius = (cs.width + cs.height) / 2.0f * gObj->getScale() / 1.04f / 2.0f;
    node->drawCircle(gPos, radius, { 1.f, 0.f, 0.f, 0.2f }, 0.5f, { 1.f, 0.f, 0.f, 1.f }, 16);
}

class $modify(AntikaCollisionLayer, GJBaseGameLayer) {
    void collisionCheckObjects(PlayerObject* object, gd::vector<GameObject*>* objects, int objectCount, float dt) {
        GJBaseGameLayer::collisionCheckObjects(object, objects, objectCount, dt);

        if (!accurateOn() && !trueHitOn()) return;
        if (!object) return;

        float px = object->getPositionX();
        for (int i = 0; i < objectCount; i++) {
            GameObject* g = objects->at(i);
            if (!g || !g->m_isActivated) continue;
            float ox = g->getPositionX();
            if (ox < px - 600 || ox > px + 600) continue;
            if (!accurateTarget(g)) continue;

            bool hit = accurateIsSaw(g) ? hitCircle(object, g) : hitTriangle(object, g);
            if (hit) {
                this->destroyPlayer(object, g);
                break;
            }
        }
    }

    void updateDebugDraw() {
        GJBaseGameLayer::updateDebugDraw();

        if ((!accurateOn() && !trueHitOn()) || !m_debugDrawNode) return;
        for (int i = 0; i < m_activeObjectsCount; i++) {
            GameObject* g = m_activeObjects.at(i);
            if (!g || !accurateTarget(g)) continue;
            if (accurateIsSaw(g)) hitDrawSaw(m_debugDrawNode, g);
            else hitDrawSpike(m_debugDrawNode, g);
        }
    }
};

/* ---------------- Don't move camera ---------------- */
static bool s_cameraSettled = false;

class $modify(AntikaCameraLayer, GJBaseGameLayer) {
    void updateCamera(float dt) {
        if (!noCameraMoveOn()) {
            s_cameraSettled = false;
            GJBaseGameLayer::updateCamera(dt);
            return;
        }

        // let the very first update settle the camera on the level's start
        // position/zoom, then never touch it again
        if (s_cameraSettled) return;
        GJBaseGameLayer::updateCamera(dt);
        s_cameraSettled = true;
    }
};

/* ---------------- Make Everything 3D ---------------- */
static void applyMakeEverything3D(PlayLayer* layer) {
    auto shaderLayer = layer->m_shaderLayer;
    if (!shaderLayer) return;

    shaderLayer->updateZLayer(2, 14, false);

    shaderLayer->m_state.m_blurRefChannel = 1234;
    shaderLayer->m_state.m_blurRefColor = cocos2d::ccc3(255, 255, 255);

    shaderLayer->triggerRadialBlur(
        0.5f, -0.50f, 500000.f, 1.00f, 1234,
        0.f, 0.f, false, 0, 0, 0.f, true
    );

    shaderLayer->triggerBulge(
        0.5f, 0.25f,
        0.f, 0.f, 0.f,
        0, 0, 0.f, false
    );
}

static bool s_launchedOnce = false;
static bool s_endingPending = false;
static bool s_endingShown = false;
static DialogLayer* s_dialog = nullptr;

DialogLayer* createEndingDialog() {
    auto dialogLines = CCArray::create();

    auto addLine = [&](char const* name, char const* text, int frame) {
        dialogLines->addObject(DialogObject::create(
            name, text, frame, 1.0f, false, ccWHITE
        ));
    };

    int background = 2;

    addLine("Auto", "...", 2);
    addLine("ANTIKA", "Why Say Nothing", 2);
    addLine("RubRub", "I May Be Creator In GD.", 28);
    addLine("Rattledash", "Welcome To GD!", 2);
    addLine("RobTop", "Hi And Welcome", 2);
    addLine("ANTIKA", "ROPTOP...?", 2);
    addLine("RobTop", "yes", 2);
    addLine("Rattledash", "<s260>THIS IS THE END</s>", 2);

    auto dialog = DialogLayer::createWithObjects(dialogLines, background);
    dialog->updateChatPlacement(DialogChatPlacement::Center);
    dialog->animateInRandomSide();
    dialog->setID("rolikakiu.multimode-ending-dialog"_spr);
    return dialog;
}

void launchEndLevel(GJGameLevel* level) {
    auto scene = PlayLayer::scene(level, false, false);
    cocos2d::CCDirector::get()->replaceScene(scene);
}

static bool isOnCurrentScene(cocos2d::CCNode* node) {
    auto running = cocos2d::CCDirector::get()->getRunningScene();
    for (auto p = node; p != nullptr; p = p->getParent()) {
        if (p == running) return true;
    }
    return false;
}

static void reapDialog() {
    if (!s_dialog) return;
    if (s_dialog->getParent() == nullptr || !isOnCurrentScene(s_dialog)) {
        s_dialog->removeFromParent();
        s_dialog = nullptr;
    }
}

static void showEndingDialog() {
    reapDialog();
    if (s_dialog) {
        s_dialog->removeFromParent();
        s_dialog = nullptr;
        return;
    }
    if (!cocos2d::CCDirector::get()->getRunningScene()) return;
    auto dialog = createEndingDialog();
    dialog->addToMainScene();
    s_dialog = dialog;
}

$on_mod(Loaded) {
    KeybindSettingPressedEventV3(Mod::get(), "ending-dialog-key")
        .listen([](Keybind const&, bool down, bool repeat, double) -> bool {
            if (!down || repeat) return false;
            showEndingDialog();
            return true;
        })
        .leak();
}

class EndLevelDownloadDelegate : public LevelDownloadDelegate {
public:
    LevelDownloadDelegate* m_previous = nullptr;

    void levelDownloadFinished(GJGameLevel* level) override {
        auto glm = GameLevelManager::sharedState();
        glm->m_levelDownloadDelegate = m_previous;
        log::info("Ending level 149188646 downloaded, starting...");
        launchEndLevel(level);
    }

    void levelDownloadFailed(int response) override {
        auto glm = GameLevelManager::sharedState();
        glm->m_levelDownloadDelegate = m_previous;
        log::error("Failed to download ending level 149188646: {}", response);
    }
};

class $modify(AntikaMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }

        bool storyOn = Mod::get()->getSettingValue<bool>("disable");
        log::info("antika: MenuLayer::init story={} launchOnce={} endingPending={} endingShown={}", storyOn, s_launchedOnce, s_endingPending, s_endingShown);

        if (storyOn) {
            if (s_endingPending && !s_endingShown) {
                s_endingPending = false;
                s_endingShown = true;
                log::info("antika: showing ending dialog");
                auto dialog = createEndingDialog();
                dialog->addToMainScene();
                s_dialog = dialog;
            }
            else if (!s_launchedOnce) {
                s_launchedOnce = true;

                auto glm = GameLevelManager::sharedState();
                if (auto level = glm->getSavedLevel(kEndLevelID)) {
                    log::info("antika: found saved ending level, launching");
                    launchEndLevel(level);
                }
                else {
                    log::info("antika: ending level not saved, downloading");
                    static EndLevelDownloadDelegate s_delegate;
                    s_delegate.m_previous = glm->m_levelDownloadDelegate;
                    glm->m_levelDownloadDelegate = &s_delegate;
                    glm->downloadLevel(kEndLevelID, false, 0);
                }
            }
        }

        this->scheduleUpdate();

        return true;
    }

    void update(float dt) {
        MenuLayer::update(dt);
        reapDialog();
    }
};

class $modify(AntikaPlayLayer, PlayLayer) {
    struct Fields {
        cocos2d::CCLayerColor* m_invert = nullptr;
    };

    void update(float dt) {
        PlayLayer::update(dt);
        reapDialog();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (noclipOn()) return;
        PlayLayer::destroyPlayer(player, object);
    }

    void setupHasCompleted() {
        if (neghitOn() && m_levelSettings) {
            m_levelSettings->m_fixNegativeScale = false;
        }
        PlayLayer::setupHasCompleted();

        if (make3dOn()) {
            applyMakeEverything3D(this);
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        applyForceGameMode(this);
        if (make3dOn()) {
            applyMakeEverything3D(this);
        }
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        s_cameraSettled = false;
        applyForceGameMode(this);

        if (allModesPlatformerOn() || forceGamemodeOn()) {
            applyForcedMode(m_player1);
            if (m_player2) applyForcedMode(m_player2);
        }

        if (negativeOn()) {
            this->toggleFlipped(true, true);

            auto scene = this->getParent();
            if (scene && !m_fields->m_invert) {
                auto win = cocos2d::CCDirector::get()->getWinSize();
                auto invert = cocos2d::CCLayerColor::create(
                    cocos2d::ccc4(255, 255, 255, 255), win.width, win.height
                );
                invert->setAnchorPoint({ 0, 0 });
                invert->setPosition({ 0, 0 });
                cocos2d::ccBlendFunc bf;
                bf.src = 0x0307; // GL_ONE_MINUS_DST_COLOR
                bf.dst = 0x0000; // GL_ZERO
                invert->setBlendFunc(bf);
                invert->setZOrder(10000);
                invert->setID("rolikakiu.multimode-negative-invert-layer"_spr);
                scene->addChild(invert, 10000);
                m_fields->m_invert = invert;
            }
        }

        return true;
    }
};

class $modify(EndPlayLayerHook, PlayLayer) {
    void levelComplete() {
        PlayLayer::levelComplete();
        if (m_level->m_levelID == kEndLevelID && !s_endingShown) {
            s_endingPending = true;
            log::info("Ending level 149188646 completed");
        }
    }
};