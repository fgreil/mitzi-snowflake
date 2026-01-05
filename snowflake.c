// Includes
#include <furi.h>           // Furi OS core functionality
#include <gui/gui.h>        // GUI system for drawing to the screen
#include <input/input.h>    // Input handling for button presses
#include <stdlib.h>         // Standard library functions (malloc, calloc, etc.)
#include <string.h>         // Memory and string manipulation functions
#include <math.h>           // Math functions (sqrt, fmax, fmin)
#include <furi_hal.h>       // Logging functionality
#include "mitzi_snowflake_icons.h"

// ===================================================================
// Constants
// ===================================================================
#define GRID_SIZE 32        // Grid is 32x32 for hexagonal cells
#define HEX_SIZE 2          // Visual size of each hex cell (2 pixels)
#define SCREEN_OFFSET_X 64  // Draw on right side of screen

#define TAG "Snowflake"

// Parameter limits
#define ALPHA_MIN 0.5f
#define ALPHA_MAX 5.0f
#define ALPHA_STEP 0.5f

#define BETA_MIN 0.1f
#define BETA_MAX 0.9f
#define BETA_STEP 0.05f

#define GAMMA_MIN 0.001f
#define GAMMA_MAX 0.1f
#define GAMMA_STEP 0.005f

// ===================================================================
// Parameter selection
// ===================================================================
typedef enum {
    PARAM_ALPHA,
    PARAM_BETA,
    PARAM_GAMMA,
    PARAM_COUNT
} ParamType;

// ===================================================================
// Application State Structure
// ===================================================================
typedef struct {
    float* s;        // State values (water content)
    float* u;        // Non-frozen diffusing water
    uint8_t* frozen; // Boolean: is cell frozen?
    int step;
    
    // Adjustable parameters
    float alpha;     // Diffusion constant
    float beta;      // Boundary vapor level
    float gamma;     // Background vapor addition
    
    ParamType selected_param;  // Which parameter is being adjusted
    uint32_t back_press_timer; // For detecting long press
} SnowflakeState;

// ===================================================================
// Function: Get array index
// ===================================================================
static inline int get_index(int x, int y) {
    return y * GRID_SIZE + x;
}

// ===================================================================
// Function: Check if cell is boundary cell
// ===================================================================
static bool is_boundary_cell(SnowflakeState* state, int x, int y) {
    if(state->frozen[get_index(x, y)]) return false;
    
    static const int dx[] = {1, 1, 0, -1, -1, 0};
    static const int dy[] = {0, -1, -1, 0, 1, 1};
    
    for(int dir = 0; dir < 6; dir++) {
        int nx = x + dx[dir];
        int ny = y + dy[dir];
        
        if(nx >= 0 && nx < GRID_SIZE && ny >= 0 && ny < GRID_SIZE) {
            if(state->frozen[get_index(nx, ny)]) {
                return true;
            }
        }
    }
    
    return false;
}

// ===================================================================
// Function: Initialize Snowflake
// ===================================================================
static void init_snowflake(SnowflakeState* state) {
    FURI_LOG_I(TAG, "Initializing snowflake");
    
    // Initialize all cells
    for(int i = 0; i < GRID_SIZE * GRID_SIZE; i++) {
        state->s[i] = state->beta;
        state->u[i] = 0.0f;
        state->frozen[i] = 0;
    }
    
    // Freeze center cell
    int center = GRID_SIZE / 2;
    int center_idx = get_index(center, center);
    state->s[center_idx] = 1.0f;
    state->frozen[center_idx] = 1;
    
    state->step = 0;
    FURI_LOG_I(TAG, "Initialized with α=%f β=%f γ=%f", 
               (double)state->alpha, (double)state->beta, (double)state->gamma);
}

