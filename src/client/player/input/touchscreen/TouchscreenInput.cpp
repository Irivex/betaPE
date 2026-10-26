#include "TouchscreenInput.h"
#include "../../../Options.h"
#include "../../../../platform/input/Multitouch.h"
#include "../../../gui/Gui.h"
#include "../../../renderer/Tesselator.h"
#include "../../../../world/entity/player/Player.h"

#include "../../../Minecraft.h"
#include "../../../../platform/log.h"
#include "../../../renderer/Textures.h"
#include "../../../sound/SoundEngine.h"
#include "client/gui/screens/ScreenChooser.h"

// ARGHHHHHH WHY NOT FUCKING ENUM
static const int AREA_DPAD_FIRST = 100;
static const int AREA_DPAD_N = 100;
static const int AREA_DPAD_S = 101;
static const int AREA_DPAD_W = 102;
static const int AREA_DPAD_E = 103;
static const int AREA_DPAD_C = 104;
static const int AREA_PAUSE = 105;
static const int AREA_CHAT = 106;
static const int AREA_DPAD_JUMP = 107;

static int cPressed = 0;
static int cReleased = 0;
static int cDiscreet = 0;
static int cPressedPause = 0;
static int cReleasedPause = 0;

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

static void Copy(int n, float* x, float* y, float* dx, float* dy)
{
    for (int i = 0; i < n; ++i)
    {
        dx[i] = x[i];
        dy[i] = y[i];
    }
}

static void Translate(int n, float* x, float* y, float xt, float yt)
{
    for (int i = 0; i < n; ++i)
    {
        x[i] += xt;
        y[i] += yt;
    }
}

static void Scale(int n, float* x, float* y, float xt, float yt)
{
    for (int i = 0; i < n; ++i)
    {
        x[i] *= xt;
        y[i] *= yt;
    }
}

static void Transformed(
    int n,
    float* x,
    float* y,
    float* dx,
    float* dy,
    float xt,
    float yt,
    float sx = 1.0f,
    float sy = 1.0f)
{
    Copy(n, x, y, dx, dy);
    Scale(n, dx, dy, sx, sy);
    Translate(n, dx, dy, xt, yt);
}

// ------------------------------------------------------------
// Constructor
// ------------------------------------------------------------

TouchscreenInput_TestFps::TouchscreenInput_TestFps(
    Minecraft* mc,
    Options* options)
    : _boundingRectangle(0, 0, 1, 1),
    _jumpControlBounds(0, 0, 1, 1),
    _options(options),
    _pressedJump(false),
    _forward(false),
    _northJump(false),
    _renderFlightImage(false),
    _minecraft(mc),
    aLeft(NULL),
    aRight(NULL),
    aUp(NULL),
    aDown(NULL),
    aPause(NULL),
    aChat(NULL),
    aJump(NULL),
    aCrouch(NULL),
    aFlightUp(NULL),
    aFlightDown(NULL),
    aUpLeft(NULL),
    aUpRight(NULL),
    _pauseIsDown(false),
    _allowHeightChange(false),
    _sneakTapTime(-999),
    _crouchToggled(false),
    _crouchButtonWasDown(false),
    _flightUpPressed(false),
    _flightDownPressed(false)
{
    releaseAllKeys();
    onConfigChanged(createConfig(mc));

    Tesselator& t = Tesselator::instance;
    const int alpha = 128;

    t.color(0xc0c0c0, alpha);
    cPressed = t.getColor();

    t.color(0xffffff, alpha);
    cReleased = t.getColor();

    t.color(0xffffff, alpha / 4);
    cDiscreet = t.getColor();

    t.color(0xc0c0c0, 80);
    cPressedPause = t.getColor();

    t.color(0xffffff, 80);
    cReleasedPause = t.getColor();
}

TouchscreenInput_TestFps::~TouchscreenInput_TestFps()
{
    clear();
}

void TouchscreenInput_TestFps::clear()
{
    _model.clear();

    // These two rectangles are not registered in _model, so we own them.
    delete aFlightUp;
    aFlightUp = NULL;

    delete aFlightDown;
    aFlightDown = NULL;

    // Diagonal movement rectangles are also not registered in _model.
    delete aUpLeft;
    aUpLeft = NULL;

    delete aUpRight;
    aUpRight = NULL;
}

bool TouchscreenInput_TestFps::isButtonDown(int areaId)
{
    const int index = areaId - AREA_DPAD_FIRST;

    if (index < 0 || index >= 8)
        return false;

    return _buttons[index];
}

