#ifndef T1M_SPECIFIC_DISPLAY_H
#define T1M_SPECIFIC_DISPLAY_H

// clang-format off
#define TempVideoRemove         ((void          (*)())0x004167D0)
#define TempVideoAdjust         ((void          (*)(int32_t hires, double sizer))0x00416550)
#define S_FadeInInventory       ((void          (*)())0x00416B20)
#define S_NoFade                ((void          (*)())0x00416B10)
// clang-format on

#endif
