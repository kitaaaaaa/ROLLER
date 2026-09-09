#include "mecha_input.h"

#include "3d.h"
#include "rollerinput.h"

#include <SDL3/SDL.h>

//-------------------------------------------------------------------------------------------------

/* Stick travel below this is treated as centred; pads rest a long way from
 * zero and a mech that drifts on a released stick is unplayable. */
#define MECHA_STICK_DEADZONE 8000

/* Triggers rest at the bottom of their range, so halfway up is a press. */
#define MECHA_TRIGGER_THRESHOLD 0

//-------------------------------------------------------------------------------------------------

static int mecha_axis_to_percent(int iAxis)
{
  int iMagnitude;

  if (iAxis > -MECHA_STICK_DEADZONE && iAxis < MECHA_STICK_DEADZONE)
    return 0;

  /* Rescaled so the first movement past the dead zone is a small input
   * rather than a jump straight to a quarter deflection. */
  if (iAxis > 0) {
    iMagnitude = (iAxis - MECHA_STICK_DEADZONE) * 100
                 / (32767 - MECHA_STICK_DEADZONE);
    return iMagnitude > 100 ? 100 : iMagnitude;
  }
  iMagnitude = (-iAxis - MECHA_STICK_DEADZONE) * 100
               / (32767 - MECHA_STICK_DEADZONE);
  if (iMagnitude > 100)
    iMagnitude = 100;
  return -iMagnitude;
}

//-------------------------------------------------------------------------------------------------

static SDL_Gamepad *mecha_first_gamepad(void)
{
  int iCount = InputGetDeviceCount();
  int i;

  for (i = 0; i < iCount; i++) {
    const tInputDevice *pDevice = InputGetDevice(i);

    /* Only a pad SDL actually recognises: the raw joystick axis order on an
     * unmapped stick has nothing to do with the twin-stick layout below. */
    if (pDevice && pDevice->bGamepad && pDevice->pGamepad)
      return pDevice->pGamepad;
  }
  return NULL;
}

//-------------------------------------------------------------------------------------------------

static int mecha_key(int iScancode)
{
  return keys[iScancode] ? 1 : 0;
}

//-------------------------------------------------------------------------------------------------

void mecha_input_poll(tMechaInput *pInput)
{
  SDL_Gamepad *pPad;
  int iMoveX = 0;
  int iMoveZ = 0;
  int iTurn = 0;
  bool bTriggerLeft = false;
  bool bTriggerRight = false;

  if (!pInput)
    return;
  SDL_memset(pInput, 0, sizeof(*pInput));

  /* --- keyboard ---------------------------------------------------------- */

  iMoveX += 100 * (mecha_key(WHIP_SCANCODE_D) - mecha_key(WHIP_SCANCODE_A));
  iMoveZ += 100 * (mecha_key(WHIP_SCANCODE_W) - mecha_key(WHIP_SCANCODE_S));
  iTurn  += 100 * (mecha_key(WHIP_SCANCODE_E) - mecha_key(WHIP_SCANCODE_Q));

  pInput->bDash        = mecha_key(WHIP_SCANCODE_LSHIFT) != 0;
  pInput->bJump        = mecha_key(WHIP_SCANCODE_SPACE) != 0;
  pInput->bGuard      = mecha_key(WHIP_SCANCODE_LCTRL) != 0
                      || mecha_key(WHIP_SCANCODE_C) != 0;
  pInput->bFireLeft    = mecha_key(WHIP_SCANCODE_J) != 0;
  pInput->bFireCenter  = mecha_key(WHIP_SCANCODE_K) != 0;
  pInput->bFireRight   = mecha_key(WHIP_SCANCODE_L) != 0;
  pInput->bCycleTarget = mecha_key(WHIP_SCANCODE_TAB) != 0;

  /* --- pad --------------------------------------------------------------- */

  pPad = mecha_first_gamepad();
  if (pPad) {
    int iPadX = mecha_axis_to_percent(
        SDL_GetGamepadAxis(pPad, SDL_GAMEPAD_AXIS_LEFTX));
    /* SDL's stick Y grows downward; forward on the stick is forward on the
     * mech. */
    int iPadZ = -mecha_axis_to_percent(
        SDL_GetGamepadAxis(pPad, SDL_GAMEPAD_AXIS_LEFTY));
    int iPadTurn = mecha_axis_to_percent(
        SDL_GetGamepadAxis(pPad, SDL_GAMEPAD_AXIS_RIGHTX));

    if (iPadX != 0) iMoveX = iPadX;
    if (iPadZ != 0) iMoveZ = iPadZ;
    if (iPadTurn != 0) iTurn = iPadTurn;

    bTriggerLeft = SDL_GetGamepadAxis(pPad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
                   > MECHA_TRIGGER_THRESHOLD;
    bTriggerRight = SDL_GetGamepadAxis(pPad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
                    > MECHA_TRIGGER_THRESHOLD;

    if (SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_SOUTH))
      pInput->bJump = true;
    if (SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_EAST)
        || SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))
      pInput->bDash = true;
    if (SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_WEST))
      pInput->bGuard = true;
    if (SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_NORTH)
        || SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))
      pInput->bCycleTarget = true;

    /* Both triggers together is the centre weapon, and neither of the outer
     * ones -- otherwise pulling both would fire all three at once and the
     * centre attack could never be reached on a pad. */
    if (bTriggerLeft && bTriggerRight) {
      pInput->bFireCenter = true;
    } else {
      if (bTriggerLeft)
        pInput->bFireLeft = true;
      if (bTriggerRight)
        pInput->bFireRight = true;
    }
  }

  pInput->iMoveX = mecha_clampi(iMoveX, -100, 100);
  pInput->iMoveZ = mecha_clampi(iMoveZ, -100, 100);
  pInput->iTurn = mecha_clampi(iTurn, -100, 100);
}

