#include <trx/game/objects/vehicles/minecart.h>

#include <trx/game/camera.h>
#include <trx/game/game_buf.h>
#include <trx/game/gun.h>
#include <trx/game/input.h>
#include <trx/game/lara.h>
#include <trx/game/music.h>
#include <trx/game/objects/common.h>
#include <trx/game/rooms.h>
#include <trx/game/sound.h>
#include <trx/game/spawn.h>

// clang-format off
#define M_GET_ON_DIST    200000
#define M_GET_OFF_DIST   330
#define M_MAX_COLL_ROOMS 12
#define M_TARGET_DIST    (WALL_L * 2) // = 2048
#define M_RADIUS         STEP_L
#define M_MAX_GRADIENT   (STEP_L / 2) // = 128
#define M_MIN_GRADIENT   (-M_MAX_GRADIENT) // = -128
#define M_MAX_ROLL       (WALL_L * 4) // = 4096
#define M_MIN_ROLL       (-M_MAX_ROLL) // = -4096
#define M_TURN_SHIFT     (WALL_L * 4) // = 4096
// clang-format on

/************************************************/
/* TODO:                                        */
/*   - duck                                     */
/*   - global wibble? see SFX_QUAD_FRONT_IMPACT */
/*   - crash SFX don't play                     */
/*   - Math_GetAngle is weird                   */
/*   - get_on always same side                  */
/*   - shaky interp at high speed               */
/*   - cart stop pos sometimes short?           */
/*   - interpret cart_info->flags               */
/*   - Lara_GetJointAbsPosition - handle all    */
/*   - savegame                                 */
/*   - see about commonality with skidoo/quad   */
/************************************************/

typedef enum {
    // clang-format off
    M_GET_ON_NONE  = 0,
    M_GET_ON_LEFT  = 1,
    M_GET_ON_RIGHT = 2,
    // clang-format on
} M_GET_ON_SIDE;

typedef enum {
    // clang-format off
    LA_MINECART_GET_ON_L         = 0,
    LA_MINECART_PREPARE_RIDE     = 5,
    LA_MINECART_SWIPE            = 6,
    LA_MINECART_PREPARE_DISMOUNT = 7,
    LA_MINECART_CRASH            = 23,
    LA_MINECART_TOPPLE_START     = 31,
    LA_MINECART_BEAM_HIT         = 34,
    LA_MINECART_GET_ON_R         = 46,
    // clang-format on
} M_LARA_ANIM;

typedef enum {
    // clang-format off
    LS_MINECART_GET_ON       = 0,
    LS_MINECART_STOP         = 1,
    LS_MINECART_GET_OFF_L    = 2,
    LS_MINECART_GET_OFF_R    = 3,
    LS_MINECART_IDLE         = 4,
    LS_MINECART_DUCK         = 5,
    LS_MINECART_RIDE         = 6,
    LS_MINECART_RIGHT        = 7,
    LS_MINECART_LEFT         = 9,
    LS_MINECART_BRAKE        = 11,
    LS_MINECART_TILT_FORWARD = 12,
    LS_MINECART_TILT_BACK    = 13,
    LS_MINECART_DEATH        = 14,
    LS_MINECART_CRASH        = 16,
    LS_MINECART_BEAM_HIT     = 17,
    LS_MINECART_SWIPE        = 18,
    LS_MINECART_BRAKING      = 19,
    // clang-format on
} M_LARA_STATE;

static void M_Initialise(const int16_t item_num)
{
    ITEM *const item = Item_Get(item_num);
    item->data = GameBuf_Alloc(sizeof(MINECART_INFO), GBUF_ITEM_DATA);
}

