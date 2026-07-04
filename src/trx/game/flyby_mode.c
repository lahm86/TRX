#include <trx/game/flyby_mode.h>

#include <trx/game/camera.h>
#include <trx/game/effects.h>
#include <trx/game/fx.h>
#include <trx/game/input.h>
#include <trx/game/interpolation.h>
#include <trx/game/lara.h>
#include <trx/game/output.h>
#include <trx/game/overlay.h>
#include <trx/game/sound.h>
#include <trx/game/sparks.h>

static struct {
    int32_t lara_health;
    int32_t lara_air;
} m_Priv = {};

static void M_CacheLaraInfo(void)
{
    m_Priv.lara_health = Lara_GetItem()->hit_points;
    m_Priv.lara_air = Lara_GetLaraInfo()->air;
}

static void M_RestoreLaraInfo(void)
{
    Lara_GetItem()->hit_points = m_Priv.lara_health;
    Lara_GetLaraInfo()->air = m_Priv.lara_air;
}

static void M_ControlGame(void)
{
    Output_ResetDynamicLights();

    Sound_ResetAmbient();
    Item_Control();
    Effect_Control();
    Sparks_Control();

    Lara_Control();
    FX_Control();
    Lara_Hair_Control(false);

    ItemAction_RunActive();
    Sound_UpdateEffects();
    Overlay_Animate(1);
    Output_AnimateTextures(1);
}

void FlybyMode_Start(void)
{
    M_CacheLaraInfo();
    Camera_FlybyMode_Enter();
}

void FlybyMode_End(void)
{
    Camera_FlybyMode_Exit();
}

PHASE_CONTROL FlybyMode_Control(void)
{
    Interpolation_Remember();

    Camera_FlybyMode_Update();
    Camera_EnsureEnvironment();

    if (!Lara_IsControllable()) {
        g_Input = (INPUT_STATE) {};
        g_InputDB = (INPUT_STATE) {};
        M_RestoreLaraInfo();
    }

    M_ControlGame();

    if (!Camera_FlybyMode_IsActive()) {
        return (PHASE_CONTROL) {
            .action = PHASE_ACTION_END,
            .gf_cmd = { .action = GF_NOOP },
        };
    }
    return (PHASE_CONTROL) { .action = PHASE_ACTION_CONTINUE };
}