// ------------------------------------------------------------
// Control layout
// ------------------------------------------------------------

void TouchscreenInput_TestFps::onConfigChanged(const Config& c)
{
    clear();

    const float w = (float)c.width;
    const float h = (float)c.height;

    // D-pad dimensions.
    float Bw = w * 0.11f;
    float Bh = Bw;

    PixelCalc& pc = _minecraft->pixelCalc;

    // Limit button size on tablets.
    if (pc.pixelsToMillimeters(Bw) > 200)
    {
        Bw = Bh = pc.millimetersToPixels(200);
    }

    float xx;
    float yy;

    const float BaseY = -8 + h - 3.0f * Bh;

    const float BaseX =
        _options->getBooleanValue(OPTIONS_IS_LEFT_HANDED)
        ? -8 + w - 3 * Bw
        : 8.0f;

    _boundingRectangle = RectangleArea(
        BaseX,
        BaseY,
        BaseX + 3 * Bw,
        BaseY + 3 * Bh);

    // Normal D-pad: up.
    xx = BaseX + Bw;
    yy = BaseY;

    _model.addArea(
        AREA_DPAD_N,
        aUp = new RectangleArea(
            xx, yy, xx + Bw, yy + Bh));

    // Diagonal areas remain as before.
    xx = BaseX;
    aUpLeft = new RectangleArea(
        xx, yy, xx + Bw, yy + Bh);

    xx = BaseX + 2 * Bw;
    aUpRight = new RectangleArea(
        xx, yy, xx + Bw, yy + Bh);

    // Centre button: crouch toggle.
    xx = BaseX + Bw;
    yy = BaseY + Bh;

    _model.addArea(
        AREA_DPAD_C,
        aCrouch = new RectangleArea(
            xx, yy, xx + Bw, yy + Bh));

    // Normal D-pad: down.
    xx = BaseX + Bw;
    yy = BaseY + 2 * Bh;

    _model.addArea(
        AREA_DPAD_S,
        aDown = new RectangleArea(
            xx, yy, xx + Bw, yy + Bh));

    // Normal D-pad: left.
    xx = BaseX;
    yy = BaseY + Bh;

    _model.addArea(
        AREA_DPAD_W,
        aLeft = new RectangleArea(
            xx, yy, xx + Bw, yy + Bh));

    // Normal D-pad: right.
    xx = BaseX + 2 * Bw;
    yy = BaseY + Bh;

    _model.addArea(
        AREA_DPAD_E,
        aRight = new RectangleArea(
            xx, yy, xx + Bw, yy + Bh));

    // Existing jump button.
    const bool leftHanded =
        _options->getBooleanValue(OPTIONS_IS_LEFT_HANDED);

    const float jumpX = leftHanded ? 8.0f : w - 8.0f - Bw;
    const float jumpY = BaseY + Bh;

    // Reuse the existing flight arrow positions around the jump button.
    // These are rectangles only; no extra button IDs are introduced.
    const float flightUpY = jumpY - Bh;
    const float flightDownY = jumpY + Bh;

    aFlightUp = new RectangleArea(
        jumpX,
        flightUpY,
        jumpX + Bw,
        flightUpY + Bh);

    aFlightDown = new RectangleArea(
        jumpX,
        flightDownY,
        jumpX + Bw,
        flightDownY + Bh);

    // Exclude the whole right-side button column from camera turning.
    _jumpControlBounds = RectangleArea(
        jumpX,
        flightUpY,
        jumpX + Bw,
        flightDownY + Bh);

    // Keep the existing jump button and its original area ID.
    _model.addArea(
        AREA_DPAD_JUMP,
        aJump = new RectangleArea(
            jumpX,
            jumpY,
            jumpX + Bw,
            jumpY + Bh));

    // Pause and chat buttons.
    float btnSize = pc.millimetersToPixels(18 * Gui::GuiScale);

    _model.addArea(
        AREA_PAUSE,
        aPause = new RectangleArea(
            w - 4 - btnSize,
            4,
            w - 4,
            4 + btnSize));

    _model.addArea(
        AREA_CHAT,
        aChat = new RectangleArea(
            w - 8 - btnSize * 2,
            4,
            w - 8 - btnSize,
            4 + btnSize));
}

// ------------------------------------------------------------
// Keyboard support
// ------------------------------------------------------------