static M_GET_ON_SIDE M_CheckGetOn(ITEM *const item, COLL_INFO *const coll)
{
    const ITEM *const lara_item = Lara_GetItem();
    const LARA_INFO *const lara = Lara_GetLaraInfo();
    if (!g_Input.action || lara->gun_status != LGS_ARMLESS
        || lara_item->gravity) {
        return M_GET_ON_NONE;
    }

    if (!Item_TestBoundsCollide(item, lara_item, coll->radius)) {
        return M_GET_ON_NONE;
    }

    if (!Collide_TestCollision(item, lara_item)) {
        return M_GET_ON_NONE;
    }

    const int32_t dx = lara_item->pos.x - item->pos.x;
    const int32_t dz = lara_item->pos.z - item->pos.z;
    const int32_t dist = SQUARE(dx) + SQUARE(dz);
    if (dist > M_GET_ON_DIST) {
        return M_GET_ON_NONE;
    }

    int16_t room_num = item->room_num;
    const SECTOR *const sector =
        Room_GetSector(item->pos.x, item->pos.y, item->pos.z, &room_num);
    const int32_t height =
        Room_GetHeight(sector, item->pos.x, item->pos.y, item->pos.z);

    if (height < -MAX_HEIGHT) {
        return M_GET_ON_NONE;
    }

    const int16_t angle =
        (int16_t)Math_GetAngle(
            item->pos.x, item->pos.z, lara_item->pos.x, lara_item->pos.z)
        - item->rot.y;
    return angle > DEG_45 && angle < DEG_135 ? M_GET_ON_LEFT : M_GET_ON_RIGHT;
}

static void M_Collision(
    const int16_t item_num, ITEM *const lara_item, COLL_INFO *const coll)
{
    if (lara_item->hit_points < 0 || Lara_Vehicle_IsMounted()) {
        return;
    }

    ITEM *const item = Item_Get(item_num);
    const M_GET_ON_SIDE get_on = M_CheckGetOn(item, coll);
    if (get_on == M_GET_ON_NONE) {
        Object_Collision(item_num, lara_item, coll);
        return;
    }

    LARA_INFO *const lara = Lara_GetLaraInfo();
    Lara_Vehicle_SetIndex(item_num);
    if (lara->gun_type == LGT_FLARE) {
        Lara_Flare_Dispose(false);
        lara->gun_type = LGT_UNARMED;
        lara->request_gun_type = LGT_UNARMED;
    }

    const M_LARA_ANIM anim_idx =
        get_on == M_GET_ON_LEFT ? LA_MINECART_GET_ON_L : LA_MINECART_GET_ON_R;
    Item_SwitchToObjAnim(lara_item, anim_idx, 0, O_LARA_MINECART);
    lara_item->current_anim_state = LS_MINECART_GET_ON;
    lara->gun_status = LGS_HANDS_BUSY;
    lara->hit_direction = DIR_UNKNOWN;

    MINECART_INFO *const cart_info = (MINECART_INFO *)item->data;
    cart_info->flags = 0;
    cart_info->speed = 0;
    cart_info->y_velocity = 0;
    cart_info->gradient = 0;

    if (Music_GetCurrentPlayingTrack() != Music_ToGameID(MX_MINECART_THEME)) {
        Music_Play(MX_MINECART_THEME, MPM_ALWAYS);
    }
}

static bool M_CheckGetOff(const int32_t direction)
{
    ITEM *const item = Lara_Vehicle_GetItem();

    int16_t rot;
    if (direction < 0) {
        rot = item->rot.y + DEG_90;
    } else {
        rot = item->rot.y - DEG_90;
    }

    const XYZ_32 pos = {
        .x = item->pos.x - ((M_GET_OFF_DIST * Math_Sin(rot)) >> W2V_SHIFT),
        .y = item->pos.y,
        .z = item->pos.z - ((M_GET_OFF_DIST * Math_Cos(rot)) >> W2V_SHIFT),
    };
    int16_t room_num = item->room_num;
    const SECTOR *const sector = Room_GetSector(pos.x, pos.y, pos.z, &room_num);
    const int32_t height = Room_GetHeight(sector, pos.x, pos.y, pos.z);
    const HEIGHT_TYPE height_type = Room_GetHeightType();

    if (height_type == HT_BIG_SLOPE || height_type == HT_DIAGONAL
        || height == NO_HEIGHT || ABS(height) <= WALL_L / 2) {
        return false;
    }

    const int32_t ceiling = Room_GetCeiling(sector, pos.x, pos.y, pos.z);
    if (ceiling - item->pos.y > -LARA_HEIGHT) {
        return false;
    }
    if (height - ceiling < LARA_HEIGHT) {
        return false;
    }

    return true;
}

