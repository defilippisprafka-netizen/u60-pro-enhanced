#ifndef U60_PANEL_POWER_H
#define U60_PANEL_POWER_H

#include <stdint.h>

#define PANEL_POWER_DOUBLE_MS UINT64_C(450)
#define PANEL_POWER_LONG_MS   UINT64_C(1200)

enum panel_power_action {
	PANEL_POWER_NONE = 0,
	PANEL_POWER_SINGLE,
	PANEL_POWER_DOUBLE,
	PANEL_POWER_LONG
};

struct panel_power_state {
	uint64_t last_ms;
	uint64_t down_ms;
	uint64_t first_up_ms;
	unsigned int have_time : 1;
	unsigned int held : 1;
	unsigned int waiting_second : 1;
	unsigned int second_down : 1;
	unsigned int long_fired : 1;
};

/* Input events interrupt poll immediately. Short ticks are only necessary
 * while resolving a click or a held button, not throughout idle standby. */
static inline int panel_power_poll_ms(const struct panel_power_state *state)
{
 return state->waiting_second || (state->held && !state->long_fired) ? 20 : 1000;
}

static inline void panel_power_init(struct panel_power_state *state)
{
	state->last_ms = 0;
	state->down_ms = 0;
	state->first_up_ms = 0;
	state->have_time = 0;
	state->held = 0;
	state->waiting_second = 0;
	state->second_down = 0;
	state->long_fired = 0;
}

/* A backwards timestamp means the event stream is stale or changed clocks. */
static inline int panel_power_time_ok(struct panel_power_state *state,
	uint64_t now_ms)
{
	if (state->have_time && now_ms < state->last_ms) {
		panel_power_init(state);
		return 0;
	}
	state->have_time = 1;
	state->last_ms = now_ms;
	return 1;
}

static inline enum panel_power_action panel_power_due(
	struct panel_power_state *state, uint64_t now_ms)
{
	if (state->held && !state->long_fired &&
	    now_ms - state->down_ms >= PANEL_POWER_LONG_MS) {
		state->long_fired = 1;
		state->waiting_second = 0;
		state->second_down = 0;
		return PANEL_POWER_LONG;
	}
	if (!state->held && state->waiting_second &&
	    now_ms - state->first_up_ms >= PANEL_POWER_DOUBLE_MS) {
		state->waiting_second = 0;
		return PANEL_POWER_SINGLE;
	}
	return PANEL_POWER_NONE;
}

/* value follows Linux key semantics: 0=up, 1=down, 2=repeat. */
static inline enum panel_power_action panel_power_event(
	struct panel_power_state *state, int value, uint64_t now_ms)
{
	enum panel_power_action due;
	if (!panel_power_time_ok(state, now_ms))
		return PANEL_POWER_NONE;
	due = panel_power_due(state, now_ms);

	if (value == 2)
		return due;
	if (value != 0 && value != 1) {
		panel_power_init(state);
		return due;
	}
	if (value == 1) {
		if (!state->held) {
			state->held = 1;
			state->down_ms = now_ms;
			state->long_fired = 0;
			state->second_down = state->waiting_second &&
				(now_ms - state->first_up_ms < PANEL_POWER_DOUBLE_MS);
			if (!state->second_down)
				state->waiting_second = 0;
		}
		return due;
	}

	if (!state->held)
		return due;
	state->held = 0;
	if (state->long_fired) {
		state->long_fired = 0;
		state->second_down = 0;
		state->waiting_second = 0;
		return due;
	}
	if (state->second_down) {
		state->second_down = 0;
		state->waiting_second = 0;
		return due == PANEL_POWER_NONE ? PANEL_POWER_DOUBLE : due;
	}
	state->first_up_ms = now_ms;
	state->waiting_second = 1;
	return due;
}

static inline enum panel_power_action panel_power_tick(
	struct panel_power_state *state, uint64_t now_ms)
{
	if (!panel_power_time_ok(state, now_ms))
		return PANEL_POWER_NONE;
	return panel_power_due(state, now_ms);
}

#endif
