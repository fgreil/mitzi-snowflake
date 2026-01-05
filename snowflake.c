// Includes
#include <furi.h>           // Furi OS core functionality
#include <gui/gui.h>        // GUI system for drawing to the screen
#include <input/input.h>    // Input handling for button presses
#include <stdlib.h>         // Standard library functions (malloc, calloc, etc.)
#include <string.h>         // Memory and string manipulation functions
#include <math.h>           // Math functions (sqrt, fmax, fmin)
#include <furi_hal.h>       // Logging functionality

// ===================================================================
// Constants - Reduced for Flipper Zero's limited RAM
// ===================================================================
#define GRID_SIZE 32        // Grid is 32x32 (1024 cells total)
#define SCREEN_OFFSET_X 80  // Draw on right side of screen
#define SCREEN_OFFSET_Y 16  // Center vertically

#define TAG "Snowflake"  // Tag for logging

// Reiter's model parameters
#define ALPHA 1.0f      // Diffusion constant
#define BETA 0.35f      // Boundary vapor level
#define GAMMA 0.0003f   // Background vapor addition

// ===================================================================
// Application State Structure - compact memory layout
// ===================================================================
typedef struct {
    float* s;        // State values (water content) - 1024 floats
    float* u;        // Non-frozen diffusing water - 1024 floats
    uint8_t* frozen; // Boolean: is cell frozen? - 1024 bytes
    int step;        // Current growth step counter
} SnowflakeState;

// ===================================================================
// Function: Get array index from x,y coordinates
// ===================================================================
static inline int get_index(int x, int y) {
    return y * GRID_SIZE + x;
}

// ===================================================================
// Function: Check if cell is boundary cell
// A boundary cell is unfrozen but has at least one frozen neighbor
// ===================================================================
static bool is_boundary_cell(SnowflakeState* state, int x, int y) {
    if(state->frozen[get_index(x, y)]) return false;
    
    // Check 6 hexagonal neighbors (approximated on square grid)
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
    
    // Initialize all cells with boundary vapor level BETA
    for(int i = 0; i < GRID_SIZE * GRID_SIZE; i++) {
        state->s[i] = BETA;
        state->u[i] = BETA;
        state->frozen[i] = 0;
    }
    
    // Set center cell as frozen seed
    int center = GRID_SIZE / 2;
    int center_idx = get_index(center, center);
    state->s[center_idx] = 1.0f;
    state->frozen[center_idx] = 1;
    
    state->step = 0;
    FURI_LOG_I(TAG, "Snowflake initialized");
}

// ===================================================================
// Function: Grow Snowflake
// Implements one step of Reiter's cellular automaton model
// ===================================================================
static void grow_snowflake(SnowflakeState* state) {
    FURI_LOG_D(TAG, "Growing snowflake - Step %d", state->step);
    
    static const int dx[] = {1, 1, 0, -1, -1, 0};
    static const int dy[] = {0, -1, -1, 0, 1, 1};
    
    // Step 1: Set u based on cell type (no v array needed - calculate on the fly)
    for(int y = 0; y < GRID_SIZE; y++) {
        for(int x = 0; x < GRID_SIZE; x++) {
            int idx = get_index(x, y);
            bool is_receptive = state->frozen[idx] || is_boundary_cell(state, x, y);
            
            if(is_receptive) {
                state->u[idx] = 0.0f;  // Receptive: no diffusion
            } else {
                state->u[idx] = state->s[idx];  // Non-receptive: all water diffuses
            }
        }
    }
    
    // Step 2: Apply diffusion in-place (use s as temporary storage)
    for(int y = 1; y < GRID_SIZE - 1; y++) {
        for(int x = 1; x < GRID_SIZE - 1; x++) {
            int idx = get_index(x, y);
            
            // Calculate neighbor average
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
            
            // Store new u value temporarily in s
            state->s[idx] = state->u[idx] + (ALPHA / 2.0f) * (avg - state->u[idx]);
        }
    }
    
    // Copy diffused values back and handle boundaries
    for(int y = 0; y < GRID_SIZE; y++) {
        for(int x = 0; x < GRID_SIZE; x++) {
            int idx = get_index(x, y);
            
            // Edge cells stay at BETA
            if(x == 0 || x == GRID_SIZE - 1 || y == 0 || y == GRID_SIZE - 1) {
                state->u[idx] = BETA;
                state->s[idx] = BETA;
            } else {
                state->u[idx] = state->s[idx];
            }
        }
    }
    
    // Step 3: Update s and freeze boundary cells
    for(int y = 0; y < GRID_SIZE; y++) {
        for(int x = 0; x < GRID_SIZE; x++) {
            int idx = get_index(x, y);
            
            bool is_receptive = state->frozen[idx] || is_boundary_cell(state, x, y);
            
            if(is_receptive) {
                // Add background vapor
                state->s[idx] = state->u[idx] + state->s[idx] + GAMMA;
                
                // Freeze if threshold reached
                if(!state->frozen[idx] && state->s[idx] >= 1.0f) {
                    state->frozen[idx] = 1;
                }
            } else {
                state->s[idx] = state->u[idx];
            }
        }
    }
    
    state->step++;
    FURI_LOG_I(TAG, "Step %d complete", state->step);
}