static void M_CheckStrikeSwitch(ITEM *const item)
{
    if (!Item_TestFrameEqual(item, 0)) {
        return;
    }

    const ITEM *const lara_item = Lara_GetItem();
    if (lara_item->current_anim_state != LS_MINECART_SWIPE
        || !Item_TestObjAnimEqual(lara_item, LA_MINECART_SWIPE, O_LARA_MINECART)
        || !Item_TestFrameRange(lara_item, 12, 22)) {
        return;
    }

    Sound_Effect(SFX_SPANNER, &item->pos, SPM_ALWAYS);
    Room_TestTriggers(item);
}

static void M_CheckObjectCollision(ITEM *const item, ITEM *const cart)
{
    if (!item->collidable || item->status == IS_INVISIBLE
        || item == Lara_GetItem() || item == cart) {
        return;
    }

    const OBJECT *const obj = Object_Get(item->object_id);
    const bool is_flip_switch = item->object_id == O_ANIMATING_2;
    if (obj->collision_func == nullptr
        || (!obj->intelligent && !is_flip_switch)) {
        return;
    }

    if (!Item_IsNearby(item, cart, M_TARGET_DIST)) {
        return;
    }

    if (!Item_TestBoundsCollide(item, cart, M_RADIUS)) {
        return;
    }

    if (is_flip_switch) {
        M_CheckStrikeSwitch(item);
        return;
    }

    Spawn_BloodBath(
        item->pos.x, cart->pos.y - STEP_L, item->pos.z, cart->speed,
        cart->rot.y, item->room_num, 3);
    Gun_HitTarget(item, nullptr, nullptr, item->hit_points);
}

static void M_ObjectCollision(ITEM *const cart)
{
    int16_t roomies[M_MAX_COLL_ROOMS];
    const int32_t roomies_count =
        Room_GetAdjoiningRooms(cart->room_num, roomies, M_MAX_COLL_ROOMS);

    for (int32_t i = 0; i < roomies_count; i++) {
        const ROOM *const room = Room_Get(roomies[i]);
        int16_t item_num = room->item_num;
        while (item_num != NO_ITEM) {
            ITEM *const item = Item_Get(item_num);
            M_CheckObjectCollision(item, cart);
            item_num = item->next_item;
        }
    }
}

static int16_t M_GetCollision(
    ITEM *const item, const int16_t rot_y, const int32_t distance,
    int16_t *const ceiling)
{
    const XYZ_32 pos = {
        .x = item->pos.x + ((distance * Math_Sin(rot_y)) >> W2V_SHIFT),
        .y = item->pos.y - LARA_HEIGHT,
        .z = item->pos.z + ((distance * Math_Cos(rot_y)) >> W2V_SHIFT),
    };
    int16_t room_num = item->room_num;
    const SECTOR *const sector = Room_GetSector(pos.x, pos.y, pos.z, &room_num);
    const int16_t height = Room_GetHeight(sector, pos.x, pos.y, pos.z);
    *ceiling = Room_GetCeiling(sector, pos.x, pos.y, pos.z);

    return height == NO_HEIGHT ? NO_HEIGHT : (height - item->pos.y);
}

static int32_t M_GetHeight(ITEM *const item, const int32_t x, const int32_t z)
{
    const int32_t s = Math_Sin(item->rot.y);
    const int32_t c = Math_Cos(item->rot.y);
    const XYZ_32 pos = {
        .x = item->pos.x + ((x * c + z * s) >> W2V_SHIFT),
        .y = (item->pos.y + ((x * Math_Sin(item->rot.z)) >> W2V_SHIFT))
            - ((z * Math_Sin(item->rot.x)) >> W2V_SHIFT),
        .z = item->pos.z + ((z * c - x * s) >> W2V_SHIFT),
    };
    int16_t room_num = item->room_num;
    const SECTOR *const sector = Room_GetSector(pos.x, pos.y, pos.z, &room_num);
    return Room_GetHeight(sector, pos.x, pos.y, pos.z);
}

