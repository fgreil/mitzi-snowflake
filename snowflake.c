#include <furi.h>        // Furi OS core functionality
#include <gui/gui.h>     // GUI system for drawing to the screen
#include <input/input.h> // Input handling for button presses
#include <stdlib.h>      // Standard library functions (malloc, calloc, etc.)
#include <string.h>      // Memory and string manipulation functions
#include <furi_hal.h>    // Logging functionality

// ===================================================================
// Constants
// ===================================================================
#define CANVAS_SIZE 64
#define SCREEN_OFFSET_X 64  // Draw on right side of screen

#define TAG "Snowflake"  // Tag for logging

// Growth probability (0-100) - lower = more sparse, branching snowflakes
#define GROWTH_PROBABILITY 35

// ===================================================================
// Hexagonal direction vectors
// 6 hexagonal directions approximated on a square grid
// Directions: 0°, 60°, 120°, 180°, 240°, 300°
// ===================================================================
static const int dx[] = {1, 1, 0, -1, -1, 0};
static const int dy[] = {0, -1, -1, 0, 1, 1};

// ===================================================================
// Application State Structure
// Holds all the data needed for the snowflake generator
// ===================================================================
typedef struct {
    char canvas[CANVAS_SIZE * CANVAS_SIZE];  // Pixel buffer for snowflake
    int actualW;                              // Actual working width (odd number)
    int step;                                 // Current growth step counter
    uint32_t seed;                            // Random seed for snowflake generation
} SnowflakeState;

// ===================================================================
// Function: Simple Random Number Generator
// Returns a random number between 0 and 99
// ===================================================================
static uint32_t random_next(uint32_t* seed) {
    // Linear congruential generator
    *seed = (*seed * 1103515245 + 12345) & 0x7fffffff;
    return (*seed / 65536) % 100;
}

// ===================================================================
// Function: Initialize Snowflake
// Sets up a new snowflake with a single center pixel
// ===================================================================
static void init_snowflake(SnowflakeState* state) {
    FURI_LOG_I(TAG, "Initializing snowflake");
    
    // Clear canvas - set all pixels to 0
    memset(state->canvas, 0, CANVAS_SIZE * CANVAS_SIZE);
    
    // If W is even, we work with W-1 to ensure a proper center point
    state->actualW = (CANVAS_SIZE % 2 == 0) ? CANVAS_SIZE - 1 : CANVAS_SIZE;
    FURI_LOG_D(TAG, "Canvas size: %d, Actual width: %d", CANVAS_SIZE, state->actualW);
    
    // Set center pixel as the seed for snowflake growth
    int center = state->actualW / 2;
    state->canvas[center * CANVAS_SIZE + center] = 1;
    FURI_LOG_D(TAG, "Center pixel set at (%d, %d)", center, center);
    
    // Initialize random seed based on system tick
    state->seed = furi_get_tick();
    FURI_LOG_D(TAG, "Random seed: %lu", state->seed);
    
    // Reset step counter
    state->step = 0;
    FURI_LOG_I(TAG, "Snowflake initialized successfully");
}

// ===================================================================
// Function: Grow Snowflake
// Advances the snowflake by one generation using probabilistic growth
// This creates branching, snowflake-like structures instead of solid blobs
// Returns: Number of new pixels added
// ===================================================================
static int grow_snowflake(SnowflakeState* state) {
    FURI_LOG_D(TAG, "Growing snowflake - Step %d", state->step);
    
    // Create temporary buffer to store new growth
    // We need a separate buffer to avoid affecting the growth algorithm
    char* newPixels = (char*)calloc(CANVAS_SIZE * CANVAS_SIZE, sizeof(char));
    if(!newPixels) {
        FURI_LOG_E(TAG, "Failed to allocate memory for growth buffer");
        return 0;
    }
    
    // Copy current state to temporary buffer
    memcpy(newPixels, state->canvas, CANVAS_SIZE * CANVAS_SIZE);
    
    int pixelsAdded = 0;
    
    // For each existing snowflake pixel, try to grow outward
    for(int y = 0; y < state->actualW; y++) {
        for(int x = 0; x < state->actualW; x++) {
            // Skip empty pixels
            if(state->canvas[y * CANVAS_SIZE + x] == 0) continue;
            
            // Try to grow in all 6 hexagonal directions
            for(int dir = 0; dir < 6; dir++) {
                int nx = x + dx[dir];
                int ny = y + dy[dir];
                
                // Check bounds - ensure we don't go outside the canvas
                if(nx < 0 || nx >= state->actualW || ny < 0 || ny >= state->actualW)
                    continue;
                
                // Only grow if the target position is empty
                if(state->canvas[ny * CANVAS_SIZE + nx] == 0) {
                    // Probabilistic growth - only add pixel based on random chance
                    // This creates the branching, snowflake-like structure
                    uint32_t rand_val = random_next(&state->seed);
                    
                    if(rand_val < GROWTH_PROBABILITY) {
                        newPixels[ny * CANVAS_SIZE + nx] = 1;
                        pixelsAdded++;
                    }
                }
            }
        }
    }
    
    // Copy result back to main canvas
    memcpy(state->canvas, newPixels, CANVAS_SIZE * CANVAS_SIZE);
    free(newPixels);
    
    // Increment step counter if we actually added pixels
    if(pixelsAdded > 0) {
        state->step++;
        FURI_LOG_I(TAG, "Step %d complete: Added %d pixels", state->step, pixelsAdded);
    } else {
        FURI_LOG_W(TAG, "No pixels added - snowflake growth may have stopped");
    }
    
    return pixelsAdded;
}