//-------------------------------------------------------------------------------------------------

bool mecha_input_quit_pressed(void)
{
  return mecha_key(WHIP_SCANCODE_ESCAPE) != 0;
}

//-------------------------------------------------------------------------------------------------

void mecha_input_poll_menu(tMechaMenuInput *pMenu)
{
  SDL_Gamepad *pPad;

  if (!pMenu)
    return;
  SDL_memset(pMenu, 0, sizeof(*pMenu));

  pMenu->bUp      = mecha_key(WHIP_SCANCODE_UP) || mecha_key(WHIP_SCANCODE_W);
  pMenu->bDown    = mecha_key(WHIP_SCANCODE_DOWN) || mecha_key(WHIP_SCANCODE_S);
  pMenu->bLeft    = mecha_key(WHIP_SCANCODE_LEFT) || mecha_key(WHIP_SCANCODE_A);
  pMenu->bRight   = mecha_key(WHIP_SCANCODE_RIGHT) || mecha_key(WHIP_SCANCODE_D);
  pMenu->bConfirm = mecha_key(WHIP_SCANCODE_RETURN)
                 || mecha_key(WHIP_SCANCODE_SPACE);
  pMenu->bBack    = mecha_key(WHIP_SCANCODE_ESCAPE) != 0;

  pPad = mecha_first_gamepad();
  if (pPad) {
    /* The stick counts as well as the pad, at a deflection well past the
     * dead zone so a resting stick never walks the selection. */
    int iPadX = mecha_axis_to_percent(
        SDL_GetGamepadAxis(pPad, SDL_GAMEPAD_AXIS_LEFTX));
    int iPadY = mecha_axis_to_percent(
        SDL_GetGamepadAxis(pPad, SDL_GAMEPAD_AXIS_LEFTY));

    if (SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_DPAD_UP) || iPadY < -55)
      pMenu->bUp = true;
    if (SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || iPadY > 55)
      pMenu->bDown = true;
    if (SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || iPadX < -55)
      pMenu->bLeft = true;
    if (SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || iPadX > 55)
      pMenu->bRight = true;
    if (SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_SOUTH)
        || SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_START))
      pMenu->bConfirm = true;
    if (SDL_GetGamepadButton(pPad, SDL_GAMEPAD_BUTTON_EAST))
      pMenu->bBack = true;
  }
}