static void M_UserControl(ITEM *const item, MINECART_INFO *const cart_info)
{
    ITEM *const lara_item = Lara_GetItem();
    LARA_INFO *const lara = Lara_GetLaraInfo();

#define L_DUCK (0) // TODO: g_Input.duck

    switch (lara_item->current_anim_state) {
    case LS_MINECART_GET_ON: {
        if (Item_TestObjAnimEqual(
                lara_item, LA_MINECART_PREPARE_RIDE, O_LARA_MINECART)
            && Item_TestFrameEqual(lara_item, 20) && cart_info->flags == 0) {
            Lara_Mesh_SwapSingle(LM_HAND_R, O_LARA_MINECART);
            cart_info->flags |= 1;
        }
        break;
    }

    case LS_MINECART_STOP: {
        if (Item_TestObjAnimEqual(
                lara_item, LA_MINECART_PREPARE_DISMOUNT, O_LARA_MINECART)) {
            if (Item_TestFrameEqual(lara_item, 20)
                && (cart_info->flags & 1) != 0) {
                Lara_Mesh_SwapSingle(LM_HAND_R, O_LARA);
                cart_info->flags &= ~1;
            }

            if ((cart_info->flags & 8) != 0) {
                lara_item->goal_anim_state = LS_MINECART_GET_OFF_R;
            } else {
                lara_item->goal_anim_state = LS_MINECART_GET_OFF_L;
            }
        }
        break;
    }

    case LS_MINECART_GET_OFF_L:
    case LS_MINECART_GET_OFF_R: {
        if (Item_TestFrameEqual(lara_item, -1)) {
            XYZ_32 vec = {
                .x = 0,
                .y = 640,
                .z = 0,
            };
            // This isn't right, will return hand pos
            Lara_GetJointAbsPosition(&vec, LM_HIPS);
            lara_item->pos = vec;

            const int16_t rot =
                lara_item->current_anim_state == LS_MINECART_GET_OFF_L
                ? DEG_90
                : -DEG_90;
            lara_item->rot.y = item->rot.y + rot;

            Lara_Vehicle_Dismount();
            lara->gun_status = LGS_ARMLESS;
        }
        break;
    }

    case LS_MINECART_IDLE: {
        if ((cart_info->flags & 0x10) == 0) {
            Sound_Effect(SFX_MINE_CART_CLUNK_START, &item->pos, SPM_ALWAYS);
            cart_info->stop_delay = 64;
            cart_info->flags |= 0x10;
        }

        if (g_Input.roll && (cart_info->flags & 0x20) != 0) {
            if (g_Input.left && M_CheckGetOff(-1)) {
                lara_item->goal_anim_state = LS_MINECART_STOP;
                cart_info->flags &= ~8;
            } else if (g_Input.right && M_CheckGetOff(1)) {
                lara_item->goal_anim_state = LS_MINECART_STOP;
                cart_info->flags |= 8;
            }
        }
        if (L_DUCK) {
            lara_item->goal_anim_state = LS_MINECART_DUCK;
        } else if (cart_info->speed > 32) {
            lara_item->goal_anim_state = LS_MINECART_RIDE;
        }

        break;
    }

    case LS_MINECART_DUCK: {
        if (g_Input.action) {
            lara_item->goal_anim_state = LS_MINECART_SWIPE;
        } else if (g_Input.jump) {
            lara_item->goal_anim_state = LS_MINECART_BRAKE;
        } else if (!L_DUCK) {
            lara_item->goal_anim_state = LS_MINECART_IDLE;
        }
        break;
    }

    case LS_MINECART_RIDE: {
        if (g_Input.action) {
            lara_item->goal_anim_state = LS_MINECART_SWIPE;
        } else if (L_DUCK) {
            lara_item->goal_anim_state = LS_MINECART_DUCK;
        } else if (g_Input.jump) {
            lara_item->goal_anim_state = LS_MINECART_BRAKE;
        } else if (cart_info->speed == 32 || (cart_info->flags & 0x20) != 0) {
            lara_item->goal_anim_state = LS_MINECART_IDLE;
        } else if (cart_info->gradient < M_MIN_GRADIENT) {
            lara_item->goal_anim_state = LS_MINECART_TILT_FORWARD;
        } else if (cart_info->gradient > M_MAX_GRADIENT) {
            lara_item->goal_anim_state = LS_MINECART_TILT_BACK;
        } else if (g_Input.left) {
            lara_item->goal_anim_state = LS_MINECART_LEFT;
        } else if (g_Input.right) {
            lara_item->goal_anim_state = LS_MINECART_RIGHT;
        }
        break;
    }

    case LS_MINECART_RIGHT: {
        if (!g_Input.right) {
            lara_item->goal_anim_state = LS_MINECART_RIDE;
        } else if (g_Input.action) {
            lara_item->goal_anim_state = LS_MINECART_SWIPE;
        } else if (L_DUCK) {
            lara_item->goal_anim_state = LS_MINECART_DUCK;
        } else if (g_Input.jump) {
            lara_item->goal_anim_state = LS_MINECART_BRAKE;
        }
        break;
    }

    case LS_MINECART_LEFT: {
        if (!g_Input.left) {
            lara_item->goal_anim_state = LS_MINECART_RIDE;
        } else if (g_Input.action) {
            lara_item->goal_anim_state = LS_MINECART_SWIPE;
        } else if (L_DUCK) {
            lara_item->goal_anim_state = LS_MINECART_DUCK;
        } else if (g_Input.jump) {
            lara_item->goal_anim_state = LS_MINECART_BRAKE;
        }
        break;
    }

    case LS_MINECART_BRAKE:
        lara_item->goal_anim_state = LS_MINECART_BRAKING;
        break;

    case LS_MINECART_TILT_FORWARD:
    case LS_MINECART_TILT_BACK: {
        if (g_Input.action) {
            lara_item->goal_anim_state = LS_MINECART_SWIPE;
        } else if (L_DUCK) {
            lara_item->goal_anim_state = LS_MINECART_DUCK;
        } else if (g_Input.jump) {
            lara_item->goal_anim_state = LS_MINECART_BRAKE;
        } else {
            const bool forward =
                lara_item->current_anim_state == LS_MINECART_TILT_FORWARD;
            if ((forward && cart_info->gradient > M_MIN_GRADIENT)
                || (!forward && cart_info->gradient < M_MAX_GRADIENT)) {
                lara_item->goal_anim_state = LS_MINECART_RIDE;
            }
        }
        break;
    }

    case LS_MINECART_DEATH: {
        g_Camera.target_elevation = -8190;
        g_Camera.target_distance = WALL_L * 2;
        int16_t ceiling = 0;
        const int16_t height =
            M_GetCollision(item, item->rot.y, STEP_L * 2, &ceiling);

        if (height > -STEP_L && height < STEP_L) {
            // if (!(wibble & 7)) {
            Sound_Effect(SFX_QUAD_FRONT_IMPACT, &item->pos, SPM_ALWAYS);
            // }

            item->pos.x += (STEP_L / 2) * Math_Sin(item->rot.y) >> W2V_SHIFT;
            item->pos.z += (STEP_L / 2) * Math_Cos(item->rot.y) >> W2V_SHIFT;
        } else if (Item_TestFrameEqual(lara_item, 30)) {
            cart_info->flags |= 0x40;
            lara_item->hit_points = -1;
        }

        break;
    }

    case LS_MINECART_CRASH: {
        g_Camera.target_elevation = -4550;
        g_Camera.target_distance = 4096;
        break;
    }

    case LS_MINECART_BEAM_HIT: {
        if (lara_item->hit_points <= 0 && Item_TestFrameEqual(lara_item, 28)) {
            cart_info->flags = (cart_info->flags & ~0x50) | 0x40;
            cart_info->speed = 0;
            item->speed = 0;
        }
        break;
    }

    case LS_MINECART_SWIPE:
        lara_item->goal_anim_state = LS_MINECART_RIDE;
        break;

    case LS_MINECART_BRAKING: {
        if (L_DUCK) {
            lara_item->goal_anim_state = LS_MINECART_DUCK;
            Sound_StopEffect(SFX_MINE_CART_SREECH_BRAKE);
        } else if (!g_Input.jump || (cart_info->flags & 0x20) != 0) {
            lara_item->goal_anim_state = LS_MINECART_RIDE;
            Sound_StopEffect(SFX_MINE_CART_SREECH_BRAKE);
        } else {
            cart_info->speed = 1536;
            Sound_Effect(
                SFX_MINE_CART_SREECH_BRAKE, &lara_item->pos, SPM_ALWAYS);
        }
        break;
    }
    default:
        break;
    }

#undef L_DUCK

    if (Lara_Vehicle_IsMounted() && (cart_info->flags & 0x40) == 0) {
        Item_Animate(lara_item);
        const int16_t lara_anim_num =
            Item_GetRelativeObjAnim(lara_item, O_LARA_MINECART);
        const int16_t lara_frame_num = Item_GetRelativeFrame(lara_item);
        Item_SwitchToObjAnim(item, lara_anim_num, lara_frame_num, O_MINECART);
    }

    if (lara_item->current_anim_state == LS_MINECART_DEATH
        || lara_item->current_anim_state == LS_MINECART_CRASH
        || lara_item->hit_points <= 0) {
        return;
    }

    if (item->rot.z > M_MAX_ROLL || item->rot.z < M_MIN_ROLL) {
        Item_SwitchToObjAnim(
            lara_item, LA_MINECART_TOPPLE_START, 0, O_LARA_MINECART);
        lara_item->current_anim_state = LS_MINECART_DEATH;
        lara_item->goal_anim_state = LS_MINECART_DEATH;
        cart_info->flags = (cart_info->flags & 0x4F) | 0xA0;
        cart_info->speed = 0;
        item->speed = 0;
        return;
    }

    int16_t ceiling = 0;
    const int16_t height =
        M_GetCollision(item, item->rot.y, STEP_L * 2, &ceiling);

    if (height < -STEP_L * 2) {
        Item_SwitchToObjAnim(lara_item, LA_MINECART_CRASH, 0, O_LARA_MINECART);
        lara_item->current_anim_state = LS_MINECART_CRASH;
        lara_item->goal_anim_state = LS_MINECART_CRASH;
        cart_info->flags = (cart_info->flags & ~0xB0) | 0xA0;
        cart_info->speed = 0;
        item->speed = 0;
        lara_item->hit_points = -1;
        return;
    }

    if (lara_item->current_anim_state != LS_MINECART_DUCK
        && lara_item->current_anim_state != LS_MINECART_BEAM_HIT) {
        COLL_INFO coll = {
            .radius = 100,
            .quadrant = Math_GetDirection(item->rot.y),
        };
        if (Collide_CollideStaticObjects(
                &coll, item->pos.x, item->pos.y, item->pos.z, item->room_num,
                STEP_L * 3)) {
            Item_SwitchToObjAnim(
                lara_item, LA_MINECART_BEAM_HIT, 0, O_LARA_MINECART);
            lara_item->current_anim_state = LS_MINECART_BEAM_HIT;
            lara_item->goal_anim_state = LS_MINECART_BEAM_HIT;
            Spawn_BloodBath(
                lara_item->pos.x, lara_item->pos.y - STEP_L * 3,
                lara_item->pos.z, item->speed, item->rot.y, lara_item->room_num,
                3);

            int16_t damage = 25 * ((uint16_t)cart_info->speed >> 11);
            CLAMPL(damage, 20);
            Lara_TakeDamage(damage, false);
        }
    }

    if (height > 576 && cart_info->y_velocity == 0) {
        cart_info->y_velocity = -WALL_L;
    }

    M_ObjectCollision(item);
}

