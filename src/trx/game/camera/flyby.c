#include <trx/game/camera/flyby.h>

#include <trx/game/camera.h>
#include <trx/game/game_buf.h>
#include <trx/game/input.h>
#include <trx/game/lara.h>
#include <trx/game/rooms.h>
#include <trx/game/viewport.h>

#include <stdlib.h>

#define M_NO_SEQUENCE (-1)
#define M_SPLINE_ONE (1 << 16)

typedef struct {
    int32_t num_cameras;
    int32_t first_camera;
} M_SEQUENCE;

static int32_t m_CameraCount = 0;
static int32_t m_SequenceCount = 0;
static FLYBY_CAMERA *m_Cameras = nullptr;
static M_SEQUENCE *m_Sequences = nullptr;
static int32_t m_CurrentSequence = M_NO_SEQUENCE;
static ITEM m_TriggerItem = {
    .object_id = O_CAMERA_TARGET,
};

static struct {
    struct {
        GAME_VECTOR camera_pos;
        GAME_VECTOR camera_target;
        int16_t fov;
        FOV_MODE fov_mode;
    } initial;
    struct {
        int32_t camera_idx;
        int32_t spline_pos;
    } current;
    struct {
        struct {
            int32_t x[18];
            int32_t y[18];
            int32_t z[18];
        } pos, target;
        int32_t roll[18];
        int32_t speed[18];
        int32_t fov[18];
    } data;
    bool spline_from_game;
    bool spline_to_game;
    bool test_triggers;
    bool pending_trigger_check;
    int16_t timer;
} m_State = {};

static int32_t M_CompareCameras(const void *const a, const void *const b)
{
    const FLYBY_CAMERA *const camera_a = (FLYBY_CAMERA *)a;
    const FLYBY_CAMERA *const camera_b = (FLYBY_CAMERA *)b;
    if (camera_a->sequence == camera_b->sequence) {
        return camera_a->index - camera_b->index;
    }
    return camera_a->sequence - camera_b->sequence;
}

static int32_t M_GetLastCamera(const M_SEQUENCE *const sequence)
{
    return sequence->first_camera + sequence->num_cameras - 1;
}

static void M_SetSplineData(const int32_t slot, const int32_t camera_idx)
{
    const FLYBY_CAMERA *const camera = &m_Cameras[camera_idx];
    m_State.data.pos.x[slot] = camera->pos.x;
    m_State.data.pos.y[slot] = camera->pos.y;
    m_State.data.pos.z[slot] = camera->pos.z;

    if (camera->flags.focus_lara) {
        const ITEM *const lara_item = Lara_GetItem();
        m_State.data.target.x[slot] = lara_item->pos.x;
        m_State.data.target.y[slot] = lara_item->pos.y;
        m_State.data.target.z[slot] = lara_item->pos.z;
    } else {
        m_State.data.target.x[slot] = camera->target.x;
        m_State.data.target.y[slot] = camera->target.y;
        m_State.data.target.z[slot] = camera->target.z;
    }

    if (camera->flags.target_item) {
        const ITEM *const item = Item_Get(camera->timer);
        if (item != nullptr) {
            m_State.data.target.x[slot] = item->pos.x;
            m_State.data.target.y[slot] = item->pos.y;
            m_State.data.target.z[slot] = item->pos.z;
        }
    }

    m_State.data.roll[slot] = camera->roll;
    m_State.data.speed[slot] = camera->speed;
    m_State.data.fov[slot] = camera->fov;
}

static int32_t M_Spline(
    int32_t t, const int32_t *const knots, const int32_t knot_count)
{
    int32_t segment = t * (knot_count - 3) >> 16;
    if (segment >= knot_count - 3) {
        segment = knot_count - 4;
    }

    const int32_t *const control = &knots[segment];
    t = t * (knot_count - 3) - segment * M_SPLINE_ONE;

    const int32_t cubic = (control[1] >> 1) - (control[2] >> 1) - control[2]
        + control[1] + (control[3] >> 1) + ((-control[0] - 1) >> 1);
    const int32_t quadratic = 2 * control[2] - 2 * control[1]
        - (control[1] >> 1) - (control[3] >> 1) + control[0];

    const int64_t t64 = t;
    const int64_t poly = t64
        * ((t64 * ((t64 * cubic >> 16) + quadratic) >> 16) + (control[2] >> 1)
           + ((-control[0] - 1) >> 1));

    return (poly >> 16) + control[1];
}