void TouchscreenInput_TestFps::setKey(int key, bool state)
{
#ifdef WIN32
    int id = -1;

    if (key == _options->getIntValue(OPTIONS_KEY_FORWARD))
        id = KEY_UP;

    if (key == _options->getIntValue(OPTIONS_KEY_BACK))
        id = KEY_DOWN;

    if (key == _options->getIntValue(OPTIONS_KEY_LEFT))
        id = KEY_LEFT;

    if (key == _options->getIntValue(OPTIONS_KEY_RIGHT))
        id = KEY_RIGHT;

    if (key == _options->getIntValue(OPTIONS_KEY_JUMP))
        id = KEY_JUMP;

    if (key == _options->getIntValue(OPTIONS_KEY_SNEAK))
        id = KEY_SNEAK;

    if (id >= 0 && id < NumKeys)
        _keys[id] = state;
#endif
}

void TouchscreenInput_TestFps::releaseAllKeys()
{
    xa = 0;
    ya = 0;

    for (int i = 0; i < 8; ++i)
        _buttons[i] = false;

#ifdef WIN32
    for (int i = 0; i < NumKeys; ++i)
        _keys[i] = false;
#endif

    _pressedJump = false;
    _allowHeightChange = false;

    _crouchButtonWasDown = false;
    _flightUpPressed = false;
    _flightDownPressed = false;
}

// ------------------------------------------------------------
// Input processing
// ------------------------------------------------------------

void TouchscreenInput_TestFps::tick(Player* player)
{
    xa = 0;
    ya = 0;
    jumping = false;

    _flightUpPressed = false;
    _flightDownPressed = false;

    bool tmpForward = false;

    // Reset the existing area states.
    for (int i = 0; i < 8; ++i)
        _buttons[i] = false;

    const int* pointerIds;
    const int pointerCount =
        Multitouch::getActivePointerIdsThisUpdate(&pointerIds);

    for (int i = 0; i < pointerCount; ++i)
    {
        const int p = pointerIds[i];
        const int x = Multitouch::getX(p);
        const int y = Multitouch::getY(p);

        // While flying, the existing flight-arrow actions use the
        // relocated hitboxes above and below the right-side jump button.
        // No new area IDs are needed.
        if (player->abilities.flying &&
            aFlightUp->isInside((float)x, (float)y))
        {
            _flightUpPressed = true;
            continue;
        }

        if (player->abilities.flying &&
            aFlightDown->isInside((float)x, (float)y))
        {
            _flightDownPressed = true;
            continue;
        }

        // Preserve the existing D-pad movement behaviour.
        // Flight-height input must not disable ordinary movement.
        if (_boundingRectangle.isInside((float)x, (float)y) &&
            _forward)
        {
            const float angle =
                Mth::PI + Mth::atan2(
                    y - _boundingRectangle.centerY(),
                    x - _boundingRectangle.centerX());

            ya = Mth::sin(angle);
            xa = Mth::cos(angle);
            tmpForward = true;
        }

        const int areaId = _model.getPointerId(x, y, p);

        if (areaId < AREA_DPAD_FIRST)
            continue;

        bool setButton = false;

        // Centre D-pad button: crouch toggle.
        if (areaId == AREA_DPAD_C)
        {
            setButton = true;
        }
        // Existing jump button.
        else if (areaId == AREA_DPAD_JUMP)
        {
            setButton = true;
            jumping = true;
        }

        // Normal D-pad up remains movement, even while flying.
        if (areaId == AREA_DPAD_N)
        {
            setButton = true;

            if (player->isInWater())
                jumping = true;
            else
                tmpForward = true;

            ya += 1;
        }
        else if (areaId == AREA_DPAD_S && !_forward)
        {
            setButton = true;
            ya -= 1;
        }
        else if (areaId == AREA_DPAD_W && !_forward)
        {
            setButton = true;
            xa += 1;
        }
        else if (areaId == AREA_DPAD_E && !_forward)
        {
            setButton = true;
            xa -= 1;
        }
        else if (areaId == AREA_PAUSE)
        {
            if (Multitouch::isReleased(p))
            {
                _minecraft->soundEngine->playUI("random.click", 1, 1);
                _minecraft->screenChooser.setScreen(SCREEN_PAUSE);
            }
        }
        else if (areaId == AREA_CHAT)
        {
            if (Multitouch::isReleased(p))
            {
                _minecraft->soundEngine->playUI("random.click", 1, 1);
                _minecraft->screenChooser.setScreen(SCREEN_CONSOLE);
                _minecraft->platform()->showKeyboard();
            }
        }

        _buttons[areaId - AREA_DPAD_FIRST] = setButton;
    }

    _forward = tmpForward;

    // Crouch requires two separate taps within 7 ticks (~0.35 seconds).
    // A single tap or holding the button does not toggle crouch.
    const bool crouchButtonDown = isButtonDown(AREA_DPAD_C);

    if (_sneakTapTime > 0.0f)
        _sneakTapTime -= 1.0f;

    if (player->abilities.flying)
    {
        // Never let crouch taps interfere with flight controls.
        _sneakTapTime = 0.0f;
    }
    else if (crouchButtonDown && !_crouchButtonWasDown)
    {
        if (_sneakTapTime > 0.0f)
        {
            _crouchToggled = !_crouchToggled;
            _sneakTapTime = 0.0f;
        }
        else
        {
            // Wait for a second, distinct tap.
            _sneakTapTime = 7.0f;
        }
    }

    _crouchButtonWasDown = crouchButtonDown;

    // A toggled crouch should not make a flying player descend.
    sneaking = _crouchToggled && !player->abilities.flying;

#ifdef WIN32
    if (_keys[KEY_UP])
        ya++;

    if (_keys[KEY_DOWN])
        ya--;

    if (_keys[KEY_LEFT])
        xa++;

    if (_keys[KEY_RIGHT])
        xa--;

    if (_keys[KEY_JUMP])
        jumping = true;

    if (_keys[KEY_CRAFT])
    {
        player->startCrafting(
            (int)player->x,
            (int)player->y,
            (int)player->z,
            Recipe::SIZE_2X2);
    }
#endif

    // Preserve the existing jump-to-toggle-flight behaviour.
    // When flying, the relocated arrows independently control height.
    _renderFlightImage = player->abilities.flying;

    if (_renderFlightImage)
    {
        // Keep the existing jump-as-ascend behaviour as well.
        wantUp = jumping || _flightUpPressed;
        wantDown = _flightDownPressed;
    }
    else
    {
        wantUp = jumping;
        wantDown = sneaking;
    }

    isChangingFlightHeight =
        _renderFlightImage && (wantUp || wantDown);

    // Deliberately do not zero ya here: normal D-pad movement
    // must remain available while ascending or descending.

    // Preserve the existing crouch movement slowdown.
    if (sneaking)
    {
        xa *= 0.3f;
        ya *= 0.3f;
    }
}

