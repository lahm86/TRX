#include "game/camera.h"
#include "game/control.h"
#include "game/draw.h"
#include "game/game.h"
#include "game/savegame.h"
#include "game/setup.h"
#include "game/text.h"
#include "game/vars.h"
#include "specific/display.h"
#include "specific/frontend.h"
#include "specific/input.h"
#include "specific/output.h"
#include "specific/shed.h"
#include "specific/sndpc.h"
#include "config.h"
#include "util.h"

int32_t StartGame(int level_num)
{
    switch (level_num) {
    case LV_GYM:
        S_PlayFMV(FMV_GYM, 1);
        break;

    case LV_LEVEL1:
        S_PlayFMV(FMV_SNOW, 1);
        break;

    case LV_LEVEL4:
        S_PlayFMV(FMV_LIFT, 1);
        break;

    case LV_LEVEL8A:
        S_PlayFMV(FMV_VISION, 1);
        break;

    case LV_LEVEL10A:
        S_PlayFMV(FMV_CANYON, 1);
        break;

    case LV_LEVEL10B:
        S_PlayFMV(FMV_PYRAMID, 1);
        break;
    }

    // TODO: this probably doesn't belong here
    CurrentLevel = level_num;
    TitleLoaded = 0;
    if (level_num != LV_CURRENT) {
        InitialiseLevelFlags();
    }

    if (!InitialiseLevel(level_num)) {
        CurrentLevel = 0;
        return GF_EXIT_TO_TITLE;
    }

    if (GameLoop(0) == GF_EXIT_TO_TITLE) {
        return GF_EXIT_TO_TITLE;
    }

    if (LevelComplete) {
        if (CurrentLevel) {
            S_FadeInInventory(1);
            return GF_LEVELCOMPLETE | CurrentLevel;
        }

        S_FadeToBlack();
        return GF_EXIT_TO_TITLE;
    }

    S_FadeToBlack();
    if (!InventoryChosen) {
        return GF_EXIT_TO_TITLE;
    }

    if (InventoryExtraData[0]) {
        if (InventoryExtraData[0] == 1) {
            return GF_STARTGAME | LV_FIRSTLEVEL;
        }
        return GF_EXIT_TO_TITLE;
    }

    S_LoadGame(SaveGame, sizeof(SAVEGAME_INFO), InventoryExtraData[1]);
    return GF_STARTGAME | LV_CURRENT;
}

int32_t GameLoop(int demo_mode)
{
    TRACE("");
    OverlayFlag = 1;
    InitialiseCamera();

    int32_t nframes = 1;
    int32_t ret;
    while (1) {
        ret = ControlPhase(nframes, demo_mode);
        if (ret) {
            break;
        }
        nframes = DrawPhaseGame(ret);
    }

    S_SoundStopAllSamples();
    S_CDStop();
    if (OptionMusicVolume) {
        S_CDVolume(OptionMusicVolume * 25 + 5);
    }

    return ret;
}