static int32_t M_GetNearestSplinePosition(
    const ITEM *const item, const int32_t spline_count)
{
    int32_t best_pos = 0;
    int32_t sample_pos = 0;
    int32_t step = 0x2000;

    for (int32_t i = 0; i < 8; i++) {
        int32_t best_distance = INT32_MAX;
        for (int32_t j = 0; j < 8; j++) {
            const XYZ_32 cpos = {
                .x = M_Spline(sample_pos, m_State.data.pos.x, spline_count)
                    - item->pos.x,
                .y = M_Spline(sample_pos, m_State.data.pos.y, spline_count)
                    - item->pos.y,
                .z = M_Spline(sample_pos, m_State.data.pos.z, spline_count)
                    - item->pos.z,
            };
            const int32_t distance = XYZ_32_GetLength(cpos);
            if (distance <= best_distance) {
                best_pos = sample_pos;
                best_distance = distance;
            }

            sample_pos += step;
            if (sample_pos > M_SPLINE_ONE) {
                break;
            }
        }

        step >>= 1;
        sample_pos = MAX(best_pos - (step << 1), 0);
    }

    CLAMP(best_pos, 0, M_SPLINE_ONE);
    return best_pos;
}

static void M_PrepareSplineToGame(void)
{
    m_State.spline_to_game = true;
    m_State.current.camera_idx--;
    M_SetSplineData(0, m_State.current.camera_idx - 1);
    M_SetSplineData(1, m_State.current.camera_idx);

    CAMERA_INFO cam_info = g_Camera;
    g_Camera.type = CAM_CHASE;
    g_Camera.speed = 1;
    Camera_Update();

    m_State.initial.camera_pos = g_Camera.pos;
    m_State.initial.camera_target = g_Camera.target;
    m_State.data.pos.x[2] = g_Camera.pos.x;
    m_State.data.pos.y[2] = g_Camera.pos.y;
    m_State.data.pos.z[2] = g_Camera.pos.z;
    m_State.data.target.x[2] = g_Camera.target.x;
    m_State.data.target.y[2] = g_Camera.target.y;
    m_State.data.target.z[2] = g_Camera.target.z;
    m_State.data.fov[2] = Viewport_GetEffectiveFOV();
    m_State.data.roll[2] = 0;
    m_State.data.speed[2] = m_State.data.speed[1];
    m_State.data.pos.x[3] = g_Camera.pos.x;
    m_State.data.pos.y[3] = g_Camera.pos.y;
    m_State.data.pos.z[3] = g_Camera.pos.z;
    m_State.data.target.x[3] = g_Camera.target.x;
    m_State.data.target.y[3] = g_Camera.target.y;
    m_State.data.target.z[3] = g_Camera.target.z;
    m_State.data.speed[3] = m_State.data.speed[1] >> 1;
    m_State.data.fov[3] = m_State.data.fov[2];

    g_Camera = cam_info;
}

static void M_TestTriggers(void)
{
    if (!m_State.test_triggers) {
        return;
    }

    // TODO: if current level == 0 (title?), test non-heavy too
    m_TriggerItem.pos = g_Camera.pos.pos;
    m_TriggerItem.room_num = g_Camera.pos.room_num;
    Room_TestTriggers(&m_TriggerItem);
    m_State.test_triggers = false;
}

void Camera_FlybyMode_Initialise(const int32_t num_cameras)
{
    if (num_cameras <= 0) {
        m_CameraCount = 0;
        m_Cameras = nullptr;
        return;
    }

    m_CameraCount = num_cameras;
    m_Cameras =
        GameBuf_Alloc(m_CameraCount * sizeof(FLYBY_CAMERA), GBUF_CAMERAS);
}