// ------------------------------------------------------------
// Rendering helpers
// ------------------------------------------------------------

static void drawRectangleArea(
    Tesselator& t,
    RectangleArea* a,
    int ux,
    int vy,
    float ssz = 64.0f)
{
    const float pm = 1.0f / 256.0f;
    const float sz = ssz * pm;
    const float uu = (float)ux * pm;
    const float vv = (float)vy * pm;

    const float x0 = a->_x0 * Gui::InvGuiScale;
    const float x1 = a->_x1 * Gui::InvGuiScale;
    const float y0 = a->_y0 * Gui::InvGuiScale;
    const float y1 = a->_y1 * Gui::InvGuiScale;

    t.vertexUV(x0, y1, 0, uu, vv + sz);
    t.vertexUV(x1, y1, 0, uu + sz, vv + sz);
    t.vertexUV(x1, y0, 0, uu + sz, vv);
    t.vertexUV(x0, y0, 0, uu, vv);
}

static void drawPolygonArea(Tesselator& t, PolygonArea* a, int x, int y)
{
    const float pm = 1.0f / 256.0f;
    const float sz = 64.0f * pm;
    const float uu = (float)x * pm;
    const float vv = (float)y * pm;

    float uvs[] = {
        uu,      vv,
        uu + sz, vv,
        uu + sz, vv + sz,
        uu,      vv + sz
    };

    const int o = 0;

    for (int j = 0; j < a->_numPoints; ++j)
    {
        t.vertexUV(
            a->_x[j] * Gui::InvGuiScale,
            a->_y[j] * Gui::InvGuiScale,
            0,
            uvs[(o + j + j) & 7],
            uvs[(o + j + j + 1) & 7]);
    }
}

// ------------------------------------------------------------
// Render entry point
// ------------------------------------------------------------

