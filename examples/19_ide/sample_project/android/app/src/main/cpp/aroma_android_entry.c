#include <aroma.h>
#include <android_native_app_glue.h>

extern int main(void);

void android_main(struct android_app *state)
{
    aroma_android_set_app(state);
    main();
}