void Camera_FlybyMode_SetupSequences(void)
{
    m_Sequences = nullptr;
    m_SequenceCount = 0;
    m_CurrentSequence = M_NO_SEQUENCE;

    if (m_CameraCount == 0) {
        return;
    }

    qsort(m_Cameras, m_CameraCount, sizeof(FLYBY_CAMERA), M_CompareCameras);

    const FLYBY_CAMERA *const last_camera = &m_Cameras[m_CameraCount - 1];
    m_SequenceCount = last_camera->sequence + 1;
    m_Sequences =
        GameBuf_Alloc(m_SequenceCount * sizeof(M_SEQUENCE), GBUF_CAMERAS);

    for (int32_t i = 0; i < m_SequenceCount; i++) {
        m_Sequences[i].first_camera = NO_CAMERA;
    }

    for (int32_t i = 0; i < m_CameraCount; i++) {
        const FLYBY_CAMERA *const camera = Camera_FlybyMode_GetCamera(i);
        M_SEQUENCE *const sequence = &m_Sequences[camera->sequence];
        sequence->num_cameras++;
        if (sequence->first_camera == NO_CAMERA) {
            sequence->first_camera = i;
        }
    }
}

FLYBY_CAMERA *Camera_FlybyMode_GetCamera(const int32_t camera_idx)
{
    if (camera_idx < 0 || camera_idx >= m_CameraCount) {
        return nullptr;
    }
    return &m_Cameras[camera_idx];
}

void Camera_FlybyMode_Activate(const int32_t sequence_idx, const bool one_shot)
{
    if (sequence_idx < 0 || sequence_idx > m_SequenceCount) {
        return;
    }

    if (sequence_idx == m_CurrentSequence) {
        m_State.pending_trigger_check = false;
        return;
    } else if (m_CurrentSequence != M_NO_SEQUENCE) {
        return;
    }

    const M_SEQUENCE *const sequence = &m_Sequences[sequence_idx];
    if (sequence->first_camera == NO_CAMERA) {
        return;
    }

    FLYBY_CAMERA *const camera = &m_Cameras[sequence->first_camera];
    if (camera->flags.one_shot) {
        return;
    }

    m_CurrentSequence = sequence_idx;
    if (one_shot) {
        camera->flags.one_shot = true;
    }
}

void Camera_FlybyMode_Deactivate(void)
{
    m_CurrentSequence = M_NO_SEQUENCE;
    // TODO: undo fade clip
}

bool Camera_FlybyMode_IsActive(void)
{
    return m_CurrentSequence != M_NO_SEQUENCE;
}

void Camera_FlybyMode_Enter(void)
{
    g_Camera.type = CAM_FLYBY_MODE;
    m_State.initial.camera_pos = g_Camera.pos;
    m_State.initial.camera_target = g_Camera.target;
    m_State.initial.fov = Viewport_GetEffectiveFOV();
    m_State.initial.fov_mode = Viewport_GetFOVMode();
    m_State.spline_from_game = false;
    m_State.spline_to_game = false;
    m_State.test_triggers = false;
    m_State.pending_trigger_check = false;
    m_State.timer = 0;

    const M_SEQUENCE *const sequence = &m_Sequences[m_CurrentSequence];
    const FLYBY_CAMERA *const camera = &m_Cameras[sequence->first_camera];
    const int32_t last_camera_idx = M_GetLastCamera(sequence);

    m_State.current.camera_idx = sequence->first_camera;
    m_State.current.spline_pos = 0;
    if (camera->flags.lara_control_off) {
        Lara_SetControllable(false);
    }

    if (camera->flags.track_path) {
        int32_t slot = 0;
        int32_t camera_idx = sequence->first_camera;
        M_SetSplineData(slot, camera_idx);
        slot++;

        for (int32_t i = 0; i < sequence->num_cameras; i++) {
            M_SetSplineData(slot, camera_idx);
            slot++;
            camera_idx++;
        }

        M_SetSplineData(slot, last_camera_idx);
        m_State.current.spline_pos = M_GetNearestSplinePosition(
            Lara_GetItem(), sequence->num_cameras + 2);
    } else if (camera->flags.snap_from_game) {
        int32_t slot = 0;
        int32_t camera_idx = sequence->first_camera;
        M_SetSplineData(slot, camera_idx);
        slot++;

        while (slot < 4) {
            if (camera_idx > last_camera_idx) {
                camera_idx = sequence->first_camera;
            }

            M_SetSplineData(slot, camera_idx);
            slot++;
            camera_idx++;
        }

        m_State.current.camera_idx++;
        if (m_State.current.camera_idx > last_camera_idx) {
            m_State.current.camera_idx = sequence->first_camera;
        }

        if (camera->flags.test_triggers) {
            m_State.test_triggers = true;
        }
    } else {
        m_State.spline_from_game = true;

        m_State.data.pos.x[0] = m_State.initial.camera_pos.x;
        m_State.data.pos.y[0] = m_State.initial.camera_pos.y;
        m_State.data.pos.z[0] = m_State.initial.camera_pos.z;
        m_State.data.target.x[0] = m_State.initial.camera_target.x;
        m_State.data.target.y[0] = m_State.initial.camera_target.y;
        m_State.data.target.z[0] = m_State.initial.camera_target.z;
        m_State.data.roll[0] = 0;
        m_State.data.fov[0] = m_State.initial.fov;
        m_State.data.speed[0] = camera->speed; // missing in OG
        m_State.data.pos.x[1] = m_State.data.pos.x[0];
        m_State.data.pos.y[1] = m_State.data.pos.y[0];
        m_State.data.pos.z[1] = m_State.data.pos.z[0];
        m_State.data.target.x[1] = m_State.data.target.x[0];
        m_State.data.target.y[1] = m_State.data.target.y[0];
        m_State.data.target.z[1] = m_State.data.target.z[0];
        m_State.data.roll[1] = m_State.data.roll[0];
        m_State.data.fov[1] = m_State.data.fov[0];
        m_State.data.speed[1] = m_State.data.speed[0];

        int32_t camera_idx = sequence->first_camera;
        M_SetSplineData(2, camera_idx);
        camera_idx++;
        if (camera_idx > last_camera_idx) {
            camera_idx = sequence->first_camera;
        }
        M_SetSplineData(3, camera_idx);
    }
}