// ===================================================================
// Function: Grow Snowflake (Reiter's model)
// ===================================================================
static void grow_snowflake(SnowflakeState* state) {
    static const int dx[] = {1, 1, 0, -1, -1, 0};
    static const int dy[] = {0, -1, -1, 0, 1, 1};
    
    float* u_new = (float*)malloc(GRID_SIZE * GRID_SIZE * sizeof(float));
    if(!u_new) return;
    
    // Step 1: Classify cells
    for(int y = 0; y < GRID_SIZE; y++) {
        for(int x = 0; x < GRID_SIZE; x++) {
            int idx = get_index(x, y);
            bool is_receptive = state->frozen[idx] || is_boundary_cell(state, x, y);
            
            if(is_receptive) {
                state->u[idx] = 0.0f;
            } else {
                state->u[idx] = state->s[idx];
            }
        }
    }
    
    // Step 2: Diffusion
    for(int y = 0; y < GRID_SIZE; y++) {
        for(int x = 0; x < GRID_SIZE; x++) {
            int idx = get_index(x, y);
            
            if(x == 0 || x == GRID_SIZE - 1 || y == 0 || y == GRID_SIZE - 1) {
                u_new[idx] = state->beta;
                continue;
            }
            
            float sum = 0.0f;
            int count = 0;
            
            for(int dir = 0; dir < 6; dir++) {
                int nx = x + dx[dir];
                int ny = y + dy[dir];
                
                if(nx >= 0 && nx < GRID_SIZE && ny >= 0 && ny < GRID_SIZE) {
                    sum += state->u[get_index(nx, ny)];
                    count++;
                }
            }
            
            float avg = (count > 0) ? (sum / count) : state->u[idx];
            u_new[idx] = state->u[idx] + (state->alpha / 2.0f) * (avg - state->u[idx]);
        }
    }
    
    memcpy(state->u, u_new, GRID_SIZE * GRID_SIZE * sizeof(float));
    free(u_new);
    
    // Step 3: Update and freeze
    int frozen_count = 0;
    for(int y = 0; y < GRID_SIZE; y++) {
        for(int x = 0; x < GRID_SIZE; x++) {
            int idx = get_index(x, y);
            bool is_receptive = state->frozen[idx] || is_boundary_cell(state, x, y);
            
            if(is_receptive) {
                state->s[idx] = state->s[idx] + state->gamma;
                
                if(!state->frozen[idx] && state->s[idx] >= 1.0f) {
                    state->frozen[idx] = 1;
                    frozen_count++;
                }
            } else {
                state->s[idx] = state->u[idx];
            }
        }
    }
    
    state->step++;
    FURI_LOG_I(TAG, "Step %d: froze %d cells", state->step, frozen_count);
}

// ===================================================================
// Function: Draw hexagonal cell
// ===================================================================
static void draw_hex(Canvas* canvas, int cx, int cy, int size) {
    // Draw a filled circle approximation of hex
    for(int dy = -size; dy <= size; dy++) {
        for(int dx = -size; dx <= size; dx++) {
            if(dx*dx + dy*dy <= size*size) {
                int px = cx + dx;
                int py = cy + dy;
                if(px >= 64 && px < 128 && py >= 0 && py < 64) {
                    canvas_draw_dot(canvas, px, py);
                }
            }
        }
    }
}

// ===================================================================
// Function: Draw Callback
// ===================================================================
static void snowflake_draw_callback(Canvas* canvas, void* ctx) {
    SnowflakeState* state = (SnowflakeState*)ctx;
    
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    // Draw header with icon and title
    canvas_set_font(canvas, FontPrimary);
	canvas_draw_icon(canvas, 1, 1, &I_icon_10x10);	
	canvas_draw_str_aligned(canvas, 13, 1, AlignLeft, AlignTop, "Snowflake");
	canvas_set_font(canvas, FontSecondary);
	
	
    // Draw parameter info on left side
    canvas_set_font(canvas, FontSecondary);
    
    char alpha_str[32];
    snprintf(alpha_str, sizeof(alpha_str), "%s alpha:%.1f", 
             (state->selected_param == PARAM_ALPHA) ? ">" : " ", (double)state->alpha);
    canvas_draw_str(canvas, 2, 18, alpha_str);
    
    char beta_str[32];
    snprintf(beta_str, sizeof(beta_str), "%s beta:%.2f", 
             (state->selected_param == PARAM_BETA) ? ">" : " ", (double)state->beta);
    canvas_draw_str(canvas, 2, 27, beta_str);
    
    char gamma_str[32];
    snprintf(gamma_str, sizeof(gamma_str), "%s gam:%.3f", 
             (state->selected_param == PARAM_GAMMA) ? ">" : " ", (double)state->gamma);
    canvas_draw_str(canvas, 2, 36, gamma_str);
    
    // Draw step counter
    char step_str[32];
    snprintf(step_str, sizeof(step_str), "Step %d", state->step);
    canvas_draw_str(canvas, 2, 46, step_str);
    
    // Count frozen cells
    int frozen_total = 0;
    for(int i = 0; i < GRID_SIZE * GRID_SIZE; i++) {
        if(state->frozen[i]) frozen_total++;
    }
    
    char frozen_str[32];
    snprintf(frozen_str, sizeof(frozen_str), "%d frozen cells", frozen_total);
    canvas_draw_str(canvas, 2, 56, frozen_str);
    
    // Draw snowflake with hexagonal cells
    for(int y = 0; y < GRID_SIZE; y++) {
        for(int x = 0; x < GRID_SIZE; x++) {
            if(state->frozen[get_index(x, y)]) {
                // Calculate screen position with hexagonal offset
                int px = SCREEN_OFFSET_X + x * HEX_SIZE;
                int py = y * HEX_SIZE;
                
                // Offset every other row for hexagonal packing
                if(y % 2 == 1) {
                    px += HEX_SIZE / 2;
                }
                
                draw_hex(canvas, px, py, HEX_SIZE / 2);
            }
        }
    }
	canvas_draw_icon(canvas, 121, 57, &I_back);
	canvas_draw_str_aligned(canvas, 120, 63, AlignRight, AlignBottom, "Exit");	
}