int32_t LevelCompleteSequence(int level_num)
{
    TRACE("");
    switch (level_num) {
    case LV_GYM:
        return GF_EXIT_TO_OPTION;

    case LV_LEVEL1:
        LevelStats(LV_LEVEL1);
        return GF_STARTGAME | LV_LEVEL2;

    case LV_LEVEL2:
        LevelStats(LV_LEVEL2);
        return GF_STARTGAME | LV_LEVEL3A;

    case LV_LEVEL3A:
        LevelStats(LV_LEVEL3A);
        return GF_STARTGAME | LV_LEVEL3B;

    case LV_LEVEL3B:
        return GF_STARTCINE | LV_CUTSCENE1;

    case LV_LEVEL4:
        LevelStats(LV_LEVEL4);
        return GF_STARTGAME | LV_LEVEL5;

    case LV_LEVEL5:
        LevelStats(LV_LEVEL5);
        return GF_STARTGAME | LV_LEVEL6;

    case LV_LEVEL6:
        LevelStats(LV_LEVEL6);
        return GF_STARTGAME | LV_LEVEL7A;

    case LV_LEVEL7A:
        LevelStats(LV_LEVEL7A);
        return GF_STARTGAME | LV_LEVEL7B;

    case LV_LEVEL7B:
        return GF_STARTCINE | LV_CUTSCENE2;

    case LV_LEVEL8A:
        LevelStats(LV_LEVEL8A);
        return GF_STARTGAME | LV_LEVEL8B;

    case LV_LEVEL8B:
        LevelStats(LV_LEVEL8B);
        return GF_STARTGAME | LV_LEVEL8C;

    case LV_LEVEL8C:
        LevelStats(LV_LEVEL8C);
        return GF_STARTGAME | LV_LEVEL10A;

    case LV_LEVEL10A:
        LevelStats(LV_LEVEL10A);
        return GF_STARTCINE | LV_CUTSCENE3;

    case LV_LEVEL10B:
        S_PlayFMV(FMV_ENDSEQ, 1);
        return GF_STARTCINE | LV_CUTSCENE4;

    case LV_LEVEL10C:
        LevelStats(LV_LEVEL10C);
        S_PlayFMV(FMV_CORE, 1);
        TempVideoAdjust(2, 1.0);
        S_DisplayPicture("data\\end");
        sub_408E41();
        S_Wait(450);
        S_FadeToBlack();
        S_DisplayPicture("data\\cred1");
        sub_408E41();
        S_Wait(450);
        S_DisplayPicture("data\\cred2");
        sub_408E41();
        S_Wait(450);
        S_FadeToBlack();
        S_DisplayPicture("data\\cred3");
        sub_408E41();
        S_Wait(450);
        S_FadeToBlack();
        S_NoFade();
        return GF_EXIT_TO_TITLE;

    case LV_CUTSCENE1:
        LevelStats(LV_LEVEL3B);
        return GF_STARTGAME | LV_LEVEL4;

    case LV_CUTSCENE2:
        LevelStats(LV_LEVEL7B);
        return GF_STARTGAME | LV_LEVEL8A;

    case LV_CUTSCENE3:
        return GF_STARTGAME | LV_LEVEL10B;

    case LV_CUTSCENE4:
        LevelStats(LV_LEVEL10B);
        return GF_STARTGAME | LV_LEVEL10C;
    }

    return GF_EXIT_TO_TITLE;
}

int32_t LevelIsValid(int16_t level_num)
{
    TRACE("%d", level_num);
    int32_t number_valid = 0;
    for (;;) {
        if (ValidLevels[number_valid] == -1) {
            break;
        }
        number_valid++;
    }
    for (int i = 0; i < number_valid; i++) {
        if (ValidLevels[i] == level_num) {
            return 1;
        }
    }
    return 0;
}

void SeedRandomControl(int32_t seed)
{
    TRACE("%d", seed);
    Rand1 = seed;
}

int32_t GetRandomControl()
{
    Rand1 = 0x41C64E6D * Rand1 + 0x3039;
    return (Rand1 >> 10) & 0x7FFF;
}

void SeedRandomDraw(int32_t seed)
{
    Rand2 = seed;
}

int32_t GetRandomDraw()
{
    Rand2 = 0x41C64E6D * Rand2 + 0x3039;
    return (Rand2 >> 10) & 0x7FFF;
}

