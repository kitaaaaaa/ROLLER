#ifndef _ROLLER_MECHA_INPUT_H
#define _ROLLER_MECHA_INPUT_H
//-------------------------------------------------------------------------------------------------
/*
 * Turns the keyboard and the first attached pad into one tMechaInput.
 *
 * The arcade lineage this mode borrows from is played on two sticks with a
 * trigger on each, and the pad mapping keeps that shape: the left stick
 * walks and strafes, the right stick turns, and the two triggers fire the
 * left and right weapons -- with both at once firing the centre one. The
 * keyboard offers the same thing on separate keys for anyone without a pad,
 * and the two are additive, so either works at any time.
 */
//-------------------------------------------------------------------------------------------------
#include "mecha_types.h"
//-------------------------------------------------------------------------------------------------

/* Fills *pInput from the current keyboard state and the first gamepad. */
void mecha_input_poll(tMechaInput *pInput);

/* True while the player is asking to leave the arena. Edge detection is the
 * caller's business. */
bool mecha_input_quit_pressed(void);

//-------------------------------------------------------------------------------------------------

/*
 * The briefing screen's input, on the same two devices. Every field is the
 * held state rather than a press, so the caller decides what counts as a
 * repeat -- a menu that stepped once per polled frame would be unusable at
 * sixty hertz.
 */
typedef struct
{
  bool bUp;
  bool bDown;
  bool bLeft;
  bool bRight;
  bool bConfirm;
  bool bBack;
} tMechaMenuInput;

void mecha_input_poll_menu(tMechaMenuInput *pMenu);

//-------------------------------------------------------------------------------------------------
#endif