static void M_Move(ITEM *const item, MINECART_INFO *const cart_info)
{
    if (cart_info->stop_delay != 0) {
        cart_info->stop_delay--;
    }

    LARA_INFO *const lara = Lara_GetLaraInfo();
    if (lara->minecart_type == MINECART_STOP && cart_info->stop_delay == 0
        && ((item->pos.x & 0x380) == 512 || (item->pos.z & 0x380) == 512)) {
        if (cart_info->speed < 0xF000) {
            cart_info->flags |= 0x30;
            cart_info->speed = 0;
            item->speed = 0;
            return;
        }

        cart_info->stop_delay = 16;
    }

    if ((lara->minecart_type == MINECART_LEFT
         || lara->minecart_type == MINECART_RIGHT)
        && cart_info->stop_delay == 0 && (cart_info->flags & 6) == 0) {
        uint16_t rot = ((uint16_t)item->rot.y) >> W2V_SHIFT;
        if (lara->minecart_type == MINECART_LEFT) {
            rot = (1 << 2) | rot;
        }

        switch (rot) {
        case 0:
            cart_info->turn_x = (item->pos.x + M_TURN_SHIFT) & ~WALL_MASK;
            cart_info->turn_z = item->pos.z & ~WALL_MASK;
            break;
        case 1:
            cart_info->turn_x = item->pos.x & ~WALL_MASK;
            cart_info->turn_z = (item->pos.z - M_TURN_SHIFT) | WALL_MASK;
            break;
        case 2:
            cart_info->turn_x = (item->pos.x - M_TURN_SHIFT) | WALL_MASK;
            cart_info->turn_z = item->pos.z | WALL_MASK;
            break;
        case 3:
            cart_info->turn_x = item->pos.x | WALL_MASK;
            cart_info->turn_z = (item->pos.z + M_TURN_SHIFT) & ~WALL_MASK;
            break;
        case 4:
            cart_info->turn_x = (item->pos.x - M_TURN_SHIFT) | WALL_MASK;
            cart_info->turn_z = item->pos.z & ~WALL_MASK;
            break;
        case 5:
            cart_info->turn_x = item->pos.x & ~WALL_MASK;
            cart_info->turn_z = (item->pos.z + M_TURN_SHIFT) & ~WALL_MASK;
            break;
        case 6:
            cart_info->turn_x = (item->pos.x + M_TURN_SHIFT) & ~WALL_MASK;
            cart_info->turn_z = item->pos.z | WALL_MASK;
            break;
        case 7:
            cart_info->turn_x = item->pos.x | WALL_MASK;
            cart_info->turn_z = (item->pos.z - M_TURN_SHIFT) | WALL_MASK;
            break;
        default:
            break;
        }

        int16_t angle =
            Math_GetAngle(
                item->pos.x, item->pos.z, cart_info->turn_x, cart_info->turn_z)
            & 0x3FFF;
        if (rot >= 4 && angle != 0) {
            angle = DEG_90 - angle;
        }

        cart_info->turn_rot = item->rot.y;
        cart_info->turn_length = angle;
        cart_info->flags |= lara->minecart_type == MINECART_LEFT ? 2 : 4;
    }

    CLAMPL(cart_info->speed, 2560);
    cart_info->speed += -4 * cart_info->gradient;
    item->speed = (int16_t)(cart_info->speed >> 8);

    if (item->speed < 32) {
        item->speed = 32;
        Sound_StopEffect(SFX_MINE_CART_TRACK_LOOP);
        if (cart_info->y_velocity != 0) {
            Sound_StopEffect(SFX_MINE_CART_PULLY_LOOP);
        } else {
            Sound_Effect(SFX_MINE_CART_PULLY_LOOP, &item->pos, SPM_ALWAYS);
        }
    } else {
        Sound_StopEffect(SFX_MINE_CART_PULLY_LOOP);
        if (cart_info->y_velocity != 0) {
            Sound_StopEffect(SFX_MINE_CART_TRACK_LOOP);
        } else {
            Sound_Effect(
                SFX_MINE_CART_TRACK_LOOP, &item->pos,
                ((item->speed << 15) + 0x1000000) | SPM_PITCH | SPM_ALWAYS);
        }
    }

    if ((cart_info->flags & 6) != 0) {
        cart_info->turn_length += 3 * item->speed;
        if (cart_info->turn_length > 0x3FFC) {
            if ((cart_info->flags & 2) != 0) {
                item->rot.y = cart_info->turn_rot - DEG_90;
            } else {
                item->rot.y = cart_info->turn_rot + DEG_90;
            }
            cart_info->flags &= ~6;
        } else {
            if ((cart_info->flags & 2) != 0) {
                item->rot.y = cart_info->turn_rot - cart_info->turn_length;
            } else {
                item->rot.y = cart_info->turn_rot + cart_info->turn_length;
            }
        }

        if ((cart_info->flags & 6) != 0) {
            const uint16_t quadrant = (uint16_t)item->rot.y >> W2V_SHIFT;
            const int16_t angle = item->rot.y & 0x3FFF;

            int32_t x;
            int32_t z;
            switch (quadrant) {
            case DIR_NORTH:
                x = -Math_Cos(angle);
                z = Math_Sin(angle);
                break;
            case DIR_EAST:
                x = Math_Sin(angle);
                z = Math_Cos(angle);
                break;
            case DIR_SOUTH:
                x = Math_Cos(angle);
                z = -Math_Sin(angle);
                break;
            default:
                x = -Math_Sin(angle);
                z = -Math_Cos(angle);
                break;
            }

            if ((cart_info->flags & 2) != 0) {
                x = -x;
                z = -z;
            }

            item->pos.x = cart_info->turn_x + ((3584 * x) >> W2V_SHIFT);
            item->pos.z = cart_info->turn_z + ((3584 * z) >> W2V_SHIFT);
        }
    } else {
        item->pos.x += (item->speed * Math_Sin(item->rot.y)) >> W2V_SHIFT;
        item->pos.z += (item->speed * Math_Cos(item->rot.y)) >> W2V_SHIFT;
    }

    cart_info->mid_pos = M_GetHeight(item, 0, 0);

    if (cart_info->y_velocity == 0) {
        cart_info->front_pos = M_GetHeight(item, 0, STEP_L);
        cart_info->gradient =
            (int16_t)(cart_info->mid_pos - cart_info->front_pos);
        item->pos.y = cart_info->mid_pos;
    } else if (item->pos.y > cart_info->mid_pos) {
        if (cart_info->y_velocity > 0) {
            Sound_Effect(SFX_QUAD_FRONT_IMPACT, &item->pos, SPM_ALWAYS);
        }

        item->pos.y = cart_info->mid_pos;
        cart_info->y_velocity = 0;
    } else {
        cart_info->y_velocity += 1025;
        CLAMPG(cart_info->y_velocity, 0x3F00);
        item->pos.y += cart_info->y_velocity >> 8;
    }

    item->rot.x = cart_info->gradient << 5;

    if ((cart_info->flags & 6) != 0) {
        const int16_t angle = item->rot.y & 0x3FFF;
        if ((cart_info->flags & 4) != 0) {
            item->rot.z = -(item->speed * angle) >> 9;
        } else {
            item->rot.z = (item->speed * (DEG_90 - angle)) >> 9;
        }
    } else {
        item->rot.z = -item->rot.z >> 3;
    }
}

