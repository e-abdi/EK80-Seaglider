/*
 * Glider command set. Lines are trimmed,
 * then matched by prefix in this order:
 *
 *   1SRTC...   dive start: power the EK80, forward "SRTC..." once it prompts
 *   1STOP      dive stop: end the ensemble (or reset if not running)
 *   2STOP      climb stop: answer "CMD:" (2SRTC... is accepted and ignored)
 *   VOLTAGE    "<ping_ok>,<start_cv>,<end_cv>" then "CMD:"
 *   live on    echo EK80 lines to the glider
 *   live off
 *   v...       pack voltage now (centivolts)
 *   r...       reset: relay to charger, clear all state
 */
#pragma once

#include <stdint.h>

void glider_on_line(char *line, int64_t now_ms);