void Camera_FlybyMode_Exit(void)
{
    m_CurrentSequence = M_NO_SEQUENCE;
    Lara_SetControllable(true);

    g_Camera.type = CAM_CHASE;
    g_Camera.roll = 0;
    Viewport_AlterFOV(m_State.initial.fov, m_State.initial.fov_mode);
    if (!m_State.spline_to_game) {
        g_Camera.speed = 1;
        g_Camera.pos = m_State.initial.camera_pos;
        g_Camera.target = m_State.initial.camera_target;
        g_Camera.interp.prev.pos = g_Camera.pos.pos;
        g_Camera.interp.prev.target = g_Camera.target.pos;
    }
}

void Camera_FlybyMode_Update(void)
{
    if (!Camera_FlybyMode_IsActive()) {
        return;
    }

    if (m_State.pending_trigger_check) {
        // Lara's no longer on a trigger for a track_path
        g_Camera.speed = Camera_GetChaseSpeed();
        m_State.spline_to_game = true;
        Camera_FlybyMode_Deactivate();
        return;
    }

    const M_SEQUENCE *const sequence = &m_Sequences[m_CurrentSequence];
    const FLYBY_CAMERA *const first_camera = &m_Cameras[sequence->first_camera];
    const FLYBY_CAMERA *const current_camera =
        &m_Cameras[m_State.current.camera_idx];
    const int32_t spline_count =
        first_camera->flags.track_path ? sequence->num_cameras + 2 : 4;
    const ITEM *const lara_item = Lara_GetItem();

    const XYZ_32 pos = {
        .x = M_Spline(
            m_State.current.spline_pos, m_State.data.pos.x, spline_count),
        .y = M_Spline(
            m_State.current.spline_pos, m_State.data.pos.y, spline_count),
        .z = M_Spline(
            m_State.current.spline_pos, m_State.data.pos.z, spline_count),
    };
    const XYZ_32 target = {
        .x = M_Spline(
            m_State.current.spline_pos, m_State.data.target.x, spline_count),
        .y = M_Spline(
            m_State.current.spline_pos, m_State.data.target.y, spline_count),
        .z = M_Spline(
            m_State.current.spline_pos, m_State.data.target.z, spline_count),
    };

    const int32_t speed =
        M_Spline(m_State.current.spline_pos, m_State.data.speed, spline_count);
    const int32_t roll =
        M_Spline(m_State.current.spline_pos, m_State.data.roll, spline_count);
    const int32_t fov =
        M_Spline(m_State.current.spline_pos, m_State.data.fov, spline_count);

    // TODO: handle fade_in_screen and fade_out_screen

    if (first_camera->flags.track_path) {
        const int32_t cp = M_GetNearestSplinePosition(lara_item, spline_count);
        m_State.current.spline_pos += (cp - m_State.current.spline_pos) >> 5;
        if (first_camera->flags.snap_to_game
            && ABS(cp - m_State.current.spline_pos) > 0x8000) {
            m_State.current.spline_pos = cp;
        }
    } else if (m_State.timer == 0) {
        m_State.current.spline_pos += speed;
    }

    // TODO: make optional
    if (g_InputDB.menu_back && !first_camera->flags.track_path) {
        m_State.current.spline_pos = M_SPLINE_ONE;
    }

    if (!first_camera->flags.no_break && g_Input.look) {
        Camera_FlybyMode_Deactivate();
        return;
    }

    g_Camera.pos.pos = pos;

    if (first_camera->flags.target_lara || first_camera->flags.track_path) {
        g_Camera.target.pos = lara_item->pos;
    } else {
        g_Camera.target.pos = target;
    }

    if (current_camera->flags.target_item) {
        const ITEM *const item = Item_Get(current_camera->timer);
        if (item != nullptr) {
            g_Camera.target.pos = item->pos;
        }
    }

    g_Camera.pos.room_num = current_camera->room_num;
    Room_GetSector(g_Camera.pos.pos, &g_Camera.pos.room_num);
    g_Camera.target.room_num = g_Camera.pos.room_num;
    Room_GetSector(g_Camera.target.pos, &g_Camera.target.room_num);
    g_Camera.roll = roll;
    Viewport_AlterFOV(fov, FOV_MODE_GAME);

    M_TestTriggers();

    if (!first_camera->flags.track_path
        && m_State.current.spline_pos > M_SPLINE_ONE - speed) {
        if (current_camera->flags.test_triggers) {
            m_State.test_triggers = true;
        }

        if (current_camera->flags.hold) {
            if (m_State.timer != 0) {
                m_State.timer--;
            } else {
                m_State.timer = current_camera->timer >> 4;
            }
        }

        if (m_State.timer == 0) {
            m_State.current.spline_pos = 0;

            int32_t next_camera = 0;
            if (m_State.current.camera_idx == sequence->first_camera) {
                next_camera = M_GetLastCamera(sequence);
            } else {
                next_camera = m_State.current.camera_idx - 1;
            }

            int32_t slot = 1;
            if (m_State.spline_from_game) {
                m_State.spline_from_game = false;
                next_camera = sequence->first_camera - 1;
            } else {
                if (current_camera->flags.lara_control_on) {
                    Lara_SetControllable(true);
                }
                if (current_camera->flags.lara_control_off) {
                    Lara_SetControllable(false);
                    // TODO: fade clip
                }

                slot = 0;
                if (current_camera->flags.jump_to_camera) {
                    next_camera =
                        sequence->first_camera + (current_camera->timer & 0xF);
                    m_State.current.camera_idx = next_camera;
                    M_SetSplineData(slot, next_camera);
                    slot = 1;
                }

                M_SetSplineData(slot, next_camera);
                slot++;
            }

            next_camera++;

            while (slot < 4) {
                if (first_camera->flags.loop) {
                    if (next_camera > M_GetLastCamera(sequence)) {
                        next_camera = sequence->first_camera;
                    }
                } else if (next_camera > M_GetLastCamera(sequence)) {
                    next_camera = M_GetLastCamera(sequence);
                }

                M_SetSplineData(slot, next_camera);
                next_camera++;
                slot++;
            }

            m_State.current.camera_idx++;

            if (m_State.current.camera_idx > M_GetLastCamera(sequence)) {
                if (current_camera->flags.loop) {
                    m_State.current.camera_idx = sequence->first_camera;
                } else if (
                    first_camera->flags.snap_to_game
                    || m_State.spline_to_game) {
                    M_TestTriggers();
                    Camera_FlybyMode_Deactivate();
                } else {
                    M_PrepareSplineToGame();
                }
            }
        }
    }

    if (first_camera->flags.track_path) {
        // Switch off until the next control run tests if Lara is still on a
        // valid trigger.
        m_State.pending_trigger_check = true;
    }
}