// ===================================================================
// Function: Draw Callback
// Called by the GUI system to render the screen
// Draws both the UI elements and the snowflake visualization
// ===================================================================
static void snowflake_draw_callback(Canvas* canvas, void* ctx) {
    SnowflakeState* state = (SnowflakeState*)ctx;
    
    // Clear the entire screen
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    
    // ---------------------------------------------------------------
    // Draw UI on the left side of the screen
    // ---------------------------------------------------------------
    
    // Draw title
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Snowflake");
    
    // Draw step counter
    canvas_set_font(canvas, FontSecondary);
    char step_str[32];
    snprintf(step_str, sizeof(step_str), "Step: %d", state->step);
    canvas_draw_str(canvas, 2, 22, step_str);
    
    // Draw control instructions
    canvas_draw_str(canvas, 2, 34, "OK: Grow");
    canvas_draw_str(canvas, 2, 44, "Left: Reset");
    canvas_draw_str(canvas, 2, 54, "Back: Exit");
    
    // ---------------------------------------------------------------
    // Draw snowflake on the right side of the screen
    // ---------------------------------------------------------------
    for(int y = 0; y < state->actualW; y++) {
        for(int x = 0; x < state->actualW; x++) {
            if(state->canvas[y * CANVAS_SIZE + x] == 1) {
                canvas_draw_dot(canvas, SCREEN_OFFSET_X + x, y);
            }
        }
    }
}

// ===================================================================
// Function: Input Callback
// Called when the user presses a button
// Queues the input event for processing in the main loop
// ===================================================================
static void snowflake_input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    
    // Put the input event into the queue
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

// ===================================================================
// Function: Main Application Entry Point
// This is the main function that runs when the app starts
// ===================================================================
int32_t snowflake_main(void* p) {
    UNUSED(p);
    
    FURI_LOG_I(TAG, "Snowflake application starting");
    
    // ---------------------------------------------------------------
    // Initialize application state
    // ---------------------------------------------------------------
    SnowflakeState* state = malloc(sizeof(SnowflakeState));
    if(!state) {
        FURI_LOG_E(TAG, "Failed to allocate state memory");
        return -1;
    }
    init_snowflake(state);
    
    // ---------------------------------------------------------------
    // Create event queue for input handling
    // ---------------------------------------------------------------
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    if(!event_queue) {
        FURI_LOG_E(TAG, "Failed to allocate event queue");
        free(state);
        return -1;
    }
    
    // ---------------------------------------------------------------
    // Set up viewport (the drawing surface)
    // ---------------------------------------------------------------
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, snowflake_draw_callback, state);
    view_port_input_callback_set(view_port, snowflake_input_callback, event_queue);
    
    // ---------------------------------------------------------------
    // Register viewport with the GUI system
    // ---------------------------------------------------------------
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    FURI_LOG_I(TAG, "GUI initialized, entering main loop");
    
    // ---------------------------------------------------------------
    // Main event loop - processes user input
    // ---------------------------------------------------------------
    InputEvent event;
    bool running = true;
    
    while(running) {
        // Wait for input event (with 100ms timeout)
        if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            // Only process press and repeat events
            if(event.type == InputTypePress || event.type == InputTypeRepeat) {
                if(event.key == InputKeyBack) {
                    // Back button - exit application
                    FURI_LOG_I(TAG, "Back button pressed, exiting");
                    running = false;
                } else if(event.key == InputKeyOk) {
                    // OK button - grow the snowflake
                    FURI_LOG_D(TAG, "OK button pressed, growing snowflake");
                    int added = grow_snowflake(state);
                    if(added == 0) {
                        FURI_LOG_I(TAG, "Snowflake growth stopped");
                    }
                    view_port_update(view_port);
                } else if(event.key == InputKeyLeft) {
                    // Left button - reset the snowflake
                    FURI_LOG_I(TAG, "Left button pressed, resetting snowflake");
                    init_snowflake(state);
                    view_port_update(view_port);
                }
            }
        }
    }
    
    // ---------------------------------------------------------------
    // Cleanup - free all allocated resources
    // ---------------------------------------------------------------
    FURI_LOG_I(TAG, "Cleaning up resources");
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    free(state);
    
    FURI_LOG_I(TAG, "Snowflake application terminated");
    return 0;
}