static void M_Setup(OBJECT *const obj)
{
    obj->initialise_func = M_Initialise;
    obj->collision_func = M_Collision;
    obj->save_position = true;
    obj->save_flags = true;
    obj->save_anim = true;
}

bool Minecart_Control(void)
{
    ITEM *const item = Lara_Vehicle_GetItem();
    MINECART_INFO *const cart_info = (MINECART_INFO *)item->data;
    M_UserControl(item, cart_info);

    if ((cart_info->flags & 0x10) != 0) {
        M_Move(item, cart_info);
    }

    LARA_INFO *const lara = Lara_GetLaraInfo();
    ITEM *const lara_item = Lara_GetItem();
    const bool mounted = Lara_Vehicle_IsMounted();
    if (mounted) {
        lara_item->pos = item->pos;
        lara_item->rot = item->rot;
    }

    int16_t room_num = item->room_num;
    const SECTOR *const sector =
        Room_GetSector(item->pos.x, item->pos.y, item->pos.z, &room_num);
    Item_UpdateRoom(lara->item_num, room_num);
    if (mounted) {
        Item_UpdateRoom(Lara_Vehicle_GetIndex(), room_num);
    }

    Room_TestTriggers(lara_item);

    if ((cart_info->flags & 0x80) == 0) {
        g_Camera.target_elevation = -8190;
        g_Camera.target_distance = 2048;
    }

    return mounted;
}

REGISTER_OBJECT(O_MINECART, M_Setup)
