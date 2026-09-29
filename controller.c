typedef enum {
} direction_t;
DIR_STOP, DIR_FORWARD, DIR_BACKWARD, DIR_LEFT, DIR_RIGHT
typedef struct {
int16_t x; // normalized -1000..1000
int16_t y; // normalized -1000..1000
bool pressed;
direction_t dir;
uint32_t seq;
int64_t time_ms;
} control_state_t;

