
struct renderer_state;

struct renderer_state *initialize_sdl(struct tnes_machine *machine);

int render(struct renderer_state *state);

// TODO: Handle quit, toggle debug window, etc.
int handle_inputs(struct renderer_state *state);
