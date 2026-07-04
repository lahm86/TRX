#pragma once

#include <trx/game/camera/types.h>

void Camera_FlybyMode_Initialise(int32_t num_cameras);
void Camera_FlybyMode_SetupSequences(void);
FLYBY_CAMERA *Camera_FlybyMode_GetCamera(int32_t camera_idx);

void Camera_FlybyMode_Activate(int32_t sequence, bool one_shot);
void Camera_FlybyMode_Deactivate(void);
bool Camera_FlybyMode_IsActive(void);

void Camera_FlybyMode_Enter(void);
void Camera_FlybyMode_Exit(void);
void Camera_FlybyMode_Update(void);