void TouchscreenInput_TestFps::render(float a)
{
    glDisable2(GL_ALPHA_TEST);

    glEnable2(GL_BLEND);
    glBlendFunc2(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    _minecraft->textures->loadAndBindTexture("gui/gui.png");

    rebuild();

    glDisable2(GL_BLEND);
}

// ------------------------------------------------------------
// Rectangles used by the unified touch input
// ------------------------------------------------------------

const RectangleArea& TouchscreenInput_TestFps::getRectangleArea()
{
    return _boundingRectangle;
}

const RectangleArea& TouchscreenInput_TestFps::getPauseRectangleArea()
{
    return *aPause;
}

const RectangleArea& TouchscreenInput_TestFps::getJumpRectangleArea()
{
    // Exclude the full right-side jump/flight column from camera turning.
    return _jumpControlBounds;
}

// ------------------------------------------------------------
// Control rendering
// ------------------------------------------------------------

void TouchscreenInput_TestFps::rebuild()
{
    if (_options->getBooleanValue(OPTIONS_HIDEGUI))
        return;

    Tesselator& t = Tesselator::instance;

    t.begin();

    const int imageU = 0;
    const int imageV = 107;
    const int imageSize = 26;

    // Normal D-pad movement icons must not change during flight.
    const bool northDiagonals = (_northJump || _forward);

    // Left button.
    if (northDiagonals)
        t.colorABGR(cDiscreet);
    else if (isButtonDown(AREA_DPAD_W))
        t.colorABGR(cPressed);
    else
        t.colorABGR(cReleased);

    drawRectangleArea(
        t, aLeft,
        imageU + imageSize, imageV,
        (float)imageSize);

    // Right button.
    if (northDiagonals)
        t.colorABGR(cDiscreet);
    else if (isButtonDown(AREA_DPAD_E))
        t.colorABGR(cPressed);
    else
        t.colorABGR(cReleased);

    drawRectangleArea(
        t, aRight,
        imageU + imageSize * 3, imageV,
        (float)imageSize);

    // Up: always display the normal movement arrow.
    if (isButtonDown(AREA_DPAD_N))
        t.colorABGR(cPressed);
    else
        t.colorABGR(cReleased);

    drawRectangleArea(
        t, aUp,
        imageU, imageV,
        (float)imageSize);

    // Diagonals, if available.
    if (northDiagonals)
    {
        t.colorABGR(cReleased);

        drawRectangleArea(
            t, aUpLeft,
            imageU, imageV + imageSize,
            (float)imageSize);

        drawRectangleArea(
            t, aUpRight,
            imageU + imageSize, imageV + imageSize,
            (float)imageSize);
    }

    // Down: always display the normal movement arrow.
    if (northDiagonals)
        t.colorABGR(cDiscreet);
    else if (isButtonDown(AREA_DPAD_S))
        t.colorABGR(cPressed);
    else
        t.colorABGR(cReleased);

    drawRectangleArea(
        t, aDown,
        imageU + imageSize * 2, imageV,
        (float)imageSize);

    // Centre button: remains visually pressed while crouch is toggled.
    if (_crouchToggled && !_renderFlightImage)
        t.colorABGR(cPressed);
    else
        t.colorABGR(cReleased);

    drawRectangleArea(
        t, aCrouch,
        imageU + imageSize * 2, imageV,
        (float)imageSize);

    // Existing jump button.
    if (isButtonDown(AREA_DPAD_JUMP))
        t.colorABGR(cPressed);
    else
        t.colorABGR(cReleased);

    if (_renderFlightImage)
    {
        drawRectangleArea(
            t, aJump,
            imageU + imageSize * 4, imageV + imageSize,
            (float)imageSize);
    }
    else
    {
        drawRectangleArea(
            t, aJump,
            imageU + imageSize * 4, imageV,
            (float)imageSize);
    }

    // Reuse the original flight-arrow sprites in their new positions.
    // The arrows are visible during flight, without replacing the D-pad.
    if (_renderFlightImage)
    {
        if (_flightUpPressed)
            t.colorABGR(cPressed);
        else
            t.colorABGR(cReleased);

        drawRectangleArea(
            t, aFlightUp,
            imageU + imageSize * 2, imageV + imageSize,
            (float)imageSize);

        if (_flightDownPressed)
            t.colorABGR(cPressed);
        else
            t.colorABGR(cReleased);

        drawRectangleArea(
            t, aFlightDown,
            imageU + imageSize * 3, imageV + imageSize,
            (float)imageSize);
    }

    // Draw the pause and chat buttons when in-game.
    if (!_minecraft->screen) {
        t.colorABGR(0xFFFFFFFF);

        drawRectangleArea(t, aPause, 200, 64, 18.0f);
        drawRectangleArea(t, aChat, 200, 82, 18.0f);
    }

    t.draw();
}