// ===================================================================
// Function: Input Callback
// ===================================================================
static void snowflake_input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

// ===================================================================
// Function: Main Application Entry Point
// ===================================================================
int32_t snowflake_main(void* p) {
    UNUSED(p);
    
    FURI_LOG_I(TAG, "Snowflake application starting");
    
    // Allocate state
    SnowflakeState* state = malloc(sizeof(SnowflakeState));
    if(!state) return -1;
    
    state->s = (float*)malloc(GRID_SIZE * GRID_SIZE * sizeof(float));
    state->u = (float*)malloc(GRID_SIZE * GRID_SIZE * sizeof(float));
    state->frozen = (uint8_t*)malloc(GRID_SIZE * GRID_SIZE * sizeof(uint8_t));
    
    if(!state->s || !state->u || !state->frozen) {
        if(state->s) free(state->s);
        if(state->u) free(state->u);
        if(state->frozen) free(state->frozen);
        free(state);
        return -1;
    }
    
    // Initialize default parameters
    state->alpha = 2.0f;
    state->beta = 0.6f;
    state->gamma = 0.05f;
    state->selected_param = PARAM_ALPHA;
    state->back_press_timer = 0;
    
    init_snowflake(state);
    
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    if(!event_queue) {
        free(state->s);
        free(state->u);
        free(state->frozen);
        free(state);
        return -1;
    }
    
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, snowflake_draw_callback, state);
    view_port_input_callback_set(view_port, snowflake_input_callback, event_queue);
    
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    
    InputEvent event;
    bool running = true;
    
    while(running) {
        if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            if(event.key == InputKeyBack) {
                if(event.type == InputTypePress) {
                    state->back_press_timer = furi_get_tick();
                } else if(event.type == InputTypeRelease) {
                    uint32_t press_duration = furi_get_tick() - state->back_press_timer;
                    if(press_duration > 500) {
                        // Long press - exit
                        FURI_LOG_I(TAG, "Long press - exiting");
                        running = false;
                    } else {
                        // Short press - reset
                        FURI_LOG_I(TAG, "Short press - reset");
                        init_snowflake(state);
                        view_port_update(view_port);
                    }
                }
            } else if(event.type == InputTypePress || event.type == InputTypeRepeat) {
                if(event.key == InputKeyOk) {
                    grow_snowflake(state);
                    view_port_update(view_port);
                } else if(event.key == InputKeyLeft) {
                    // Previous parameter
                    state->selected_param = (state->selected_param + PARAM_COUNT - 1) % PARAM_COUNT;
                    view_port_update(view_port);
                } else if(event.key == InputKeyRight) {
                    // Next parameter
                    state->selected_param = (state->selected_param + 1) % PARAM_COUNT;
                    view_port_update(view_port);
                } else if(event.key == InputKeyUp) {
                    // Increase parameter
                    if(state->selected_param == PARAM_ALPHA) {
                        state->alpha = fminf(state->alpha + ALPHA_STEP, ALPHA_MAX);
                    } else if(state->selected_param == PARAM_BETA) {
                        state->beta = fminf(state->beta + BETA_STEP, BETA_MAX);
                    } else if(state->selected_param == PARAM_GAMMA) {
                        state->gamma = fminf(state->gamma + GAMMA_STEP, GAMMA_MAX);
                    }
                    view_port_update(view_port);
                } else if(event.key == InputKeyDown) {
                    // Decrease parameter
                    if(state->selected_param == PARAM_ALPHA) {
                        state->alpha = fmaxf(state->alpha - ALPHA_STEP, ALPHA_MIN);
                    } else if(state->selected_param == PARAM_BETA) {
                        state->beta = fmaxf(state->beta - BETA_STEP, BETA_MIN);
                    } else if(state->selected_param == PARAM_GAMMA) {
                        state->gamma = fmaxf(state->gamma - GAMMA_STEP, GAMMA_MIN);
                    }
                    view_port_update(view_port);
                }
            }
        }
    }
    
    // Cleanup
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    free(state->s);
    free(state->u);
    free(state->frozen);
    free(state);
    
    FURI_LOG_I(TAG, "Terminated");
    return 0;
}