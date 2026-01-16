#include "backends/aroma_backend_interface.h"

int main()
{
    aroma_glps_opengl_backend.initialize();
    aroma_glps_opengl_backend.create_window("Aroma GLPS OpenGL Window", 0 , 0, 800, 600);
    aroma_glps_opengl_backend.run_event_loop();
    aroma_glps_opengl_backend.shutdown();
    return 0;
}