// ===================================================================
// Function: Draw Callback
// ===================================================================
static void snowflake_draw_callback(Canvas* canvas, void* ctx) {
    SnowflakeState* state = (SnowflakeState*)ctx;
    
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    
    // Draw UI
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Snowflake");
    
    canvas_set_font(canvas, FontSecondary);
    char step_str[32];
    snprintf(step_str, sizeof(step_str), "Step: %d", state->step);
    canvas_draw_str(canvas, 2, 22, step_str);
    
    canvas_draw_str(canvas, 2, 34, "OK: Grow");
    canvas_draw_str(canvas, 2, 44, "Left: Reset");
    canvas_draw_str(canvas, 2, 54, "Back: Exit");
    
    // Draw snowflake - only frozen cells
    for(int y = 0; y < GRID_SIZE; y++) {
        for(int x = 0; x < GRID_SIZE; x++) {
            if(state->frozen[get_index(x, y)]) {
                int px = SCREEN_OFFSET_X + x;
                int py = SCREEN_OFFSET_Y + y;
                
                if(px >= 64 && px < 128 && py >= 0 && py < 64) {
                    canvas_draw_dot(canvas, px, py);
                }
            }
        }
    }
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
    if(!state) {
        FURI_LOG_E(TAG, "Failed to allocate state");
        return -1;
    }
    
    // Allocate arrays - total: ~8KB (much smaller than before)
    state->s = (float*)malloc(GRID_SIZE * GRID_SIZE * sizeof(float));
    state->u = (float*)malloc(GRID_SIZE * GRID_SIZE * sizeof(float));
    state->frozen = (uint8_t*)malloc(GRID_SIZE * GRID_SIZE * sizeof(uint8_t));
    
    if(!state->s || !state->u || !state->frozen) {
        FURI_LOG_E(TAG, "Failed to allocate arrays");
        if(state->s) free(state->s);
        if(state->u) free(state->u);
        if(state->frozen) free(state->frozen);
        free(state);
        return -1;
    }
    
    init_snowflake(state);
    
    // Create event queue
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    if(!event_queue) {
        FURI_LOG_E(TAG, "Failed to allocate event queue");
        free(state->s);
        free(state->u);
        free(state->frozen);
        free(state);
        return -1;
    }
    
    // Set up viewport
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, snowflake_draw_callback, state);
    view_port_input_callback_set(view_port, snowflake_input_callback, event_queue);
    
    // Register viewport
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    FURI_LOG_I(TAG, "GUI initialized, entering main loop");
    
    // Main event loop
    InputEvent event;
    bool running = true;
    
    while(running) {
        if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypePress || event.type == InputTypeRepeat) {
                if(event.key == InputKeyBack) {
                    FURI_LOG_I(TAG, "Back button pressed, exiting");
                    running = false;
                } else if(event.key == InputKeyOk) {
                    FURI_LOG_D(TAG, "OK button pressed, growing snowflake");
                    grow_snowflake(state);
                    view_port_update(view_port);
                } else if(event.key == InputKeyLeft) {
                    FURI_LOG_I(TAG, "Left button pressed, resetting snowflake");
                    init_snowflake(state);
                    view_port_update(view_port);
                }
            }
        }
    }
    
    // Cleanup
    FURI_LOG_I(TAG, "Cleaning up resources");
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    free(state->s);
    free(state->u);
    free(state->frozen);
    free(state);
    
    FURI_LOG_I(TAG, "Snowflake application terminated");
    return 0;
}