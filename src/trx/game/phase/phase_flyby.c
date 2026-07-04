#include <trx/game/phase/phase_flyby.h>

#include <trx/core/memory.h>
#include <trx/game/flyby_mode.h>
#include <trx/game/game.h>
#include <trx/game/input.h>
#include <trx/game/shell.h>

static PHASE_CONTROL M_Start(PHASE *phase)
{
    FlybyMode_Start();
    return (PHASE_CONTROL) { .action = PHASE_ACTION_CONTINUE };
}

static void M_End(PHASE *const phase)
{
    FlybyMode_End();
}

static PHASE_CONTROL M_Control(PHASE *const phase)
{
    Input_Update();
    Shell_ProcessInput();
    return FlybyMode_Control();
}

static void M_Draw(PHASE *const phase)
{
    Game_Draw(false);
}

PHASE *Phase_Flyby_Create(void)
{
    PHASE *const phase = Memory_Alloc(sizeof(PHASE));
    phase->start = M_Start;
    phase->end = M_End;
    phase->control = M_Control;
    phase->draw = M_Draw;
    return phase;
}

void Phase_Flyby_Destroy(PHASE *phase)
{
    Memory_Free(phase);
}