void LevelStats(int32_t level_num)
{
    static char string[100];
    TEXTSTRING* txt;

    TempVideoAdjust(HiRes, 1.0);
    T_InitPrint();

    // heading
    sprintf(string, "%s", LevelTitles[level_num]); // TODO: translation
    txt = T_Print(0, -50, 0, string);
    T_CentreH(txt, 1);
    T_CentreV(txt, 1);

    // time taken
    int seconds = SaveGame[0].timer / 30;
    int hours = seconds / 3600;
    int minutes = (seconds / 60) % 60;
    seconds %= 60;
    if (hours) {
        sprintf(
            string, "%s %d:%d%d:%d%d",
            "TIME TAKEN", // TODO: translation
            hours, minutes / 10, minutes % 10, seconds / 10, seconds % 10);
    } else {
        sprintf(
            string, "%s %d:%d%d",
            "TIME TAKEN", // TODO: translation
            minutes, seconds / 10, seconds % 10);
    }
    txt = T_Print(0, 70, 0, string);
    T_CentreH(txt, 1);
    T_CentreV(txt, 1);

    // secrets
    int secrets_taken = 0;
    int secrets_total = MAX_SECRETS;
    do {
        if (SaveGame[0].secrets & 1) {
            ++secrets_taken;
        }
        SaveGame[0].secrets >>= 1;
        --secrets_total;
    } while (secrets_total);
    sprintf(
        string, "%s %d %s %d",
        "SECRETS", // TODO: translation
        secrets_taken,
        "OF", // TODO: translation
        SecretTotals[level_num]);
    txt = T_Print(0, 40, 0, string);
    T_CentreH(txt, 1);
    T_CentreV(txt, 1);

    // pickups
    sprintf(
        string, "%s %d", "PICKUPS",
        SaveGame[0].pickups); // TODO: translation
    txt = T_Print(0, 10, 0, string);
    T_CentreH(txt, 1);
    T_CentreV(txt, 1);

    // kills
    sprintf(string, "%s %d", "KILLS", SaveGame[0].kills); // TODO: translation
    txt = T_Print(0, -20, 0, string);
    T_CentreH(txt, 1);
    T_CentreV(txt, 1);

    // wait till action key release
#ifdef T1M_FEAT_OG_FIXES
    if (T1MConfig.fix_end_of_level_freeze) {
        while (Input & IN_SELECT) {
            S_UpdateInput();
            S_InitialisePolyList();
            S_CopyBufferToScreen();
            S_UpdateInput();
            T_DrawText();
            S_OutputPolyList();
            S_DumpScreen();
        }
    } else {
#else
    {
#endif
        while (Input & IN_SELECT) {
            S_UpdateInput();
        }
        S_InitialisePolyList();
        S_CopyBufferToScreen();
        S_UpdateInput();
        T_DrawText();
        S_OutputPolyList();
        S_DumpScreen();
    }

    // wait till action key press
    while (!(Input & IN_SELECT)) {
        if (ResetFlag) {
            break;
        }
        S_InitialisePolyList();
        S_CopyBufferToScreen();
        S_UpdateInput();
        T_DrawText();
        S_OutputPolyList();
        S_DumpScreen();
    }

    // go to next level
    if (level_num == LV_LEVEL10C) {
        SaveGame[0].bonus_flag = 1;
        for (int level = LV_LEVEL1; level <= LV_LEVEL10C; level++) {
            ModifyStartInfo(level);
        }
        SaveGame[0].current_level = 1;
    } else {
        CreateStartInfo(level_num + 1);
        ModifyStartInfo(level_num + 1);
        SaveGame[0].current_level = level_num + 1;
    }

    SaveGame[0].start[LV_CURRENT].available = 0;
    S_FadeToBlack();
    TempVideoRemove();
}

int32_t S_LoadGame(void* data, int32_t size, int slot)
{
    char filename[80];
    sprintf(filename, "saveati.%d", slot);
    TRACE("%s", filename);
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        return 0;
    }
    fread(filename, 1u, 75u, fp);
    fread(&slot, 4u, 1u, fp);
    fread(data, size, 1u, fp);
    fclose(fp);
    return 1;
}

void T1MInjectGameGame()
{
    INJECT(0x0041D0C0, StartGame);
    INJECT(0x0041D2C0, GameLoop);
    INJECT(0x0041D330, LevelCompleteSequence);
    INJECT(0x0041D5A0, LevelStats);
    INJECT(0x0041D8F0, GetRandomControl);
    INJECT(0x0041D910, SeedRandomControl);
    INJECT(0x0041D920, GetRandomDraw);
    INJECT(0x0041D940, SeedRandomDraw);
    INJECT(0x0041D950, LevelIsValid);
    INJECT(0x0041DC70, S_LoadGame);
}
