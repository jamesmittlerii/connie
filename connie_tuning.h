/*****************************************************************************
 *
 *   connie_tuning.h
 *
 *   Keyboard range ("size of the instrument")
 *
 *****************************************************************************/
#ifndef CONNIE_TUNING_H
#define CONNIE_TUNING_H

// Lowest key: 24 = C1, 28 = E1, 36 = C2
#define LOWNOTE 24

// Number of octaves above LOWNOTE (original Connie used 5; 7 covers most MIDI keyboards)
#define OCTAVES 7

#define HIGHNOTE (LOWNOTE + 12 * OCTAVES)

#endif
