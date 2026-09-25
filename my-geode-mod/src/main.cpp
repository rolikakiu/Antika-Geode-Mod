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
static bool allModesPlatformerOn() { return Mod::get()->getSettingValue<bool>("all-modes-platformer"); }
static bool make3dOn() { return Mod::get()->getSettingValue<bool>("make-3d"); }

static GameObjectType chosenPlatformerMode() {
    auto s = Mod::get()->getSettingValue<std::string>("platformer-mode");
    if (s == "Ship") return GameObjectType::ShipPortal;
    if (s == "Ball") return GameObjectType::BallPortal;
    if (s == "UFO") return GameObjectType::UfoPortal;
    if (s == "Wave") return GameObjectType::WavePortal;
    if (s == "Robot") return GameObjectType::RobotPortal;
    if (s == "Spider") return GameObjectType::SpiderPortal;
    if (s == "Swing") return GameObjectType::SwingPortal;
    return GameObjectType::CubePortal;
}

static void applyPlatformerMode(PlayerObject* player) {
    if (!player || !allModesPlatformerOn() || !player->m_isPlatformer) return;
    switch (chosenPlatformerMode()) {
        case GameObjectType::ShipPortal: player->toggleFlyMode(true, true); break;
        case GameObjectType::BallPortal: player->toggleRollMode(true, true); break;
        case GameObjectType::UfoPortal: player->toggleBirdMode(true, true); break;
        case GameObjectType::WavePortal: player->toggleDartMode(true, true); break;
        case GameObjectType::RobotPortal: player->toggleRobotMode(true, true); break;
        case GameObjectType::SpiderPortal: player->toggleSpiderMode(true, true); break;
        case GameObjectType::SwingPortal: player->toggleSwingMode(true, true); break;
        default: break;
    }
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

    void resetObject() {
        PlayerObject::resetObject();
        applyPlatformerMode(this);
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
    struct Fields {
        DialogLayer* m_dialog = nullptr;
    };

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
                m_fields->m_dialog = dialog;
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
        auto dialog = m_fields->m_dialog;
        if (dialog) {
            if (dialog->getParent() == nullptr) {
                m_fields->m_dialog = nullptr;
            }
            else {
                auto running = cocos2d::CCDirector::get()->getRunningScene();
                bool visible = false;
                for (auto p = this->getParent(); p != nullptr; p = p->getParent()) {
                    if (p == running) {
                        visible = true;
                        break;
                    }
                }
                if (!visible) {
                    dialog->removeFromParent();
                    m_fields->m_dialog = nullptr;
                }
            }
        }
    }
};

class $modify(AntikaPlayLayer, PlayLayer) {
    struct Fields {
        cocos2d::CCLayerColor* m_invert = nullptr;
    };

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
        if (make3dOn()) {
            applyMakeEverything3D(this);
        }
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        if (forcePlatformerOn() && !m_isPlatformer) {
            m_isPlatformer = true;
            if (m_levelSettings) m_levelSettings->m_platformerMode = true;
            if (m_player1) m_player1->togglePlatformerMode(true);
            if (m_player2) m_player2->togglePlatformerMode(true);
            if (m_uiLayer) m_uiLayer->togglePlatformerMode(true);
        }

        if (allModesPlatformerOn()) {
            applyPlatformerMode(m_player1);
            if (m_player2) applyPlatformerMode(m_player2);
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